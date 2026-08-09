#include "devmgr/services/device_key.hpp"

#include <algorithm>

#include "devmgr/core/identity.hpp"

namespace devmgr::services {

std::string keyBusString(core::BusType bus) {
    switch (bus) {
        case core::BusType::Usb:
            return "usb";
        case core::BusType::Pci:
            return "pci";
        case core::BusType::Platform:
            return "platform";
        case core::BusType::Virtio:
            return "virtio";
        case core::BusType::Other:
            return "other";
    }
    return "other";
}

std::string positionFor(core::BusType /*bus*/, const std::string& nativeId) {
    // The last segment is the platform's positional name on every bus we key:
    // USB port chain ("2-1.4"), PCI address ("0000:03:00.0"), platform name.
    return core::identityTail(nativeId);
}

core::DeviceKey makeDeviceKey(const core::Device& device) {
    return core::DeviceKey{.bus = keyBusString(device.bus),
                           .vendorId = device.vendorId,
                           .productId = device.productId,
                           .serial = device.serial,
                           .position = positionFor(device.bus, device.nativeId)};
}

core::DeviceKey makeDeviceKey(const core::Device& device,
                              const std::vector<core::Device>& present) {
    auto key = makeDeviceKey(device);
    if (key.serial.empty()) return key;
    const bool cloned = std::ranges::any_of(present, [&](const core::Device& d) {
        return d.nativeId != device.nativeId && keyBusString(d.bus) == key.bus &&
               d.vendorId == key.vendorId && d.productId == key.productId && d.serial == key.serial;
    });
    if (cloned) key.serial.clear();  // downgrade to positional (spec §5.1)
    return key;
}

bool matchesDevice(const core::DeviceKey& key, const core::Device& device) {
    if (keyBusString(device.bus) != key.bus) return false;
    if (!key.serial.empty()) {
        return device.vendorId == key.vendorId && device.productId == key.productId &&
               device.serial == key.serial;
    }
    return positionFor(device.bus, device.nativeId) == key.position &&
           device.vendorId == key.vendorId && device.productId == key.productId;
}

}  // namespace devmgr::services
