# packaging-deb Specification

## Purpose
TBD - created by archiving change beta-05-packaging. Update Purpose after archive.
## Requirements
### Requirement: Single deb with full stack
CPack SHALL produce one `devmgr_<version>_amd64.deb` for Ubuntu 22.04/24.04 containing: `devmgrd`, `devmgr-tui`, `devmgr-gui`, `devmgr` (CLI), systemd unit, D-Bus system-bus activation service file, D-Bus bus policy, and the polkit policy (including `org.devmgr.manage-snapshots`). `sdbus-c++` SHALL be statically linked into shipped binaries (distro package is an incompatible 1.x). Runtime `Depends:` SHALL name distro packages for Qt6, libudev, libkmod, and other dynamic needs, derived per target series.

#### Scenario: Install on fresh Ubuntu 22.04
- **WHEN** `sudo apt install ./devmgr_0.5.0_amd64.deb` runs on a fresh system
- **THEN** apt resolves all dependencies and installs without any manual sdbus-c++ step

### Requirement: D-Bus activation, no manual daemon start
The deb SHALL install a D-Bus system service activation file so `devmgrd` starts on first `org.devmgr.Manager1` call, plus a systemd unit the activation delegates to (`SystemdService=`). `postinst` SHALL reload systemd and D-Bus configuration. Users MUST NOT need a manual start step before launching a UI.

#### Scenario: First launch activates daemon
- **WHEN** a user launches `devmgr-gui` right after install and triggers a privileged action
- **THEN** devmgrd auto-starts via bus activation and the polkit prompt appears

### Requirement: Clean uninstall
`apt remove devmgr` SHALL remove binaries, unit, and policy files and stop the daemon; `apt purge` SHALL additionally remove `/var/lib/devmgrd` (state + snapshots). Plain remove SHALL preserve `/var/lib/devmgrd` so reinstalls keep state.

#### Scenario: Remove preserves state, purge deletes it
- **WHEN** the package is removed and later purged
- **THEN** `/var/lib/devmgrd` survives the remove and is gone after the purge

### Requirement: Fresh-VM install smoke gate
The change's exit gate SHALL include an automated fresh-VM test: install the deb on a clean Ubuntu VM, verify bus activation, both UIs enumerate devices, a snapshot/restore round-trip succeeds, and uninstall leaves no unit/policy/binary residue (state dir per the remove rule).

#### Scenario: Install smoke passes
- **WHEN** the install-smoke script runs in the clean VM
- **THEN** it ends with an explicit `INSTALL SMOKE OK` after all steps pass

### Requirement: Upgrade preserves state and configuration
Installing a newer devmgr deb over an existing install SHALL preserve `/var/lib/devmgrd` (state and all snapshots) and devmgr-owned modprobe files, restart the daemon on the new version, and require no manual migration for supported version steps. Pre-upgrade snapshots SHALL list and restore correctly after the upgrade.

#### Scenario: Upgrade keeps snapshots
- **WHEN** the 0.6 deb is installed over a 0.5 install with existing snapshots
- **THEN** after the upgrade the daemon reports the new version and all pre-upgrade snapshots list and restore

### Requirement: Interrupted install and replacement recovery
An install or upgrade interrupted mid-transaction SHALL be recoverable by re-running the install (idempotent maintainer scripts, no half-configured daemon left running). Installing the deb over a previous tarball install SHALL be a defined path: the packaged files take over and the documented tarball uninstall removes any remaining tarball-only residue.

#### Scenario: Reinstall after interruption
- **WHEN** dpkg is killed during postinst and `apt install ./devmgr...deb` runs again
- **THEN** the second run completes cleanly and bus activation works

