# Tasks: beta-06-hardening-ux

## 1. Core & daemon hardening

- [x] 1.1 EventBus unsubscribe barrier: generation-counted in-flight tracking, condition_variable drain in `unsubscribe()`, thread-id re-entrancy detection with deferred removal; unit tests including re-entrant unsubscribe-from-callback and cross-thread teardown race
- [x] 1.2 Central IPC validation layer at RequestProcessor entry (string length caps, JSON size caps, id hex charset, label rules — consolidate Phase 7 checks); InvalidArgs on violation, no state change; tests with oversized/malformed/hostile inputs on every verb
- [x] 1.3 Privilege audit doc (reads/writes/executes enumeration) + hardened `devmgrd.service` per design decision 5 with per-directive justification comments; verify manually in the VM that privileged flows still work

## 2. IPC v4 — diff verb & history core

- [x] 2.1 core: payload diff engine + per-entry diff JSON shape in `snapshot_json` (device/module/modprobe kinds, explicit no-differences marker) + parent-chain builder helper (pruned-parent chain starts, HEAD, last-good); unit tests
- [x] 2.2 daemon: `SnapshotDiff(baseId, targetId)` verb (empty target = live state; corrupt → Io, unknown → NotFound), `kApiVersion` 3→4, manager_adaptor + dbus_contract; polkit-free read parity with List; tests/ipc coverage (diff two snapshots, diff vs live, corrupt refusal, unauthorized-free access)
- [x] 2.3 `pal::IPrivilegedChannel` + DbusPrivilegedChannel + FakePrivilegedChannel grow the diff verb (v4 negotiation via `ensureApi(4)`); ipc round-trip tests

## 3. Snapshot UX & accessibility

- [x] 3.1 SnapshotsVM: history presentation (chain view rows, HEAD/last-good markers), `previewLines(id)` (diff vs live + selected/current/last-good + partial-convergence note), restore-failure guidance composition (failed items, safety id, CLI recovery command), filter support; VM tests with byte-freeze
- [x] 3.2 TUI: `/` filter extended to Snapshots tab, `d` diff view, `h` history toggle, restore preview modal replacing plain confirm, failure guidance surface; DESIGN.md conventions
- [x] 3.3 GUI: Snapshots filter field, Diff/History actions + views, restore preview dialog, failure guidance via StatusLine; SnapshotListModel/main_window tests updated for parity
- [x] 3.4 CLI: `devmgr snapshot history`, `devmgr snapshot diff <a> [<b>]`, `devmgr snapshot restore --preview <id>` (default restore unchanged/non-interactive); `--json` + prefix rules + exit codes; tests
- [x] 3.5 Accessibility pass: GUI accessible names, tab order, shortcuts, minimum window size, row elision with full-value detail; TUI minimum-size guard; loading/empty/error state wording from VMs shared across UIs; tests where the harness allows
- [x] 3.6 docs/DESIGN.md: add restore-preview modal conventions, the loading/empty/error state matrix, accessibility rules, and `displayBus()` unification + modalias cosmetic fix in both UIs

## 4. Packaging — RPM & deb upgrade behavior

- [x] 4.1 CPack RPM generator config (metadata, Fedora Requires, %post/%preun scriptlets, state-dir preserve on erase, LICENSE) + Fedora container job building and `rpm -V`-verifying the package; README install/cleanup docs
- [x] 4.2 deb upgrade behavior: idempotent maintainer scripts (interrupted-install recovery), upgrade-preserves-state verification, tarball→deb replacement path documented and checked

## 5. Release supply chain

- [x] 5.1 SBOM generation in the release workflow (syft SPDX + hand-maintained overlay for sdbus-c++/FTXUI/in-tree sha256) + dependency/license audit doc in the repo
- [x] 5.2 Artifact signing: minisign detached signatures for all assets (private key in GH secrets, public key committed), documented one-line verification in README/BETA-TESTING; provenance via GitHub artifact attestation + documented `gh attestation verify` check
- [x] 5.3 Reproducibility job: double build with `SOURCE_DATE_EPOCH`, checksum diff report, known-deviations doc; unexplained diffs block release
- [x] 5.4 Release workflow asset set extended (deb, rpm, tarball, SHA256SUMS covering SBOM, signatures) + release-notes template updated

## 6. Acceptance suite & exit gate

- [x] 6.1 VM acceptance script against installed artifacts: enumeration both UIs, hotplug, disable+restore, blacklist round-trip, firmware check, CLI recovery, journal sandbox-denial scan; ends `ACCEPTANCE OK`; rig wiring beside existing smokes
- [x] 6.2 Upgrade matrix scripts: 0.5→0.6 upgrade preserving config+snapshots (`--previous <path>` pinned artifact, loud failure if absent), downgrade outcome, interrupted install recovery, tarball→package replacement, purge residue — DEB path and RPM path
- [x] 6.3 Version bump to 0.6.0 + full standard gates (build/ctest both configs, format, gated tidy, purity greps) + acceptance suite green
- [ ] 6.4 Tag `v0.6.0-beta.1` through the extended pipeline — **owner action**; draft release verified (full asset set incl. signatures + SBOM, attestation verify passes), owner publishes

## 7. Stretch (optional — droppable by pre-agreement)

- [ ] 7.1 libFuzzer harness for the validation layer + `snapshot_json` parsing, optional CI job
- [ ] 7.2 Resource-exhaustion and restart/recovery tests (daemon kill/restart mid-operation, disk-full on state dir)
- [ ] 7.3 Written compatibility/versioning policy: D-Bus ApiVersion additive-only rules + snapshot formatVersion migration policy
