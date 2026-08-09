#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "devmgr/core/criticality.hpp"
#include "devmgr/services/critical_device_guard.hpp"

using namespace devmgr;

namespace {

constexpr const char* kRootDisk = "/sys/devices/pci0000:00/0000:c1:00.0/nvme/nvme0";
constexpr const char* kKeyboard = "/sys/devices/platform/i8042/serio0";
constexpr const char* kWebcam = "/sys/devices/pci0000:00/usb2/2-1";

pal::CriticalityFacts facts() {
    return pal::CriticalityFacts{.rootBackingPaths = {kRootDisk},
                                 .bootBackingPaths = {},
                                 .keyboardPaths = {kKeyboard},
                                 .pointerPaths = {}};
}

}  // namespace

// --- Devices: the marker IS the guard's verdict ------------------------------

// The whole point of classifying from facts: whatever the marker says, the
// guard says the same thing, because it is the same call on the same facts.
TEST(ClassifyDevice, AgreesWithTheGuardItWillBeCheckedAgainst) {
    const auto f = facts();
    for (const char* path : {kRootDisk, kKeyboard, kWebcam}) {
        const bool refused = !services::evaluateDisable(f, path).allowed;
        const auto level = core::classifyDevice(f, path);
        EXPECT_EQ(refused, level == core::Criticality::Essential) << "path: " << path;
    }
}

TEST(ClassifyDevice, RootBackingDeviceIsEssential) {
    EXPECT_EQ(core::classifyDevice(facts(), kRootDisk), core::Criticality::Essential);
}

TEST(ClassifyDevice, SoleKeyboardIsEssential) {
    EXPECT_EQ(core::classifyDevice(facts(), kKeyboard), core::Criticality::Essential);
}

TEST(ClassifyDevice, OrdinaryDeviceCarriesNoMarker) {
    EXPECT_EQ(core::classifyDevice(facts(), kWebcam), core::Criticality::Ordinary);
}

// A parent subtree containing the root disk is itself essential — disabling the
// controller takes the disk with it.
TEST(ClassifyDevice, AncestorOfACriticalDeviceIsEssential) {
    EXPECT_EQ(core::classifyDevice(facts(), "/sys/devices/pci0000:00/0000:c1:00.0"),
              core::Criticality::Essential);
}

// A second keyboard means removing either one is survivable, so neither is
// essential — the marker must track the machine's current state, not a name.
TEST(ClassifyDevice, SecondKeyboardDemotesBothToOrdinary) {
    pal::CriticalityFacts f = facts();
    f.keyboardPaths.emplace_back("/sys/devices/pci0000:00/usb3/3-1");
    EXPECT_EQ(core::classifyDevice(f, kKeyboard), core::Criticality::Ordinary);
    EXPECT_EQ(core::classifyDevice(f, "/sys/devices/pci0000:00/usb3/3-1"),
              core::Criticality::Ordinary);
}

TEST(ClassifyDevice, EmptyFactsMarkNothing) {
    EXPECT_EQ(core::classifyDevice(pal::CriticalityFacts{}, kRootDisk),
              core::Criticality::Ordinary);
}

// --- Modules: curated list over live facts ----------------------------------

// The case refcounts cannot see: nothing holds the display driver right now,
// and unloading it still blanks the session.
TEST(ClassifyModule, CuratedEssentialModuleIsMarkedEvenAtRefcountZero) {
    for (const char* name : {"amdgpu", "ext4", "nvme", "xhci_hcd", "usbcore", "i8042"}) {
        EXPECT_EQ(core::classifyModule(name, 0, {}), core::Criticality::Essential)
            << "module: " << name;
    }
}

TEST(ClassifyModule, InUseModuleIsImportant) {
    EXPECT_EQ(core::classifyModule("snd_hda_intel", 3, {}), core::Criticality::Important);
    EXPECT_EQ(core::classifyModule("videobuf2_common", 0, {"uvcvideo"}),
              core::Criticality::Important);
}

TEST(ClassifyModule, SecurityModuleIsImportantNotEssential) {
    for (const char* name : {"apparmor", "selinux", "ima", "integrity", "lockdown"}) {
        EXPECT_EQ(core::classifyModule(name, 0, {}), core::Criticality::Important)
            << "module: " << name;
    }
}

TEST(ClassifyModule, EssentialOutranksInUse) {
    EXPECT_EQ(core::classifyModule("ext4", 12, {"overlay"}), core::Criticality::Essential);
}

TEST(ClassifyModule, UnusedOrdinaryModuleCarriesNoMarker) {
    EXPECT_EQ(core::classifyModule("uvcvideo", 0, {}), core::Criticality::Ordinary);
}

// Exact match only. A prefix or substring rule would mark unrelated modules,
// and a marker that cries wolf is one users stop reading.
TEST(ClassifyModule, MatchingIsExactNotSubstring) {
    for (const char* name :
         {"ext4_extra", "my_amdgpu", "nvme_core_helper", "hidraw", "pcie_dummy"}) {
        EXPECT_EQ(core::classifyModule(name, 0, {}), core::Criticality::Ordinary)
            << "module: " << name;
    }
}

TEST(ClassifyModule, EmptyNameIsOrdinary) {
    EXPECT_EQ(core::classifyModule("", 0, {}), core::Criticality::Ordinary);
}

// --- The user-facing word ---------------------------------------------------

// Every tier that shows a marker must also have a word, or mono/plain terminals
// lose the signal entirely (DESIGN.md §10).
TEST(DisplayCriticality, EveryMarkedTierHasAWord) {
    EXPECT_STREQ(core::displayCriticality(core::Criticality::Essential), "essential");
    EXPECT_STREQ(core::displayCriticality(core::Criticality::Important), "important");
    EXPECT_STREQ(core::displayCriticality(core::Criticality::Ordinary), "");
}
