#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
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

// Barrier contract: unsubscribe() must not return while the handler is
// executing on another thread (the Phase 2 documented race).
TEST(EventBus, UnsubscribeWaitsForInFlightCallback) {
    EventBus bus;
    std::atomic<bool> inCallback{false};
    std::atomic<bool> release{false};
    std::atomic<bool> finished{false};
    auto sub = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) {
        inCallback = true;
        while (!release) std::this_thread::yield();
        finished = true;
    });
    std::thread publisher([&] { bus.publish(DeviceAddedEvent{}); });
    while (!inCallback) std::this_thread::yield();
    std::thread releaser([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        release = true;
    });
    sub.reset();  // must block until the in-flight callback returns
    EXPECT_TRUE(finished.load());
    publisher.join();
    releaser.join();
}

TEST(EventBus, ReentrantUnsubscribeFromOwnCallbackDoesNotDeadlock) {
    EventBus bus;
    int count = 0;
    Subscription sub;
    sub = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) {
        ++count;
        sub.reset();  // self-unsubscribe from inside the callback
    });
    bus.publish(DeviceAddedEvent{});
    bus.publish(DeviceAddedEvent{});
    EXPECT_EQ(count, 1);
}

// A handler unsubscribed by an earlier handler in the same publish must not
// fire, even though it was in the publish snapshot.
TEST(EventBus, UnsubscribeFromCallbackSuppressesPendingDelivery) {
    EventBus bus;
    int bCount = 0;
    Subscription b;
    auto a = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { b.reset(); });
    b = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { ++bCount; });
    bus.publish(DeviceAddedEvent{});
    EXPECT_EQ(bCount, 0);
}

// Cross-thread teardown race: once unsubscribe returns, the handler never
// runs again no matter how many publishers are mid-flight.
TEST(EventBus, NoDeliveryAfterUnsubscribeReturnsUnderConcurrentPublish) {
    EventBus bus;
    for (int round = 0; round < 20; ++round) {
        std::atomic<int> count{0};
        auto sub = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { ++count; });
        std::atomic<bool> stop{false};
        std::vector<std::thread> publishers;
        for (int t = 0; t < 4; ++t)
            publishers.emplace_back([&] {
                while (!stop) bus.publish(DeviceAddedEvent{});
            });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        sub.reset();
        const int atUnsubscribe = count.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        EXPECT_EQ(count.load(), atUnsubscribe);
        stop = true;
        for (auto& th : publishers) th.join();
    }
}
