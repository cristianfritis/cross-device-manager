# Proposal: backup-rollback-engine

## Why

devmgr can now mutate system state (device disable, module blacklists, driver overrides, firmware installs) but offers no undo. Before the v0.5 public beta puts these controls in testers' hands, every mutating operation needs an automatic safety net and a one-step restore — a tester who breaks their input devices or network must be able to get back to a known-good state.

## What Changes

- New `SnapshotService` inside `devmgrd`: content-hash-identified, parent-linked JSON snapshots of all devmgr-owned state (`state.json` entries — which carry disable mechanisms incl. driver unbind/override — plus devmgr-written modprobe.d content) stored under `/var/lib/devmgrd/snapshots/`.
- Automatic snapshot before every mutating IPC verb (single choke point in `RequestProcessor`), with hash-dedupe (no-op when state unchanged) and keep-last-20 pruning of auto snapshots; manual snapshots kept until deleted.
- Restore: atomic write-back of snapshot payload plus hardware convergence (enforcement sweep re-applies disables with per-device guard re-check; entries removed by restore trigger re-enable attempts).
- IPC contract bumps to ApiVersion 3: `SnapshotList`, `SnapshotCreate`, `SnapshotRestore`, `SnapshotDelete`; new polkit action for mutating snapshot verbs (`auth_admin_keep`), list unprivileged.
- `ApplicationFacade` grows an async snapshot API + `SnapshotsVM` (byte-frozen row pattern).
- New Snapshots surface in BOTH UIs (TUI 4th tab, GUI tab) with create/restore/delete + confirmation, per docs/DESIGN.md parity rules.
- New minimal CLI binary `devmgr` (`snapshot list|create|restore|delete`) — thin D-Bus client, recovery path when UIs are unusable.
- Forward-compatibility contract: snapshot format v1 carries `id` (SHA-256 of payload) and `parent` fields so the post-beta 0.6 work (content-addressed store, `HistoryGraph`, boot-success sentinel + auto-rollback) extends this format without migration.

## Capabilities

### New Capabilities

- `snapshot-store`: on-disk snapshot format, integrity (content hash, quarantine-on-corruption), atomic write discipline, HEAD/parent chain, pruning.
- `snapshot-lifecycle`: create (manual + automatic pre-mutation hook), list, restore (state write-back + hardware convergence + guard re-check), delete; concurrency rules under the apply mutex.
- `snapshot-ipc`: ApiVersion 3 D-Bus verbs, polkit authorization mapping, error taxonomy.
- `snapshot-ui`: SnapshotsVM row/detail contract and TUI/GUI parity surface (create/restore/delete with confirmation).
- `snapshot-cli`: `devmgr` binary verbs, exit codes, output format.

### Modified Capabilities

<!-- none: openspec/specs/ is empty; existing behavior (enforcement, IPC v2) is captured in docs/superpowers specs, not openspec capabilities. Requirement-level interaction (RequestProcessor hook, ApiVersion bump) is specified inside the new capabilities above. -->

## Impact

- **Code**: `daemon/` (SnapshotService, RequestProcessor hook, main wiring), `core/` (snapshot models, events), `app/` (facade + SnapshotsVM), `tui/`, `gui/`, new `cli/` target, `daemon/data/` (polkit policy, D-Bus policy).
- **IPC**: `org.devmgr.Manager1` ApiVersion 2 → 3 (additive verbs; existing verbs unchanged).
- **Security**: new polkit action; restore path re-runs CriticalDeviceGuard per device — restore can partially converge if guard refuses (reported, never bypassed).
- **Docs/limits**: module load state is not forced on restore (config-level restore; effective on next boot/hotplug); firmware is never rolled back (fwupd owns it).
- **Tests**: unit (store integrity, prune, restore convergence on FakePal, hook), IPC round-trip suite, VM `phase7-smoke.sh` E2E (disable → snapshot → restore → re-enabled).
- **Dependencies**: none new (reuses nlohmann/json, sdbus-c++ v2, existing polkit machinery). Prereq for the follow-up `beta-05-packaging` change.
