#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "devmgr/platform/windows/windows_device_mapper.hpp"
#include "devmgr/platform/windows/windows_hotplug_monitor.hpp"

// The Windows hotplug lifecycle, against a FAKE notification source. The Linux
// monitor shipped a use-after-free against this exact contract in Phase 2, so
// the rules live in platform-neutral code and are tested here rather than being
// discoverable only on a Windows machine.
namespace {

using devmgr::pal::HotplugEvent;
using devmgr::platform_windows::INotificationSource;
using devmgr::platform_windows::NativeNotification;
using devmgr::platform_windows::WindowsHotplugMonitor;

constexpr auto kInstanceId = R"(USB\VID_046D&PID_C52B\5&1234abcd&0&2)";

// Deliberately PERMISSIVE: it will deliver whenever a test tells it to, even
// outside a started period, so the guarantees under test are the monitor's own
// and not the fake's. `joinsOnEnd` models the one thing the real source does
// provide — an unregistration that blocks until in-flight delivery returns —
// and can be switched off to prove the monitor survives a source that doesn't.
class FakeNotificationSource final : public INotificationSource {
   public:
    explicit FakeNotificationSource(bool joinsOnEnd = true) : joinsOnEnd_(joinsOnEnd) {}

    devmgr::core::Result<void> beginDelivery(Handler handler) override {
        const std::scoped_lock lock(mutex_);
        ++beginCount_;
        handler_ = std::move(handler);
        return {};
    }

    void endDelivery() override {
        std::unique_lock lock(mutex_);
        ++endCount_;
        if (joinsOnEnd_) idle_.wait(lock, [this] { return inFlight_ == 0; });
    }

    // Delivers on the CALLING thread, as a system thread-pool callback would.
    void deliver(const NativeNotification& notification) {
        Handler handler;
        {
            const std::scoped_lock lock(mutex_);
            handler = handler_;
            ++inFlight_;
        }
        if (handler) handler(notification);
        {
            const std::scoped_lock lock(mutex_);
            --inFlight_;
        }
        idle_.notify_all();
    }

    [[nodiscard]] int beginCount() const {
        const std::scoped_lock lock(mutex_);
        return beginCount_;
    }
    [[nodiscard]] int endCount() const {
        const std::scoped_lock lock(mutex_);
        return endCount_;
    }

   private:
    mutable std::mutex mutex_;
    std::condition_variable idle_;
    Handler handler_;
    int inFlight_ = 0;
    int beginCount_ = 0;
    int endCount_ = 0;
    bool joinsOnEnd_ = true;
};

NativeNotification arrival(std::string instanceId = kInstanceId) {
    return NativeNotification{.action = NativeNotification::Action::Arrived,
                              .instanceId = std::move(instanceId)};
}

NativeNotification removal(std::string instanceId = kInstanceId) {
    return NativeNotification{.action = NativeNotification::Action::Removed,
                              .instanceId = std::move(instanceId)};
}

// Resolves every identifier, as a machine with the device still attached would.
devmgr::platform_windows::DeviceResolver alwaysResolves() {
    return [](const std::string& instanceId) {
        return std::optional<devmgr::core::Device>(
            devmgr::platform_windows::mapKnownOnlyByInstanceId(instanceId));
    };
}

// Collects delivered events for assertions.
class Recorder {
   public:
    void operator()(const HotplugEvent& event) {
        const std::scoped_lock lock(mutex_);
        events_.push_back(event);
    }
    [[nodiscard]] std::vector<HotplugEvent> events() const {
        const std::scoped_lock lock(mutex_);
        return events_;
    }
    [[nodiscard]] std::size_t count() const {
        const std::scoped_lock lock(mutex_);
        return events_.size();
    }

