# Design: tui-instrument-panel-polish

## Context

`tui/src/tui_app.cpp` is one 965-line `runTuiApp` closure. It renders with bold/inverted/separator only — no color, no theme abstraction, no per-view functions, and no render tests. `docs/DESIGN.md` is binding: §4.1 defines semantic color roles and TUI color mapping, §8 defines FTXUI rules (pure render, no ANSI strings, non-color signals), §11 lists anti-patterns, §12.1 defines the TUI render-test matrix. GUI already expresses the same roles via QPalette; ViewModels own all wording and state (§6.1), so this change is presentation-only.

All five architectural decisions below were brainstormed and user-approved on 2026-07-22 (locked; do not relitigate during implementation).

Pass 2 (manual pass 2026-07-22) revises this change in place. Classification rule: bug-fixes (B1–B6) and completions (K1–K4) satisfy the *existing* contract and add tasks/tests only; only new scope (R1–R6) adds requirements to the specs delta.

## Goals / Non-Goals

**Goals:**

- Instrument-panel visual quality within DESIGN.md — semantic color + hierarchy, not decoration.
- Full per-view decomposition of the TUI into pure, testable render functions.
- Color-capability degradation: full → mono → plain, meaning never color-dependent.
- First TUI render-test suite covering §12.1's matrix.

**Non-Goals:**

- Not a btop clone: no gauges, graphs, sparklines, per-metric decorative color, colored header blocks, or borders-on-every-value (§11 anti-patterns).
- No ViewModel *wording* changes and no ApiVersion change. (Read-only per-row **state** accessors are added to the VMs — see Decision 1a — but no string/behavior changes.)
- No `gui/` *colouring/presentation* change this cycle: the GUI keeps its near-colourless presentation under the DESIGN §9 temporary parity exception. Narrow exception (parity-mandated wording, not a presentation change): canonical device names (R1) and criticality marker *text* (R4/R6) are added to the GUI device list/detail this cycle, sourced from the same shared VM/core fields as the TUI, with **no GUI colour change** — required by the §9 "visible nouns match" invariant once the TUI shows them.
- No true-color or 256-color output; no new dependencies.
- No DESIGN.md changes — polish happens inside the existing contract.

## Decisions

1. **Direction: polish within DESIGN.md.** Premium feel comes from semantic color and hierarchy, not new visual vocabulary. Alternative (btop-style dashboard) rejected: violates §11 and the product register (§1). Color is native TUI presentation, mirroring what QPalette already does for the GUI.

1a. **Per-row state comes from the VM, not from parsing rows (revises the original "no app change" decision).** Implementation revealed the premise "color is a pure TUI presentation concern needing no app change" was false: `rowsRef()` returns formatted strings only (device rows carry no status word at all), and `StatusLineVM::text()` exposes no severity — so the TUI cannot color a row/outcome without either an app-layer accessor or parsing strings. Parsing strings to re-derive state is the §11 anti-pattern (frontend business logic) and would silently break when wording changes. Therefore the VMs gain **read-only, per-row state accessors** returning the state each already computes internally — `DeviceListVM::statusForRow(int) -> optional<core::DeviceStatus>`, `ModulesVM::signedForRow(int)`, `UpdatesVM::stateForRow(int)`, `SnapshotsVM::healthForRow(int)`/marker predicates, `StatusLineVM::severity()`. Header/placeholder/out-of-range rows return nullopt. The **role→color mapping lives in the TUI** (per-surface presentation, §4.1), so the VM stays presentation-free. Chosen over (a) presentation-only categorical color (drops the headline per-row coloring) and (b) also wiring the GUI (larger, deferred). GUI stays as-is under a DESIGN §9 temporary parity exception — acceptable because §10 forbids color as the sole signal, so facts/words remain at parity; only the additive color differs between surfaces for now.

2. **Full per-view decomposition (not minimal patching).** `tui_app.cpp` shrinks to a thin shell. New files: `tui/src/theme.{hpp,cpp}`, `tui/src/render_util.hpp` (status glyph, kv row, region frame), `tui/src/views/{tab_bar,status_bar,devices_view,modules_view,updates_view,snapshots_view}.cpp`. Each view exposes pure `render(const XxxVM&, const Theme&, Size) -> ftxui::Element` — no sysfs/D-Bus/filesystem work in render (§8). Rationale: coloring an untested 965-line closure is unverifiable; decomposition is what makes §12.1 render tests possible at all. Alternative (color in place, decompose later) rejected: doubles the touch count and ships untested color.

