#include <gtest/gtest.h>

#include "devmgr/services/device_key.hpp"

using devmgr::core::BusType;
using devmgr::core::Device;
using devmgr::core::DeviceKey;
namespace svc = devmgr::services;

namespace {
Device usb(std::string path, std::string vid, std::string pid, std::string serial) {
    Device d;
    d.bus = BusType::Usb;
    d.sysfsPath = std::move(path);
    d.vendorId = std::move(vid);
    d.productId = std::move(pid);
    d.serial = std::move(serial);
    return d;
}
}  // namespace

TEST(DeviceKey, UsbPositionIsThePortChainSegment) {
    EXPECT_EQ(svc::positionFor(BusType::Usb, "/sys/devices/pci0000:00/usb2/2-1/2-1.4"), "2-1.4");
}

TEST(DeviceKey, PciPositionIsTheAddressSegment) {
    EXPECT_EQ(svc::positionFor(BusType::Pci, "/sys/devices/pci0000:00/0000:03:00.0"),
              "0000:03:00.0");
}

TEST(DeviceKey, SerialTupleMatchesAcrossPorts) {
    const auto key =
        svc::makeDeviceKey(usb("/sys/devices/pci0000:00/usb2/2-1", "046d", "c52b", "AB12"));
    // Same physical device replugged at a different port: still matches.
    EXPECT_TRUE(
        svc::matchesDevice(key, usb("/sys/devices/pci0000:00/usb1/1-3", "046d", "c52b", "AB12")));
    // Different serial: no match.
    EXPECT_FALSE(
        svc::matchesDevice(key, usb("/sys/devices/pci0000:00/usb1/1-3", "046d", "c52b", "XX")));
}

TEST(DeviceKey, SerialLessMatchesByPositionValidatedByIds) {
    const auto key =
        svc::makeDeviceKey(usb("/sys/devices/pci0000:00/usb2/2-1.4", "1a2b", "3c4d", ""));
    EXPECT_EQ(key.serial, "");
    EXPECT_EQ(key.position, "2-1.4");
    EXPECT_TRUE(
        svc::matchesDevice(key, usb("/sys/devices/pci0000:00/usb2/2-1.4", "1a2b", "3c4d", "")));
    // Same port, DIFFERENT device: validation refuses (never enforce on a stranger).
    EXPECT_FALSE(
        svc::matchesDevice(key, usb("/sys/devices/pci0000:00/usb2/2-1.4", "dead", "beef", "")));
    // Serial-less device moved port = new device (Windows semantics).
    EXPECT_FALSE(
        svc::matchesDevice(key, usb("/sys/devices/pci0000:00/usb1/1-2", "1a2b", "3c4d", "")));
}

TEST(DeviceKey, ClonedSerialDowngradesToPositional) {
    const auto target = usb("/sys/devices/pci0000:00/usb2/2-1", "aaaa", "bbbb", "0123456789");
    const auto clone = usb("/sys/devices/pci0000:00/usb2/2-2", "aaaa", "bbbb", "0123456789");
    const auto key = svc::makeDeviceKey(target, {target, clone});
    EXPECT_EQ(key.serial, "");       // downgraded
    EXPECT_EQ(key.position, "2-1");  // pinned to the port instead
    // Unique serial is NOT downgraded.
    const auto unique = usb("/sys/devices/pci0000:00/usb2/2-1", "aaaa", "bbbb", "UNIQ");
    EXPECT_EQ(svc::makeDeviceKey(unique, {unique, clone}).serial, "UNIQ");
}

TEST(DeviceKey, BusStringsAreStable) {
    EXPECT_EQ(svc::keyBusString(BusType::Usb), "usb");
    EXPECT_EQ(svc::keyBusString(BusType::Pci), "pci");
    EXPECT_EQ(svc::keyBusString(BusType::Platform), "platform");
    EXPECT_EQ(svc::keyBusString(BusType::Virtio), "virtio");
    EXPECT_EQ(svc::keyBusString(BusType::Other), "other");
}
