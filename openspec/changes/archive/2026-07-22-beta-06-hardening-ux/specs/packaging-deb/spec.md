# packaging-deb Delta

## ADDED Requirements

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
