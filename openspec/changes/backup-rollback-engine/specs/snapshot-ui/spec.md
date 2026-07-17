# snapshot-ui

## ADDED Requirements

### Requirement: SnapshotsVM is the single row/detail source
A `SnapshotsVM` in `app/` SHALL own the Snapshots presentation state for both UIs, following the established VM pattern: byte-frozen row format rendered identically by TUI and GUI, `rowsRef()/selectedRef()/rebuild()/banner()/detailLines()` shape, async facade calls, coalesced refresh, teardown-safe subscriptions. Rows SHALL show: short id (first 12 hex), local date-time, trigger, reason subject. Detail SHALL show full id, parent id, and payload summary (entry counts).

#### Scenario: Byte-identical rows across UIs
- **WHEN** the same snapshot list is rendered in TUI and GUI
- **THEN** the row strings are byte-identical (pinned by a row-format unit test both UIs consume)

### Requirement: TUI Snapshots screen
The TUI SHALL add a 4th tab `[4]Snapshots` reachable via the existing digit hotkeys and `m` cycle. Verbs: `s` = create manual snapshot (prompt for label), `r` = restore selected (confirmation modal stating the restore outcome semantics), `x` = delete selected (confirmation modal). Corrupt snapshots SHALL render marked and refuse restore locally. All modals follow docs/DESIGN.md conventions.

#### Scenario: Restore requires confirmation
- **WHEN** the user presses `r` on a snapshot row
- **THEN** a confirmation modal appears and restore is only invoked on explicit confirm

### Requirement: GUI Snapshots tab with semantic parity
The GUI SHALL add a Snapshots tab with a `SnapshotListModel` (mirroring the existing list-model pattern: hook-unregister-in-dtor, model-tester clean) rendering `SnapshotsVM` rows verbatim, plus Create/Restore/Delete actions gated to the tab, Qt confirmation dialogs, and status-bar outcome reporting through the existing StatusLine path. Semantic parity with the TUI per docs/DESIGN.md MUST hold: same verbs, same confirmation semantics, same outcome visibility.

#### Scenario: Outcome summary surfaces
- **WHEN** a restore completes with one guard-refused item
- **THEN** both UIs surface the partial-convergence summary through their status surfaces

### Requirement: Snapshot events drive refresh
Snapshot create/restore/delete completion SHALL publish through the existing event/task machinery so both UIs refresh their Snapshots view without manual reload, including when the mutation originated in the other UI or the CLI.

#### Scenario: Cross-frontend refresh
- **WHEN** the CLI creates a snapshot while the GUI is open on the Snapshots tab
- **THEN** the GUI list shows the new snapshot after the daemon signal round-trip
