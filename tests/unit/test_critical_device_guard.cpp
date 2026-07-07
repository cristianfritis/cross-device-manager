#include <gtest/gtest.h>

#include "devmgr/services/critical_device_guard.hpp"

using devmgr::pal::CriticalityFacts;
using devmgr::services::evaluateDisable;

namespace {
CriticalityFacts facts() {
    CriticalityFacts f;
    f.rootBackingPaths = {"/sys/devices/pci0000:00/0000:00:1f.2/ata1/host0/target0/0:0:0:0"};
    f.bootBackingPaths = {"/sys/devices/pci0000:00/0000:00:1f.2/ata1/host0/target0/0:0:0:0"};
    f.keyboardPaths = {"/sys/devices/pci0000:00/usb1/1-3/1-3:1.0/input/input5"};
    f.pointerPaths = {"/sys/devices/pci0000:00/usb1/1-4/1-4:1.0/input/input6"};
    return f;
}
}  // namespace

TEST(CriticalDeviceGuardTest, AllowsUnrelatedDevice) {
    const auto v = evaluateDisable(facts(), "/sys/devices/pci0000:00/usb1/1-9");
    EXPECT_TRUE(v.allowed);
    EXPECT_TRUE(v.reason.empty());
}

TEST(CriticalDeviceGuardTest, RefusesRootBackingAncestor) {
    // Disabling the ATA controller would take the root disk with it.
    const auto v = evaluateDisable(facts(), "/sys/devices/pci0000:00/0000:00:1f.2");
    EXPECT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "backs the root filesystem");
}

TEST(CriticalDeviceGuardTest, RefusesExactRootBackingPath) {
    const auto v =
        evaluateDisable(facts(), "/sys/devices/pci0000:00/0000:00:1f.2/ata1/host0/target0/0:0:0:0");
    EXPECT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "backs the root filesystem");
}

TEST(CriticalDeviceGuardTest, PathBoundaryIsRespected) {
    // "1-1" must not match a root-backing path under "1-10" (prefix-with-boundary).
    CriticalityFacts f;
    f.rootBackingPaths = {"/sys/devices/pci0000:00/usb1/1-10/1-10:1.0/host7/block/sdb"};
    EXPECT_TRUE(evaluateDisable(f, "/sys/devices/pci0000:00/usb1/1-1").allowed);
    EXPECT_FALSE(evaluateDisable(f, "/sys/devices/pci0000:00/usb1/1-10").allowed);
}

TEST(CriticalDeviceGuardTest, RefusesSoleKeyboard) {
    const auto v = evaluateDisable(facts(), "/sys/devices/pci0000:00/usb1/1-3");
    EXPECT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "would disable the only keyboard");
}

TEST(CriticalDeviceGuardTest, AllowsKeyboardWhenAnotherRemains) {
    auto f = facts();
    f.keyboardPaths.push_back("/sys/devices/platform/i8042/serio0/input/input1");
    EXPECT_TRUE(evaluateDisable(f, "/sys/devices/pci0000:00/usb1/1-3").allowed);
}

TEST(CriticalDeviceGuardTest, RefusesSolePointer) {
    const auto v = evaluateDisable(facts(), "/sys/devices/pci0000:00/usb1/1-4");
    EXPECT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "would disable the only pointer");
}

TEST(CriticalDeviceGuardTest, NoFactsMeansAllowed) {
    EXPECT_TRUE(evaluateDisable(CriticalityFacts{}, "/sys/devices/pci0000:00/usb1/1-3").allowed);
}

TEST(CriticalDeviceGuardTest, RootRefusalWinsOverInputRefusal) {
    // A USB disk that both backs / and hosts the only keyboard: root reason first.
    CriticalityFacts f;
    f.rootBackingPaths = {"/sys/devices/pci0000:00/usb1/1-3/1-3:1.0/host7/block/sdb"};
    f.keyboardPaths = {"/sys/devices/pci0000:00/usb1/1-3/1-3:1.1/input/input5"};
    const auto v = evaluateDisable(f, "/sys/devices/pci0000:00/usb1/1-3");
    EXPECT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "backs the root filesystem");
}

TEST(EvaluateModuleUnload, CleanModuleIsAllowed) {
    const auto v = devmgr::services::evaluateModuleUnload({}, {});
    EXPECT_TRUE(v.allowed);
}

TEST(EvaluateModuleUnload, HoldersRefuseFirstWithNames) {
    devmgr::services::ModuleUnloadFacts m;
    m.holders = {"child_a", "child_b"};
    m.refCount = 2;
    const auto v = devmgr::services::evaluateModuleUnload({}, m);
    ASSERT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "in use by child_a, child_b");
}

TEST(EvaluateModuleUnload, NonZeroRefcountWithoutHoldersRefuses) {
    devmgr::services::ModuleUnloadFacts m;
    m.refCount = 3;
    const auto v = devmgr::services::evaluateModuleUnload({}, m);
    ASSERT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "in use (refcount 3)");
}

TEST(EvaluateModuleUnload, ModuleBackingRootDeviceRefusesWithGuardReason) {
    devmgr::pal::CriticalityFacts facts;
    facts.rootBackingPaths = {"/sys/devices/pci0000:00/0000:03:00.0/nvme/nvme0"};
    devmgr::services::ModuleUnloadFacts m;
    m.affectedDevicePaths = {"/sys/devices/pci0000:00/0000:03:00.0"};
    const auto v = devmgr::services::evaluateModuleUnload(facts, m);
    ASSERT_FALSE(v.allowed);
    // Prefix + the underlying evaluateDisable reason.
    EXPECT_EQ(v.reason.rfind("module backs a critical device: ", 0), 0U) << v.reason;
}

TEST(EvaluateModuleUnload, HarmlessBoundDevicesAllowed) {
    devmgr::pal::CriticalityFacts facts;
    facts.rootBackingPaths = {"/sys/devices/pci0000:00/0000:03:00.0/nvme/nvme0"};
    devmgr::services::ModuleUnloadFacts m;
    m.affectedDevicePaths = {"/sys/devices/pci0000:00/usb1/1-2"};  // unrelated device
    EXPECT_TRUE(devmgr::services::evaluateModuleUnload(facts, m).allowed);
}
