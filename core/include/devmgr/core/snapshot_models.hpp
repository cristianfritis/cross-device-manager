#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "devmgr/core/models.hpp"

namespace devmgr::core {

// Snapshot file format v1 (backup-rollback-engine spec, snapshot-store).
// Readers MUST reject documents whose formatVersion is greater than this.
inline constexpr int kSnapshotFormatVersion = 1;

enum class SnapshotTrigger { Manual, Auto };

// Serialized forms are the spec's lowercase strings: "manual" / "auto".
const char* to_string(SnapshotTrigger trigger);
std::optional<SnapshotTrigger> snapshotTriggerFrom(std::string_view text);

// Why a snapshot exists. For Auto: verb = the triggering IPC verb, subject =
// its subject (device path, module name, snapshot id, ...). For Manual: verb
// is empty and subject is the user-provided label (possibly empty).
struct SnapshotReason {
    std::string verb;
    std::string subject;
    friend bool operator==(const SnapshotReason&, const SnapshotReason&) = default;
};

// Read-side health, derived by the store — never serialized into the file.
// Corrupt: content hash mismatch, file quarantined. Unsupported: formatVersion
// newer than kSnapshotFormatVersion; restore/delete refused, file untouched.
enum class SnapshotHealth { Ok, Corrupt, Unsupported };
const char* to_string(SnapshotHealth health);

// Everything devmgr owns: the full state.json entry list (entries carry the
// disable mechanism, including driver unbind/override state) plus the content
// of every devmgr-written modprobe.d file, keyed by file name. std::map keeps
// modprobe file order canonical for serialization and hashing.
struct SnapshotPayload {
    std::vector<DisabledDeviceEntry> entries;
    std::map<std::string, std::string> modprobeFiles;
    friend bool operator==(const SnapshotPayload&, const SnapshotPayload&) = default;
};

struct SnapshotMeta {
    std::string id;  // lowercase hex SHA-256 of the canonical payload serialization
    std::optional<std::string> parent;  // nullopt for the first snapshot in the chain
    std::int64_t createdAtUtc = 0;      // Unix seconds
    SnapshotTrigger trigger = SnapshotTrigger::Auto;
    SnapshotReason reason;
    SnapshotHealth health = SnapshotHealth::Ok;  // derived at read time
    std::size_t entryCount = 0;                  // payload summary for list/detail
    std::size_t modprobeFileCount = 0;
    friend bool operator==(const SnapshotMeta&, const SnapshotMeta&) = default;
};

// Full snapshot document: meta + payload. `parent` links snapshots into the
// v1 linear history; the field is the extension point for the post-beta
// history graph and must not be repurposed.
struct Snapshot {
    SnapshotMeta meta;
    SnapshotPayload payload;
    friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

// One convergence step of a restore. Guard refusals are items, never verb
// errors: a restore that partially converges still succeeds and reports.
struct RestoreItemOutcome {
    std::string subject;  // device sysfs path or modprobe.d file name
    std::string action;   // "re-apply-disable" | "re-enable" | "modprobe-write" | "modprobe-remove"
    std::string status;   // "ok" | "guard-refused" | "failed" | "device-absent"
    std::string detail;   // guard reason, error text, or config-level note
    friend bool operator==(const RestoreItemOutcome&, const RestoreItemOutcome&) = default;
};

struct RestoreOutcome {
    std::string snapshotId;
    std::string safetySnapshotId;  // auto snapshot taken before mutating — restore is undoable
    std::vector<RestoreItemOutcome> items;
    friend bool operator==(const RestoreOutcome&, const RestoreOutcome&) = default;
};

}  // namespace devmgr::core
