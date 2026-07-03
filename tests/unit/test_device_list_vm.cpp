#include <gtest/gtest.h>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/inline_ui_dispatcher.hpp"

using namespace devmgr;

namespace {
core::Device dev(std::string id, core::BusType bus, std::string name) {
    core::Device d;
    d.id = core::DeviceId{std::move(id)};
    d.bus = bus;
    d.name = std::move(name);
    d.status = core::DeviceStatus::Active;
    return d;
}

struct Fixture {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    test::FakePal pal;
    app::DeviceService svc{bus};
    test::InlineUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal, scheduler, bus, svc};
};
}  // namespace

TEST(DeviceListVmTest, RowsGroupedByBusAfterRefresh) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    f.pal.seedDevice(dev("p1", core::BusType::Pci, "GPU"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);

    f.facade.refresh().wait();  // InlineUiDispatcher applies the rebuild synchronously

    // PCI group precedes USB group; group headers present.
    const auto& rows = vm.rowsRef();
    ASSERT_GE(rows.size(), 4u);  // 2 headers + 2 devices
    auto idxPci = -1, idxUsb = -1, idxGpu = -1, idxMouse = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (rows[i].find("PCI") != std::string::npos) idxPci = i;
        if (rows[i].find("USB") != std::string::npos) idxUsb = i;
        if (rows[i].find("GPU") != std::string::npos) idxGpu = i;
        if (rows[i].find("Mouse") != std::string::npos) idxMouse = i;
    }
    EXPECT_LT(idxPci, idxUsb);
    EXPECT_LT(idxGpu, idxMouse);
}

TEST(DeviceListVmTest, FilterNarrowsCaseInsensitivelyAndClampsSelection) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Logitech Mouse"));
    f.pal.seedDevice(dev("p1", core::BusType::Pci, "NVIDIA GPU"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();

    vm.selectedRef() = static_cast<int>(vm.rowsRef().size()) - 1;
    vm.setFilter("mouse");  // case-insensitive

    bool anyGpu = false;
    bool anyMouse = false;
    for (const auto& r : vm.rowsRef()) {
        if (r.find("GPU") != std::string::npos) anyGpu = true;
        if (r.find("Mouse") != std::string::npos) anyMouse = true;
    }
    EXPECT_FALSE(anyGpu);
    EXPECT_TRUE(anyMouse);  // the matched device must survive — guards a reject-everything filter
    EXPECT_LT(vm.selectedRef(), static_cast<int>(vm.rowsRef().size()));  // clamped
    EXPECT_GE(vm.selectedRef(), 0);
}

TEST(DeviceListVmTest, SelectedDeviceIdMapsRowsAndIsNulloptOnHeader) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();

    // Header row (index 0) → nullopt; the device row → its id.
    vm.selectedRef() = 0;
    EXPECT_FALSE(vm.selectedDeviceId().has_value());
    for (int i = 0; i < static_cast<int>(vm.rowsRef().size()); ++i) {
        vm.selectedRef() = i;
        if (vm.rowsRef()[i].find("Mouse") != std::string::npos) {
            ASSERT_TRUE(vm.selectedDeviceId().has_value());
            EXPECT_EQ(vm.selectedDeviceId()->value, "u1");
        }
    }
}

TEST(DeviceListVmTest, PreservesSelectionByDeviceIdAcrossRebuild) {
    Fixture f;
    // Two USB devices; names chosen so inserting "Alpha" later shifts row order.
    f.pal.seedDevice(dev("dev-beta", core::BusType::Usb, "Beta"));

    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);

    f.facade.refresh().wait();  // publishes Added; InlineUiDispatcher rebuilds synchronously

    // Select "Beta" by finding its row.
    auto rowOf = [&](const char* id) {
        for (int i = 0; std::cmp_less(i, vm.rowsRef().size()); ++i) {
            vm.selectedRef() = i;
            auto sel = vm.selectedDeviceId();
            if (sel && sel->value == id) return i;
        }
        return -1;
    };
    const int betaRow = rowOf("dev-beta");
    ASSERT_GE(betaRow, 0);
    vm.selectedRef() = betaRow;

    // A new device "Alpha" appears and sorts before "Beta", shifting indices.
    core::Device alpha;
    alpha.id = core::DeviceId{"dev-alpha"};
    alpha.name = "Alpha";
    alpha.bus = core::BusType::Usb;
    alpha.status = core::DeviceStatus::Active;
    f.svc.applyDelta(
        pal::HotplugEvent{.action = pal::HotplugEvent::Action::Added, .device = alpha});

    // Selection must still resolve to Beta, not whatever now sits at the old index.
    auto sel = vm.selectedDeviceId();
    ASSERT_TRUE(sel.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_EQ(sel->value, "dev-beta");
}

TEST(DeviceListVmTest, IsHeaderDistinguishesHeaderAndDeviceRows) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();

    ASSERT_EQ(vm.rowsRef().size(), 2u);  // one USB header + one device
    int headers = 0;
    int devices = 0;
    for (int i = 0; std::cmp_less(i, vm.rowsRef().size()); ++i) {
        vm.selectedRef() = i;
        if (vm.isHeader(i)) {
            ++headers;
            EXPECT_FALSE(vm.selectedDeviceId().has_value());  // header ⇔ no DeviceId
        } else {
            ++devices;
            EXPECT_TRUE(vm.selectedDeviceId().has_value());
        }
    }
    EXPECT_EQ(headers, 1);
    EXPECT_EQ(devices, 1);
    EXPECT_FALSE(vm.isHeader(-1));  // out of range is never a header
    EXPECT_FALSE(vm.isHeader(9999));
}

TEST(DeviceListVmTest, RebuildHooksBracketDeltaAndFilterRebuilds) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    std::vector<std::string> log;
    vm.setRebuildHooks([&] { log.emplace_back("before"); }, [&] { log.emplace_back("after"); });

    f.facade.refresh().wait();  // one Added event → one rebuild (InlineUiDispatcher: synchronous)
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "before");
    EXPECT_EQ(log[1], "after");

    vm.setFilter("mouse");  // filter path calls rebuild() directly — must also be bracketed
    ASSERT_EQ(log.size(), 4u);
    EXPECT_EQ(log[2], "before");
    EXPECT_EQ(log[3], "after");

    vm.setRebuildHooks({}, {});  // cleared hooks: rebuild must not crash and log stays frozen
    vm.setFilter("");
    EXPECT_EQ(log.size(), 4u);
}
