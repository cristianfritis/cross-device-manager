#pragma once
#include <string>
#include <vector>

#include "devmgr/core/snapshot_models.hpp"

namespace devmgr::core {

// What-changed diff between two snapshot payloads (snapshot-history spec).
// Computed in the daemon and shipped over IPC as JSON, so GUI, TUI and CLI
// all describe the same change with the same words — no surface recomputes it.
//
// Direction is base -> target: `before` is the base's state, `after` the
// target's. When the target is the live system, the "after" column is what the
// machine looks like right now, which is what a restore preview needs.

// Entry kinds. Strings, not an enum, because they cross the wire verbatim and
// UIs group rows by them.
inline constexpr const char* kDiffKindDevice = "device";
inline constexpr const char* kDiffKindModule = "module";
inline constexpr const char* kDiffKindModprobe = "modprobe";

// Shared state vocabulary, so every surface says "enabled" the same way.
inline constexpr const char* kDiffStateEnabled = "enabled";
inline constexpr const char* kDiffStateAbsent = "absent";
inline constexpr const char* kDiffStatePresent = "present";
inline constexpr const char* kDiffStateBlacklisted = "blacklisted";
// A devmgr-owned file that exists on both sides but whose bytes differ without
// any module-level directive changing (comments, reordering, whitespace).
inline constexpr const char* kDiffStateEdited = "present, contents differ";

// One named difference. `key` identifies the subject within its kind: a device
// label, a module name, or a modprobe.d file name.
struct SnapshotDiffEntry {
    std::string kind;
    std::string key;
    std::string before;
    std::string after;
    friend bool operator==(const SnapshotDiffEntry&, const SnapshotDiffEntry&) = default;
};

// A whole diff. `targetId` is empty when the target is live system state.
// An empty `entries` is the explicit "no differences" result the spec requires
// — callers MUST NOT treat it as a failure.
struct SnapshotDiff {
    std::string baseId;
    std::string targetId;
    std::vector<SnapshotDiffEntry> entries;
    [[nodiscard]] bool identical() const { return entries.empty(); }
    friend bool operator==(const SnapshotDiff&, const SnapshotDiff&) = default;
};

// Human-readable, stable identification of a disabled-device entry, used as the
// diff key for device rows: "usb 1d6b:0002 @2-1" (+ " #serial" when the key
// carries one). Falls back to the recorded sysfs path when there is no
// position, so a row is never anonymous.
std::string deviceEntryLabel(const DisabledDeviceEntry& entry);

// Disable state of a device entry as it appears in a diff cell:
// "disabled (authorized)" / "disabled (unbind)".
std::string deviceEntryState(const DisabledDeviceEntry& entry);

// Diffs two payloads. Device rows come from the entry lists (present = the
// device is disabled by devmgr). Module rows come from the modprobe.d
// directives inside devmgr-owned files, so a blacklist that moves between
// files still reads as one module change. Modprobe rows cover file
// appearance/disappearance and content changes that produce no module-level
// difference (comments, options reordering).
// Rows are ordered device, then module, then modprobe; each group sorted by
// key, so the same pair of payloads always yields byte-identical output.
SnapshotDiff diffPayloads(const SnapshotPayload& base, const SnapshotPayload& target);

}  // namespace devmgr::core
