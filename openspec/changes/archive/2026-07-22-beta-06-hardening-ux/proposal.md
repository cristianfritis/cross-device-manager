# Proposal: beta-06-hardening-ux

## Why

v0.5.0-beta.1 is out: the product is installable and testable, but only on deb/tarball, with no upgrade story, no supply-chain guarantees, and a snapshot UX that stops at linear create/restore/delete. Phase 8 hardens what beta testers now depend on (packages, daemon, release artifacts) and enriches the snapshot/restore experience with the safety-relevant UX that was deferred from 0.5 — before the audience grows with the public repo flip.

## What Changes

**Hardening**

- RPM packaging (CPack RPM) for the Fedora family, mirroring the deb: metadata, scriptlets, state-dir remove-vs-purge rule, container build+install verification.
- Upgrade & rollback acceptance: upgrading from the previous release preserves configuration and snapshots; downgrade/failure behavior, interrupted installs, package replacement, and clean purge are tested on both DEB and RPM paths.
- Full E2E VM acceptance suite: extends install-smoke into scenario coverage (enumerate/hotplug, disable/restore, driver ops, firmware check, snapshot lifecycle) against the *installed artifact*, plus the upgrade matrix above.
- Daemon security hardening: tighten systemd sandboxing directives, privilege-minimization review, input-validation audit across all IPC verbs.
- Release supply-chain hardening: artifact signing and documented signature verification, SBOM generation, dependency/license audit, provenance/attestation for release assets, reproducibility check where practical.
- Fix the Phase 2 documented residual: `EventBus::publish()` snapshot race (unsubscribe lacks a completion barrier).
- *Optional (stretch, explicitly droppable):* IPC fuzzing / malformed & oversized input tests; resource-exhaustion and restart/recovery testing; written compatibility/versioning policy for the D-Bus API and snapshot format.

**UX enrichment**

- Snapshot history (pulled forward from the 0.6 backlog): parent-chain history view and a "what changed" diff between snapshots. Format v1 already carries `id` + `parent`; no store migration.
- Restore preview + recovery guidance: before restore, show what will change, identify the selected/current/last-good snapshots, explain possible partial convergence, and give actionable recovery guidance when a restore fails — same facts and wording in GUI and TUI.
- Accessibility & keyboard completion: keyboard-only operation for every flow, focus visibility, accessible control names, minimum-size layouts, long-value handling, and defined loading/empty/error states across GUI and TUI.
- UI polish batch: filter on the Snapshots tab, shared `displayBus()` casing unify, modalias cosmetic fix, richer error surfacing.

**Explicit non-goals:** boot-success sentinel + auto-rollback stays deferred (needs the acceptance rig this change builds first); UX beyond the items above waits for beta-tester feedback; Windows/macOS untouched. Release target: `v0.6.0-beta.1`.

## Capabilities

### New Capabilities

- `packaging-rpm`: RPM artifact contents, scriptlets, install/uninstall/upgrade behavior on Fedora-family systems.
- `acceptance-suite`: E2E VM acceptance run against installed artifacts — scenario coverage and the upgrade/downgrade/interrupted-install/purge matrix for DEB and RPM.
- `daemon-hardening`: sandboxing baseline for `devmgrd`, privilege minimization, IPC input-validation guarantees (with optional fuzzing/robustness extensions).
- `release-supply-chain`: signing + verification, SBOM, license audit, provenance for release artifacts.
- `snapshot-history`: parent-chain history model and snapshot-to-snapshot diff semantics surfaced in the UIs and CLI.
- `ui-accessibility`: keyboard completion, focus/naming/sizing rules, loading/empty/error states, and cross-UI presentation consistency.

### Modified Capabilities

- `snapshot-ui`: restore flow gains preview (what will change, selected/current/last-good identification, partial-convergence explanation) and failure recovery guidance; Snapshots tab gains a filter; history/diff entry points.
- `snapshot-ipc`: ApiVersion 3 → 4 with additive read verbs needed for history/diff/preview (payload retrieval or server-side diff).
- `snapshot-cli`: gains history/diff listing and restore preview so the recovery path has feature parity.
- `packaging-deb`: upgrade requirements added — upgrade preserves configuration and snapshots; interrupted-install and package-replacement behavior defined.
- `release-pipeline`: the draft-release artifact set grows to include signatures and SBOM; checksum requirement extended to cover them.

## Impact

- **Code**: `core` (EventBus barrier), `daemon` (validation, hardening), `app` (SnapshotsVM history/preview, UpdatesVM filter), `gui`/`tui` (history/diff/preview views, accessibility), `cli` (new verbs), `platform/linux` (none expected).
- **Build/packaging**: `CMakeLists.txt`/`packaging/` (RPM config, signing hooks), `.github/workflows` (SBOM, signing, provenance steps), new container image variant for RPM verification.
- **Test**: `test/vm` (acceptance + upgrade scripts), `tests/ipc` (validation/fuzz cases).
- **Docs**: `docs/DESIGN.md` is binding for all `gui/`/`tui/` work (semantic parity); README + BETA-TESTING.md gain verification instructions; new compat-policy doc if the optional item lands.
- **Constraints carried forward**: sdbus-c++ v2 static link in packaged builds; polkit policy re-install after any policy change; user commits every task; dangerous ops verified in VM only.
