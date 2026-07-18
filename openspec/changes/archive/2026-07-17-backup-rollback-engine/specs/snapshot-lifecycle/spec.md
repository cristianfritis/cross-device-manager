# snapshot-lifecycle

## ADDED Requirements

### Requirement: Automatic snapshot before every mutating verb
`RequestProcessor` SHALL create an `auto` snapshot (reason = verb + subject) before executing any mutating IPC verb (device enable/disable, module verbs, driver-override verbs, and any future mutating verb routed through it). Snapshot creation failure SHALL fail the mutating request with an `Io` error — a mutation MUST NOT proceed without its safety net. The hash-dedupe rule makes the common no-change case free.

#### Scenario: Disable creates a snapshot first
- **WHEN** `SetDeviceEnabled(false)` is processed for a device
- **THEN** a snapshot whose reason records the verb and device exists before the sysfs write happens

#### Scenario: Snapshot failure blocks mutation
- **WHEN** the snapshots directory is not writable
- **THEN** the mutating verb returns an Io error and no device/module state is changed

### Requirement: Consistent point-in-time capture
Snapshot creation SHALL run under the daemon apply mutex so the captured payload reflects a consistent state with no concurrent mutation interleaved.

#### Scenario: Concurrent mutation serialized
- **WHEN** a snapshot creation and another mutating request race
- **THEN** the snapshot payload equals the state either fully before or fully after the other mutation, never a mix

### Requirement: Restore writes state back atomically then converges hardware
`SnapshotRestore(id)` SHALL, under the apply mutex: (1) verify the snapshot's integrity; (2) atomically replace `state.json` content and devmgr-written modprobe.d files with the payload; (3) converge hardware: run an enforcement sweep so entries present in the restored state are re-applied (per-device `CriticalDeviceGuard` re-check, existing enforcement path), and for each entry present before restore but absent after, attempt to re-enable the device. Guard refusals during convergence SHALL be reported per device and MUST NOT be bypassed; restore completes with a per-item outcome summary.

#### Scenario: Restore re-enables a device disabled after the snapshot
- **WHEN** a device was disabled after snapshot S was taken and `SnapshotRestore(S)` runs
- **THEN** the entry is removed from the store and a re-enable is attempted on the device

#### Scenario: Restore re-applies a disable removed after the snapshot
- **WHEN** a device entry present in snapshot S was re-enabled after S and `SnapshotRestore(S)` runs
- **THEN** the enforcement path re-applies the disable, subject to the criticality guard

#### Scenario: Guard refusal is reported, not bypassed
- **WHEN** convergence would disable a device that now hosts the root disk
- **THEN** that entry is marked guard-suspended, the device stays enabled, and the restore result reports the refusal

### Requirement: Restore takes its own safety snapshot
Before mutating anything, restore SHALL create an `auto` snapshot of the current state (reason verb = `SnapshotRestore`), so a restore is itself undoable.

#### Scenario: Undo a restore
- **WHEN** a restore turns out to be wrong
- **THEN** restoring the auto snapshot taken at restore time returns the system to its pre-restore state

### Requirement: Documented restore limits
Restore SHALL be config-level for modules: modprobe.d content is restored, but currently-loaded modules are not force-unloaded and blacklists take effect on next boot/hotplug. Firmware SHALL never be rolled back. Both limits SHALL be stated in user-facing docs and in the restore result text where applicable.

#### Scenario: Loaded module untouched
- **WHEN** a restore removes a module blacklist entry for a module that is currently loaded
- **THEN** no module unload is attempted and the result notes config-level scope

### Requirement: Delete refuses HEAD ambiguity safely
`SnapshotDelete(id)` SHALL remove the named snapshot file. Deleting the snapshot `HEAD` names SHALL move `HEAD` to its parent. Deleting a corrupt (`.bad`) file is out of scope for the verb — quarantined files are managed manually.

#### Scenario: Delete head snapshot
- **WHEN** the newest snapshot is deleted
- **THEN** `HEAD` names its parent and a subsequent create chains from there
