#include <gtest/gtest.h>

#include <QCoreApplication>
#include <string>
#include <vector>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "devmgr/core/criticality.hpp"
#include "devmgr/core/device_presentation.hpp"
#include "devmgr/core/models.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_criticality_prober.hpp"
#include "fakes/fake_pal.hpp"
#include "gui/src/device_list_model.hpp"
#include "gui/src/module_list_model.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"

// R6 (task 11.2): the GUI is colourless this cycle (DESIGN §9 temporary parity
// exception), so every state the TUI colours it must convey in words from the
// SAME shared VM/core field. These offscreen tests prove the two GUI-only
// consequences of R6 — that the device list/detail show the canonical
// `core::displayDeviceName` (not the raw kernel address) and that criticality
// rides as text, not colour — against the exact seam the composition root
// builds (real QtUiDispatcher + criticality prober).

using namespace devmgr;

namespace {

constexpr const char* kRootDiskPath = "/sys/devices/pci0000:00/0000:c1:00.0";

core::Device device(std::string id, core::BusType bus, std::string name, std::string sysfsPath) {
    core::Device d;
    d.id = core::DeviceId{std::move(id)};
    d.bus = bus;
    d.name = std::move(name);
    d.sysfsPath = std::move(sysfsPath);
    d.vendorId = "1022";
    d.productId = "151b";
    d.status = core::DeviceStatus::Active;
    return d;
}

// Same wiring the GUI composition root uses: the real QtUiDispatcher delivers a
// cross-thread refresh to this (GUI) thread via processEvents(), and the
// criticality prober is present so the essential-device path is reachable.
struct Fixture {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    test::FakePal pal;
    test::FakeCriticalityProber prober;
    app::DeviceService svc{bus};
    gui::QtUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal, scheduler, bus, svc, nullptr, &prober, &pal, &pal};
    app::DeviceListVM listVm{facade, bus, dispatcher};
    app::DeviceDetailVM detailVm{facade};

    void refreshAndPump() {
        facade.refresh().wait();            // publish happened → rebuild queued
        QCoreApplication::processEvents();  // deliver it on this (GUI) thread
    }
};

std::string rowText(const QAbstractListModel& model, int row) {
    return model.data(model.index(row, 0), Qt::DisplayRole).toString().toStdString();
}

int rowContaining(const QAbstractListModel& model, const std::string& needle) {
    for (int i = 0; i < model.rowCount(); ++i)
        if (rowText(model, i).find(needle) != std::string::npos) return i;
    return -1;
}

bool lineContains(const std::vector<std::string>& lines, const std::string& label,
                  const std::string& value) {
    for (const auto& l : lines)
        if (l.find(label) != std::string::npos && l.find(value) != std::string::npos) return true;
    return false;
}

}  // namespace

// Scenario "GUI shows the same canonical name": an uncatalogued device whose raw
// name IS the kernel address but whose udev properties resolve a vendor+class
// name. The list label and the detail Name: row must both be that canonical
// string — the very one the TUI reads — with the address demoted to its own row.
TEST(GuiR6Parity, CanonicalNameInListAndDetailMatchesTheCoreFormatter) {
    Fixture f;
    core::Device dev =
        device("p1", core::BusType::Pci, "0000:c5:00.4", "/sys/devices/pci0000:c5/0000:c5:00.4");
    dev.properties["ID_VENDOR_FROM_DATABASE"] = "Advanced Micro Devices, Inc. [AMD]";
    dev.properties["ID_PCI_SUBCLASS_FROM_DATABASE"] = "USB controller";
    const std::string canonical = core::displayDeviceName(dev);
    ASSERT_EQ(canonical, "AMD USB controller");  // guards the fixture, not the SUT
    f.pal.seedDevice(dev);

    gui::DeviceListModel model(f.listVm);
    f.refreshAndPump();

    const int row = rowContaining(model, canonical);
    ASSERT_GE(row, 0) << "list did not show the canonical name";
    // The canonical name is the PRIMARY label; the raw id/address is demoted to
    // secondary text after it (spec: "the raw id appears only as secondary muted
    // text"), never standing in for the name.
    const std::string text = rowText(model, row);
    EXPECT_LT(text.find(canonical), text.find("0000:c5:00.4"))
        << "canonical name is not the primary label: " << text;
    EXPECT_LT(text.find(canonical), text.find("1022:151b"))
        << "canonical name is not the primary label: " << text;

    // Detail parity is structural: the Name: row is fed by the same
    // core::displayDeviceName, and the positional address survives as its own
    // R1 row rather than being lost now that the label is a name.
    f.listVm.selectedRef() = row;
    const auto lines = f.detailVm.lines(f.listVm.selectedDeviceId());
    EXPECT_TRUE(lineContains(lines, "Name:", canonical));
    EXPECT_TRUE(lineContains(lines, "Address:", "0000:c5:00.4"));
}

