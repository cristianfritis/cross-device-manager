#include "devmgr/platform/windows/windows_device_mapper.hpp"

#include <array>
#include <cstdio>
#include <string>
#include <string_view>

#include "devmgr/core/device_detail_fields.hpp"

namespace devmgr::platform_windows {
namespace {

// Deliberately not std::hash: that is allowed to differ per process, and this
// value is compared across enumerations and against hotplug notifications.
// Same function and same output shape as the other platforms' identity.
std::uint64_t fnv1a64(std::string_view s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

char upperAscii(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// Device instance identifiers are case-insensitive: the same device can be
// spelled one way by an enumeration and another by a notification's symbolic
// link. Identity folds case so the two agree; nativeId still stores what the
// operating system actually said.
std::string foldedForIdentity(std::string_view instanceId) {
    std::string folded;
    folded.reserve(instanceId.size());
    for (const char c : instanceId) folded.push_back(upperAscii(c));
    return folded;
}

void setProperty(core::Device& device, core::DetailField field, const std::string& value) {
    // Empty means the platform reported nothing. Writing it anyway would make
    // the field present-but-blank, which is the one thing the detail vocabulary
    // exists to prevent — so the absent field simply never appears.
    if (value.empty()) return;
    device.properties.insert_or_assign(std::string(core::detailFieldKey(field)), value);
}

std::string joinHardwareIds(const std::vector<std::string>& ids) {
    std::string out;
    for (const auto& id : ids) {
        if (!out.empty()) out += ", ";
        out += id;
    }
    return out;
}

core::DeviceStatus statusFor(const DevnodeFacts& facts) {
    // A status that could not be read is Unknown. Reporting Active for it would
    // claim a working device on the strength of a failed query.
    if (!facts.statusRead) return core::DeviceStatus::Unknown;
    if ((facts.status & kDnHasProblem) != 0U) {
        // Only the two problems that MEAN "switched off" are Disabled; every
        // other problem condition is a fault, and the shared taxonomy already
        // colours and words both (spec: no Windows-specific status wording, and
        // no second status field in the detail rows).
        if (facts.problem == kProblemDisabled || facts.problem == kProblemHardwareDisabled)
            return core::DeviceStatus::Disabled;
        return core::DeviceStatus::Error;
    }
    return (facts.status & kDnStarted) != 0U ? core::DeviceStatus::Active
                                             : core::DeviceStatus::Unknown;
}

}  // namespace

std::string instanceIdPrefix(std::string_view instanceId) {
    const auto sep = instanceId.find('\\');
    if (sep == std::string_view::npos) return {};
    return std::string(instanceId.substr(0, sep));
}

core::BusType busForInstanceId(std::string_view instanceId) {
    const std::string prefix = instanceIdPrefix(instanceId);
    if (prefix == "USB") return core::BusType::Usb;
    if (prefix == "PCI") return core::BusType::Pci;
    // Fixed, on-board, not a removable bus — which is what `Platform` already
    // means on every other backend and how displayBus() renders it.
    if (prefix == "ACPI" || prefix == "ROOT") return core::BusType::Platform;
    return core::BusType::Other;
}

core::DeviceId deviceIdFor(std::string_view instanceId) {
    std::array<char, 21> buf{};
    std::snprintf(buf.data(), buf.size(), "dev-%016llx",
                  static_cast<unsigned long long>(fnv1a64(foldedForIdentity(instanceId))));
    return core::DeviceId{std::string(buf.data())};
}

core::Device mapDevice(const DevnodeFacts& facts) {
    core::Device device;
    device.id = deviceIdFor(facts.instanceId);
    device.bus = busForInstanceId(facts.instanceId);
    device.nativeId = facts.instanceId;  // verbatim, always
    device.name = !facts.friendlyName.empty() ? facts.friendlyName : facts.deviceDesc;
    // The first hardware identifier is the most specific one, which is the one
    // a driver database is keyed by — the same role MODALIAS plays on Linux.
    if (!facts.hardwareIds.empty()) device.hardwareId = facts.hardwareIds.front();
    device.status = statusFor(facts);

    const std::string driver = !facts.service.empty() ? facts.service : facts.driverKey;
    if (!driver.empty()) device.boundDriver = driver;

    setProperty(device, core::DetailField::Manufacturer, facts.manufacturer);
    setProperty(device, core::DetailField::Driver, driver);
    setProperty(device, core::DetailField::DriverVersion, facts.driverVersion);
    setProperty(device, core::DetailField::DriverProvider, facts.driverProvider);
    setProperty(device, core::DetailField::DriverDate, facts.driverDate);
    setProperty(device, core::DetailField::Class, facts.deviceClass);
    setProperty(device, core::DetailField::HardwareIds, joinHardwareIds(facts.hardwareIds));
    setProperty(device, core::DetailField::DeviceInstanceId, facts.instanceId);

    // Raw facts the shared taxonomy flattened away, kept under this backend's
    // own key space. Nothing above the PAL reads the raw map — surfaces reach it
    // only through core::detailFields() — so these can never reach a screen.
    if (const std::string prefix = instanceIdPrefix(facts.instanceId); !prefix.empty())
        device.properties.insert_or_assign("windows.enumerator_prefix", prefix);
    if (!facts.locationInfo.empty())
        device.properties.insert_or_assign("windows.location_info", facts.locationInfo);
    return device;
}

core::Device mapKnownOnlyByInstanceId(std::string_view instanceId) {
    core::Device device;
    device.id = deviceIdFor(instanceId);
    device.bus = busForInstanceId(instanceId);
    device.nativeId = std::string(instanceId);
    // Everything else is genuinely unknown: the device node is gone by the time
    // a removal is reported, and a guess here would be indistinguishable from a
    // fact once it is in the model.
    device.status = core::DeviceStatus::Unknown;
    return device;
}

std::string instanceIdFromSymbolicLink(std::string_view symbolicLink) {
    std::string_view link = symbolicLink;
    // Both spellings of the device-namespace prefix appear in the wild.
    if (link.starts_with(R"(\\?\)") || link.starts_with(R"(\\.\)")) link.remove_prefix(4);

    // The trailing `{…}` is the interface class, not part of the identifier.
    // Only drop it when it is actually there: a link that ends in a plain
    // segment must not lose that segment.
    if (const auto lastHash = link.rfind('#'); lastHash != std::string_view::npos &&
                                               lastHash + 1 < link.size() &&
                                               link[lastHash + 1] == '{') {
        link = link.substr(0, lastHash);
    }

    std::string instanceId(link);
    for (char& c : instanceId) {
        if (c == '#') c = '\\';
    }
    return instanceId;
}

}  // namespace devmgr::platform_windows
