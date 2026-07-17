# snapshot-store

## ADDED Requirements

### Requirement: Snapshot file format v1
A snapshot SHALL be a single JSON document with fields: `formatVersion` (integer, 1), `id` (lowercase hex SHA-256 of the canonical serialized `payload`), `parent` (id of the previous snapshot, or null for the first), `createdAtUtc` (Unix seconds), `trigger` (`"manual"` or `"auto"`), `reason` (object: `verb`, `subject` — for `auto`: the triggering IPC verb and its subject; for `manual`: `verb` empty, `subject` = the user-provided label, possibly empty), and `payload`. The `payload` SHALL capture all devmgr-owned state: the full `state.json` entry list (these entries already carry the disable mechanism, including driver unbind/override state) and the content of every devmgr-written modprobe.d file. Readers MUST reject documents whose `formatVersion` is greater than the highest version they know.

#### Scenario: Round-trip fidelity
- **WHEN** a snapshot is created from current daemon state and read back
- **THEN** the parsed payload compares equal to the state captured at creation time

#### Scenario: Unknown future version
- **WHEN** a snapshot file with `formatVersion: 2` is present in the store directory
- **THEN** the store lists it as unsupported and MUST NOT attempt restore from it or delete it

### Requirement: Content-hash identity and integrity check
The snapshot `id` SHALL be the SHA-256 of the canonical payload serialization. On every read, the store SHALL recompute the hash; on mismatch the file SHALL be quarantined (renamed to `<name>.bad-<timestamp>` in place) and surfaced as corrupt in listings. Evidence MUST never be silently deleted.

#### Scenario: Tampered snapshot detected
- **WHEN** a snapshot file's payload no longer matches its `id`
- **THEN** the file is renamed to a `.bad-` name, the snapshot appears as corrupt in `list`, and restore from it is refused

### Requirement: Atomic write discipline
Snapshot files and the `HEAD` pointer file SHALL be written with the tmp+fsync+rename idiom (write to `<file>.tmp`, flush, fsync file, rename, fsync directory), matching the existing `StateStore` discipline. A crash at any point SHALL leave either the old state or the new state, never a torn file.

#### Scenario: Crash during snapshot write
- **WHEN** the process is killed after the tmp file is written but before rename
- **THEN** the store directory contains no partially-written snapshot visible to `list`, and `HEAD` still points at the previous snapshot

### Requirement: Linear history via parent chain and HEAD
The store SHALL maintain a `HEAD` file naming the most recent snapshot id. Each new snapshot's `parent` SHALL be the id `HEAD` named at creation time. The chain gives a linear history in v1; the field is the extension point for the post-beta history graph and MUST NOT be repurposed.

#### Scenario: Chain continuity
- **WHEN** three snapshots are created in sequence
- **THEN** `HEAD` names the third, whose `parent` names the second, whose `parent` names the first

### Requirement: Pruning policy
The store SHALL keep at most 20 `auto`-trigger snapshots, deleting the oldest (by chain order) beyond that count. `manual` snapshots SHALL be exempt from pruning and persist until explicitly deleted. Pruning MUST NOT break the parent chain for remaining snapshots: the oldest surviving snapshot MAY have a `parent` id that no longer resolves, and `list` SHALL tolerate that.

#### Scenario: Auto snapshots pruned at 21
- **WHEN** a 21st auto snapshot is created
- **THEN** the oldest auto snapshot file is removed and 20 auto snapshots remain

#### Scenario: Manual snapshots survive pruning
- **WHEN** pruning runs while manual snapshots older than every auto snapshot exist
- **THEN** no manual snapshot is deleted

### Requirement: Hash-dedupe on unchanged state
When a snapshot is requested and the computed payload hash equals the id `HEAD` points at, the store SHALL NOT write a new file and SHALL report the existing id as the result.

#### Scenario: Two consecutive identical auto snapshots
- **WHEN** two mutating requests arrive with no state change between them
- **THEN** only one snapshot file exists for that state and both operations reference the same id
