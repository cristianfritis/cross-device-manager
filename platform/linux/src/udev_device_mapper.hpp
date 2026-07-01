#pragma once
#include <libudev.h>

#include "devmgr/core/models.hpp"

namespace devmgr::platform_linux {

// Maps a libudev device to our domain model. Shared by the enumerator and the
// hotplug monitor so a hotplugged device is byte-for-byte equal (same stableId)
// to the same device seen via a full enumeration. Internal header — includes
// <libudev.h> and is NOT part of the public devmgr/ surface.
core::Device mapDevice(udev_device* d);

}  // namespace devmgr::platform_linux
