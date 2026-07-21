#pragma once
#include <optional>
#include <string>
#include <vector>

#include "devmgr/core/snapshot_diff.hpp"
#include "devmgr/core/snapshot_history.hpp"
#include "devmgr/core/snapshot_models.hpp"

namespace devmgr::core {

// Shared wording for everything a user reads about a diff or a restore
// outcome (snapshot-history + snapshot-ui specs, design decision 3). Lives in
// core, not app/, because the CLI has no VM layer: GUI and TUI reach these
// strings through SnapshotsVM and the CLI's printer calls them directly, so
// the three surfaces cannot drift apart in wording.
//
// ASCII only, deliberately: these lines are rendered verbatim in FTXUI, where
// docs/DESIGN.md §8 forbids ambiguous-width Unicode. The arrow is "->".

// One diff row: "<kind><key>: <before> -> <after>", with the kind padded to a
// fixed column so a group of rows scans vertically in both a terminal and a
// proportional-font detail pane.
std::string diffEntryLine(const SnapshotDiffEntry& entry);

// The whole diff as display lines. An identical pair yields exactly one line
// stating that plainly — the spec's explicit "no differences" result, which
// callers MUST NOT render as a failure or an empty state.
std::vector<std::string> diffLines(const SnapshotDiff& diff);

// The chain-position markers for one history row as words in brackets
// (snapshot-history spec, docs/DESIGN.md §10 — never a bare glyph or color):
// "  [chain start, HEAD, last good]". Several can apply at once (the newest
// row after a prune is both a chain start and HEAD). Empty string when none
// apply. Shared so the history view reads identically in the VM (both UIs) and
// the recovery CLI.
std::string chainMarkers(const SnapshotChainRow& row);

// The "what will change" section of a restore preview (snapshot-ui spec). The
// diff runs snapshot -> current live state, so the "after" column is the live
// machine; the heading says so, because a restore moves the live side back to
// the snapshot side and an unlabelled arrow would read as the reverse. A
// nullopt diff (it could not be computed) and an identical diff each yield
// their own single explanatory line, never an empty section.
std::vector<std::string> restorePreviewChangeLines(const std::optional<SnapshotDiff>& diff);

// The partial-convergence note every restore preview ends with (snapshot-ui
// spec): a plain-language statement that the critical-device guard can refuse
// individual items while the restore still completes and reports each one.
// Single-sourced so the CLI preview and both UIs word the caveat identically.
std::string restorePreviewConvergenceNote();

// One-line convergence summary for the status surfaces: per-status item
// counts with zero counts omitted, "ok" always shown, then the safety
// snapshot. Guard refusals are items here, never a failed task.
std::string restoreSummary(const RestoreOutcome& outcome);

// The command that undoes this restore. Uses the short id, which the CLI
// accepts as an id prefix.
std::string restoreRecoveryCommand(const RestoreOutcome& outcome);

// Actionable recovery guidance for a restore that did not fully converge
// (snapshot-ui spec): every item that did not end "ok" with its reason, the
// safety snapshot id, and the exact CLI command to fall back to. Returns an
// EMPTY vector when every item converged — there is nothing to recover from,
// and the summary alone is the right surface. A bare error string with no
// next step is not a permitted rendering of a failure, so callers that get a
// non-empty vector must show all of it.
std::vector<std::string> restoreGuidanceLines(const RestoreOutcome& outcome);

}  // namespace devmgr::core
