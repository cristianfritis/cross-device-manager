#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace devmgr::platform_windows {

// Everything the Windows enumerator harvests for one device node, already
// decoded out of the native property store into plain strings and lists.
//
// THIS STRUCT IS THE SEAM. Below it, cfgmgr_device_enumerator.cpp makes the
// Win32 calls and knows every native property key. Above it,
// windows_device_mapper.cpp decides what those values MEAN for the shared
// model — and, because nothing here is a Windows type, that decision is
// compiled and unit-tested on any host from captured fixtures, with no live
// device and no Windows machine involved.
//
// A field Windows did not report stays empty. Emptiness is the only "absent"
// signal the mapper needs, and it is what makes the spec's "leave unset rather
// than fill with a placeholder" rule automatic rather than remembered.
struct DevnodeFacts {
    // The device instance identifier, verbatim as the operating system
    // reported it — no case change, separator substitution, prefix stripping
    // or truncation, here or anywhere downstream.
    std::string instanceId;

    std::string friendlyName;  // preferred display name
    std::string deviceDesc;    // display-name fallback
    std::string manufacturer;
    std::string deviceClass;
    // In reported order, most specific first. The first entry is the primary
    // hardware-matching string; the whole list is published as one detail row.
    std::vector<std::string> hardwareIds;
    // The bound driver, from two properties that answer it differently: the
    // service is the name a person recognises ("usbhub"), the driver key is the
    // installer's own identifier. The mapper prefers the service and falls back
    // to the key, so a device with a driver never shows none.
    std::string service;
    std::string driverKey;
    std::string driverVersion;
    std::string driverProvider;
    std::string driverDate;
    std::string locationInfo;

    // The device node's status bits and problem code, as reported. `statusRead`
    // is false when the status could not be read at all, which is a different
    // thing from a device with no problem — see kDn* in the mapper header.
    std::uint32_t status = 0;
    std::uint32_t problem = 0;
    bool statusRead = false;
};

}  // namespace devmgr::platform_windows
