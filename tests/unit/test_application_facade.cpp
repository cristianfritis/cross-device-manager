#include <gtest/gtest.h>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"

using namespace devmgr;

namespace {
core::Device dev(std::string id) {
    core::Device d;
    d.id = core::DeviceId{std::move(id)};
    d.status = core::DeviceStatus::Active;
    return d;
}

// Enumerator that always fails — exercises the ErrorEvent path.
class FailingEnumerator final : public pal::IDeviceEnumerator {
   public:
    core::Result<std::vector<core::Device>> enumerate() override {
        return core::makeError(core::Error::Code::Io, "boom");
    }
};
}  // namespace

TEST(ApplicationFacadeTest, RefreshPopulatesModelAndEmitsAdded) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(dev("a"));
    pal.seedDevice(dev("b"));
    app::DeviceService svc(bus);
    std::atomic<int> added{0};
    auto sub = bus.subscribe<core::DeviceAddedEvent>([&](const auto&) { ++added; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    EXPECT_EQ(added.load(), 2);
    EXPECT_EQ(facade.devices().size(), 2u);
    EXPECT_TRUE(facade.findById(core::DeviceId{"a"}).has_value());
}

TEST(ApplicationFacadeTest, RefreshErrorEmitsErrorEventAndLeavesModelIntact) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    FailingEnumerator pal;
    app::DeviceService svc(bus);
    std::atomic<int> errors{0};
    auto sub = bus.subscribe<core::ErrorEvent>([&](const auto&) { ++errors; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    EXPECT_EQ(errors.load(), 1);
    EXPECT_EQ(facade.devices().size(), 0u);
}
