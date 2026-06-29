#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include "devmgr/core/events.hpp"
#include "devmgr/runtime/event_bus.hpp"

using devmgr::core::DeviceAddedEvent;
using devmgr::runtime::EventBus;
using devmgr::runtime::Subscription;

TEST(EventBus, DeliversEventToSubscriber) {
    EventBus bus;
    int count = 0;
    auto sub = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { ++count; });
    bus.publish(DeviceAddedEvent{});
    EXPECT_EQ(count, 1);
}

TEST(EventBus, StopsDeliveryAfterUnsubscribe) {
    EventBus bus;
    int count = 0;
    {
        auto sub = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { ++count; });
        bus.publish(DeviceAddedEvent{});
    }  // sub destroyed -> unsubscribed
    bus.publish(DeviceAddedEvent{});
    EXPECT_EQ(count, 1);
}

TEST(EventBus, DeliversToAllSubscribers) {
    EventBus bus;
    int a = 0, b = 0;
    auto s1 = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { ++a; });
    auto s2 = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { ++b; });
    bus.publish(DeviceAddedEvent{});
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

TEST(EventBus, IsThreadSafeUnderConcurrentPublish) {
    EventBus bus;
    std::atomic<int> count{0};
    auto sub = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { ++count; });
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < 250; ++i) bus.publish(DeviceAddedEvent{});
        });
    for (auto& th : threads) th.join();
    EXPECT_EQ(count.load(), 1000);
}
