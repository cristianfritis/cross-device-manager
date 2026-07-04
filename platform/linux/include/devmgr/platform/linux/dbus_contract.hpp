#pragma once
#include <cstdint>
#include <string>
#include <utility>

#include "devmgr/core/result.hpp"

namespace devmgr::platform_linux {

// The Phase 4 IPC contract (spec 2026-07-03) — names + the error table, shared
// by devmgrd (throw side) and DbusPrivilegedChannel (catch side). Pure
// strings: NO sdbus-c++ include here (CI purity guard).
inline constexpr const char* kBusName = "org.devmgr.Manager1";
inline constexpr const char* kObjectPath = "/org/devmgr/Manager1";
inline constexpr const char* kInterfaceName = "org.devmgr.Manager1";
inline constexpr std::uint32_t kApiVersion = 1;

inline constexpr const char* kErrCritical = "org.devmgr.Error.CriticalDevice";
inline constexpr const char* kErrNotAuthorized = "org.devmgr.Error.NotAuthorized";
inline constexpr const char* kErrNotFound = "org.devmgr.Error.NotFound";
inline constexpr const char* kErrUnsupported = "org.devmgr.Error.Unsupported";
inline constexpr const char* kErrIo = "org.devmgr.Error.Io";

inline const char* dbusErrorNameFor(core::Error::Code code) {
    switch (code) {
        case core::Error::Code::Conflict:
            return kErrCritical;
        case core::Error::Code::Permission:
            return kErrNotAuthorized;
        case core::Error::Code::NotFound:
            return kErrNotFound;
        case core::Error::Code::Unsupported:
            return kErrUnsupported;
        default:  // Io, Busy, Network collapse to Io on the wire
            return kErrIo;
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline core::Error coreErrorFor(const std::string& dbusErrorName, std::string message) {
    if (dbusErrorName == kErrCritical)
        return {.code = core::Error::Code::Conflict, .message = std::move(message)};
    if (dbusErrorName == kErrNotAuthorized)
        return {.code = core::Error::Code::Permission, .message = std::move(message)};
    if (dbusErrorName == kErrNotFound)
        return {.code = core::Error::Code::NotFound, .message = std::move(message)};
    if (dbusErrorName == kErrUnsupported)
        return {.code = core::Error::Code::Unsupported, .message = std::move(message)};
    if (dbusErrorName == "org.freedesktop.DBus.Error.ServiceUnknown")
        return {.code = core::Error::Code::Io, .message = "helper devmgrd is not available"};
    if (dbusErrorName == "org.freedesktop.DBus.Error.NoReply" ||
        dbusErrorName == "org.freedesktop.DBus.Error.Timeout")
        return {.code = core::Error::Code::Busy, .message = "helper timed out"};
    return {.code = core::Error::Code::Io, .message = std::move(message)};
}

}  // namespace devmgr::platform_linux
