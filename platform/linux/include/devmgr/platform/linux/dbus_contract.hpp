#pragma once
#include <cstdint>
#include <string>
#include <utility>

#include "devmgr/core/result.hpp"

namespace devmgr::platform_linux {

// The Phase 4 IPC contract (spec 2026-07-03) — names + the error table, shared
// by devmgrd (throw side) and DbusPrivilegedChannel (catch side). Pure
// strings: NO sdbus-c++ include here (CI purity guard).
// ApiVersion 3 (Phase 7): additive snapshot verbs — SnapshotList() -> s,
// SnapshotCreate(s label) -> s, SnapshotRestore(s id) -> s, SnapshotDelete(s
// id). All v2 verbs unchanged; clients check ApiVersion >= required.
// ApiVersion 4 (beta-06): one additive read verb — SnapshotDiff(s base_id,
// s target_id) -> s, empty target_id meaning live system state — plus the
// InvalidArgs error name below. Every v2/v3 verb is untouched, so a v3 client
// keeps working against a v4 daemon.
inline constexpr const char* kBusName = "org.devmgr.Manager1";
inline constexpr const char* kObjectPath = "/org/devmgr/Manager1";
inline constexpr const char* kInterfaceName = "org.devmgr.Manager1";
inline constexpr std::uint32_t kApiVersion = 4;

inline constexpr const char* kErrCritical = "org.devmgr.Error.CriticalDevice";
inline constexpr const char* kErrNotAuthorized = "org.devmgr.Error.NotAuthorized";
inline constexpr const char* kErrNotFound = "org.devmgr.Error.NotFound";
inline constexpr const char* kErrUnsupported = "org.devmgr.Error.Unsupported";
inline constexpr const char* kErrIo = "org.devmgr.Error.Io";
// ApiVersion 4. Pre-v4 clients do not know this name; coreErrorFor() below
// collapses any unknown name to Io, so an old client reports a malformed
// request as an I/O failure rather than misreporting it as NotFound.
inline constexpr const char* kErrInvalidArgs = "org.devmgr.Error.InvalidArgs";

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
        case core::Error::Code::InvalidArgs:
            return kErrInvalidArgs;
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
    if (dbusErrorName == kErrInvalidArgs)
        return {.code = core::Error::Code::InvalidArgs, .message = std::move(message)};
    if (dbusErrorName == "org.freedesktop.DBus.Error.ServiceUnknown")
        return {.code = core::Error::Code::Io, .message = "helper devmgrd is not available"};
    if (dbusErrorName == "org.freedesktop.DBus.Error.NoReply" ||
        dbusErrorName == "org.freedesktop.DBus.Error.Timeout")
        return {.code = core::Error::Code::Busy, .message = "helper timed out"};
    // Daemon not present or not activatable on the bus: systemd refused/failed
    // activation, or D-Bus has no owner/server/connection. These are
    // "unreachable" (CLI exit 4), distinct from a reached daemon that failed an
    // op. Classified by the stable error NAME here — the authoritative signal,
    // which survives message localization — and carried as Busy, the domain code
    // the CLI already routes to unreachable, with the real cause preserved so the
    // user still sees why (e.g. "Unit devmgrd.service is masked.").
    if (dbusErrorName == "org.freedesktop.systemd1.UnitMasked" ||
        dbusErrorName == "org.freedesktop.systemd1.NoSuchUnit" ||
        dbusErrorName == "org.freedesktop.systemd1.UnitInactive" ||
        dbusErrorName == "org.freedesktop.DBus.Error.NameHasNoOwner" ||
        dbusErrorName == "org.freedesktop.DBus.Error.NoServer" ||
        dbusErrorName == "org.freedesktop.DBus.Error.Disconnected" ||
        dbusErrorName == "org.freedesktop.DBus.Error.Spawn.ExecFailed" ||
        dbusErrorName == "org.freedesktop.DBus.Error.Spawn.ServiceNotValid" ||
        dbusErrorName == "org.freedesktop.DBus.Error.Spawn.FileNotFound")
        return {.code = core::Error::Code::Busy, .message = std::move(message)};
    return {.code = core::Error::Code::Io, .message = std::move(message)};
}

}  // namespace devmgr::platform_linux
