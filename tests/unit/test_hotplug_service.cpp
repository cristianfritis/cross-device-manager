#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "devmgr/app/device_service.hpp"
#include "devmgr/app/hotplug_service.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "fakes/fake_hotplug_monitor.hpp"

using namespace std::chrono_literals;

namespace {

devmgr::core::Device usbDevice(std::string id, std::string name) {
    devmgr::core::Device d;
    d.id = devmgr::core::DeviceId{std::move(id)};
    d.name = std::move(name);
    d.bus = devmgr::core::BusType::Usb;
    return d;
}

template <class Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return pred();
}

}  // namespace

TEST(HotplugService, CoalescesBurstIntoOneAddAfterWindow) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;
    devmgr::app::DeviceService service(bus);
    devmgr::test::FakeHotplugMonitor monitor;

    std::atomic<int> added{0}, changed{0};
    auto sA =
        bus.subscribe<devmgr::core::DeviceAddedEvent>([&](const auto&) { added.fetch_add(1); });
    auto sC =
        bus.subscribe<devmgr::core::DeviceChangedEvent>([&](const auto&) { changed.fetch_add(1); });

    devmgr::app::HotplugService hotplug(monitor, service, timer, 30ms);
    ASSERT_TRUE(hotplug.start().has_value());
    ASSERT_TRUE(monitor.started());

    using Ev = devmgr::pal::HotplugEvent;
    monitor.emit(Ev{Ev::Action::Added, usbDevice("dev-1", "Widget")});
    monitor.emit(Ev{Ev::Action::Changed, usbDevice("dev-1", "Widget")});
    monitor.emit(Ev{Ev::Action::Changed, usbDevice("dev-1", "Widget v2")});

    EXPECT_TRUE(waitFor([&] { return added.load() == 1; }, 1s));
    EXPECT_EQ(changed.load(), 0);  // burst collapsed to a single Added carrying the latest device
    EXPECT_EQ(service.findById(devmgr::core::DeviceId{"dev-1"})->name, "Widget v2");

    hotplug.stop();
    EXPECT_FALSE(monitor.started());
}

TEST(HotplugService, AddThenRemoveWithinWindowCancelsCleanly) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;
    devmgr::app::DeviceService service(bus);
    devmgr::test::FakeHotplugMonitor monitor;

    std::atomic<int> added{0}, removed{0};
    auto sA =
        bus.subscribe<devmgr::core::DeviceAddedEvent>([&](const auto&) { added.fetch_add(1); });
    auto sR =
        bus.subscribe<devmgr::core::DeviceRemovedEvent>([&](const auto&) { removed.fetch_add(1); });

    devmgr::app::HotplugService hotplug(monitor, service, timer, 30ms);
    ASSERT_TRUE(hotplug.start().has_value());

    using Ev = devmgr::pal::HotplugEvent;
    monitor.emit(Ev{Ev::Action::Added, usbDevice("dev-x", "Blip")});
    monitor.emit(Ev{Ev::Action::Removed, usbDevice("dev-x", "Blip")});

    // The net effect is a Remove of a never-committed device -> no-op, no phantom.
    std::this_thread::sleep_for(120ms);
    EXPECT_EQ(added.load(), 0);
    EXPECT_EQ(removed.load(), 0);
    EXPECT_TRUE(service.devices().empty());
    hotplug.stop();
}

// Regression/stress test for the confirmed use-after-free race: flush() must
// claim its outstanding status at schedule() time (in onEvent()), not from
// inside the callback itself, or stop()'s outstandingFlushes_==0 barrier can
// observe zero outstanding work and return early while DelayedScheduler's
// worker thread has already dequeued a flush() entry (so a subsequent
// cancel() on it reports false) but not yet acquired mutex_ to run it — at
// which point ~HotplugService() proceeds to destroy mutex_/pending_/etc.
// while that dequeued flush() is still about to touch them. See the class
// doc comment in hotplug_service.hpp.
//
// A true UAF race is inherently non-deterministic to force open on demand, so
// this test instead stresses the window heavily, mirroring
// StatusLineVM.RepeatedShortTtlDestructionDoesNotRaceOrHang: many
// construct/start/inject-event/destroy cycles against a single shared, busy
// DelayedScheduler with a ~0ms debounce window, so the flush is very likely
// already due — and often already dequeued by the worker thread — the
// instant the destructor runs. A hang or crash here is a real regression,
// not test flakiness (see the task report for the revert-experiment that
// confirms this test actually fails without the fix).
//
// kIterations is much larger here than StatusLineVM's analogous 200: this
// class's per-iteration constructor/destructor path is far cheaper (no
// EventBus subscribe()/unsubscribe() calls), so each loop iteration completes
// in only a few microseconds — far too fast, empirically, for
// DelayedScheduler's worker thread to reliably wake and dequeue an entry
// before the object is already gone. At 200-20,000 iterations the
// claim-inside-callback bug reproduced only intermittently (0/3-5 runs); at
// 100,000 it reproduced 5/5 (a glibc fatal error locking a destroyed mutex)
// while still completing in well under a second under the fix — see the task
// report for the full tuning data.
TEST(HotplugService, RepeatedNearZeroWindowDestructionDoesNotRaceOrHang) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;  // shared across iterations: one busy worker thread
    devmgr::app::DeviceService service(bus);

    constexpr int kIterations = 100000;
    for (int i = 0; i < kIterations; ++i) {
        devmgr::test::FakeHotplugMonitor monitor;
        devmgr::app::HotplugService hotplug(monitor, service, timer, 0ms);
        ASSERT_TRUE(hotplug.start().has_value());

        using Ev = devmgr::pal::HotplugEvent;
        monitor.emit(Ev{Ev::Action::Added, usbDevice("dev-" + std::to_string(i), "Widget")});
        // No synchronization delay here on purpose: destroying immediately
        // maximizes the chance the ~0ms flush is already due (or already
        // dequeued by the timer thread) right as the destructor's cancel()
        // calls race it.
    }
    SUCCEED();
}
