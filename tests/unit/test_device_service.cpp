#include <atomic>

#include <gtest/gtest.h>

#include "devmgr/app/device_service.hpp"
#include "devmgr/core/events.hpp"
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
