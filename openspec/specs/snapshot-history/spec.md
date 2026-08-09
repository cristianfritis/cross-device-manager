# snapshot-history Specification

## Purpose

Parent-chain history and what-changed diff over the existing snapshot metadata (format v1, no store migration), presented identically across GUI, TUI, and CLI through the shared VM layer.

## Requirements

### Requirement: Parent-chain history presentation
Both UIs and the CLI SHALL present snapshot history as a parent chain derived from the existing `id` + `parent` metadata (format v1, no store migration): newest first, chain relationship visible (which snapshot each entry descends from), with the current HEAD identified and the last-good (most recent healthy) snapshot identifiable. Snapshots whose parent is missing (pruned) SHALL render as chain starts, not errors.

#### Scenario: Chain renders after pruning
- **WHEN** the history view is opened after auto-prune removed an old parent
- **THEN** the surviving snapshots render as a chain with the pruned ancestor shown as an absent chain start, and HEAD is marked

### Requirement: What-changed diff between snapshots
The system SHALL compute a per-entry diff between two snapshots, and between a snapshot and the current live state: device enable/disable differences, module differences, and devmgr-owned modprobe content differences, each named per entry. Identical payloads SHALL yield an explicit "no differences" result. Diff SHALL be read-only and require no polkit authorization (parity with `SnapshotList`).

#### Scenario: Diff names the changed entry
- **WHEN** two snapshots differ only in one device's enabled state
- **THEN** the diff lists exactly that device with its before/after state and reports every other entry as unchanged

#### Scenario: Identical payloads
- **WHEN** a diff is requested between two snapshots that share the same content hash
- **THEN** the result explicitly states no differences

### Requirement: History and diff parity across surfaces
The history view and diff SHALL present the same facts in GUI, TUI, and CLI, with row/label wording shared through the VM layer per docs/DESIGN.md so the three surfaces never disagree about chain order, HEAD, or diff content.

#### Scenario: Same diff everywhere
- **WHEN** the same snapshot pair is diffed in GUI, TUI, and CLI
- **THEN** all three report the same entries with the same change descriptions
