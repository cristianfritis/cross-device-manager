#include <gtest/gtest.h>

#include "devmgr/app/disabled_overlay.hpp"
#include "devmgr/services/device_key.hpp"

using devmgr::core::BusType;
using devmgr::core::Device;
using devmgr::core::DeviceStatus;
using devmgr::core::DisabledDeviceEntry;

namespace {
Device usb(std::string path, std::string serial) {
    Device d;
    d.bus = BusType::Usb;
    d.sysfsPath = std::move(path);
    d.vendorId = "046d";
    d.productId = "c52b";
    d.serial = std::move(serial);
    d.status = DeviceStatus::Active;
    return d;
}
}  // namespace

TEST(DisabledOverlay, MatchingDeviceRendersDisabledEvenWhileTransientlyBound) {
    std::vector<Device> devices{usb("/sys/devices/usb1/1-9", "AB12")};  // replugged, new port
    DisabledDeviceEntry e;
    e.key = devmgr::services::makeDeviceKey(usb("/sys/devices/usb2/2-1", "AB12"));
    e.lastSysfsPath = "/sys/devices/usb2/2-1";
    devmgr::app::applyDisabledOverlay(devices, {e});
    EXPECT_EQ(devices[0].status, DeviceStatus::Disabled);  // flicker suppressed
}

TEST(DisabledOverlay, GuardSuspensionSurfacesInErrorNote) {
    std::vector<Device> devices{usb("/sys/devices/usb2/2-1", "AB12")};
    DisabledDeviceEntry e;
    e.key = devmgr::services::makeDeviceKey(devices[0]);
    e.guardSuspended = true;
    devmgr::app::applyDisabledOverlay(devices, {e});
    ASSERT_TRUE(devices[0].errorNote.has_value());
    EXPECT_NE(devices[0].errorNote->find("enforcement suspended"), std::string::npos);
}

TEST(DisabledOverlay, UnrelatedDevicesUntouched) {
    std::vector<Device> devices{usb("/sys/devices/usb2/2-2", "OTHER")};
    DisabledDeviceEntry e;
    e.key = devmgr::services::makeDeviceKey(usb("/sys/devices/usb2/2-1", "AB12"));
    devmgr::app::applyDisabledOverlay(devices, {e});
    EXPECT_EQ(devices[0].status, DeviceStatus::Active);
}
