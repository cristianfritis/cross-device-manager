# snapshot-ipc

## ADDED Requirements

### Requirement: ApiVersion 3 with additive snapshot verbs
`org.devmgr.Manager1` SHALL advertise `ApiVersion` 3 and add four verbs: `SnapshotList() -> s` (JSON array of metadata: id, parent, createdAtUtc, trigger, reason, corrupt flag), `SnapshotCreate(s label) -> s` (returns id), `SnapshotRestore(s id) -> s` (returns JSON per-item outcome summary), `SnapshotDelete(s id)`. All ApiVersion 2 verbs SHALL remain unchanged — the bump is additive only.

#### Scenario: Version negotiation
- **WHEN** a client reads `ApiVersion`
- **THEN** it receives 3 and all v2 verbs still behave as before

#### Scenario: List returns chain metadata
- **WHEN** `SnapshotList` is called with snapshots present
- **THEN** the JSON includes every snapshot's id, parent, timestamp, trigger, and reason, newest first

### Requirement: Polkit authorization mapping
`SnapshotCreate`, `SnapshotRestore`, and `SnapshotDelete` SHALL require a new polkit action `org.devmgr.manage-snapshots` with `auth_admin_keep`, checked via the existing raw-D-Bus `CheckAuthorization` path. `SnapshotList` SHALL be callable without authorization (metadata only, no secrets). The polkit policy file SHALL ship the new action, and installation docs MUST note the policy re-install requirement when upgrading.

#### Scenario: Unauthorized restore refused
- **WHEN** a caller without polkit authorization invokes `SnapshotRestore`
- **THEN** the call fails with the existing NotAuthorized error mapping and no state changes

#### Scenario: List without auth
- **WHEN** an unprivileged caller invokes `SnapshotList`
- **THEN** metadata is returned without a polkit prompt

### Requirement: Error taxonomy
Snapshot verbs SHALL map failures onto the existing daemon error names: unknown id → NotFound; corrupt snapshot → Io with a message naming the quarantined file; store write failure → Io; guard-refused convergence items are NOT verb-level errors (they appear in the restore outcome summary with the verb succeeding).

#### Scenario: Restore of unknown id
- **WHEN** `SnapshotRestore("deadbeef")` is called and no such snapshot exists
- **THEN** the call fails with NotFound and no snapshot is taken

#### Scenario: Partial convergence still succeeds
- **WHEN** restore converges 3 entries and the guard refuses 1
- **THEN** the verb returns success with an outcome summary marking 3 applied, 1 guard-refused
