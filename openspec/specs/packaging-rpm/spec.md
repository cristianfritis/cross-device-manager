# packaging-rpm Specification

## Purpose

CPack RPM generator for current Fedora releases: one RPM carrying the full devmgr stack with static sdbus-c++, lifecycle scriptlets mirroring the deb, and container build/install verification.

## Requirements

### Requirement: Single RPM with full stack
CPack SHALL produce one `devmgr-<version>.x86_64.rpm` for current Fedora releases containing the same file set as the deb: `devmgrd`, `devmgr-tui`, `devmgr-gui`, `devmgr` (CLI), systemd unit, D-Bus system-bus activation service file, D-Bus bus policy, polkit policy, and LICENSE. `sdbus-c++` SHALL be statically linked. `Requires:` SHALL name Fedora packages for Qt6, libudev, libkmod, and other dynamic needs.

#### Scenario: Install on fresh Fedora
- **WHEN** `sudo dnf install ./devmgr-0.6.0.x86_64.rpm` runs on a fresh Fedora system
- **THEN** dnf resolves all dependencies and installs without any manual sdbus-c++ step

### Requirement: Scriptlets mirror deb lifecycle behavior
`%post` SHALL reload systemd and D-Bus configuration so bus activation works immediately; `%preun` SHALL stop the daemon on package erase. `rpm -e` SHALL preserve `/var/lib/devmgrd` (RPM has no purge concept); the README SHALL document manual state-dir removal for a full cleanup.

#### Scenario: Erase preserves state
- **WHEN** the package is erased with `rpm -e devmgr`
- **THEN** the daemon is stopped, unit/policy/binary files are removed, and `/var/lib/devmgrd` survives

### Requirement: Container build and install verification
The RPM SHALL be built and install-verified in a Fedora container as part of the standard gates, analogous to the existing deb container check.

#### Scenario: Container verification passes
- **WHEN** the Fedora container job builds the RPM and installs it
- **THEN** the install completes, `rpm -V devmgr` reports no errors, and the packaged binaries run `--version` successfully
