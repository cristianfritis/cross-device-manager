#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <umockdev.h>

#include "devmgr/platform/linux/udev_hotplug_monitor.hpp"

using namespace std::chrono_literals;

namespace {

// Thread-safe sink the monitor callback writes into (callback runs on the
// monitor's reader thread).
class EventSink {
   public:
    void push(const devmgr::pal::HotplugEvent& e) {
        std::scoped_lock lock(m_);
        events_.push_back(e);
        cv_.notify_all();
    }
    // Wait until at least `n` events are collected, or timeout.
    bool waitForCount(std::size_t n, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_);
        return cv_.wait_for(lock, timeout, [&] { return events_.size() >= n; });
    }
    std::vector<devmgr::pal::HotplugEvent> snapshot() {
        std::scoped_lock lock(m_);
        return events_;
    }

   private:
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<devmgr::pal::HotplugEvent> events_;
};

class UdevHotplugMonitorTest : public ::testing::Test {
   protected:
    UMockdevTestbed* bed_ = nullptr;
    void SetUp() override {
        bed_ = umockdev_testbed_new();
        ASSERT_NE(bed_, nullptr);
    }
    void TearDown() override {
        if (bed_ != nullptr) g_object_unref(bed_);
    }
};

TEST_F(UdevHotplugMonitorTest, DeliversAddThenRemoveForUsbDevice) {
    EventSink sink;
    devmgr::platform_linux::UdevHotplugMonitor monitor;
    auto started = monitor.start([&](const devmgr::pal::HotplugEvent& e) { sink.push(e); });
    ASSERT_TRUE(started.has_value()) << started.error().message;

    // add_device auto-emits an "add" uevent that our now-listening monitor sees.
    gchar* sys = umockdev_testbed_add_device(bed_, "usb", "1-1", nullptr, "idVendor", "1d6b",
                                             "idProduct", "0002", nullptr, "ID_VENDOR_ID", "1d6b",
                                             "ID_MODEL_ID", "0002", "SUBSYSTEM", "usb", nullptr);
    ASSERT_NE(sys, nullptr);

    ASSERT_TRUE(sink.waitForCount(1, 2s)) << "no add event delivered";
    auto afterAdd = sink.snapshot();
    EXPECT_EQ(afterAdd.front().action, devmgr::pal::HotplugEvent::Action::Added);
    EXPECT_EQ(afterAdd.front().device.bus, devmgr::core::BusType::Usb);
    const auto addedId = afterAdd.front().device.id;

    umockdev_testbed_uevent(bed_, sys, "remove");
    ASSERT_TRUE(sink.waitForCount(2, 2s)) << "no remove event delivered";
    auto afterRemove = sink.snapshot();
    EXPECT_EQ(afterRemove.back().action, devmgr::pal::HotplugEvent::Action::Removed);
    EXPECT_EQ(afterRemove.back().device.id, addedId);  // remove reconciles by the same id

    monitor.stop();  // must join cleanly
    g_free(sys);
}

// Regression test for the reentrant-stop fix: a callback running on the
// monitor's own reader thread must be able to call monitor.stop() on itself
// without deadlocking (self-join) or crashing (freeing monitor_/udev_ out from
// under drainEvents()'s still-executing loop). Mirrors
// DelayedScheduler.CallbackShuttingDownItsOwnSchedulerDoesNotCrashOrHang, but
// needs a real uevent (hence umockdev, not a plain unit test) to actually
// invoke the callback on the reader thread.
TEST_F(UdevHotplugMonitorTest, ReentrantStopFromCallbackDoesNotCrashOrHang) {
    devmgr::platform_linux::UdevHotplugMonitor monitor;
    std::atomic<bool> callbackRan{false};

    auto started = monitor.start([&](const devmgr::pal::HotplugEvent&) {
        monitor.stop();  // reentrant self-call: must only signal stop, never join/free
        callbackRan.store(true);
    });
    ASSERT_TRUE(started.has_value()) << started.error().message;

    gchar* sys =
        umockdev_testbed_add_device(bed_, "usb", "1-3", nullptr, "idVendor", "1d6b", "idProduct",
                                    "0002", nullptr, "SUBSYSTEM", "usb", nullptr);
    ASSERT_NE(sys, nullptr);

    for (int i = 0; i < 200 && !callbackRan.load(); ++i) {
        std::this_thread::sleep_for(10ms);
    }
    ASSERT_TRUE(callbackRan.load()) << "callback never ran on the reader thread";

    // A later external stop() (here, via the destructor at scope exit) must
    // still perform the actual join + libudev cleanup the self-call deferred.
    monitor.stop();
    g_free(sys);
}

}  // namespace
