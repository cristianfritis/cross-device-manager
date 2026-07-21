# Design: beta-06-hardening-ux

## Context

v0.5.0-beta.1 shipped deb + tarball with a tag-triggered draft-release pipeline. The daemon runs under a systemd unit with basic sandboxing, IPC is ApiVersion 3, and the snapshot UX is linear create/restore/delete. Known residuals: EventBus unsubscribe race (Phase 2), no rpm, no upgrade testing, no supply-chain guarantees, snapshot history/diff deferred from 0.5. Constraints: sdbus-c++ v2 static in packaged builds; dev host Gentoo/OpenRC (systemd behavior verified in VM/containers only); `docs/DESIGN.md` is binding for all GUI/TUI work; the user commits every task; dangerous ops verified in the VM.

## Goals / Non-Goals

**Goals:** rpm parity with deb; upgrade/downgrade/interrupted-install acceptance for both package formats; hardened daemon (sandbox + input validation + audit); signed, SBOM'd, attested releases; snapshot history + diff + restore preview + recovery guidance; accessibility/keyboard completion; EventBus race closed. Release target `v0.6.0-beta.1`.

**Non-Goals:** boot sentinel + auto-rollback (needs this change's acceptance rig first); UX beyond the listed items (wait for beta feedback); repo/package *repository* hosting (we sign artifacts, we do not stand up an apt/dnf repo); Windows/macOS; bit-perfect reproducibility (check-and-report only).

## Decisions

1. **Server-side `SnapshotDiff` (ApiVersion 4), one new verb only.**
   Diff computed in the daemon from stored payloads; `SnapshotDiff(baseId, targetId)`, empty target = live state. Restore preview = `SnapshotDiff(selectedId, "")`. *Alternative rejected:* payload-fetch verb + client-side diff — duplicates diff logic in app/ and cli/, ships payload internals across the bus, two implementations to keep in parity. JSON shape lives in `core/snapshot_json.hpp` (existing single-shape-source convention): per-entry records `{kind: device|module|modprobe, key, before, after}` plus an explicit empty marker.

   *Amended at apply time:* the payload carries no module list — only entries plus devmgr-owned modprobe.d file contents — so module-kind rows are derived by parsing `blacklist`/`options`/`install` directives out of those files, unioned across files. Consequence: a blacklist that moves between two devmgr files is one unchanged module plus two file rows, not a module change. Modprobe-kind rows therefore cover file appearance/disappearance and byte changes that leave every directive intact (comments, reordering), which keeps the two kinds from double-reporting the same edit. The empty marker is a `differences` boolean, and the reader refuses a document whose marker contradicts its entry count rather than silently trusting one side.

2. **History is client-side from existing metadata — no new IPC.**
   `SnapshotList` already returns `id` + `parent`. A chain-builder helper goes in `core/` (not app/) so SnapshotsVM (both UIs) and the CLI share it. Missing parents (pruned) become chain starts. HEAD comes from list order + store HEAD semantics already exposed; last-good = newest non-corrupt.

3. **Restore preview/guidance wording lives in the VM layer.**

   SnapshotsVM grows `previewLines(id)` and failure-guidance composition; GUI/TUI render verbatim (byte-parity pattern already proven). CLI formats the same core diff JSON with its own thin printer (CLI has no VM; parity of *facts* via shared JSON + core chain-builder, wording kept identical by reusing the same format strings from a core header). `docs/DESIGN.md` gains sections for preview modals and loading/empty/error state wording — extended, never contradicted.

   *Amended at apply time (task 3.1):* `previewLines(id)` as written implied a synchronous call, but the preview's diff is a D-Bus round trip and `docs/DESIGN.md` §7/§11 forbid blocking either UI thread on I/O. Split into `requestPreview(id)` (async, returns a `shared_future`, mirrors `ModulesVM::fillSignatures`) plus a parameterless `previewLines()` that renders the pending request — including its loading and unavailable states, so neither UI invents wording for them. The facade grew `refreshSnapshotDiff(base, target)` caching into a mutex-guarded copy behind a new `SnapshotDiffRefreshedEvent`, the same async-into-cache shape as `refreshSnapshots()`. A *failed* diff clears the cache rather than leaving the previous pair's result visible, since a preview showing a stale diff would misdescribe what a restore is about to do. Restore guidance needs per-item detail that the `TaskCompletedEvent` message cannot carry, so the facade also retains the last `RestoreOutcome` (cleared by an outright failure, which would otherwise name the wrong safety snapshot). The shared strings landed in `core/snapshot_presentation.hpp` — `restoreSummary` moved there out of the facade's anonymous namespace so task 3.4's CLI printer reuses it verbatim.

   *Also amended (task 3.1):* the chain view indents from the chain **tip**, not from `buildSnapshotChain`'s structural depth. Depth is measured from the chain start (oldest = 0), but rows render newest-first, so indenting by it directly pushes the newest rows deepest and — past the indent cap that keeps rows inside an 80-column terminal — flattens every recent row to the same level, erasing the distinction where the eye lands first. Core keeps the structural depth; only the presentation reverses it. Markers are likewise derived from the **full** list and then filtered, never from the visible subset: a filter that hides the real HEAD must not promote another row to HEAD.

4. **EventBus fix: generation-counted unsubscribe barrier.**
   `publish()` snapshots subscribers under the mutex and bumps a per-subscriber in-flight count; `unsubscribe()` blocks on a condition_variable until that subscriber's in-flight count drains. Re-entrant unsubscribe from inside a callback on the publishing thread is detected (thread-id check) and skips the wait (deferred removal) to avoid self-deadlock. *Alternative rejected:* shared_ptr-token-only approach — keeps the object alive but still lets a callback run after unsubscribe returns, which is the documented bug.

5. **Sandboxing: lock everything except the daemon's actual write surface.**
   Unit gains `ProtectHome=yes`, `ProtectSystem=strict` + `ReadWritePaths=/var/lib/devmgrd /etc/modprobe.d`, `PrivateTmp=yes`, `RestrictAddressFamilies=AF_UNIX AF_NETLINK`, `RestrictNamespaces=yes`, `LockPersonality=yes`, `MemoryDenyWriteExecute=yes`, `CapabilityBoundingSet=` reduced to the audited set. Deliberately **not** set: `ProtectKernelTunables` (sysfs writes — `authorized`, bind/unbind, `driver_override` — are the product) and `ProtectKernelModules` (module load/unload is a spec'd capability); both get justification comments. Every directive verified by the acceptance suite + a journal scan for denials.

6. **Input validation: one validation layer at RequestProcessor entry.**
   Central caps (string length, JSON size, id hex charset, label printable ≤128 — some already exist from Phase 7) applied before any verb logic, so new verbs inherit them. Fuzzing (stretch) targets this layer + `snapshot_json` parsing with libFuzzer, as an optional CI job, not a required gate.

   *Amended at apply time:* the spec referenced an "existing InvalidArgs mapping" that did not exist — Phase 7 returned `NotFound` for malformed input, conflating "you sent garbage" with "that id names nothing" (`cli.cpp` even commented the conflation). Resolved by adding `core::Error::Code::InvalidArgs` and the wire name `org.devmgr.Error.InvalidArgs` as part of the ApiVersion 4 bump. *Alternatives rejected:* keeping `NotFound` and amending the spec (zero compat risk but preserves a misleading code on a security-relevant path); internal-only code mapped to `kErrNotFound` on the wire (no compat risk, but an internal/external asymmetry that a future version would have to unwind anyway). Accepted cost: a pre-v4 client meeting the new name falls through `coreErrorFor()` to `Io`, so a v0.5 CLI reports malformed input as an I/O error until it is upgraded — additive per the versioning policy, and strictly better than the previous silent conflation. Validators live in `daemon/include/devmgr/daemon/request_validation.hpp`; the CLI maps `InvalidArgs` to the usage exit code, since a malformed argument is the caller's mistake.

7. **Supply chain: GitHub artifact attestations + minisign, syft for SBOM.**
   Provenance via `actions/attest-build-provenance` (verifiable with `gh attestation verify` — no extra infra). Detached signatures via **minisign** (single small tool, packaged in Ubuntu and Fedora, one-line verify; key pair owner-generated, private key in GH secrets, public key committed). *Alternatives rejected:* GPG (heavier tester UX, keyserver noise), cosign keyless for signatures (testers need cosign; attestation already covers the sigstore angle). SBOM via **syft** (SPDX JSON) plus a hand-maintained overlay for statically-linked/vendored deps (sdbus-c++, FTXUI, in-tree sha256) merged in the workflow. License audit = a doc table generated once and updated per dependency change.

8. **Reproducibility: same-container double build with `SOURCE_DATE_EPOCH`.**
   Pipeline job rebuilds the tag in the same container with `SOURCE_DATE_EPOCH` set from the tag commit, strips/normalizes archive timestamps where cpack allows, diffs checksums, and publishes the report. Differences beyond the documented known list block the release. Kept report-only to avoid a bit-perfect rabbit hole.

9. **RPM via CPack RPM generator + Fedora container job.**
   Same install rules; per-generator metadata split (Depends vs Requires — Fedora Qt6/udev/kmod package names differ). New `Dockerfile` variant (or stage) for Fedora build+`rpm -V` verification. Upgrade matrix runs dnf/rpm paths in that container or a Fedora VM box where scriptlet-level behavior needs PID 1.

10. **Upgrade acceptance sources the previous release from a pinned local artifact.**
    The repo is private until the flip, and CI can't always fetch old release assets; the rig takes a `--previous <path>` pointing at the kept v0.5.0-beta.1 artifacts (checked into the release archive location the owner maintains, not the repo). Downgrade expectation: state format v1 is forward-compatible, so downgrade must start and list snapshots; anything else is a documented refusal.

11. **Accessibility implementation.**
    GUI: pass over every control for `setAccessibleName`, explicit tab order per tab, `QKeySequence` shortcuts for tab switching + primary verbs, minimum window size, elide-with-full-value-in-detail. TUI: minimum-size guard with graceful message, hotkey coverage for new views. Loading/empty/error strings come from the VMs so both UIs share wording; DESIGN.md records the state matrix.

## Risks / Trade-offs

- [Scope is large — hardening + UX in one change] → tasks are ordered so each track lands independently (hardening tasks don't depend on UX tasks); if the change must ship early, either track can be deferred by unchecking its tasks and re-scoping, and the proposal's optional items are pre-agreed droppable.
- [Sandbox breaks a privileged flow only on real systemd] → dev host is OpenRC; all sandbox verification happens in the VM/acceptance suite, with a journal-denial scan step so breakage is visible, not silent.
- [Fedora package naming / Qt6 version drift] → container job pins a Fedora release; Requires list derived there, not guessed.
- [minisign key is a single owner secret] → documented rotation procedure + the attestation path remains verifiable independently of the signature path.
- [Server-side diff on corrupt snapshots] → diff refuses corrupt inputs with the existing Io/NotFound taxonomy; UI already marks corrupt rows and disables actions locally.
- [EventBus barrier introduces a wait on unsubscribe] → bounded by callback runtime; re-entrancy path avoids self-deadlock; existing `ReentrantStopFromCallback` test pattern extends to cover it.
- [Upgrade tests depend on a previous-release artifact] → pinned local copy with checksum, failing loudly if absent rather than skipping silently.

## Migration Plan

Implementation order: (1) core/daemon hardening — EventBus barrier, validation layer, sandbox unit; (2) IPC v4 + diff + history core; (3) VM/UI/CLI UX (preview, guidance, filter, accessibility) under DESIGN.md; (4) packaging-rpm + deb upgrade behavior; (5) supply-chain pipeline steps; (6) acceptance suite last — it verifies everything above; (7) `v0.6.0-beta.1` tag through the extended pipeline. Rollback: all IPC changes are additive (v3 clients unaffected); packaging changes are new artifacts; a failed release is just an unpublished draft.

## Open Questions

- Fedora target release for the RPM job (default: latest stable at implementation time) — owner confirms.
- minisign vs owner's existing key tooling (default: minisign; owner generates the key pair) — owner confirms before the pipeline task.
- Whether the optional items (fuzzing, resource-exhaustion tests, compat-policy doc) run in this change or the next — decided at apply time based on remaining appetite.
