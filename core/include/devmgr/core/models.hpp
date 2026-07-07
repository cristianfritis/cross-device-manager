#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace devmgr::core {

struct DeviceId {
    std::string value;
};
inline bool operator==(const DeviceId& a, const DeviceId& b) {
    return a.value == b.value;
}

enum class DeviceStatus { Active, Disabled, Transitioning, Error, Unknown };
enum class BusType { Pci, Usb, Platform, Virtio, Other };
enum class DriverKind { KernelModule, Firmware, Package, Builtin };

const char* to_string(DeviceStatus status);
const char* to_string(BusType bus);
const char* to_string(DriverKind kind);

struct Device {
    DeviceId id;
    BusType bus = BusType::Other;
    std::string name;
    std::string sysfsPath;
    std::string modalias;
    std::string vendorId;
    std::string productId;
    std::string serial;
    std::optional<DeviceId> parent;
    DeviceStatus status = DeviceStatus::Unknown;
    std::optional<std::string> boundDriver;
    std::map<std::string, std::string> properties;
    std::optional<std::string> errorNote;

    friend bool operator==(const Device&, const Device&) = default;
};

struct Driver {
    std::string name;
    DriverKind kind = DriverKind::KernelModule;
    std::string version;
    std::string path;
    bool loaded = false;
    bool isSigned = false;
    std::optional<std::string> signer;
    std::optional<std::string> availableUpdate;
    std::vector<std::string> dependencies;
};

struct LoadedModule {
    std::string name;
    std::uint64_t sizeBytes = 0;
    long refCount = 0;
    std::vector<std::string> holders;
    friend bool operator==(const LoadedModule&, const LoadedModule&) = default;
};

struct ModprobeInfo {
    std::optional<std::string> options;  // concatenated modprobe.d options, nullopt if none
    bool blacklisted = false;
    friend bool operator==(const ModprobeInfo&, const ModprobeInfo&) = default;
};

// Tiered identity (spec §5.1): serial tuple when serial != "", else position
// validated by vendor/product. Field values are lowercase hex ids WITHOUT 0x
// (mapper convention) and the bus string from services::keyBusString().
struct DeviceKey {
    std::string bus;  // "usb" | "pci" | "platform" | "virtio" | "other"
    std::string vendorId;
    std::string productId;
    std::string serial;    // "" => positional matching
    std::string position;  // usb port chain "2-1.4" | pci address "0000:03:00.0"
    friend bool operator==(const DeviceKey&, const DeviceKey&) = default;
};

struct DisabledDeviceEntry {
    DeviceKey key;
    std::string mechanism;      // "authorized" | "unbind"
    std::string lastDriver;     // "" when unknown (plain drivers_probe rebind)
    std::string lastSysfsPath;  // display/debug + fallback match
    std::int64_t disabledAtUtc = 0;
    bool guardSuspended = false;
    friend bool operator==(const DisabledDeviceEntry&, const DisabledDeviceEntry&) = default;
};

struct DeviceState {
    DeviceId id;
    bool enabled = true;
    std::optional<std::string> boundDriver;
    std::optional<std::string> driverVersion;
};

struct DriverState {
    std::string name;
    std::string version;
    bool loaded = false;
    std::vector<std::string> options;
};

struct Snapshot {
    std::string id;  // sha256 of canonical body (filled by BackupService in Phase 7)
    std::optional<std::string> parent;
    std::int64_t timestampUtc = 0;
    std::string author;
    std::string description;
    std::string osVersion;
    std::string kernelVersion;
    std::vector<DeviceState> devices;
    std::vector<DriverState> drivers;
    std::vector<std::string> modprobeConfigDigests;
};

struct HistoryGraph {
    std::string head;
    std::map<std::string, Snapshot> nodes;
};

}  // namespace devmgr::core

template <>
struct std::hash<devmgr::core::DeviceId> {
    std::size_t operator()(const devmgr::core::DeviceId& id) const noexcept {
        return std::hash<std::string>{}(id.value);
    }
};
