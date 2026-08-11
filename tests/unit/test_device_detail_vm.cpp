#include <gtest/gtest.h>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/core/device_detail_fields.hpp"
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
    d.nativeId = "/sys/devices/usb1/1-3";
    d.status = core::DeviceStatus::Active;
    d.boundDriver = "usbhid";
    pal.seedDevice(d);

    core::Driver bound;
    bound.name = "usbhid";
    bound.isSigned = true;
    bound.signer = "Build key";
    core::Driver candidate;
    candidate.name = "dummy";
    pal.seedDriver(d.nativeId, bound);  // bound first => "bound first" ordering
    pal.seedDriver(d.nativeId, candidate);

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

// Bus casing comes from the shared core::displayBus() ("USB", not "Usb"), and
// the fixed-width label column gives every value a gap after its colon — the
// modalias line in particular gains the separating space it used to lack
// (beta-06 task 3.6 consistent presentation).
TEST(DeviceDetailVmTest, BusCasingAndHardwareIdSpacingAreConsistent) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    core::Device d;
    d.id = core::DeviceId{"u1"};
    d.bus = core::BusType::Usb;
    d.name = "Mouse";
    d.status = core::DeviceStatus::Active;
    d.hardwareId = "usb:v1D6Bp0002";
    pal.seedDevice(d);
    app::DeviceService svc(bus);
    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    app::DeviceDetailVM vm(facade);
    const auto lines = vm.lines(core::DeviceId{"u1"});
    bool busLine = false, hardwareIdLine = false;
    for (const auto& l : lines) {
        if (l == "Bus:         USB") busLine = true;  // displayBus, not "Usb"
        // "Hardware ID", never "Modalias": the label may not name a Linux
        // mechanism (ui-accessibility). One space after the colon on the widest
        // label, which is what sets the shared column width.
        if (l == "Hardware ID: usb:v1D6Bp0002") hardwareIdLine = true;
    }
    EXPECT_TRUE(busLine);
    EXPECT_TRUE(hardwareIdLine);
}

// R1 (task 11.1): the detail pane leads with the canonical name and then the
// three identity rows. The address must be shown somewhere — the list label is
// now a NAME rather than a bare kernel address, so without this row the address
// would simply be lost.
TEST(DeviceDetailVmTest, LeadsWithCanonicalNameThenAddressVidPidAndId) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    core::Device d;
    d.id = core::DeviceId{"pci-0000:c5:00.4"};
    d.name = "0000:c5:00.4";  // positional: the mapper's last-resort fallback
    d.bus = core::BusType::Pci;
    d.nativeId = "/sys/devices/pci0000:c0/0000:c0:08.3/0000:c5:00.4";
    d.vendorId = "1022";
    d.productId = "15b8";
    d.properties["ID_VENDOR_FROM_DATABASE"] = "Advanced Micro Devices, Inc. [AMD]";
    d.properties["ID_PCI_SUBCLASS_FROM_DATABASE"] = "USB controller";
    pal.seedDevice(d);
    app::DeviceService svc(bus);
    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();
    app::DeviceDetailVM vm(facade);

    const auto lines = vm.lines(d.id);
    ASSERT_GE(lines.size(), 4U);
    EXPECT_TRUE(lines[0].starts_with("Name:")) << lines[0];
    EXPECT_NE(lines[0].find("AMD USB controller"), std::string::npos) << lines[0];
    EXPECT_TRUE(lines[1].starts_with("Address:")) << lines[1];
    EXPECT_NE(lines[1].find("0000:c5:00.4"), std::string::npos) << lines[1];
    EXPECT_TRUE(lines[2].starts_with("VID:PID:")) << lines[2];
    EXPECT_NE(lines[2].find("1022:15b8"), std::string::npos) << lines[2];
    EXPECT_TRUE(lines[3].starts_with("Id:")) << lines[3];
}

// ui-accessibility: "A property the backend did not supply SHALL be omitted
// from the rendering rather than shown as an empty, placeholder, or
// literal-unknown row, so that a device enumerated by a backend with fewer
// properties reads as a smaller correct record rather than a damaged one."
TEST(DeviceDetailVmTest, AbsentPropertiesAreOmittedNotBlanked) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    core::Device d;  // the minimum a backend can report: id, bus, name, status
    d.id = core::DeviceId{"sparse-1"};
    d.bus = core::BusType::Usb;
    d.name = "Sparse Device";
    d.status = core::DeviceStatus::Active;
    pal.seedDevice(d);
    app::DeviceService svc(bus);
    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    app::DeviceDetailVM vm(facade);
    const auto lines = vm.lines(core::DeviceId{"sparse-1"});

    for (const auto& l : lines) {
        // Every rendered row carries a value after its padded label.
        const auto colon = l.find(':');
        ASSERT_NE(colon, std::string::npos) << l;
        const auto value = l.substr(colon + 1);
        EXPECT_NE(value.find_first_not_of(' '), std::string::npos)
            << "blank value row: [" << l << "]";
        EXPECT_EQ(l.find("unknown"), std::string::npos) << l;
    }
    // The rows whose values this backend did not supply are simply not there.
    for (const char* label : {"Serial:", "Identity:", "Hardware ID:", "Driver:", "VID:PID:"})
        for (const auto& l : lines)
            EXPECT_NE(l.rfind(label, 0), 0U) << "unsupplied property rendered: " << l;
    // ...and the ones the model always carries still are.
    bool sawName = false, sawId = false, sawBus = false, sawStatus = false;
    for (const auto& l : lines) {
        sawName = sawName || l.rfind("Name:", 0) == 0;
        sawId = sawId || l.rfind("Id:", 0) == 0;
        sawBus = sawBus || l.rfind("Bus:", 0) == 0;
        sawStatus = sawStatus || l.rfind("Status:", 0) == 0;
    }
    EXPECT_TRUE(sawName);
    EXPECT_TRUE(sawId);
    EXPECT_TRUE(sawBus);
    EXPECT_TRUE(sawStatus);
}