3. **Glyphs: ASCII default, Unicode opt-in.** `+` enabled, `-` disabled, `?` unavailable, `!` unsigned — width-safe in every terminal (§8 forbids ambiguous-width dependence). Unicode dots (● ○ ◉) only behind the capability flag; mono/plain always ASCII. Alternative (Unicode default with fallback) rejected: default must be the safe path.

4. **Planning = OpenSpec change** (this change), matching the last two phases. New capability `tui-presentation`; `ui-accessibility` main spec extended with a color-independence requirement.

5. **Theme = DESIGN §4.1 roles → FTXUI 16-color ANSI only.** accent→Cyan (+inverted for selection/focus), success→Green, warning→Yellow, danger→Red, info→Blue, muted→dim. Capability modes full→mono→plain resolved once at startup from `NO_COLOR` env, `--no-color`/`--ascii` flags, `TERM=dumb`. Color decorators become identity functions in mono/plain — every colored signal pairs with a glyph AND existing text, so meaning never depends on color (§8/§11). Never emit ANSI strings directly; FTXUI decorators only.

6. **(DN1) Active tab = accent (cyan) + bold, never yellow.** Yellow is the warning/risk role (§4.1); using it for "current mode" mis-signals risk and breaks the locked role set + §11. An override would require a DESIGN §4.1 role change and is out of scope.

7. **(DN2) Criticality folds into the warning role.** **Amended 2026-07-24 (owner decision) from "colour applies to the NAME/marker" to marker-element-only**, so the decision and spec R4 stop disagreeing about what the colour lands on. The criticality marker is its OWN element adjacent to the name, carrying its own glyph and the warning role + bold; it recolours nothing else — not the name, not the +/- state glyph, not the signed glyph — and criticality never uses danger. Rationale found at apply time (task 10.3): a module row is ONE preformatted string from `ModulesVM` (`Name Signed Ref Size Used-by`) shared verbatim with the GUI's `ModuleListModel`, so warning-colouring just the name segment would require splitting that row contract in the VM — a ViewModel state-ownership change, explicitly out of scope. Splitting the *rendered* row into prefix / badge / label instead (`render::menuRow`) achieves the same containment with no VM change. A blacklisted-and-essential component legitimately shows warning twice (different columns, both genuine risks) — not a collision; MONO/PLAIN disambiguates by the row glyph + the detail pane's criticality word.

8. **(DN3) Canonical name + criticality TEXT are VM/core data** (Decision 1a extensions, same rationale as `core::displayBus`); the TUI never parses pci.ids/strings; the wording reaches the GUI for the §9 "visible nouns match" invariant; only COLOUR stays TUI-only under the §9 temporary exception. New accessors: `DeviceListVM` canonical name + `criticalityForRow`, `ModulesVM::criticalityForRow`, optional segmented `StatusLineVM` accessor. `core::displayDeviceName` is a pure formatter over fields the daemon already provides (no bundled hardware database, no new dependency, no ApiVersion change); if the daemon DTO lacks a resolved-name field this becomes a separate daemon/ApiVersion decision and is escalated, not worked around. Curated criticality policy (OWNER sign-off pending final string confirmation at apply time; classifier matches on module name + device class the VM already exposes; misclassification is safety-relevant; marker-only + never-danger bounds the blast radius):
   - Essential (unbind/unload may make the system unusable): storage/filesystem — `ext4`, `xfs`, `btrfs`, `f2fs`, `nvme`, `ahci`, `scsi_mod`, `sd_mod`, `libata`, `dm-mod`, `dm-crypt`, `md-mod`; input — `hid`, `usbhid`, `xhci_hcd`, `ehci_hcd`, `i8042`, `atkbd`, `evdev`; display/DRM — `amdgpu`, `i915`, `nouveau`, `drm`, `drm_kms_helper`; USB host / root bus — `xhci_hcd`, `ehci_hcd`, `ohci_hcd`, `usbcore`, `pcieport`, `pci`.
   - Important: modules with refcount > 0 not already Essential; security-relevant — `apparmor`, `selinux`, `ima`/`integrity`, lockdown/module-signing enforcement; the net driver bound to the only interface.
   - Ordinary: everything else (no marker).

