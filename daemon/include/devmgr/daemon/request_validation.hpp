#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

#include "devmgr/core/result.hpp"

// The central IPC validation layer (daemon-hardening spec). EVERY
// org.devmgr.Manager1 verb validates its arguments here, at RequestProcessor
// entry, before guard/authorize/act — so a malformed request never reaches
// verb logic, never prompts for a password, and never changes state.
//
// Adding a verb? Call the validator for each of its arguments as the first
// statement of the method. Arguments arrive from an unprivileged bus caller
// and are untrusted: cap length BEFORE any work proportional to it (parsing,
// canonicalization, filesystem lookup), so an oversized payload costs a
// comparison rather than a syscall.
//
// Every failure returns InvalidArgs (wire: org.devmgr.Error.InvalidArgs),
// distinct from NotFound — malformed vs. names-something-absent.
namespace devmgr::daemon::validation {

// D-Bus caps a bus name at 255 bytes; a longer sender cannot be authentic.
inline constexpr std::size_t kCallerMaxLength = 255;
// Linux MODULE_NAME_LEN is 64 including the NUL, so 63 usable bytes. Driver
// names share the sysfs-directory-name namespace and the same bound.
inline constexpr std::size_t kNameMaxLength = 63;
// Longest meaningful sysfs path, well above any real one; bounds the string
// before canonicalContained() touches the filesystem.
inline constexpr std::size_t kPathMaxLength = 4096;
inline constexpr std::size_t kSnapshotIdMaxLength = 64;      // SHA-256 hex digest
inline constexpr std::size_t kSnapshotLabelMaxLength = 128;  // free text, bounded
// Cap on any JSON document parsed from an untrusted source. Snapshot payloads
// are small (a few hundred entries); a megabyte is generous and still bounds
// the parser's allocation.
inline constexpr std::size_t kJsonMaxBytes = 1U << 20U;

inline constexpr unsigned char kHighBit = 0x80;  // UTF-8 lead/continuation byte

inline core::Result<void> invalid(std::string message) {
    return core::makeError(core::Error::Code::InvalidArgs, std::move(message));
}

// The sender bus name as reported by sdbus. Only bounded here — authenticity
// is polkit's job, not ours; this exists so a hostile length cannot reach the
// authority layer or a log line.
inline core::Result<void> caller(const std::string& value) {
    if (value.empty()) return invalid("empty caller");
    if (value.size() > kCallerMaxLength) return invalid("caller too long");
    return {};
}

// Module and driver names: [A-Za-z0-9_-]+, bounded. The charset is what keeps
// a name from escaping into a sysfs path or a modprobe.d line.
inline core::Result<void> name(const std::string& value, const char* what) {
    if (value.empty()) return invalid(std::string("empty ") + what + " name");
    if (value.size() > kNameMaxLength) return invalid(std::string(what) + " name too long");
    const bool ok = std::ranges::all_of(
        value, [](unsigned char c) { return std::isalnum(c) != 0 || c == '_' || c == '-'; });
    if (!ok) return invalid(std::string("invalid ") + what + " name");
    return {};
}

// Length only — containment is canonicalContained()'s job, and it needs the
// real filesystem to decide. NUL bytes are rejected because the path becomes a
// C string on the way to sysfs, where a NUL would silently truncate it.
inline core::Result<void> sysfsPath(const std::string& value) {
    if (value.empty()) return invalid("empty sysfs path");
    if (value.size() > kPathMaxLength) return invalid("sysfs path too long");
    if (value.find('\0') != std::string::npos) return invalid("sysfs path contains NUL");
    return {};
}

// Snapshot ids become <id>.json file names inside the store directory, so the
// charset check is a path-traversal guard, not just hygiene: lowercase hex
// only, never longer than a SHA-256. Rejects "" and "../x".
inline core::Result<void> snapshotId(const std::string& value) {
    if (value.empty()) return invalid("empty snapshot id");
    if (value.size() > kSnapshotIdMaxLength) return invalid("snapshot id too long");
    const bool ok = std::ranges::all_of(
        value, [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    if (!ok) return invalid("invalid snapshot id");
    return {};
}

// Manual snapshot labels are free text shown back in UIs: printable characters
// only (no control bytes, which would corrupt a TUI row), bounded length.
// Bytes with the high bit set pass so multi-byte UTF-8 labels survive.
inline core::Result<void> snapshotLabel(const std::string& value) {
    if (value.size() > kSnapshotLabelMaxLength) return invalid("snapshot label too long");
    const bool ok = std::ranges::all_of(
        value, [](unsigned char c) { return std::isprint(c) != 0 || (c & kHighBit) != 0; });
    if (!ok) return invalid("invalid snapshot label");
    return {};
}

// Size gate for a JSON document before it reaches a parser. Checked by byte
// count so it costs nothing on the happy path.
inline core::Result<void> jsonSize(const std::string& text) {
    if (text.size() > kJsonMaxBytes) return invalid("json payload too large");
    return {};
}

}  // namespace devmgr::daemon::validation