   private:
    mutable std::mutex mutex_;
    std::vector<HotplugEvent> events_;
};

TEST(WindowsHotplugMonitorTest, DeliversArrivalsAndRemovalsWithTheInstanceIdentifier) {
    FakeNotificationSource source;
    WindowsHotplugMonitor monitor(source, alwaysResolves());
    Recorder recorder;
    ASSERT_TRUE(monitor.start([&recorder](const HotplugEvent& e) { recorder(e); }));

    source.deliver(arrival());
    source.deliver(removal());
    monitor.stop();

    const auto events = recorder.events();
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].action, HotplugEvent::Action::Added);
    EXPECT_EQ(events[0].device.nativeId, kInstanceId);
    EXPECT_EQ(events[1].action, HotplugEvent::Action::Removed);
    EXPECT_EQ(events[1].device.nativeId, kInstanceId);
    // The two halves of the application must agree on what a device is.
    EXPECT_EQ(events[0].device.id.value, events[1].device.id.value);
}

TEST(WindowsHotplugMonitorTest, NoCallbackIsDeliveredOutsideAStartedPeriod) {
    FakeNotificationSource source;
    WindowsHotplugMonitor monitor(source, alwaysResolves());
    Recorder recorder;

    source.deliver(arrival());  // before start
    EXPECT_EQ(recorder.count(), 0U);

    ASSERT_TRUE(monitor.start([&recorder](const HotplugEvent& e) { recorder(e); }));
    source.deliver(arrival());
    EXPECT_EQ(recorder.count(), 1U);

    monitor.stop();
    source.deliver(arrival());  // after stop
    source.deliver(removal());
    EXPECT_EQ(recorder.count(), 1U);
}

TEST(WindowsHotplugMonitorTest, StartingTwiceIsRefused) {
    FakeNotificationSource source;
    WindowsHotplugMonitor monitor(source, alwaysResolves());
    ASSERT_TRUE(monitor.start([](const HotplugEvent&) {}));
    EXPECT_FALSE(monitor.start([](const HotplugEvent&) {}));
    monitor.stop();
}

// An arrival that cannot be read is not reported half-populated: a nameless row
// no refresh removes is worse than waiting for the next enumeration.
TEST(WindowsHotplugMonitorTest, UnresolvableArrivalIsDropped) {
    FakeNotificationSource source;
    WindowsHotplugMonitor monitor(
        source, [](const std::string&) { return std::optional<devmgr::core::Device>(); });
    Recorder recorder;
    ASSERT_TRUE(monitor.start([&recorder](const HotplugEvent& e) { recorder(e); }));

    source.deliver(arrival());
    EXPECT_EQ(recorder.count(), 0U);
    // A removal needs no resolution — the device node is already gone.
    source.deliver(removal());
    EXPECT_EQ(recorder.count(), 1U);
    monitor.stop();
}

// The core shutdown contract: stop() blocks until the in-flight callback has
// returned, and that callback's delivery does not reach the consumer.
TEST(WindowsHotplugMonitorTest, StopBlocksUntilAnInFlightCallbackReturns) {
    FakeNotificationSource source;
    std::mutex mutex;
    std::condition_variable cv;
    bool insideResolver = false;
    bool releaseResolver = false;

    WindowsHotplugMonitor monitor(source, [&](const std::string& instanceId) {
        {
            const std::scoped_lock lock(mutex);
            insideResolver = true;
        }
        cv.notify_all();
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return releaseResolver; });
        return std::optional<devmgr::core::Device>(
            devmgr::platform_windows::mapKnownOnlyByInstanceId(instanceId));
    });

    Recorder recorder;
    ASSERT_TRUE(monitor.start([&recorder](const HotplugEvent& e) { recorder(e); }));

    std::atomic<bool> deliveryFinished{false};
    std::thread deliverer([&] {
        source.deliver(arrival());
        deliveryFinished = true;
    });

    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return insideResolver; });
    }

    std::atomic<bool> stopReturned{false};
    std::thread stopper([&] {
        monitor.stop();
        stopReturned = true;
    });

    // stop() is now inside the source's blocking unregistration.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(stopReturned.load()) << "stop() returned while a callback was still running";

    {
        const std::scoped_lock lock(mutex);
        releaseResolver = true;
    }
    cv.notify_all();
    deliverer.join();
    stopper.join();

    EXPECT_TRUE(deliveryFinished.load());
    EXPECT_TRUE(stopReturned.load());
    // The delivery was in flight when shutdown began, so it is dropped rather
    // than handed to a consumer that has already torn down.
    EXPECT_EQ(recorder.count(), 0U);
}

