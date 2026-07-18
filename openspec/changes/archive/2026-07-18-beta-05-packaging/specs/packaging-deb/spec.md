# packaging-deb

## ADDED Requirements

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
