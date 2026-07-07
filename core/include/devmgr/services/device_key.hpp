#pragma once
#include <string>
#include <vector>
#include "devmgr/core/models.hpp"

namespace devmgr::services {

// Stable serialization bus string (independent of core::to_string casing).
std::string keyBusString(core::BusType bus);

// USB: last path segment when it looks like a port chain ("2-1.4").
// Everything else: last path segment verbatim ("0000:03:00.0").
std::string positionFor(core::BusType bus, const std::string& sysfsPath);

// Plain key from one device.
core::DeviceKey makeDeviceKey(const core::Device& device);

// Cloned-serial guard (spec §5.1): if any OTHER present device shares the
// (bus, vendorId, productId, serial) tuple, the key is downgraded to
// positional (serial forced to "").
core::DeviceKey makeDeviceKey(const core::Device& device, const std::vector<core::Device>& present);

// Tier 1: serial tuple. Tier 2: bus+position, validated by vendor/product —
// a vendor/product mismatch at the stored position returns false.
bool matchesDevice(const core::DeviceKey& key, const core::Device& device);

}  // namespace devmgr::services
