# snapshot-ui Specification

## Purpose

Snapshot presentation across both frontends: a shared `SnapshotsVM` as the single row/detail source, the TUI Snapshots screen, the GUI Snapshots tab with semantic parity per docs/DESIGN.md, and event-driven cross-frontend refresh.

## Requirements

### Requirement: SnapshotsVM is the single row/detail source
A `SnapshotsVM` in `app/` SHALL own the Snapshots presentation state for both UIs, following the established VM pattern: byte-frozen row format rendered identically by TUI and GUI, `rowsRef()/selectedRef()/rebuild()/banner()/detailLines()` shape, async facade calls, coalesced refresh, teardown-safe subscriptions. Rows SHALL show: short id (first 12 hex), local date-time, trigger, reason subject. Detail SHALL show full id, parent id, and payload summary (entry counts).

#### Scenario: Byte-identical rows across UIs
- **WHEN** the same snapshot list is rendered in TUI and GUI
- **THEN** the row strings are byte-identical (pinned by a row-format unit test both UIs consume)

### Requirement: TUI Snapshots screen
The TUI SHALL add a 4th tab `[4]Snapshots` reachable via the existing digit hotkeys and `m` cycle. Verbs: `s` = create manual snapshot (prompt for label), `r` = restore selected (opens the restore preview; restore only on explicit confirm), `x` = delete selected (confirmation modal), `d` = diff selected against current state, `h` = toggle the parent-chain history presentation, `/` = filter (extended to this tab). Corrupt snapshots SHALL render marked and refuse restore locally. All modals follow docs/DESIGN.md conventions.

#### Scenario: Restore requires preview confirmation
- **WHEN** the user presses `r` on a snapshot row
- **THEN** the restore preview appears (diff, selected/current/last-good, partial-convergence note) and restore is only invoked on explicit confirm

### Requirement: GUI Snapshots tab with semantic parity
The GUI SHALL add a Snapshots tab with a `SnapshotListModel` (mirroring the existing list-model pattern: hook-unregister-in-dtor, model-tester clean) rendering `SnapshotsVM` rows verbatim, plus Create/Restore/Delete/Diff/History actions gated to the tab, a filter field, the restore preview dialog, and status-bar outcome reporting through the existing StatusLine path. Semantic parity with the TUI per docs/DESIGN.md MUST hold: same verbs, same preview and confirmation semantics, same outcome visibility.

#### Scenario: Outcome summary surfaces
- **WHEN** a restore completes with one guard-refused item
- **THEN** both UIs surface the partial-convergence summary through their status surfaces

### Requirement: Snapshot events drive refresh
Snapshot create/restore/delete completion SHALL publish through the existing event/task machinery so both UIs refresh their Snapshots view without manual reload, including when the mutation originated in the other UI or the CLI.

#### Scenario: Cross-frontend refresh
- **WHEN** the CLI creates a snapshot while the GUI is open on the Snapshots tab
- **THEN** the GUI list shows the new snapshot after the daemon signal round-trip

### Requirement: Restore preview before confirmation
Before any restore executes, both UIs SHALL present a preview: what will change (the diff between the selected snapshot and the current live state), which snapshot is selected, which is current HEAD, and which is last-good, plus a plain-language note that convergence may be partial (guard refusals reported per item, verb still succeeding). The preview facts and wording SHALL come from the shared VM layer so GUI and TUI are identical. Restore SHALL only proceed on explicit confirmation from the preview.

#### Scenario: Preview shows the pending change
- **WHEN** a user initiates restore of a snapshot that re-enables one device
- **THEN** the preview names that device with its before/after state, marks selected/current/last-good, and restore runs only after confirm

### Requirement: Restore failure recovery guidance
When a restore fails or completes with failed items, both UIs SHALL surface actionable recovery guidance with the same facts: which items failed and why, the id of the safety snapshot taken before the restore, and the CLI command to fall back to (`devmgr snapshot restore <safety-id>`). A bare error string with no next step is not permitted.

#### Scenario: Failed restore names the way back
- **WHEN** a restore reports a failed item
- **THEN** the outcome surface in both UIs shows the failure reason, the safety snapshot id, and the exact CLI recovery command

### Requirement: Snapshots filter
Both UIs SHALL provide a filter on the Snapshots view matching rows by case-insensitive substring over id, trigger, and reason, following the same interaction pattern as the existing Devices filter (TUI `/`, GUI filter field). Filtering SHALL never hide the selected snapshot's detail surface.

#### Scenario: Filter narrows rows
- **WHEN** the user filters for "auto" on the Snapshots view
- **THEN** only auto-trigger snapshots remain listed in both UIs, with identical resulting row sets

### Requirement: History and diff entry points
The Snapshots view in both UIs SHALL expose entry points for the parent-chain history view and for diffing (selected snapshot vs current state, and selected vs another snapshot), surfacing the semantics defined in the snapshot-history capability.

#### Scenario: Diff from the Snapshots tab
- **WHEN** the user invokes diff on a selected snapshot
- **THEN** the per-entry diff against the current live state is shown in that UI using the shared wording
