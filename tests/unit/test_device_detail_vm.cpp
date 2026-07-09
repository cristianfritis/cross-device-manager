#include <gtest/gtest.h>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"

using namespace devmgr;

TEST(DeviceDetailVmTest, RendersLabeledLinesForSelectedDevice) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    core::Device d;
    d.id = core::DeviceId{"u1"};
    d.bus = core::BusType::Usb;
    d.name = "Mouse";
    d.vendorId = "1d6b";
    d.productId = "0002";
    d.status = core::DeviceStatus::Active;
    pal.seedDevice(d);
    app::DeviceService svc(bus);
    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    app::DeviceDetailVM vm(facade);
    auto lines = vm.lines(core::DeviceId{"u1"});

    bool hasName = false, hasVidPid = false;
    for (const auto& l : lines) {
        if (l.find("Mouse") != std::string::npos) hasName = true;
        if (l.find("1d6b") != std::string::npos && l.find("0002") != std::string::npos)
            hasVidPid = true;
    }
    EXPECT_TRUE(hasName);
    EXPECT_TRUE(hasVidPid);
}

TEST(DeviceDetailVmTest, EmptySelectionYieldsPlaceholder) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    app::DeviceDetailVM vm(facade);

    auto lines = vm.lines(std::nullopt);
    ASSERT_FALSE(lines.empty());
    EXPECT_NE(lines.front().find("no device"), std::string::npos);
}

TEST(DeviceDetailVmTest, DriverSectionListsBoundFirstWithSignature) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    core::Device d;
    d.id = core::DeviceId{"u1"};
    d.bus = core::BusType::Usb;
    d.name = "Keyboard";
    d.sysfsPath = "/sys/devices/usb1/1-3";
    d.status = core::DeviceStatus::Active;
    d.boundDriver = "usbhid";
    pal.seedDevice(d);

    core::Driver bound;
    bound.name = "usbhid";
    bound.isSigned = true;
    bound.signer = "Build key";
    core::Driver candidate;
    candidate.name = "dummy";
    pal.seedDriver(d.sysfsPath, bound);  // bound first => "bound first" ordering
    pal.seedDriver(d.sysfsPath, candidate);

    app::DeviceService svc(bus);
    app::ApplicationFacade facade(pal, scheduler, bus, svc, nullptr, nullptr, &pal, nullptr);
    facade.refresh().wait();

    app::DeviceDetailVM vm(facade);
    const auto lines = vm.lines(core::DeviceId{"u1"});
    std::string all;
    for (const auto& l : lines) all += l + "\n";

    EXPECT_NE(all.find("— Driver —"), std::string::npos);
    EXPECT_NE(all.find("* usbhid"), std::string::npos);  // bound marker
    EXPECT_NE(all.find("— signed: Build key"), std::string::npos);
    EXPECT_NE(all.find("  dummy"), std::string::npos);  // candidate, unmarked
}
