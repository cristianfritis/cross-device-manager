# Design: backup-rollback-engine

## Context

Phases 0–6 delivered enumeration, hotplug, enable/disable via privileged `devmgrd` (polkit-authorized, sdbus-c++ v2, IPC ApiVersion 2), module management, and firmware/DKMS updates, all merged to `dev` (290/290 tests). Mutable state devmgr owns today: `/var/lib/devmgrd/state.json` (`StateStore`, disabled-device entries, tmp+fsync+rename atomic writes, quarantine-on-corruption), devmgr-written modprobe.d content, and driver-override records. `EnforcementService` already converges hardware to desired state (startup sweep + hotplug re-apply, per-device `CriticalDeviceGuard` re-check). `RequestProcessor` is the single dispatch point for every mutating IPC verb.

This change is the first of two for the v0.5 public beta: rollback ships before packaging so testers always have an undo. The roadmap's fuller engine (content-addressed store, `HistoryGraph`, boot-success sentinel + auto-rollback) is deliberately deferred to post-beta 0.6 — this design's formats and interfaces must extend into it without migration.

## Goals / Non-Goals

**Goals:**
- Automatic pre-mutation snapshots with one-step restore, surfaced in TUI, GUI, and a new minimal CLI.
- Zero-throwaway path to the 0.6 engine: content-hash ids, parent links, `ISnapshotStore` seam.
- Reuse existing invariants: apply-mutex serialization, atomic write idiom, never-destroy-evidence quarantine, guard-checked convergence.

**Non-Goals:**
- HistoryGraph/branching, content-addressed dedup store, boot-success sentinel, auto-rollback offer (0.6).
- Firmware rollback (fwupd owns firmware; excluded by policy).
- Forced module unload on restore (config-level restore only).
- Snapshotting state devmgr does not own (arbitrary /etc, kernel params).

## Decisions

1. **Daemon-owned engine.** `SnapshotService` lives in `devmgrd`; store dir `/var/lib/devmgrd/snapshots/` (root-owned, next to `state.json`). Alternatives rejected: frontend-side snapshots (state is root-owned; restore needs privilege; two writers to state.json) and an external tool mutating the store dir directly (races the running daemon).
2. **Hook at `RequestProcessor`.** One choke point covers every mutating verb, present and future, instead of per-service hooks that can drift. Snapshot failure fails the mutation (fail-closed): a beta tester's mutation without a safety net is worse than a refused mutation.
3. **Whole-state JSON snapshots, not diffs.** State is ~KB; whole-payload files with SHA-256 ids give free integrity checking, free dedupe (hash == HEAD → skip), and trivially correct restore. Content-addressing arrives as identity now, as a store layout in 0.6. Alternative rejected: per-op inverse diffs (complex, fragile against drift, no integrity story).
4. **Restore = write-back + convergence via existing enforcement.** Restoring desired state then letting the existing sweep/guard machinery converge hardware reuses tested code and keeps the guard authoritative (restore can partially converge, reported per item, never bypassed). The only new convergence logic: diffing pre/post entry sets to trigger re-enables. Restore takes its own auto snapshot first, so restore is undoable.
5. **`ISnapshotStore` interface** between service and disk layout; v1 = one JSON file per snapshot + `HEAD`. The 0.6 content-addressed backend replaces the implementation behind the same interface; `id`/`parent` fields are already graph-shaped.
6. **IPC additive bump to ApiVersion 3** with one new polkit action `org.devmgr.manage-snapshots` (`auth_admin_keep`) for the three mutating verbs; `SnapshotList` unprivileged. Follows Phase 4/5 authorization pattern; carries the Phase 5 lesson: polkit policy must be re-installed when actions change (called out in tasks + docs).
7. **Three surfaces, one VM.** `SnapshotsVM` (byte-frozen rows, V3 house pattern) feeds both UIs; CLI bypasses the VM and prints daemon JSON directly (different medium, same verbs). CLI is sdbus-gated, UI-dependency-free — its reason to exist is recovery when UIs are broken.

## Risks / Trade-offs

- [Fail-closed hook makes a full/unwritable /var/lib block all mutations] → clear Io error text naming the snapshots dir; prune keeps the store bounded (~20 files × KB).
- [Restore convergence can be partial under guard refusal] → by design; per-item outcome summary in verb result, surfaced by all three frontends.
- [Module blacklist restore is config-level; loaded modules persist until reboot/hotplug] → documented limit in specs, docs, and restore outcome text.
- [Prune deletes the parent of the oldest survivor → dangling parent id] → `list` tolerates unresolvable parents; chain display degrades gracefully.
- [ApiVersion bump breaks stale clients pinned to ==2 checks] → all shipped clients (facade, CLI) check >= required; verbs are additive.
- [Percent-only progress frames show stage "unknown" (Phase 6 cosmetic carry-over)] → optional polish task; UIs retain last named stage. Dropped if it endangers the beta timeline.

## Migration Plan

Additive: first daemon start creates `snapshots/`; no existing-file migration. Downgrade = ignore the directory. Packaging change (`beta-05-packaging`) installs the updated polkit policy; source installs must re-install `daemon/data/org.devmgr.policy` manually (documented).

## Open Questions

- None blocking. Label max length / charset for manual snapshots decided at implementation (mirror existing input validation conventions).
