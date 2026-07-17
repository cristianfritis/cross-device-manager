# Proposal: beta-05-packaging

## Why

devmgr's features through the backup-rollback engine are only usable by people who can build a C++20/Qt6/sdbus-c++ project from source. A v0.5.0 public beta needs installable artifacts with a few-step install, integrity checksums, and a test guide, so real users can exercise the stack (with the rollback safety net from `backup-rollback-engine`) and report issues.

## What Changes

- Project versioning: CMake `project(VERSION 0.5.0)` + generated `version.hpp`; `--version` on all four binaries (`devmgrd`, `devmgr-tui`, `devmgr-gui`, `devmgr`).
- CPack packaging: one `devmgr` .deb (Ubuntu 22.04/24.04) containing daemon + TUI + GUI + CLI, systemd unit, D-Bus system-bus activation + bus policy, polkit policy; `sdbus-c++ 2.3.1` static-linked (apt ships incompatible 1.x). Generic tarball with `install.sh`/`uninstall.sh` (systemd + OpenRC support) for other distros.
- Release pipeline: GitHub Actions workflow on `v*` tags — container build, full ctest, cpack DEB+TGZ, `SHA256SUMS`, draft GitHub prerelease with artifacts attached.
- User docs: README Install section (download → `apt install ./…deb` → launch), `BETA-TESTING.md` (scenarios incl. snapshot/restore round-trips, issue-reporting guide), release-notes template.
- Version tag `v0.5.0-beta.1`; owner flips the repo public and publishes the draft release when ready.
- **User-owned prerequisites (flagged, not automated): add LICENSE file (license choice is the owner's) and secret-scan of git history before the repo goes public.**

## Capabilities

### New Capabilities

- `release-versioning`: version single-source in CMake, `version.hpp` generation, `--version` behavior, prerelease tag scheme.
- `packaging-deb`: .deb content/layout, static sdbus-c++, runtime Depends, systemd unit + D-Bus activation + polkit/bus policies, postinst/prerm behavior, clean uninstall.
- `packaging-tarball`: tarball layout, `install.sh`/`uninstall.sh` contract, systemd/OpenRC detection.
- `release-pipeline`: tag-triggered CI job, artifact set, checksums, draft prerelease.
- `beta-docs`: install steps, beta test guide, release notes template, known-limits section.

### Modified Capabilities

<!-- none — additive packaging/release layer; no requirement changes to snapshot-* capabilities -->

## Impact

- **Code**: root `CMakeLists.txt` (version, CPack config, install rules), `daemon/data/` (systemd unit, D-Bus service/activation files), new `packaging/` dir (install.sh, uninstall.sh, OpenRC script), `.github/workflows/` (release job), README, new `BETA-TESTING.md`.
- **Build/CI**: existing Ubuntu container gains cpack step; new tag-triggered workflow; no change to per-push CI gates.
- **Dependencies**: sdbus-c++ becomes static-linked in packaged builds (already built from source in the container); distro Qt6/libudev/libkmod remain dynamic Depends.
- **Prereq**: depends on `backup-rollback-engine` landing first (beta test guide leans on restore; deb ships the v3 polkit policy).
- **Exit gate (pulled forward from Phase 8)**: fresh-VM install smoke — deb install → D-Bus activation → UIs list devices → snapshot/restore round-trip → clean uninstall.