9. **(DN4) Selected-row overflow uses a *bounded reveal*, not a perpetual scroll** (§4.5 forbids idle decoration; a perpetual scroll also needs an always-on redraw loop). On selection/focus of a row whose name overflows, the elided tail is revealed by a finite horizontal reveal that comes to rest; non-selected rows keep `ElideRight`. The shared reveal helper in `render_util` is a *pure function of an explicit integer offset* (deterministic; off-screen testable at offset=0 and offset=max). The single live time source — a gated redraw (`RequestAnimationFrame`, or a `PostEvent(Event::Custom)` timer) enabled *only while* an overflowing row is selected/focused and disabled at rest — lives in the view, not the helper. Pass-1's Devices marquee driver is audited and, if perpetual, converted to this model.

   **Audit closed at apply time (task 10.2).** Verdict: pass-1's Devices driver WAS perpetual — `marqueeWindow` computed `tick % cycle`, i.e. pause / scroll / pause / restart forever. It was removed. All four lists now share ONE helper (`revealLabel`, `tui/src/tui_app.cpp`) over three pure functions in `render_util`: `revealMaxOffset`, `revealWindow(s, width, offset)` (clamped, glyph-based), and `revealOffset(tick, maxOffset) = clamp(tick − 4, 0, maxOffset)` — a ~0.6 s lead-in, one glyph per tick, then **at rest at max forever**. Monotonic and terminating, which is what lets the 150 ms ticker stop instead of animating forever. **The open question "amend the spec to bless a loop (a), or converge on the at-rest reveal (b)" therefore resolves as (b), already shipped** — no spec amendment blessing a perpetual reveal is needed or wanted. A 2026-07-24 report of a looping Devices reveal was traced to a stale build tree, not to the code (see task 12.3).

10. **(DN5) The master-detail split is computed from the terminal width, not fixed** (owner decision 2026-07-24, taken at apply time during task 12.3b). Both-panes-always is only a defensible layout if both panes can actually say something. Pinning the three wide views' collection pane at 72 columns left the detail pane six columns at the 80-column minimum, which rendered every line as three characters and an ellipsis — so the R4 colour-independence pairing (marker on the row, criticality WORD in the detail pane, on ONE screen) quietly stopped holding at exactly the size DESIGN §3.2 names as the floor, while render tests that all ran at 120 columns stayed green. `views::wideLeftPaneWidth(cols)` makes the split a pure function of width — preferred 72 at ≥108, yielding down to 44 at 80, the split Devices already used — and the shell passes the result in as view data so the views stay pure (§8). **Trade-off accepted with the decision:** at 80 columns a Modules row clips after `Name` and `Signed` rather than after `Size`; the dropped columns are carried by the detail pane for the selected row, which is a strictly better place for them than a clipped row.

**Layout:** keep master-detail (§2.3). Borders only on major regions (list/detail/status); separators + muted group headers for sub-regions (§4.3); one legend line, one status line (§8). 80x24 keeps the existing list/detail switching (§3.2); borders degrade to ASCII in plain mode.

**Per-view coloring:**

| View | Mapping |
| --- | --- |
| Devices | enabled→success, disabled→danger, unavailable→warning, unknown→muted; guard refusal→danger on status line |
| Modules | signed→success, NO→danger, `?`→muted, blacklisted→warning; Secure Boot/lockdown banner info, →warning only when it explains a likely refusal (§5.5) |
| Updates | available→info, up-to-date→muted-success, error→danger |
| Snapshots | healthy→default, corrupt→danger, unsupported→warning; HEAD and last-good markers→accent (SnapshotHealth = Ok/Corrupt/Unsupported) |
| Status line | success/warning/danger/info by task outcome |
| Tab bar | active → accent + bold (never yellow); inactive → default; MONO/PLAIN bold + ASCII marker |
| Criticality (Modules required; Devices optional/phase-2) | essential/important → warning marker on the NAME only; never recolours the +/- or signed glyph; never danger |

**Tests (§12.1 — none exist today):** new `tui/tests/` — `test_theme.cpp` (role→color mapping + capability downgrade) and `test_<view>_render.cpp` per view: render to fixed `ftxui::Screen` at 120x32 / 100x28 / 80x24; assert no row exceeds width and no out-of-bounds writes; glyph+text present in MONO (color-independence proof); selection/focus markers; states matrix (empty/loading/prompt/confirmation/refusal/failure). Wired into ctest; format + gated clang-tidy cover `tui/src` + `tui/tests` (host clang-tidy is fine for `tui/`, unlike `gui/`).

Pass-2 test additions (§12.1): the `selectedIndex == cursorIndex == detailIndex` invariant per populated view; identical selection colour across all four views; no cursor/invert on a placeholder; a single empty indicator on Snapshots; the status line occupies exactly one row with a >120-char string at 80 cols; MONO colour-independence for selection, focus, active-tab, criticality, signed, update-state, and snapshot-markers; the three non-colour markers (focus `>`, criticality `#`, active-tab bold+bracket) are pairwise distinct; the bounded reveal is width-bounded at offset=0 and at offset=max with no out-of-bounds write; `test_theme` asserts criticality→warning and active-tab→accent (not yellow); unit tests for `displayDeviceName` (incl. GUI==TUI parity on the same VM field), the criticality classifier, and the filter no-leak key routing; a GUI offscreen test for canonical-name + criticality-text parity.

