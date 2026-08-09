#include <gtest/gtest.h>

#include "devmgr/core/identity.hpp"

using devmgr::core::identityTail;

TEST(IdentityTailTest, SlashSeparatedYieldsLastSegment) {
    EXPECT_EQ(identityTail("/sys/devices/pci0000:00/usb3/3-1"), "3-1");
    EXPECT_EQ(identityTail("/sys/devices/pci0000:c0/0000:c5:00.4"), "0000:c5:00.4");
}

TEST(IdentityTailTest, BackslashSeparatedYieldsLastSegment) {
    EXPECT_EQ(identityTail("USB\\VID_046D&PID_C52B\\5&1234&0&2"), "5&1234&0&2");
    EXPECT_EQ(identityTail("ACPI\\PNP0C0C\\2&daba3ff&1"), "2&daba3ff&1");
}

TEST(IdentityTailTest, MixedSeparatorsSplitOnWhicheverIsLater) {
    // The later separator wins regardless of which kind it is.
    EXPECT_EQ(identityTail("/sys/devices/odd\\name"), "name");
    EXPECT_EQ(identityTail("PCI\\VEN_1022/tail"), "tail");
}

TEST(IdentityTailTest, TrailingSeparatorYieldsEmptyTail) {
    EXPECT_EQ(identityTail("/sys/devices/usb1/"), "");
    EXPECT_EQ(identityTail("USB\\VID_046D&PID_C52B\\"), "");
}

TEST(IdentityTailTest, NoSeparatorReturnsInputUnchanged) {
    EXPECT_EQ(identityTail("3-1"), "3-1");
    EXPECT_EQ(identityTail("ROOT_HUB"), "ROOT_HUB");
}

TEST(IdentityTailTest, EmptyInputReturnsEmpty) {
    EXPECT_EQ(identityTail(""), "");
}

TEST(IdentityTailTest, SeparatorOnlyYieldsEmptyTail) {
    EXPECT_EQ(identityTail("/"), "");
    EXPECT_EQ(identityTail("\\"), "");
}
