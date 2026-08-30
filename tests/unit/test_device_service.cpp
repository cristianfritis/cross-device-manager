#include <atomic>

#include <gtest/gtest.h>

#include "devmgr/app/device_service.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/runtime/event_bus.hpp"

using namespace devmgr;

namespace {
core::Device dev(std::string id, std::string name = "n") {
    core::Device d;
    d.id = core::DeviceId{std::move(id)};
    d.name = std::move(name);
    d.status = core::DeviceStatus::Active;
    return d;
}
}  // namespace

TEST(DeviceServiceTest, FirstEnumerationEmitsAddedPerDevice) {
    runtime::EventBus bus;
    std::atomic<int> added{0}, removed{0}, changed{0};
    auto s1 = bus.subscribe<core::DeviceAddedEvent>([&](const auto&) { ++added; });
    auto s2 = bus.subscribe<core::DeviceRemovedEvent>([&](const auto&) { ++removed; });
    auto s3 = bus.subscribe<core::DeviceChangedEvent>([&](const auto&) { ++changed; });

    app::DeviceService svc(bus);
    svc.applyEnumeration({dev("a"), dev("b")});

    EXPECT_EQ(added.load(), 2);
    EXPECT_EQ(removed.load(), 0);
    EXPECT_EQ(changed.load(), 0);
    EXPECT_EQ(svc.devices().size(), 2u);
}

TEST(DeviceServiceTest, ReapplyingIdenticalSnapshotEmitsNothing) {
    runtime::EventBus bus;
    std::atomic<int> events{0};
    auto s1 = bus.subscribe<core::DeviceAddedEvent>([&](const auto&) { ++events; });
    auto s2 = bus.subscribe<core::DeviceRemovedEvent>([&](const auto&) { ++events; });
    auto s3 = bus.subscribe<core::DeviceChangedEvent>([&](const auto&) { ++events; });

    app::DeviceService svc(bus);
    svc.applyEnumeration({dev("a"), dev("b")});
    events = 0;
    svc.applyEnumeration({dev("a"), dev("b")});  // identical

    EXPECT_EQ(events.load(), 0);
    EXPECT_EQ(svc.devices().size(), 2u);  // model unchanged, not cleared
}

TEST(DeviceServiceTest, DeltaEmitsAddedRemovedChanged) {
    runtime::EventBus bus;
    std::atomic<int> added{0}, removed{0}, changed{0};
    auto s1 = bus.subscribe<core::DeviceAddedEvent>([&](const auto&) { ++added; });
    auto s2 = bus.subscribe<core::DeviceRemovedEvent>([&](const auto&) { ++removed; });
    auto s3 = bus.subscribe<core::DeviceChangedEvent>([&](const auto&) { ++changed; });

    app::DeviceService svc(bus);
    svc.applyEnumeration({dev("a", "old"), dev("b")});
    added = removed = changed = 0;
    // 'a' name changes, 'b' removed, 'c' added.
    svc.applyEnumeration({dev("a", "new"), dev("c")});

    EXPECT_EQ(added.load(), 1);
    EXPECT_EQ(removed.load(), 1);
    EXPECT_EQ(changed.load(), 1);
    EXPECT_EQ(svc.findById(core::DeviceId{"a"})->name, "new");
    EXPECT_FALSE(svc.findById(core::DeviceId{"b"}).has_value());
    EXPECT_TRUE(svc.findById(core::DeviceId{"c"}).has_value());  // added device is in the model
}

TEST(DeviceServiceTest, NoDeadlockWhenHandlerReadsDevicesDuringPublish) {
    runtime::EventBus bus;
    app::DeviceService svc(bus);
    std::atomic<std::size_t> seen{0};
    auto sub = bus.subscribe<core::DeviceAddedEvent>(
        [&](const auto&) { seen = svc.devices().size(); });  // read during publish
    svc.applyEnumeration({dev("a")});
    EXPECT_EQ(seen.load(), 1u);
}

