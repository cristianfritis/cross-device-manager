#pragma once
#include <string>
#include <vector>

#include "devmgr/core/result.hpp"
#include "devmgr/core/snapshot_diff.hpp"
#include "devmgr/core/snapshot_models.hpp"

namespace devmgr::core {

// The ApiVersion 3 snapshot wire shapes (snapshot-ipc spec): SnapshotList
// returns a JSON array of metadata, SnapshotRestore returns a JSON per-item
// outcome summary. One serializer/parser pair in core keeps the daemon
// adaptor, the privileged-channel client, and the recovery CLI byte-compatible.
// `corrupt` is kept alongside the finer `health` string because the spec names
// a corrupt flag; health != "ok" implies corrupt-for-restore purposes.

std::string snapshotListToJson(const std::vector<SnapshotMeta>& metas);
Result<std::vector<SnapshotMeta>> snapshotListFromJson(const std::string& text);

std::string restoreOutcomeToJson(const RestoreOutcome& outcome);
Result<RestoreOutcome> restoreOutcomeFromJson(const std::string& text);

// ApiVersion 4: SnapshotDiff returns this shape. `differences` is the explicit
// no-differences marker the spec requires — a reader must not have to infer it
// from an empty array, and a surface can render the "no differences" line
// without inspecting entries at all.
//   {"baseId":"..","targetId":"","differences":true,
//    "entries":[{"kind":"device","key":"..","before":"..","after":".."}]}
// An empty `targetId` means the target was live system state.
std::string snapshotDiffToJson(const SnapshotDiff& diff);
Result<SnapshotDiff> snapshotDiffFromJson(const std::string& text);

}  // namespace devmgr::core
