# Tasks: beta-05-packaging

## 1. Versioning

- [x] 1.1 Root CMake `project(VERSION 0.5.0)` + prerelease suffix cache var + configure-generated `version.hpp`; remove any hard-coded versions
- [x] 1.2 `--version` flag on all four binaries (no D-Bus/display side effects) + unit tests

## 2. Daemon lifecycle files

- [x] 2.1 systemd unit (`devmgrd.service`, sandboxing directives) + D-Bus system activation file (`org.devmgr.Manager1.service` with `SystemdService=`) + verify activation flow manually against a local install
- [x] 2.2 OpenRC init script for the tarball path

## 3. CPack packaging

- [x] 3.1 Install rules for binaries, unit, activation, bus policy, polkit policy; packaged-build option flipping sdbus-c++ to static link (dev builds unchanged)
- [x] 3.2 CPack DEB config: metadata, per-series runtime Depends resolution, postinst/prerm (systemd + D-Bus reload, daemon stop), remove-vs-purge state-dir rule; build + install locally in container
- [x] 3.3 CPack TGZ config + `packaging/install.sh` (root check, systemd/OpenRC detect, idempotent, file-list summary) + `uninstall.sh` (same file list, `--purge`) + `bash -n`/shellcheck + container dry-run

## 4. Release pipeline

- [x] 4.1 GH Actions workflow on `v*` tags: container build, tag==version guard, full ctest, cpack DEB+TGZ, SHA256SUMS, draft prerelease with 3 assets + template-filled body

## 5. Docs

- [x] 5.1 README Install section (3 steps Ubuntu, tarball path, checksum verify, polkit re-install note for source upgrades)
- [x] 5.2 `BETA-TESTING.md`: scenario loop (enumerate/hotplug, disable+restore, blacklist+restore, firmware check, manual snapshot), expected results, warnings (spare devices, restore limits), log collection, issue template + `.github/ISSUE_TEMPLATE`
- [x] 5.3 Release-notes template with substitutable version fields; owner pre-public checklist (LICENSE, secret scan, flip, publish) recorded in docs

## 6. Exit gate

- [x] 6.1 `test/vm/install-smoke.sh` + rig wiring: fresh Ubuntu VM → install deb → bus activation → both UIs enumerate → snapshot/restore round-trip → uninstall residue check → `INSTALL SMOKE OK`
- [x] 6.2 Full standard gates (build/ctest both configs, format, gated tidy, purity greps) + tag `v0.5.0-beta.1` dry-run through the workflow (draft release verified, then discarded or kept per owner)
  - [x] Gates: host ctest 395/395, nosdbus 379/379, clang-format clean, container clang-tidy clean (exit 0), Qt/FTXUI/GLib/sdbus purity greps 0 hits. Tidy needed four fixes — see below.
  - [x] Tag `v0.5.0-beta.1` dry-run — owner pushed tag 2026-07-18; tag object 46b9704 → commit 284ffd5 (main HEAD); draft release assets verified by owner; stale draft tags deleted.