TEST(DeviceServiceDelta, AddThenNoopThenChangeThenRemove) {
    using devmgr::pal::HotplugEvent;
    devmgr::runtime::EventBus bus;
    devmgr::app::DeviceService service(bus);

    int added = 0, changed = 0, removed = 0;
    auto sA = bus.subscribe<devmgr::core::DeviceAddedEvent>([&](const auto&) { ++added; });
    auto sC = bus.subscribe<devmgr::core::DeviceChangedEvent>([&](const auto&) { ++changed; });
    auto sR = bus.subscribe<devmgr::core::DeviceRemovedEvent>([&](const auto&) { ++removed; });

    devmgr::core::Device dev;
    dev.id = devmgr::core::DeviceId{"dev-1"};
    dev.name = "Widget";

    service.applyDelta(HotplugEvent{HotplugEvent::Action::Added, dev});
    EXPECT_EQ(added, 1);
    EXPECT_EQ(service.devices().size(), 1u);

    // Added again, identical -> no-op (no event).
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Added, dev});
    EXPECT_EQ(added, 1);
    EXPECT_EQ(changed, 0);

    // Changed with a real difference -> DeviceChanged.
    dev.name = "Widget v2";
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Changed, dev});
    EXPECT_EQ(changed, 1);
    EXPECT_EQ(service.findById(devmgr::core::DeviceId{"dev-1"})->name, "Widget v2");

    // Removed -> DeviceRemoved, model empty.
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Removed, dev});
    EXPECT_EQ(removed, 1);
    EXPECT_TRUE(service.devices().empty());
}

// Real udev strips ID_VENDOR_ID / ID_MODEL_ID / ID_SERIAL_SHORT from `remove`
// uevents, so the hotplug monitor's mapDevice() computes a DIFFERENT DeviceId
// on removal than the one stored at add time. Only the platform-native id
// (syspath on Linux, instance id on Windows) survives on both. applyDelta()
// must fall back to matching a removal on nativeId so the device actually
// leaves the model on a physical unplug.
TEST(DeviceServiceDelta, RemovalMatchesOnNativeIdWhenTheDerivedIdDiffers) {
    using devmgr::pal::HotplugEvent;
    devmgr::runtime::EventBus bus;
    devmgr::app::DeviceService service(bus);

    int removed = 0;
    devmgr::core::DeviceId removedId{""};
    auto sR = bus.subscribe<devmgr::core::DeviceRemovedEvent>([&](const auto& e) {
        ++removed;
        removedId = e.id;
    });

    devmgr::core::Device added;
    added.id = devmgr::core::DeviceId{"dev-fullprops"};
    added.nativeId = "/sys/devices/pci0000:00/0000:00:08.3/0000:c5:00.0/usb4/4-1";
    added.name = "DataTraveler";
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Added, added});
    ASSERT_EQ(service.devices().size(), 1u);

    // The removal event carries the same nativeId but a degraded derived id.
    devmgr::core::Device removeEvent;
    removeEvent.id = devmgr::core::DeviceId{"dev-degraded"};
    removeEvent.nativeId = added.nativeId;
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Removed, removeEvent});

    EXPECT_EQ(removed, 1);
    EXPECT_EQ(removedId.value, "dev-fullprops");  // the model's real id, not the event's
    EXPECT_TRUE(service.devices().empty());
}

// The nativeId fallback must not turn an unrelated removal into a spurious
// erase: an empty nativeId on the event never matches a real device.
TEST(DeviceServiceDelta, RemovalWithEmptyNativeIdDoesNotMatchByFallback) {
    using devmgr::pal::HotplugEvent;
    devmgr::runtime::EventBus bus;
    devmgr::app::DeviceService service(bus);

    int removed = 0;
    auto sR = bus.subscribe<devmgr::core::DeviceRemovedEvent>([&](const auto&) { ++removed; });

    devmgr::core::Device added;
    added.id = devmgr::core::DeviceId{"dev-1"};
    added.nativeId = "";  // a backend that does not populate it
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Added, added});

    devmgr::core::Device removeEvent;
    removeEvent.id = devmgr::core::DeviceId{"dev-other"};
    removeEvent.nativeId = "";
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Removed, removeEvent});

    EXPECT_EQ(removed, 0);
    EXPECT_EQ(service.devices().size(), 1u);
}

TEST(DeviceServiceDelta, ChangeOfUnknownActsAsAddAndRemoveOfAbsentIsNoop) {
    using devmgr::pal::HotplugEvent;
    devmgr::runtime::EventBus bus;
    devmgr::app::DeviceService service(bus);

    int added = 0, removed = 0;
    auto sA = bus.subscribe<devmgr::core::DeviceAddedEvent>([&](const auto&) { ++added; });
    auto sR = bus.subscribe<devmgr::core::DeviceRemovedEvent>([&](const auto&) { ++removed; });

    devmgr::core::Device dev;
    dev.id = devmgr::core::DeviceId{"ghost"};

    // Remove of absent device -> no-op.
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Removed, dev});
    EXPECT_EQ(removed, 0);

    // Change for an id we've never seen -> treated as Added.
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Changed, dev});
    EXPECT_EQ(added, 1);
    EXPECT_EQ(service.devices().size(), 1u);
}
