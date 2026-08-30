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

Each gate SHALL declare the platforms it covers, and the suite SHALL NOT be read as covering a platform it does not run on. The VM acceptance suite, the upgrade matrix, and the failure-path checks cover Linux packaging and the privileged helper, and SHALL remain Linux-only. A platform that ships without packaging or a helper SHALL have its own declared gates rather than being treated as covered by, or as exempt from, the Linux ones.

#### Scenario: Coverage claims name their platform
- **WHEN** the release exit gates are reviewed
- **THEN** each gate states which platforms it exercises, and no platform is recorded as verified by a gate that does not run on it

### Requirement: A supported platform without packaging still has declared gates
Every platform the project ships a runnable program for SHALL have a declared build gate and a declared behavioural gate, whether or not it has packaging. The build gate SHALL be automated and SHALL compile the platform's targets and run the platform-independent unit tests together with that platform's own backend unit tests. The behavioural gate MAY be an owner-run manual checklist where automation cannot reach real hardware, and SHALL be recorded with its result before release.

#### Scenario: A platform with no packaging is still gated
- **WHEN** a platform ships a runnable program but no installable package
- **THEN** it has both an automated build gate and a recorded behavioural gate, and the absence of packaging does not remove either

#### Scenario: Missing behavioural result blocks the release
- **WHEN** a supported platform's behavioural gate has not been run for the candidate
- **THEN** the release does not proceed until that gate is run and its result recorded

### Requirement: An automated build gate is not evidence of device behaviour
A hosted continuous-integration runner that cannot attach or detach real devices SHALL be recorded as covering compilation and unit tests only. Its result SHALL NOT be presented as verifying enumeration against real hardware, hotplug reaction, or any behaviour requiring a physical device, and release records SHALL state that limitation explicitly rather than leaving it to be inferred from a green run.

#### Scenario: CI result is scoped in the record
- **WHEN** a hosted runner's result is recorded for a platform
- **THEN** the record states that it covers compilation and unit tests only, and names the device behaviours it does not cover

#### Scenario: Green CI does not satisfy the behavioural gate
- **WHEN** a platform's hosted build gate passes and its behavioural gate has not run
- **THEN** the platform is not considered verified and the release does not proceed

### Requirement: The read-only inventory path is gated on every platform
The command-line inventory verbs SHALL be exercised on every platform that implements device enumeration, in a configuration with no privileged helper present, verifying that listing succeeds, that structured output parses, and that no connection to a helper is attempted.

#### Scenario: Inventory works with no helper installed
- **WHEN** the inventory gate runs on a system where the privileged helper is absent
- **THEN** listing succeeds, the structured output parses, the exit code indicates success, and no helper connection is attempted

### Requirement: The Linux behavioural gate covers the portability refactor
Because the platform backend seam rewrites every program's startup path, a release containing that change SHALL include a Linux behavioural gate covering startup and basic operation of every Linux surface — graphical, terminal, and command-line — in addition to the existing scenario coverage. A Linux regression introduced by portability work SHALL NOT be discoverable only on another platform.

#### Scenario: Every Linux surface starts and operates
- **WHEN** the Linux behavioural gate runs for a release containing platform-seam changes
- **THEN** each surface starts, populates its device view, performs one mutation end to end, and exits cleanly, and any failure blocks the release
