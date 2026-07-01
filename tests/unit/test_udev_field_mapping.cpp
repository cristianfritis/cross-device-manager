#include <gtest/gtest.h>

#include "devmgr/platform/linux/udev_field_mapping.hpp"

using namespace devmgr::platform_linux;

TEST(UdevFieldMapping, BusForKnownSubsystems) {
    EXPECT_EQ(busFor("pci"), devmgr::core::BusType::Pci);
    EXPECT_EQ(busFor("usb"), devmgr::core::BusType::Usb);
    EXPECT_EQ(busFor("platform"), devmgr::core::BusType::Platform);
    EXPECT_EQ(busFor("virtio"), devmgr::core::BusType::Virtio);
    EXPECT_EQ(busFor("block"), devmgr::core::BusType::Other);
}

// Pins the FNV-1a-64 constants to the canonical reference vectors. The
// empty-string case returns the offset basis unchanged (no loop iterations),
// so it guards specifically against an offset-basis typo; "a" and "foobar"
// additionally exercise the prime-multiply path.
TEST(UdevFieldMapping, Fnv1a64MatchesCanonicalVectors) {
    EXPECT_EQ(fnv1a64(""), 14695981039346656037ULL);       // offset basis
    EXPECT_EQ(fnv1a64("a"), 12638187200555641996ULL);      // 0xaf63dc4c8601ec8c
    EXPECT_EQ(fnv1a64("foobar"), 9625390261332436968ULL);  // 0x85944171f73967e8
}

TEST(UdevFieldMapping, Strip0xPrefix) {
    EXPECT_EQ(strip0x("0x8086"), "8086");
    EXPECT_EQ(strip0x("0X8086"), "8086");  // uppercase prefix also stripped
    EXPECT_EQ(strip0x("1d6b"), "1d6b");
    EXPECT_EQ(strip0x(""), "");
}

TEST(UdevFieldMapping, StableIdIsDeterministicAndFormatted) {
    const auto a = stableId("usb", "/sys/devices/pci0000:00/usb1", "1d6b", "0002", "");
    const auto b = stableId("usb", "/sys/devices/pci0000:00/usb1", "1d6b", "0002", "");
    EXPECT_EQ(a, b);                    // stable across calls
    EXPECT_EQ(a.rfind("dev-", 0), 0u);  // "dev-" prefix
    EXPECT_EQ(a.size(), 20u);           // "dev-" + 16 hex chars
}

TEST(UdevFieldMapping, StableIdDistinguishesDevices) {
    EXPECT_NE(stableId("usb", "/sys/a", "1d6b", "0002", ""),
              stableId("usb", "/sys/b", "1d6b", "0002", ""));
}

TEST(UdevFieldMapping, FirstNonEmptyPicksFirstUsableValue) {
    const char* a = nullptr;
    const char* b = "";
    const char* c = "good";
    EXPECT_EQ(firstNonEmpty({a, b, c}), "good");
    EXPECT_EQ(firstNonEmpty({a, b}), "");
}

TEST(UdevFieldMapping, ActionFromStringCoversTheFullUdevActionSet) {
    using devmgr::platform_linux::actionFromString;
    using Action = devmgr::pal::HotplugEvent::Action;

    EXPECT_EQ(actionFromString("add"), Action::Added);
    EXPECT_EQ(actionFromString("remove"), Action::Removed);
    for (const char* changed : {"change", "bind", "unbind", "move", "online", "offline"}) {
        EXPECT_EQ(actionFromString(changed), Action::Changed) << changed;
    }
    EXPECT_EQ(actionFromString(nullptr), std::nullopt);
    EXPECT_EQ(actionFromString("bogus"), std::nullopt);
}
