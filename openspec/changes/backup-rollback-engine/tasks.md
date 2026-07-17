# Tasks: backup-rollback-engine

## 1. Core models and store

- [ ] 1.1 Snapshot core models in `core/` (SnapshotMeta, SnapshotPayload, trigger/reason types, events) + unit tests
- [ ] 1.2 `ISnapshotStore` interface + JSON file store v1 in `daemon/`: canonical payload serialization, SHA-256 id, parent/HEAD chain, tmp+fsync+rename writes, hash-verify + quarantine on read, formatVersion gate + unit tests (round-trip, torn-write, corrupt, future-version)
- [ ] 1.3 Pruning (keep-last-20 auto, manual exempt, dangling-parent tolerance) + hash-dedupe (payload hash == HEAD → return existing id) + unit tests

## 2. Lifecycle service and hook

- [ ] 2.1 `SnapshotService`: create (under apply mutex, payload gathered from StateStore + devmgr modprobe.d files), list, delete (HEAD→parent move) + unit tests on fake store dir
- [ ] 2.2 Restore: integrity check → own auto safety snapshot → atomic write-back (StateStore replace-all, modprobe.d rewrite) → convergence (enforcement sweep re-applies; pre/post entry-set diff triggers re-enables; guard refusals collected) → per-item outcome summary + unit tests on FakePal (re-enable case, re-apply case, guard-refusal case, undo-restore case)
- [ ] 2.3 `RequestProcessor` auto-snapshot hook before every mutating verb, fail-closed on snapshot error + unit tests (snapshot-before-write ordering, unwritable-dir blocks mutation)

## 3. IPC v3

- [ ] 3.1 `org.devmgr.Manager1` ApiVersion 2→3: SnapshotList/Create/Restore/Delete verbs, dbus_contract + adaptor + error taxonomy mapping (NotFound/Io/NotAuthorized) + polkit action `org.devmgr.manage-snapshots` (auth_admin_keep; List unprivileged) in `daemon/data/org.devmgr.policy`
- [ ] 3.2 `DbusPrivilegedChannel` client side for the four verbs + `tests/ipc` round-trip suite extension (authorized, unauthorized, unknown id, partial-convergence summary)

## 4. Facade and SnapshotsVM

- [ ] 4.1 `ApplicationFacade` async snapshot API (list/create/restore/delete snapshots, completion via existing task/event machinery)
- [ ] 4.2 `SnapshotsVM`: byte-frozen row format (short id, local datetime, trigger, subject), detail lines (full id, parent, payload counts), corrupt marking, coalesced refresh, teardown-safe + unit tests (row byte-freeze, cross-frontend refresh via event)

## 5. Frontends

- [ ] 5.1 TUI `[4]Snapshots` tab: digit/m-cycle integration, s=create (label prompt), r=restore (confirm modal), x=delete (confirm modal), outcome via status line; DESIGN.md conventions
- [ ] 5.2 GUI Snapshots tab: `SnapshotListModel` (list-model house pattern + model-tester), Create/Restore/Delete actions tab-gated, Qt confirm dialogs, status-bar outcomes + gui tests (row parity vs VM, refusal path)
- [ ] 5.3 CLI binary `devmgr`: `snapshot list|create|restore|delete`, unique-prefix ids, exit codes 0–5, `--json`, sdbus-gated target + unit/integration tests (prefix ambiguity, daemon-down exit 4)

## 6. E2E, docs, close-out

- [ ] 6.1 VM smoke `test/vm/phase7-smoke.sh` + `test-vm.sh` wiring: disable device → auto snapshot exists → restore → device re-enabled; module blacklist round-trip; CLI restore path
- [ ] 6.2 README Snapshots section + restore-limits documentation (module config-level, no firmware rollback, polkit policy re-install note)
- [ ] 6.3 Optional (drop if timeline pressed): UIs retain last named progress stage on percent-only frames (Phase 6 cosmetic carry-over)
- [ ] 6.4 Full gates: linux-debug build + ctest, nosdbus build + ctest (CLI absent), clang-format, CI-form clang-tidy on gated dirs, purity greps; container suite + VM run by user
