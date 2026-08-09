#include <gtest/gtest.h>

#include <string>

#include "devmgr/core/device_presentation.hpp"

using namespace devmgr::core;

namespace {

// A PCI function as the Linux enumerator maps it: udev properties verbatim,
// hex ids without 0x, the sysfs path the position is derived from.
Device pciDevice(std::string_view name, std::string_view address) {
    Device d;
    d.bus = BusType::Pci;
    d.name = name;
    d.sysfsPath = std::string("/sys/devices/pci0000:c5/") + std::string(address);
    d.vendorId = "1022";
    d.productId = "151b";
    return d;
}

Device usbDevice(std::string_view name, std::string_view port) {
    Device d;
    d.bus = BusType::Usb;
    d.name = name;
    d.sysfsPath = std::string("/sys/devices/pci0000:00/usb3/") + std::string(port);
    d.vendorId = "06cb";
    d.productId = "0174";
    return d;
}

}  // namespace

// --- Tier 1: a catalogued name is already the answer -------------------------

TEST(DisplayDeviceName, CataloguedNameIsUsedVerbatim) {
    const Device d = pciDevice("Audio Coprocessor", "0000:c3:00.5");
    EXPECT_EQ(displayDeviceName(d), "Audio Coprocessor");
}

// The vendor is deliberately NOT prefixed onto a catalogued name: it would
// reword every already-correct row and push the widest label past the 80-column
// budget, while adding nothing the identity line and detail pane do not carry.
TEST(DisplayDeviceName, CataloguedNameIsNotPrefixedWithVendor) {
    Device d = pciDevice("Audio Coprocessor", "0000:c3:00.5");
    d.properties["ID_VENDOR_FROM_DATABASE"] = "Advanced Micro Devices, Inc. [AMD]";
    EXPECT_EQ(displayDeviceName(d), "Audio Coprocessor");
}

TEST(DisplayDeviceName, CataloguedPropertyIsUsedWhenNameFellThroughToTheAddress) {
    Device d = pciDevice("0000:c3:00.5", "0000:c3:00.5");
    d.properties["ID_MODEL_FROM_DATABASE"] = "Audio Coprocessor";
    EXPECT_EQ(displayDeviceName(d), "Audio Coprocessor");
}

// --- Positional detection: the defect this formatter exists to fix -----------

TEST(DisplayDeviceName, PciAddressIsNeverTheLabel) {
    Device d = pciDevice("0000:c5:00.4", "0000:c5:00.4");
    ASSERT_EQ(d.name, "0000:c5:00.4");  // == basename(sysfsPath): the mapper's fallback
    d.properties["ID_VENDOR_FROM_DATABASE"] = "Advanced Micro Devices, Inc. [AMD]";
    d.properties["ID_PCI_CLASS_FROM_DATABASE"] = "Serial bus controller";
    d.properties["ID_PCI_SUBCLASS_FROM_DATABASE"] = "USB controller";

    EXPECT_EQ(displayDeviceName(d), "AMD USB controller");
}

// Same shape, but the sysfs path is unknown — the address pattern alone still
// has to be recognised, or the label silently regresses to the bare address.
TEST(DisplayDeviceName, PciAddressIsRejectedWithoutASysfsPath) {
    Device d;
    d.bus = BusType::Pci;
    d.name = "0000:c5:00.4";
    d.vendorId = "1022";
    d.productId = "151b";
    d.properties["ID_VENDOR_FROM_DATABASE"] = "Advanced Micro Devices, Inc. [AMD]";
    d.properties["ID_PCI_SUBCLASS_FROM_DATABASE"] = "USB controller";

    EXPECT_EQ(displayDeviceName(d), "AMD USB controller");
}

TEST(DisplayDeviceName, UsbPortChainIsNeverTheLabel) {
    Device d = usbDevice("3-1.4", "3-1.4");
    d.properties["ID_VENDOR_FROM_DATABASE"] = "Synaptics, Inc.";
    EXPECT_EQ(displayDeviceName(d), "Synaptics device");
}

TEST(DisplayDeviceName, UsbRootHubNameIsPositional) {
    Device d = usbDevice("usb3", "usb3");
    d.properties["ID_VENDOR_FROM_DATABASE"] = "Linux Foundation";
    d.properties["ID_MODEL"] = "2.0_root_hub";
    EXPECT_EQ(displayDeviceName(d), "2.0 root hub");
}

// A name that merely looks address-ish must survive: "3D controller" is a real
// pci.ids string and must not be mistaken for a port chain.
TEST(DisplayDeviceName, RealNamesAreNotMistakenForPositions) {
    for (const char* name : {"3D controller", "Wi-Fi 7 adapter", "2.5G Ethernet", "usb serial"}) {
        const Device d = pciDevice(name, "0000:c5:00.4");
        EXPECT_EQ(displayDeviceName(d), name);
    }
}

// --- Tier 2: the device's own product string --------------------------------

TEST(DisplayDeviceName, SelfReportedModelLosesUdevUnderscores) {
    Device d = usbDevice("2-1", "2-1");
    d.properties["ID_MODEL"] = "HP_9MP_Camera";
    d.properties["ID_VENDOR_FROM_DATABASE"] = "Chicony Electronics Co., Ltd";
    EXPECT_EQ(displayDeviceName(d), "HP 9MP Camera");
}

// Devices that declare nothing report their own hex product id. That is not a
// name, and must fall through rather than render as one.
TEST(DisplayDeviceName, SelfReportedModelThatIsJustTheProductIdIsRejected) {
    Device d = usbDevice("3-3", "3-3");
    d.properties["ID_MODEL"] = "0174";  // == productId
    d.properties["ID_VENDOR_FROM_DATABASE"] = "Synaptics, Inc.";
    EXPECT_EQ(displayDeviceName(d), "Synaptics device");
}

