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
