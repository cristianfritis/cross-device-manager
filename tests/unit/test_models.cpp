#include <gtest/gtest.h>
#include <unordered_set>
#include "devmgr/core/models.hpp"

using namespace devmgr::core;

TEST(Models, StatusToString) {
    EXPECT_STREQ(to_string(DeviceStatus::Active), "Active");
    EXPECT_STREQ(to_string(DeviceStatus::Disabled), "Disabled");
    EXPECT_STREQ(to_string(DeviceStatus::Transitioning), "Transitioning");
    EXPECT_STREQ(to_string(DeviceStatus::Error), "Error");
    EXPECT_STREQ(to_string(DeviceStatus::Unknown), "Unknown");
}

TEST(Models, BusAndDriverKindToString) {
    EXPECT_STREQ(to_string(BusType::Pci), "Pci");
    EXPECT_STREQ(to_string(BusType::Usb), "Usb");
    EXPECT_STREQ(to_string(DriverKind::KernelModule), "KernelModule");
    EXPECT_STREQ(to_string(DriverKind::Firmware), "Firmware");
}

TEST(Models, DeviceIdIsHashableAndComparable) {
    DeviceId a{"pci:0000:00:1f.2"};
    DeviceId b{"pci:0000:00:1f.2"};
    DeviceId c{"usb:1-2"};
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a == c);
    std::unordered_set<DeviceId> seen{a};
    EXPECT_EQ(seen.count(b), 1u);
}
