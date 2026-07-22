# acceptance-suite Specification

## Purpose

VM acceptance suite that exercises the *installed* package (not the build tree) end to end — enumeration, hotplug, disable+restore, blacklist, firmware, CLI recovery, upgrade matrix, and failure paths — and serves as the required release exit gate.

## Requirements

### Requirement: Scenario coverage against installed artifacts
A VM acceptance suite SHALL exercise the *installed package* (not the build tree) end to end: device enumeration in both UIs, hotplug reaction, disable + snapshot restore round-trip, driver blacklist round-trip, firmware update check against the test remote, and CLI recovery path. The run SHALL end with an explicit `ACCEPTANCE OK` marker; any step failure fails the run.

#### Scenario: Full pass on clean VM
- **WHEN** the acceptance script runs on a clean VM with the release deb installed
- **THEN** every scenario passes and the script prints `ACCEPTANCE OK`

### Requirement: Upgrade preserves user data
Upgrading from the previous published release to the candidate package SHALL preserve configuration and all snapshots: after upgrade, `SnapshotList` returns the pre-upgrade snapshots, restore of a pre-upgrade snapshot works, and devmgr-owned modprobe files survive. This SHALL be verified for the DEB path and the RPM path.

#### Scenario: Snapshots survive a deb upgrade
- **WHEN** v0.5.0-beta.1 is installed, snapshots are created, and the candidate deb is installed over it
- **THEN** the daemon runs the new version and every pre-upgrade snapshot lists and restores correctly

### Requirement: Failure-path package behavior is defined and tested
The acceptance suite SHALL cover: downgrade to the previous release (documented outcome — at minimum, no crash and state-format compatibility or a clear refusal), an interrupted install (package manager killed mid-transaction) recovering via reinstall, replacement of a tarball install by a package install, and purge leaving no residue. Each path SHALL have a scripted check on both DEB and RPM where the mechanism exists.

#### Scenario: Interrupted install recovers
- **WHEN** an install is killed mid-transaction and the same package is installed again
- **THEN** the second install completes and the acceptance scenarios pass

### Requirement: Acceptance is the release exit gate
The acceptance suite (scenario coverage + upgrade matrix) SHALL be wired into the VM rig alongside the existing smoke scripts and SHALL be a required exit gate for this change and future releases.

#### Scenario: Gate blocks on failure
- **WHEN** any acceptance scenario fails during release verification
- **THEN** the release does not proceed until the failure is fixed or the scenario's expected outcome is explicitly re-specified
