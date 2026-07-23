#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "devmgr/core/criticality.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_criticality_prober.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/inline_ui_dispatcher.hpp"

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

struct Fixture {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    test::FakePal pal;
    test::FakeCriticalityProber prober;
    app::DeviceService svc{bus};
    test::InlineUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal, scheduler, bus, svc, nullptr, &prober, &pal, &pal};
};

// Row index of the first row whose text contains `needle`, or -1.
int rowContaining(const std::vector<std::string>& rows, const std::string& needle) {
    for (int i = 0; std::cmp_less(i, rows.size()); ++i)
        if (rows[i].find(needle) != std::string::npos) return i;
    return -1;
}

}  // namespace

// --- DeviceListVM::nameForRow ------------------------------------------------

// R1 parity: the accessor is not a second opinion about the name, it is the
// same string the row was built from.
TEST(DeviceListVmCriticality, NameForRowMatchesTheRenderedRow) {
    Fixture f;
    f.pal.seedDevice(device("p1", core::BusType::Pci, "Audio Coprocessor",
                            "/sys/devices/pci0000:00/0000:c3:00.5"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();

    const int row = rowContaining(vm.rowsRef(), "Audio Coprocessor");
    ASSERT_GE(row, 0);
    const auto name = vm.nameForRow(row);
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "Audio Coprocessor");
    EXPECT_NE(vm.rowsRef()[row].find(*name), std::string::npos);
}

// An uncatalogued device: the accessor must expose the canonical label, never
// the kernel address the row used to show.
TEST(DeviceListVmCriticality, NameForRowExposesTheCanonicalLabelNotTheAddress) {
    Fixture f;
    core::Device uncatalogued =
        device("p2", core::BusType::Pci, "0000:c5:00.4", "/sys/devices/pci0000:c5/0000:c5:00.4");
    uncatalogued.properties["ID_VENDOR_FROM_DATABASE"] = "Advanced Micro Devices, Inc. [AMD]";
    uncatalogued.properties["ID_PCI_SUBCLASS_FROM_DATABASE"] = "USB controller";
    f.pal.seedDevice(uncatalogued);
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();

    const int row = rowContaining(vm.rowsRef(), "AMD USB controller");
    ASSERT_GE(row, 0) << "row text: " << (vm.rowsRef().empty() ? "" : vm.rowsRef().front());
    EXPECT_EQ(vm.nameForRow(row).value_or(""), "AMD USB controller");
}

TEST(DeviceListVmCriticality, HeaderAndPlaceholderRowsCarryNoNameOrCriticality) {
    Fixture f;
    f.pal.seedDevice(
        device("p1", core::BusType::Pci, "GPU", "/sys/devices/pci0000:00/0000:c3:00.0"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();

    const int header = rowContaining(vm.rowsRef(), "── PCI ──");
    ASSERT_GE(header, 0);
    EXPECT_FALSE(vm.nameForRow(header).has_value());
    EXPECT_FALSE(vm.criticalityForRow(header).has_value());

    for (const int oob : {-1, 9999}) {
        EXPECT_FALSE(vm.nameForRow(oob).has_value());
        EXPECT_FALSE(vm.criticalityForRow(oob).has_value());
    }
}

TEST(DeviceListVmCriticality, EmptyListPlaceholderCarriesNeither) {
    Fixture f;
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();
    // An empty model publishes no delta, so nothing posts a rebuild — drive it
    // directly to reach the "(no devices)" placeholder path.
    vm.rebuild();

    ASSERT_EQ(vm.rowsRef().size(), 1u);
    EXPECT_FALSE(vm.nameForRow(0).has_value());
    EXPECT_FALSE(vm.criticalityForRow(0).has_value());
}

// --- DeviceListVM::criticalityForRow ----------------------------------------

// The marker exists to predict the refusal, so this asserts against the guard
// itself rather than against a hardcoded expectation.
TEST(DeviceListVmCriticality, MarkedRowIsExactlyTheRowTheGuardWouldRefuse) {
    Fixture f;
    f.prober.next = pal::CriticalityFacts{.rootBackingPaths = {kRootDiskPath},
                                          .bootBackingPaths = {},
                                          .keyboardPaths = {},
                                          .pointerPaths = {}};
    f.pal.seedDevice(device("p1", core::BusType::Pci, "NVMe SSD", kRootDiskPath));
    f.pal.seedDevice(device("p2", core::BusType::Pci, "Audio Coprocessor",
                            "/sys/devices/pci0000:00/0000:c3:00.5"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();

    const int rootRow = rowContaining(vm.rowsRef(), "NVMe SSD");
    const int audioRow = rowContaining(vm.rowsRef(), "Audio Coprocessor");
    ASSERT_GE(rootRow, 0);
    ASSERT_GE(audioRow, 0);

    EXPECT_EQ(vm.criticalityForRow(rootRow).value_or(core::Criticality::Ordinary),
              core::Criticality::Essential);
    EXPECT_FALSE(f.facade.canDisable(core::DeviceId{"p1"}).allowed);

    EXPECT_EQ(vm.criticalityForRow(audioRow).value_or(core::Criticality::Essential),
              core::Criticality::Ordinary);
    EXPECT_TRUE(f.facade.canDisable(core::DeviceId{"p2"}).allowed);
}

// No prober wired (the D-Bus-free / degraded configuration): rows must be
// unmarked rather than guessed at, matching how the advisory guard degrades.
TEST(DeviceListVmCriticality, WithoutAProberNothingIsMarked) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    test::InlineUiDispatcher dispatcher;
    app::ApplicationFacade facade(pal, scheduler, bus, svc);  // prober defaults to nullptr
    pal.seedDevice(device("p1", core::BusType::Pci, "NVMe SSD", kRootDiskPath));

    app::DeviceListVM vm(facade, bus, dispatcher);
    facade.refresh().wait();

    const int row = rowContaining(vm.rowsRef(), "NVMe SSD");
    ASSERT_GE(row, 0);
    EXPECT_EQ(vm.criticalityForRow(row).value_or(core::Criticality::Essential),
              core::Criticality::Ordinary);
}

// Filtering re-runs rebuild() without re-probing; the surviving rows must keep
// their markers and stay aligned with their new indices.
TEST(DeviceListVmCriticality, MarkersSurviveFilteringAndStayAligned) {
    Fixture f;
    f.prober.next = pal::CriticalityFacts{.rootBackingPaths = {kRootDiskPath},
                                          .bootBackingPaths = {},
                                          .keyboardPaths = {},
                                          .pointerPaths = {}};
    f.pal.seedDevice(device("p1", core::BusType::Pci, "NVMe SSD", kRootDiskPath));
    f.pal.seedDevice(device("p2", core::BusType::Pci, "Audio Coprocessor",
                            "/sys/devices/pci0000:00/0000:c3:00.5"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();

    vm.setFilter("nvme");
    const int row = rowContaining(vm.rowsRef(), "NVMe SSD");
    ASSERT_GE(row, 0);
    EXPECT_EQ(vm.criticalityForRow(row).value_or(core::Criticality::Ordinary),
              core::Criticality::Essential);
    EXPECT_EQ(vm.nameForRow(row).value_or(""), "NVMe SSD");
    EXPECT_EQ(rowContaining(vm.rowsRef(), "Audio Coprocessor"), -1);
}

// The canonical name is filterable: a user types what the row shows.
TEST(DeviceListVmCriticality, FilterMatchesTheCanonicalName) {
    Fixture f;
    core::Device uncatalogued =
        device("p2", core::BusType::Pci, "0000:c5:00.4", "/sys/devices/pci0000:c5/0000:c5:00.4");
    uncatalogued.properties["ID_VENDOR_FROM_DATABASE"] = "Advanced Micro Devices, Inc. [AMD]";
    uncatalogued.properties["ID_PCI_SUBCLASS_FROM_DATABASE"] = "USB controller";
    f.pal.seedDevice(uncatalogued);
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();

    vm.setFilter("usb controller");
    EXPECT_GE(rowContaining(vm.rowsRef(), "AMD USB controller"), 0);

    vm.setFilter("0000:c5:00.4");  // the kernel name still matches too
    EXPECT_GE(rowContaining(vm.rowsRef(), "AMD USB controller"), 0);
}

// --- ModulesVM::criticalityForRow -------------------------------------------

TEST(ModulesVmCriticality, ClassifiesFromTheRowsOwnRefcountAndHolders) {
    Fixture f;
    f.pal.seedLoadedModule(core::LoadedModule{.name = "amdgpu", .sizeBytes = 1, .refCount = 0});
    f.pal.seedLoadedModule(
        core::LoadedModule{.name = "uvcvideo", .sizeBytes = 1, .refCount = 0, .holders = {}});
    f.pal.seedLoadedModule(core::LoadedModule{
        .name = "videobuf2_common", .sizeBytes = 1, .refCount = 2, .holders = {"uvcvideo"}});
    app::ModulesVM vm(f.facade, f.bus, f.scheduler, f.dispatcher);
    vm.rebuild();

    const int amdgpu = rowContaining(vm.rowsRef(), "amdgpu");
    const int uvcvideo = rowContaining(vm.rowsRef(), "uvcvideo");
    const int videobuf = rowContaining(vm.rowsRef(), "videobuf2_common");
    ASSERT_GE(amdgpu, 0);
    ASSERT_GE(uvcvideo, 0);
    ASSERT_GE(videobuf, 0);

    // Curated essential beats a zero refcount — that is the case refcounts miss.
    EXPECT_EQ(vm.criticalityForRow(amdgpu).value_or(core::Criticality::Ordinary),
              core::Criticality::Essential);
    EXPECT_EQ(vm.criticalityForRow(videobuf).value_or(core::Criticality::Ordinary),
              core::Criticality::Important);
    EXPECT_EQ(vm.criticalityForRow(uvcvideo).value_or(core::Criticality::Essential),
              core::Criticality::Ordinary);
}

TEST(ModulesVmCriticality, PlaceholderAndOutOfRangeRowsCarryNothing) {
    Fixture f;
    app::ModulesVM vm(f.facade, f.bus, f.scheduler, f.dispatcher);
    vm.rebuild();

    ASSERT_EQ(vm.rowsRef().size(), 1u);  // "(no modules)"
    EXPECT_FALSE(vm.criticalityForRow(0).has_value());
    EXPECT_FALSE(vm.criticalityForRow(-1).has_value());
    EXPECT_FALSE(vm.criticalityForRow(9999).has_value());
}

TEST(ModulesVmCriticality, StaysAlignedWithTheRowsAfterFiltering) {
    Fixture f;
    f.pal.seedLoadedModule(core::LoadedModule{.name = "amdgpu", .sizeBytes = 1, .refCount = 0});
    f.pal.seedLoadedModule(core::LoadedModule{.name = "uvcvideo", .sizeBytes = 1, .refCount = 0});
    app::ModulesVM vm(f.facade, f.bus, f.scheduler, f.dispatcher);
    vm.rebuild();

    vm.setFilter("amdgpu");
    const int row = rowContaining(vm.rowsRef(), "amdgpu");
    ASSERT_GE(row, 0);
    EXPECT_EQ(vm.criticalityForRow(row).value_or(core::Criticality::Ordinary),
              core::Criticality::Essential);
    EXPECT_EQ(rowContaining(vm.rowsRef(), "uvcvideo"), -1);
}