// --- Vendor shortening ------------------------------------------------------

TEST(DisplayDeviceName, VendorIsShortenedToItsPublishedAlias) {
    struct Case {
        const char* vendor;
        const char* expected;
    };
    for (const auto& [vendor, expected] : {
             Case{.vendor = "Advanced Micro Devices, Inc. [AMD]", .expected = "AMD USB controller"},
             Case{.vendor = "Advanced Micro Devices, Inc. [AMD/ATI]",
                  .expected = "AMD/ATI USB controller"},
             Case{.vendor = "Chicony Electronics Co., Ltd",
                  .expected = "Chicony Electronics USB controller"},
             Case{.vendor = "MediaTek Inc.", .expected = "MediaTek USB controller"},
             Case{.vendor = "KIOXIA Corporation", .expected = "KIOXIA USB controller"},
             Case{.vendor = "Logitech, Inc.", .expected = "Logitech USB controller"},
             Case{.vendor = "Linux Foundation", .expected = "Linux Foundation USB controller"},
         }) {
        Device d = pciDevice("0000:c5:00.4", "0000:c5:00.4");
        d.properties["ID_VENDOR_FROM_DATABASE"] = vendor;
        d.properties["ID_PCI_SUBCLASS_FROM_DATABASE"] = "USB controller";
        EXPECT_EQ(displayDeviceName(d), expected) << "vendor: " << vendor;
    }
}

TEST(DisplayDeviceName, VendorIsNotRepeatedWhenTheCategoryAlreadyLeadsWithIt) {
    Device d = pciDevice("0000:c5:00.4", "0000:c5:00.4");
    d.properties["ID_VENDOR_FROM_DATABASE"] = "AMD";
    d.properties["ID_PCI_SUBCLASS_FROM_DATABASE"] = "AMD USB controller";
    EXPECT_EQ(displayDeviceName(d), "AMD USB controller");
}

// --- Tier ordering ----------------------------------------------------------

TEST(DisplayDeviceName, SubclassBeatsClassBecauseItIsMoreSpecific) {
    Device d = pciDevice("0000:c5:00.4", "0000:c5:00.4");
    d.properties["ID_VENDOR_FROM_DATABASE"] = "Advanced Micro Devices, Inc. [AMD]";
    d.properties["ID_PCI_CLASS_FROM_DATABASE"] = "Serial bus controller";
    d.properties["ID_PCI_SUBCLASS_FROM_DATABASE"] = "USB controller";
    EXPECT_EQ(displayDeviceName(d), "AMD USB controller");

    d.properties.erase("ID_PCI_SUBCLASS_FROM_DATABASE");
    EXPECT_EQ(displayDeviceName(d), "AMD Serial bus controller");
}

TEST(DisplayDeviceName, CategoryWithoutAVendorStillReads) {
    Device d = pciDevice("0000:c5:00.4", "0000:c5:00.4");
    d.properties["ID_PCI_SUBCLASS_FROM_DATABASE"] = "USB controller";
    EXPECT_EQ(displayDeviceName(d), "USB controller");
}

// --- Tier 6 and degenerate inputs -------------------------------------------

// A Device from a test fake or a non-udev backend carries no properties at all.
TEST(DisplayDeviceName, NoPropertiesFallsBackToTheBusWithoutError) {
    const Device d = pciDevice("0000:c5:00.4", "0000:c5:00.4");
    EXPECT_EQ(displayDeviceName(d), "PCI device");
}

TEST(DisplayDeviceName, EmptyNameYieldsTheRawIdTierNotAnEmptyLabel) {
    Device d;
    d.bus = BusType::Usb;
    d.vendorId = "1d6b";
    d.productId = "0002";
    EXPECT_EQ(displayDeviceName(d), "USB device");
}

TEST(DisplayDeviceName, DeviceWithNothingAtAllStillNamesItself) {
    const Device d;
    EXPECT_EQ(displayDeviceName(d), "(unknown device)");
}

// --- The identity line ------------------------------------------------------

TEST(DisplayDeviceIdentity, PairsIdsWithThePosition) {
    const Device d = pciDevice("Audio Coprocessor", "0000:c3:00.5");
    EXPECT_EQ(displayDeviceIdentity(d), "1022:151b · 0000:c3:00.5");
}

TEST(DisplayDeviceIdentity, OmitsWhicheverHalfIsUnknown) {
    Device withoutPath;
    withoutPath.vendorId = "1022";
    withoutPath.productId = "151b";
    EXPECT_EQ(displayDeviceIdentity(withoutPath), "1022:151b");

    Device withoutIds;
    withoutIds.sysfsPath = "/sys/devices/pci0000:c5/0000:c5:00.4";
    EXPECT_EQ(displayDeviceIdentity(withoutIds), "0000:c5:00.4");

    EXPECT_EQ(displayDeviceIdentity(Device{}), "");
}

// Six identically named bridges are a real configuration; the name alone cannot
// tell them apart, so the identity line is what makes each row distinct.
TEST(DisplayDeviceIdentity, DistinguishesDevicesSharingAName) {
    const Device a = pciDevice("Dummy Host Bridge", "0000:00:01.0");
    const Device b = pciDevice("Dummy Host Bridge", "0000:00:02.0");
    EXPECT_EQ(displayDeviceName(a), displayDeviceName(b));
    EXPECT_NE(displayDeviceIdentity(a), displayDeviceIdentity(b));
}
