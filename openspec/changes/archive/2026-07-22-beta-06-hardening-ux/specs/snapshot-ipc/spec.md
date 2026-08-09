# snapshot-ipc Delta

## RENAMED Requirements

- FROM: `### Requirement: ApiVersion 3 with additive snapshot verbs`
- TO: `### Requirement: ApiVersion 4 with additive snapshot verbs`

## MODIFIED Requirements

### Requirement: ApiVersion 4 with additive snapshot verbs
`org.devmgr.Manager1` SHALL advertise `ApiVersion` 4. The v3 verbs remain unchanged: `SnapshotList() -> s` (JSON array of metadata: id, parent, createdAtUtc, trigger, reason, corrupt flag), `SnapshotCreate(s label) -> s` (returns id), `SnapshotRestore(s id) -> s` (returns JSON per-item outcome summary), `SnapshotDelete(s id)`. ApiVersion 4 adds one read verb: `SnapshotDiff(s baseId, s targetId) -> s` returning a JSON per-entry diff; an empty `targetId` means the current live system state. All ApiVersion 2 and 3 verbs SHALL remain unchanged — the bump is additive only. ApiVersion 4 also adds the error name `org.devmgr.Error.InvalidArgs` for malformed arguments (see daemon-hardening); pre-v4 clients that do not recognize it fall through to the existing unknown-error rule and report it as `Io`.

#### Scenario: Version negotiation
- **WHEN** a client reads `ApiVersion`
- **THEN** it receives 4 and all v2/v3 verbs still behave as before

#### Scenario: List returns chain metadata
- **WHEN** `SnapshotList` is called with snapshots present
- **THEN** the JSON includes every snapshot's id, parent, timestamp, trigger, and reason, newest first

#### Scenario: Diff against live state
- **WHEN** `SnapshotDiff(id, "")` is called
- **THEN** the returned JSON lists per-entry differences between that snapshot and the current system state

### Requirement: Polkit authorization mapping
`SnapshotCreate`, `SnapshotRestore`, and `SnapshotDelete` SHALL require a new polkit action `org.devmgr.manage-snapshots` with `auth_admin_keep`, checked via the existing raw-D-Bus `CheckAuthorization` path. `SnapshotList` and `SnapshotDiff` SHALL be callable without authorization (metadata and configuration-state reads only, no secrets). The polkit policy file SHALL ship the new action, and installation docs MUST note the policy re-install requirement when upgrading.

#### Scenario: Unauthorized restore refused
- **WHEN** a caller without polkit authorization invokes `SnapshotRestore`
- **THEN** the call fails with the existing NotAuthorized error mapping and no state changes

#### Scenario: List without auth
- **WHEN** an unprivileged caller invokes `SnapshotList` or `SnapshotDiff`
- **THEN** the result is returned without a polkit prompt
