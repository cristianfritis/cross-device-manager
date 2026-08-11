#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/core/device_detail_fields.hpp"
#include "devmgr/core/models.hpp"
#include "devmgr/platform/windows/windows_device_facts.hpp"
#include "devmgr/platform/windows/windows_device_mapper.hpp"

// Covers the Windows backend's mapping decisions against CAPTURED PROPERTY
// FIXTURES — no live device, no Windows machine, no Win32 call. That is why the
// mapper is platform-neutral: these run on the Linux CI that gates this project.
namespace {

using devmgr::core::BusType;
using devmgr::core::DetailField;
using devmgr::core::DeviceStatus;
using devmgr::platform_windows::busForInstanceId;
using devmgr::platform_windows::deviceIdFor;
using devmgr::platform_windows::DevnodeFacts;
using devmgr::platform_windows::instanceIdFromSymbolicLink;
using devmgr::platform_windows::instanceIdPrefix;
using devmgr::platform_windows::mapDevice;
using devmgr::platform_windows::mapKnownOnlyByInstanceId;

// Devnode status bits, as captured. Named here so the fixtures read as the
// conditions they represent rather than as magic numbers.
constexpr std::uint32_t kStarted = 0x00000008U;
constexpr std::uint32_t kHasProblem = 0x00000400U;

// A mouse, fully populated: the shape almost every real device has.
DevnodeFacts mouseFixture() {
    return DevnodeFacts{
        .instanceId = R"(USB\VID_046D&PID_C52B\5&1234abcd&0&2)",
        .friendlyName = "USB Composite Device",
        .deviceDesc = "USB Input Device",
        .manufacturer = "Logitech",
        .deviceClass = "Mouse",
        .hardwareIds = {R"(USB\VID_046D&PID_C52B&REV_1203)", R"(USB\VID_046D&PID_C52B)"},
        .service = "usbhub",
        .driverKey = R"({36fc9e60-c465-11cf-8056-444553540000}\0004)",
        .driverVersion = "10.0.19041.1",
        .driverProvider = "Microsoft",
        .driverDate = "2006-06-21",
        .locationInfo = "Port_#0002.Hub_#0003",
        .status = kStarted,
        .problem = 0,
        .statusRead = true};
}

std::optional<std::string> fieldValue(const devmgr::core::Device& device, DetailField field) {
    const auto fields = devmgr::core::detailFields(device);
    const auto it =
        std::ranges::find_if(fields, [field](const auto& f) { return f.field == field; });
    if (it == fields.end()) return std::nullopt;
    return it->value;
}

TEST(WindowsDeviceMapperTest, StoresTheInstanceIdentifierVerbatim) {
    const auto facts = mouseFixture();
    const auto device = mapDevice(facts);
    EXPECT_EQ(device.nativeId, facts.instanceId);  // no case change, no separator substitution
}

TEST(WindowsDeviceMapperTest, IdentityRoundTripsAcrossEnumerations) {
    const auto first = mapDevice(mouseFixture());
    const auto second = mapDevice(mouseFixture());
    EXPECT_EQ(first.id.value, second.id.value);
    EXPECT_EQ(first, second);
}

// A removal notification carries only the identifier, and the row it must drop
// was keyed by an id built from a full enumeration. They have to agree.
TEST(WindowsDeviceMapperTest, RemovalIdentityMatchesTheEnumeratedOne) {
    const auto enumerated = mapDevice(mouseFixture());
    const auto removed = mapKnownOnlyByInstanceId(mouseFixture().instanceId);
    EXPECT_EQ(enumerated.id.value, removed.id.value);
    EXPECT_EQ(enumerated.bus, removed.bus);
    EXPECT_EQ(removed.status, DeviceStatus::Unknown);
    EXPECT_TRUE(removed.name.empty());
}

// Instance identifiers are case-insensitive, and a symbolic link may spell one
// differently from the enumeration that produced it.
TEST(WindowsDeviceMapperTest, IdentityFoldsCase) {
    EXPECT_EQ(deviceIdFor(R"(USB\VID_046D&PID_C52B\5&1234ABCD&0&2)").value,
              deviceIdFor(R"(usb\vid_046d&pid_c52b\5&1234abcd&0&2)").value);
}

TEST(WindowsDeviceMapperTest, PrefersFriendlyNameOverDescription) {
    EXPECT_EQ(mapDevice(mouseFixture()).name, "USB Composite Device");
}

TEST(WindowsDeviceMapperTest, FallsBackToDescriptionWhenFriendlyNameIsMissing) {
    auto facts = mouseFixture();
    facts.friendlyName.clear();
    EXPECT_EQ(mapDevice(facts).name, "USB Input Device");
}

TEST(WindowsDeviceMapperTest, PrimaryHardwareIdIsTheFirstReported) {
    const auto device = mapDevice(mouseFixture());
    EXPECT_EQ(device.hardwareId, R"(USB\VID_046D&PID_C52B&REV_1203)");
}

TEST(WindowsDeviceMapperTest, HardwareIdsFieldKeepsTheWholeListInOrder) {
    const auto device = mapDevice(mouseFixture());
    EXPECT_EQ(
        fieldValue(device, DetailField::HardwareIds),
        std::optional<std::string>(R"(USB\VID_046D&PID_C52B&REV_1203, USB\VID_046D&PID_C52B)"));
}

TEST(WindowsDeviceMapperTest, PublishesTheSpecifiedDetailFields) {
    const auto device = mapDevice(mouseFixture());
    EXPECT_EQ(fieldValue(device, DetailField::Manufacturer),
              std::optional<std::string>("Logitech"));
    EXPECT_EQ(fieldValue(device, DetailField::Driver), std::optional<std::string>("usbhub"));
    EXPECT_EQ(fieldValue(device, DetailField::DriverVersion),
              std::optional<std::string>("10.0.19041.1"));
    EXPECT_EQ(fieldValue(device, DetailField::DriverProvider),
              std::optional<std::string>("Microsoft"));
    EXPECT_EQ(fieldValue(device, DetailField::DriverDate),
              std::optional<std::string>("2006-06-21"));
    EXPECT_EQ(fieldValue(device, DetailField::Class), std::optional<std::string>("Mouse"));
    EXPECT_EQ(fieldValue(device, DetailField::DeviceInstanceId),
              std::optional<std::string>(mouseFixture().instanceId));
}

// The whole point of the shared vocabulary: a user reads product words, never a
// property-store key name.
TEST(WindowsDeviceMapperTest, NoNativePropertyKeyReachesARenderedField) {
    const auto device = mapDevice(mouseFixture());
    for (const auto& field : devmgr::core::detailFields(device)) {
        EXPECT_EQ(std::string(field.label).find("DEVPKEY"), std::string::npos) << field.label;
        EXPECT_EQ(std::string(field.label).find("Device_"), std::string::npos) << field.label;
    }
}

TEST(WindowsDeviceMapperTest, MissingManufacturerOmitsTheRow) {
    auto facts = mouseFixture();
    facts.manufacturer.clear();
    const auto device = mapDevice(facts);
    EXPECT_EQ(fieldValue(device, DetailField::Manufacturer), std::nullopt);
}

TEST(WindowsDeviceMapperTest, MissingDriverOmitsEveryDriverRow) {
    auto facts = mouseFixture();
    facts.service.clear();
    facts.driverKey.clear();
    facts.driverVersion.clear();
    facts.driverProvider.clear();
    facts.driverDate.clear();
    const auto device = mapDevice(facts);
    EXPECT_FALSE(device.boundDriver.has_value());
    EXPECT_EQ(fieldValue(device, DetailField::Driver), std::nullopt);
    EXPECT_EQ(fieldValue(device, DetailField::DriverVersion), std::nullopt);
    EXPECT_EQ(fieldValue(device, DetailField::DriverProvider), std::nullopt);
    EXPECT_EQ(fieldValue(device, DetailField::DriverDate), std::nullopt);
}

// The service is the name a person recognises; the installer's key is the
// fallback, so a device with a driver never renders as having none.
TEST(WindowsDeviceMapperTest, FallsBackToTheDriverKeyWhenThereIsNoService) {
    auto facts = mouseFixture();
    facts.service.clear();
    const auto device = mapDevice(facts);
    EXPECT_EQ(device.boundDriver, std::optional<std::string>(facts.driverKey));
    EXPECT_EQ(fieldValue(device, DetailField::Driver), std::optional<std::string>(facts.driverKey));
}

TEST(WindowsDeviceMapperTest, ClassifiesKnownPrefixes) {
    EXPECT_EQ(busForInstanceId(R"(USB\VID_046D&PID_C52B\5&1&2)"), BusType::Usb);
    EXPECT_EQ(busForInstanceId(R"(PCI\VEN_8086&DEV_A0F0\3&11583659&0&A3)"), BusType::Pci);
    EXPECT_EQ(busForInstanceId(R"(ACPI\PNP0C0C\2&DABA3FF&2)"), BusType::Platform);
    EXPECT_EQ(busForInstanceId(R"(ROOT\SYSTEM\0000)"), BusType::Platform);
}

TEST(WindowsDeviceMapperTest, UnknownPrefixDoesNotInventABus) {
    EXPECT_EQ(busForInstanceId(R"(SWD\PRINTENUM\{GUID})"), BusType::Other);
    EXPECT_EQ(busForInstanceId(R"(HID\VID_046D&PID_C52B&MI_01\7&2&0000)"), BusType::Other);
    EXPECT_EQ(busForInstanceId("NOSEPARATOR"), BusType::Other);
}

TEST(WindowsDeviceMapperTest, RawPrefixIsPreservedInThePropertyMap) {
    const auto device = mapDevice(mouseFixture());
    ASSERT_TRUE(device.properties.contains("windows.enumerator_prefix"));
    EXPECT_EQ(device.properties.at("windows.enumerator_prefix"), "USB");
    EXPECT_EQ(instanceIdPrefix(R"(SWD\PRINTENUM\X)"), "SWD");
    EXPECT_EQ(instanceIdPrefix("NOSEPARATOR"), "");
}

TEST(WindowsDeviceMapperTest, StartedDeviceIsActive) {
    EXPECT_EQ(mapDevice(mouseFixture()).status, DeviceStatus::Active);
}

// A problem condition uses the SHARED taxonomy: no extra status detail row and
// no Windows-specific wording.
TEST(WindowsDeviceMapperTest, DisabledProblemMapsToDisabled) {
    auto facts = mouseFixture();
    facts.status = kHasProblem;
    facts.problem = 22;  // CM_PROB_DISABLED
    const auto device = mapDevice(facts);
    EXPECT_EQ(device.status, DeviceStatus::Disabled);

    facts.problem = 29;  // CM_PROB_HARDWARE_DISABLED
    EXPECT_EQ(mapDevice(facts).status, DeviceStatus::Disabled);
}

TEST(WindowsDeviceMapperTest, OtherProblemsMapToError) {
    auto facts = mouseFixture();
    facts.status = kHasProblem;
    facts.problem = 28;  // CM_PROB_FAILED_INSTALL — a fault, not a switch-off
    EXPECT_EQ(mapDevice(facts).status, DeviceStatus::Error);
}

TEST(WindowsDeviceMapperTest, ProblemConditionAddsNoStatusDetailRow) {
    auto facts = mouseFixture();
    facts.status = kHasProblem;
    facts.problem = 28;
    const auto device = mapDevice(facts);
    for (const auto& field : devmgr::core::detailFields(device)) {
        EXPECT_EQ(std::string(field.label).find("Status"), std::string::npos) << field.label;
    }
}

TEST(WindowsDeviceMapperTest, UnreadableStatusIsUnknownNotActive) {
    auto facts = mouseFixture();
    facts.statusRead = false;
    EXPECT_EQ(mapDevice(facts).status, DeviceStatus::Unknown);
}

TEST(WindowsDeviceMapperTest, NotStartedWithoutAProblemIsUnknown) {
    auto facts = mouseFixture();
    facts.status = 0;
    EXPECT_EQ(mapDevice(facts).status, DeviceStatus::Unknown);
}

TEST(WindowsDeviceMapperTest, RecoversTheInstanceIdFromASymbolicLink) {
    EXPECT_EQ(
        instanceIdFromSymbolicLink(
            R"(\\?\USB#VID_046D&PID_C52B#5&1234abcd&0&2#{a5dcbf10-6530-11d2-901f-00c04fb951ed})"),
        R"(USB\VID_046D&PID_C52B\5&1234abcd&0&2)");
    EXPECT_EQ(
        instanceIdFromSymbolicLink(R"(\\.\PCI#VEN_8086&DEV_A0F0#3&11583659&0&A3#{guid-shaped})"),
        R"(PCI\VEN_8086&DEV_A0F0\3&11583659&0&A3)");
}

// A link with no interface-class suffix must not lose its last segment.
TEST(WindowsDeviceMapperTest, SymbolicLinkWithoutAClassSuffixKeepsEverySegment) {
    EXPECT_EQ(instanceIdFromSymbolicLink(R"(\\?\USB#VID_046D&PID_C52B#5&1&2)"),
              R"(USB\VID_046D&PID_C52B\5&1&2)");
    EXPECT_EQ(instanceIdFromSymbolicLink(""), "");
}

// The link and the enumeration must produce the same row, or a plug event would
// add a second copy of a device already listed.
TEST(WindowsDeviceMapperTest, SymbolicLinkIdentityMatchesTheEnumeratedIdentity) {
    const auto enumerated = mapDevice(mouseFixture());
    const std::string fromLink = instanceIdFromSymbolicLink(
        R"(\\?\USB#VID_046D&PID_C52B#5&1234abcd&0&2#{a5dcbf10-6530-11d2-901f-00c04fb951ed})");
    EXPECT_EQ(deviceIdFor(fromLink).value, enumerated.id.value);
}

}  // namespace
