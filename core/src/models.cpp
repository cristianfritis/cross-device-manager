#include "devmgr/core/models.hpp"

namespace devmgr::core {

const char* to_string(DeviceStatus status) {
    switch (status) {
        case DeviceStatus::Active:
            return "Active";
        case DeviceStatus::Disabled:
            return "Disabled";
        case DeviceStatus::Transitioning:
            return "Transitioning";
        case DeviceStatus::Error:
            return "Error";
        case DeviceStatus::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

const char* to_string(BusType bus) {
    switch (bus) {
        case BusType::Pci:
            return "Pci";
        case BusType::Usb:
            return "Usb";
        case BusType::Platform:
            return "Platform";
        case BusType::Virtio:
            return "Virtio";
        case BusType::Other:
            return "Other";
    }
    return "Other";
}

const char* displayBus(BusType bus) {
    switch (bus) {
        case BusType::Pci:
            return "PCI";
        case BusType::Usb:
            return "USB";
        case BusType::Platform:
            return "Platform";
        case BusType::Virtio:
            return "Virtio";
        case BusType::Other:
            return "Other";
    }
    return "Other";
}

const char* to_string(DriverKind kind) {
    switch (kind) {
        case DriverKind::KernelModule:
            return "KernelModule";
        case DriverKind::Firmware:
            return "Firmware";
        case DriverKind::Package:
            return "Package";
        case DriverKind::Builtin:
            return "Builtin";
    }
    return "KernelModule";
}

}  // namespace devmgr::core
