# packaging-tarball Specification

## Purpose
TBD - created by archiving change beta-05-packaging. Update Purpose after archive.
## Requirements
### Requirement: Generic tarball layout
CPack SHALL produce `devmgr-<version>-linux-x86_64.tar.gz` containing the same binaries as the deb plus `install.sh`, `uninstall.sh`, and both init integrations (systemd unit + OpenRC init script) under a `packaging/` subdirectory, and the D-Bus/polkit policy files.

#### Scenario: Tarball is self-contained
- **WHEN** the tarball is extracted on a distro without apt
- **THEN** everything `install.sh` needs is present inside the extracted tree

### Requirement: install.sh contract
`install.sh` SHALL: require root; detect systemd vs OpenRC (fall back to printing manual steps when neither is detected); copy binaries to `/usr/local/bin`, policies to the D-Bus/polkit system directories, and the matching init integration; reload D-Bus/systemd; and print a post-install summary naming every file it wrote. It SHALL be idempotent (safe to re-run) and abort without changes when a partial previous install cannot be reconciled.

#### Scenario: OpenRC host
- **WHEN** `install.sh` runs on a Gentoo/OpenRC system
- **THEN** the OpenRC script is installed instead of the systemd unit and the summary says so

#### Scenario: Re-run is safe
- **WHEN** `install.sh` runs twice
- **THEN** the second run succeeds and the system state equals the single-run state

### Requirement: uninstall.sh contract
`uninstall.sh` SHALL remove exactly the files `install.sh` writes (using the same path list), stop the daemon first, and leave `/var/lib/devmgrd` unless `--purge` is passed.

#### Scenario: Uninstall then purge
- **WHEN** `uninstall.sh` runs and then `uninstall.sh --purge` runs
- **THEN** binaries/policies are gone after the first and `/var/lib/devmgrd` only after the second