// "No label or value names a platform-specific enumeration mechanism, and the
// identity field is presented under a platform-neutral label." Both surfaces
// render these same lines, so one assertion covers the GUI and the TUI.
TEST(DeviceDetailVmTest, NoLabelNamesAPlatformMechanism) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    core::Device d;
    d.id = core::DeviceId{"pci-1"};
    d.bus = core::BusType::Pci;
    d.name = "Controller";
    d.status = core::DeviceStatus::Active;
    d.nativeId = "/sys/devices/pci0000:00/0000:00:14.0";
    d.hardwareId = "pci:v00008086d00009DED";
    d.serial = "SN123";
    d.boundDriver = "xhci_hcd";
    pal.seedDevice(d);
    app::DeviceService svc(bus);
    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    app::DeviceDetailVM vm(facade);
    const auto lines = vm.lines(core::DeviceId{"pci-1"});

    for (const auto& l : lines) {
        const auto colon = l.find(':');
        if (colon == std::string::npos) continue;  // section headings carry no label
        const std::string label = l.substr(0, colon);
        for (const char* mechanism : {"Sysfs", "sysfs", "Modalias", "modalias", "udev", "DEVPKEY",
                                      "CfgMgr", "SetupAPI", "Registry"})
            EXPECT_EQ(label.find(mechanism), std::string::npos)
                << "label names a platform mechanism: " << l;
    }
    // The identity field is still shown — neutrally labelled, not dropped.
    bool sawIdentity = false;
    for (const auto& l : lines)
        if (l.rfind("Identity:", 0) == 0) {
            sawIdentity = true;
            EXPECT_NE(l.find("/sys/devices/pci0000:00/0000:00:14.0"), std::string::npos) << l;
        }
    EXPECT_TRUE(sawIdentity);
}

// 5a.3/5a.4: the detail pane renders the shared vocabulary through the shared
// accessor, in core's order, with core's labels. The GUI splits these same
// lines on their first colon into (label, value) and the TUI prints them
// verbatim, so asserting the VM's lines asserts both surfaces at once — neither
// authors a label of its own.
TEST(DeviceDetailVmTest, DetailFieldsRenderInSharedOrderWithSharedLabels) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    core::Device d;
    d.id = core::DeviceId{"w1"};
    d.bus = core::BusType::Usb;
    d.name = "Wireless Receiver";
    d.status = core::DeviceStatus::Active;
    // Published out of order on purpose: the accessor imposes the order.
    d.properties[std::string(core::detailFieldKey(core::DetailField::DeviceInstanceId))] =
        "USB\\VID_046D&PID_C52B\\5&1234&0&2";
    d.properties[std::string(core::detailFieldKey(core::DetailField::Class))] = "HIDClass";
    d.properties[std::string(core::detailFieldKey(core::DetailField::Manufacturer))] = "Logitech";
    d.properties[std::string(core::detailFieldKey(core::DetailField::DriverVersion))] =
        "10.0.19041";
    pal.seedDevice(d);
    app::DeviceService svc(bus);
    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    app::DeviceDetailVM vm(facade);
    const auto lines = vm.lines(core::DeviceId{"w1"});

    std::vector<std::string> labels;
    for (const auto& l : lines) {
        const auto colon = l.find(':');
        if (colon == std::string::npos) continue;
        labels.push_back(l.substr(0, colon));
    }
    const auto indexOf = [&labels](const std::string& label) {
        for (std::size_t i = 0; i < labels.size(); ++i)
            if (labels[i] == label) return static_cast<int>(i);
        return -1;
    };
    for (const char* label : {"Manufacturer", "Driver Version", "Class", "Device Instance ID"})
        EXPECT_NE(indexOf(label), -1) << "missing detail field: " << label;
    EXPECT_LT(indexOf("Manufacturer"), indexOf("Driver Version"));
    EXPECT_LT(indexOf("Driver Version"), indexOf("Class"));
    EXPECT_LT(indexOf("Class"), indexOf("Device Instance ID"));

    // The published field replaces the model row for the same fact rather than
    // standing beside it — the user must not read one string twice under two
    // labels and assume they differ.
    EXPECT_EQ(indexOf("Identity"), -1);
    for (const auto& l : lines)
        if (l.rfind("Device Instance ID:", 0) == 0)
            EXPECT_NE(l.find("USB\\VID_046D&PID_C52B\\5&1234&0&2"), std::string::npos) << l;
}

// The mirror case: a backend that publishes nothing keeps the model's own
// identity row, so Linux is unchanged by the vocabulary existing.
TEST(DeviceDetailVmTest, ModelIdentityRowSurvivesWhenNoFieldIsPublished) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    core::Device d;
    d.id = core::DeviceId{"l1"};
    d.bus = core::BusType::Pci;
    d.name = "Controller";
    d.status = core::DeviceStatus::Active;
    d.nativeId = "/sys/devices/pci0000:00/0000:00:14.0";
    pal.seedDevice(d);
    app::DeviceService svc(bus);
    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    app::DeviceDetailVM vm(facade);
    bool sawIdentity = false;
    for (const auto& l : vm.lines(core::DeviceId{"l1"}))
        sawIdentity = sawIdentity || l.rfind("Identity:", 0) == 0;
    EXPECT_TRUE(sawIdentity);
}
