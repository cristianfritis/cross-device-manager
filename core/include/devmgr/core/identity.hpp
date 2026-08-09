#pragma once
#include <string>
#include <string_view>

namespace devmgr::core {

// The trailing segment of an opaque platform-native device identifier
// (Device::nativeId): everything after the last separator, where a separator is
// either '/' or '\', whichever occurs later in the string.
//
// Both separators are accepted because the identifier's shape is the platform's
// business, not core's: Linux supplies a canonical sysfs path
// ("/sys/devices/pci0000:00/usb3/3-1") and Windows a device instance ID
// ("USB\\VID_046D&PID_C52B\\5&1234&0&2"). Code above the PAL derives a
// positional fragment from that tail and must not assume either separator.
//
// An identifier with no separator, and an empty identifier, are returned
// unchanged — the whole value is its own tail. A trailing separator therefore
// yields an empty tail, which is the honest answer for an identifier that names
// no final segment.
std::string identityTail(std::string_view nativeId);

}  // namespace devmgr::core