## Risks / Trade-offs

- [Behavior drift during extraction of a 965-line closure] → extract one view at a time with the closure delegating to the new function; app stays working and green at every step (see Migration Plan).
- [Color applied before tests exist would be unverifiable] → sequencing puts theme+tests and per-view extraction before any semantic coloring lands.
- [Terminal theme variance makes ANSI colors unpredictable] → 16-color roles only (user's palette decides exact rendering), every signal paired with glyph+text, mono/plain modes always available.
- [clang-tidy/format gates newly covering tui/ may surface latent noise] → gates extended in the same step as the file moves; fix-forward within the change.
- [GUI/TUI color divergence from the TUI-only accessors] → documented DESIGN §9 temporary parity exception; §10 bars color-as-sole-signal so facts/words stay at parity; accessors are designed GUI-consumable so a later change adopts them without rework.
- [Per-row state accessors mis-map a row (header/placeholder) to a state] → accessors return `optional`/`nullopt` for non-data rows and are unit-tested against header/placeholder/empty/filtered row layouts.
- [Curated criticality policy may misclassify] → mitigated by the curated list + unit tests, marker-only rendering, and never-danger (a wrong marker mislabels risk but cannot hide a failure or look like a destructive outcome).
- [Bounded-reveal timing is not unit-testable] → the structural bound (element width ≤ region at every offset) is asserted in render tests; animation feel is covered by the manual matrix.
- [Canonical-name source may be absent] → stable raw-id fallback; no bundled database; if a resolved name requires new IPC it is escalated as a separate decision (no silent ApiVersion change).

## Migration Plan

Low-risk sequencing — each step leaves the build green and the app working:

1. `theme` + `render_util` + `test_theme` — no UI change.
2. Extract each view as a pure `render(VM, Theme, Size)` (behavior-preserving, no color yet); closure delegates. Render tests per view.
3. Add read-only per-row state accessors to the VMs (app/) + unit tests — no wording/behavior change.
4. Apply semantic coloring (views map accessor state → theme role) + border policy.
5. Full states-matrix render tests + mono/plain color-independence proof.
6. T1–T3 (core/ + VM data, no UI change): `displayDeviceName` + tests; criticality classifier + tests; VM fields/accessors + tests (no change to existing shared strings).
7. T4 (bug-fixes B1–B6, presentation-only) + regression tests.
8. T5 (completions K1–K2: per-view colour on Updates/Snapshots + Devices non-green confirmation; status severity colouring) + render tests.
9. T6–T7 (new-scope presentation): R2 active-tab; R3 bounded reveal extracted to `render_util` + reused in Modules/Updates/Snapshots; R4 criticality marker (Modules required, Devices optional); R5 muted column headers; R1 canonical `Name:` detail + bounded long values at 80 cols. + render tests.
10. T8–T10: R6 GUI parity (canonical name + criticality text + offscreen test); T9 verifications (K3 80-col toggle render test, K4 filter no-leak key-routing test); T10 gates over touched `core/`+`app/`+`gui/`+`tui/` (container ctest green) + manual matrix + `openspec validate --strict`.

Rollback: pass-1 scope is presentation-only and confined to `tui/`; pass-2 VM/core fields are additive and ignored by the old renderers. Reverting the change restores the prior closure with no data or IPC implications.

## Open Questions

None blocking — Decisions 1–5 remain locked and user-approved; Decisions 6–9 extend them (pass 2). Decision 7 was amended 2026-07-24 (marker-element-only colour) and Decision 9's reveal audit is closed (option (b), shipped). The Decision 8 curated criticality string list carried OWNER sign-off pending final confirmation at apply time and was signed off in task 7.2 (amended to facts-first + curated fallback).

**Parked follow-up — not this change: DESIGN.md §3.2 vs. the built 80–109 column layout.** §3.2 says the TUI should "prefer a compact or list/detail switching layout" between 80 and 109 columns. The build deliberately renders BOTH panes at 80 columns and above, with the DESIGN §3.2 minimum-size notice as the only width-driven switch (established in task 10.4, verified off-screen by `views::belowMinimumSize` in task 12.1/K3). The spec text that claimed the switching layout existed has been corrected in this pass (`tui-presentation` "Muted column headers" and "Region border discipline"), but **DESIGN.md itself is untouched** — this change's non-goals bar DESIGN.md edits, and reconciling the guide is a decision about the product's layout policy, not a presentation fix. A later change should either amend §3.2 to record both-panes-always as the chosen layout, or specify and build the compact layout §3.2 asks for. Decision 10 narrows what that later change has to decide: the split is now width-adaptive, so both-panes-always at least stays legible at 80 columns.
