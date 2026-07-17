# Design: beta-05-packaging

## Context

All product code for 0.5 lands with `backup-rollback-engine`; this change makes it installable. Constraints discovered up front: Ubuntu's `sdbus-c++` apt package is a 1.x major incompatible with our v2 API usage (the CI container already builds 2.3.1 from source); the dev host is Gentoo/OpenRC (no systemd) while the test VM and expected beta audience are Ubuntu/systemd; `devmgrd` currently runs manually in the foreground (D-Bus activation + unit were deferred from Phase 4 to "Phase 8", now pulled forward here); the repo is private until the owner flips it.

## Goals / Non-Goals

**Goals:**
- Installable beta: deb (Ubuntu 22.04/24.04) + generic tarball, few-step install, checksums, draft prerelease automated end-to-end from a tag.
- Daemon lifecycle done right: D-Bus system activation + systemd unit (OpenRC script in tarball).
- Tester-ready docs: install, test scenarios leaning on snapshot/restore, issue reporting.

**Non-Goals:**
- rpm, PPA/apt repo, Flatpak/AppImage (GUI+system-daemon+polkit fit poorly), arm64, distro submission.
- Auto-update mechanism.
- Making the repo public (owner action) or choosing the LICENSE (owner decision; flagged prerequisite).

## Decisions

1. **CPack over debian/-tooling or nfpm.** CMake-native, DEB and TGZ generators from one build tree, no new toolchain in CI. `dpkg-buildpackage` only pays off for archive/PPA submission (non-goal); nfpm adds a Go tool. Debian-policy lintian cleanliness is not a beta gate.
2. **One deb, not split packages.** daemon/tui/gui/cli as separate debs is archive practice; for a beta the single-artifact install beats granularity. Revisit at 1.0.
3. **Static sdbus-c++ in packaged builds.** Bundling a private .so needs rpath care and still collides with distro upgrades; static link of a small library is simpler and the container already builds 2.3.1 from source. Dynamic linking stays the default for dev builds; packaging config flips it.
4. **D-Bus activation + `SystemdService=` unit.** Bus activation gives zero-manual-start UX and on-demand lifetime; the systemd unit gives proper sandboxing/restart semantics. OpenRC (tarball path) starts the daemon directly; activation file still works for message routing. This closes the Phase 4 deferral.
5. **Draft prerelease, manual publish.** The pipeline stops at a draft: the first release is coupled to the owner's repo-public flip and LICENSE addition, which must stay human actions. Checklist lives in beta-docs.
6. **Fresh-VM install smoke as the exit gate** (pulled from Phase 8's acceptance idea): the existing Vagrant rig gains an install-smoke script driving deb install → activation → UI enumeration → snapshot/restore → uninstall residue check. This tests the artifact, not the build tree — the thing testers actually receive.

## Risks / Trade-offs

- [Static sdbus-c++ misses distro security updates] → acceptable for beta cadence; note in release notes; revisit for 1.0.
- [Qt6 Depends drift between 22.04 and 24.04 package names] → derive Depends per series via `dpkg-shlibdeps`-style resolution in the packaging container; smoke-test on 22.04 (rig), docs state 24.04 as expected-compatible.
- [GUI needs a polkit agent present; barebones sessions lack one] → documented requirement (graphical session with polkit agent — same constraint since Phase 4); TUI/CLI paths work with `pkttyagent` noted as advanced fallback.
- [Tag-triggered workflow on a private repo consumes private-repo Actions minutes] → negligible at beta cadence.
- [install.sh partial-failure states] → idempotency + reconcile-or-abort rule in spec; uninstall.sh driven by the same file list.

## Migration Plan

First packaged install on machines that previously ran source builds: install.sh/deb overwrite policy/unit locations cleanly; docs tell source users to remove manual policy copies (Phase 5 stale-policy lesson). Rollback = `apt remove` (state preserved) or `uninstall.sh`.

## Open Questions

- LICENSE choice — owner decision before public flip (does not block implementation).
- Exact Ubuntu 24.04 Depends set — resolved mechanically during 8.x packaging task.
