#pragma once
#include <string>

#include "devmgr/core/models.hpp"

namespace devmgr::daemon {

// Build a core::Device straight from sysfs attributes, for a device the live
// enumerator does not list: a fake --sysfs-root tree (IPC E2E suite), or udev
// that has not settled yet when the daemon sweeps at startup. Field semantics
// mirror UdevDeviceMapper (udev_device_mapper.cpp:84-92): ids are 0x-stripped,
// a deauthorized USB device is Disabled and everything else Active, and
// boundDriver is the `driver` symlink's target name when one is present.
core::Device deviceFromSysfs(const std::string& canonical);

}  // namespace devmgr::daemon