// Scenario "GUI shows criticality text without color": the essential device
// carries the criticality word in its DisplayRole text (list) and its Risk: row
// (detail); the ordinary control device carries neither; and the model exposes
// no colour role, proving the fact does not depend on colour.
TEST(GuiR6Parity, EssentialDeviceCriticalityIsTextNotColour) {
    Fixture f;
    f.prober.next = pal::CriticalityFacts{.rootBackingPaths = {kRootDiskPath},
                                          .bootBackingPaths = {},
                                          .keyboardPaths = {},
                                          .pointerPaths = {}};
    f.pal.seedDevice(device("p1", core::BusType::Pci, "NVMe SSD", kRootDiskPath));
    f.pal.seedDevice(device("p2", core::BusType::Pci, "Audio Coprocessor",
                            "/sys/devices/pci0000:00/0000:c3:00.5"));

    gui::DeviceListModel model(f.listVm);
    f.refreshAndPump();

    const std::string word = core::displayCriticality(core::Criticality::Essential);  // "essential"
    ASSERT_FALSE(word.empty());

    const int essentialRow = rowContaining(model, "NVMe SSD");
    ASSERT_GE(essentialRow, 0);
    EXPECT_NE(rowText(model, essentialRow).find(word), std::string::npos)
        << "essential device row lacks the criticality word: " << rowText(model, essentialRow);

    // No colour this cycle: the fact rides in the DisplayRole text and the model
    // offers no decoration/foreground role to carry it instead.
    const QModelIndex idx = model.index(essentialRow, 0);
    EXPECT_FALSE(model.data(idx, Qt::DecorationRole).isValid());
    EXPECT_FALSE(model.data(idx, Qt::ForegroundRole).isValid());

    // The device the guard would allow carries no criticality word at all.
    const int ordinaryRow = rowContaining(model, "Audio Coprocessor");
    ASSERT_GE(ordinaryRow, 0);
    EXPECT_EQ(rowText(model, ordinaryRow).find(word), std::string::npos);

    // Detail names the same risk in words.
    f.listVm.selectedRef() = essentialRow;
    EXPECT_TRUE(lineContains(f.detailVm.lines(f.listVm.selectedDeviceId()), "Risk:", word));
}

// The same parity holds for the Modules list: a curated-essential module carries
// the criticality word; an ordinary one does not. Both read the SAME
// ModulesVM::criticalityForRow the TUI marker reads.
TEST(GuiR6Parity, EssentialModuleCriticalityIsTextInTheModuleList) {
    Fixture f;
    f.pal.seedLoadedModule(core::LoadedModule{.name = "amdgpu", .sizeBytes = 1, .refCount = 0});
    f.pal.seedLoadedModule(core::LoadedModule{.name = "uvcvideo", .sizeBytes = 1, .refCount = 0});
    app::ModulesVM modulesVm{f.facade, f.bus, f.scheduler, f.dispatcher};
    gui::ModuleListModel model(modulesVm);  // ctor rebuilds from the seeded modules

    const std::string word = core::displayCriticality(core::Criticality::Essential);
    const int amdgpuRow = rowContaining(model, "amdgpu");
    const int uvcRow = rowContaining(model, "uvcvideo");
    ASSERT_GE(amdgpuRow, 0);
    ASSERT_GE(uvcRow, 0);

    EXPECT_NE(rowText(model, amdgpuRow).find(word), std::string::npos)
        << "essential module row lacks the criticality word: " << rowText(model, amdgpuRow);
    EXPECT_EQ(rowText(model, uvcRow).find(word), std::string::npos);
}