// stop() from inside a callback would deadlock the real unregistration, so it
// is refused rather than trusted not to happen. The monitor stays started and a
// later external stop() does the real work.
TEST(WindowsHotplugMonitorTest, ReentrantStopFromACallbackDoesNotDeadlock) {
    FakeNotificationSource source;
    WindowsHotplugMonitor monitor(source, alwaysResolves());
    Recorder recorder;
    ASSERT_TRUE(monitor.start([&](const HotplugEvent& e) {
        recorder(e);
        monitor.stop();  // the hazard
    }));

    source.deliver(arrival());
    EXPECT_EQ(recorder.count(), 1U);
    EXPECT_EQ(source.endCount(), 0) << "a reentrant stop must not unregister";

    monitor.stop();  // external: this one really stops
    EXPECT_EQ(source.endCount(), 1);
    source.deliver(arrival());
    EXPECT_EQ(recorder.count(), 1U);
}

// Even against a source that BREAKS its join guarantee, a callback that entered
// before shutdown cannot be delivered to a later generation's consumer.
TEST(WindowsHotplugMonitorTest, ACallbackCannotCrossAGeneration) {
    FakeNotificationSource source(/*joinsOnEnd=*/false);
    std::mutex mutex;
    std::condition_variable cv;
    bool insideResolver = false;
    bool releaseResolver = false;

    WindowsHotplugMonitor monitor(source, [&](const std::string& instanceId) {
        {
            const std::scoped_lock lock(mutex);
            insideResolver = true;
        }
        cv.notify_all();
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return releaseResolver; });
        return std::optional<devmgr::core::Device>(
            devmgr::platform_windows::mapKnownOnlyByInstanceId(instanceId));
    });

    Recorder first;
    ASSERT_TRUE(monitor.start([&first](const HotplugEvent& e) { first(e); }));
    std::thread deliverer([&] { source.deliver(arrival()); });
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return insideResolver; });
    }

    monitor.stop();  // returns immediately: this source does not join
    Recorder second;
    ASSERT_TRUE(monitor.start([&second](const HotplugEvent& e) { second(e); }));

    {
        const std::scoped_lock lock(mutex);
        releaseResolver = true;
    }
    cv.notify_all();
    deliverer.join();

    EXPECT_EQ(first.count(), 0U) << "delivered to a consumer that had stopped";
    EXPECT_EQ(second.count(), 0U) << "delivered to a consumer that never asked for it";
    monitor.stop();
}

TEST(WindowsHotplugMonitorTest, RepeatedStartStopCyclesUnderEventPressure) {
    FakeNotificationSource source;
    WindowsHotplugMonitor monitor(source, alwaysResolves());
    std::atomic<bool> pressing{true};
    std::atomic<int> delivered{0};

    std::vector<std::thread> pressure;
    pressure.reserve(3);
    for (int i = 0; i < 3; ++i) {
        pressure.emplace_back([&, i] {
            const std::string id = std::string(R"(USB\VID_046D&PID_C52B\)") + std::to_string(i);
            while (pressing.load()) {
                source.deliver(arrival(id));
                source.deliver(removal(id));
            }
        });
    }

    constexpr int kCycles = 25;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        ASSERT_TRUE(monitor.start([&delivered](const HotplugEvent&) { ++delivered; }));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        monitor.stop();
    }

    pressing = false;
    for (auto& thread : pressure) thread.join();

    EXPECT_EQ(source.beginCount(), kCycles);
    EXPECT_EQ(source.endCount(), kCycles);

    // Every cycle completed and the monitor is genuinely stopped: further
    // pressure reaches nobody.
    const int settled = delivered.load();
    source.deliver(arrival());
    source.deliver(removal());
    EXPECT_EQ(delivered.load(), settled);
}

}  // namespace
