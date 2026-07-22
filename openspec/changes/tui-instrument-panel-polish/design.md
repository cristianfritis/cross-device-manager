# Design: tui-instrument-panel-polish

## Context

`tui/src/tui_app.cpp` is one 965-line `runTuiApp` closure. It renders with bold/inverted/separator only — no color, no theme abstraction, no per-view functions, and no render tests. `docs/DESIGN.md` is binding: §4.1 defines semantic color roles and TUI color mapping, §8 defines FTXUI rules (pure render, no ANSI strings, non-color signals), §11 lists anti-patterns, §12.1 defines the TUI render-test matrix. GUI already expresses the same roles via QPalette; ViewModels own all wording and state (§6.1), so this change is presentation-only.

All five architectural decisions below were brainstormed and user-approved on 2026-07-22 (locked; do not relitigate during implementation).

## Goals / Non-Goals

**Goals:**

- Instrument-panel visual quality within DESIGN.md — semantic color + hierarchy, not decoration.
- Full per-view decomposition of the TUI into pure, testable render functions.
- Color-capability degradation: full → mono → plain, meaning never color-dependent.
- First TUI render-test suite covering §12.1's matrix.

**Non-Goals:**

- Not a btop clone: no gauges, graphs, sparklines, per-metric decorative color, colored header blocks, or borders-on-every-value (§11 anti-patterns).
- No ViewModel *wording* changes and no ApiVersion change. (Read-only per-row **state** accessors are added to the VMs — see Decision 1a — but no string/behavior changes.)
- No `gui/` change this cycle: the GUI keeps its current (near-colorless) presentation under a documented DESIGN §9 temporary parity exception; a later change may adopt the same accessors for GUI coloring.
- No true-color or 256-color output; no new dependencies.
- No DESIGN.md changes — polish happens inside the existing contract.

## Decisions

1. **Direction: polish within DESIGN.md.** Premium feel comes from semantic color and hierarchy, not new visual vocabulary. Alternative (btop-style dashboard) rejected: violates §11 and the product register (§1). Color is native TUI presentation, mirroring what QPalette already does for the GUI.

1a. **Per-row state comes from the VM, not from parsing rows (revises the original "no app change" decision).** Implementation revealed the premise "color is a pure TUI presentation concern needing no app change" was false: `rowsRef()` returns formatted strings only (device rows carry no status word at all), and `StatusLineVM::text()` exposes no severity — so the TUI cannot color a row/outcome without either an app-layer accessor or parsing strings. Parsing strings to re-derive state is the §11 anti-pattern (frontend business logic) and would silently break when wording changes. Therefore the VMs gain **read-only, per-row state accessors** returning the state each already computes internally — `DeviceListVM::statusForRow(int) -> optional<core::DeviceStatus>`, `ModulesVM::signedForRow(int)`, `UpdatesVM::stateForRow(int)`, `SnapshotsVM::healthForRow(int)`/marker predicates, `StatusLineVM::severity()`. Header/placeholder/out-of-range rows return nullopt. The **role→color mapping lives in the TUI** (per-surface presentation, §4.1), so the VM stays presentation-free. Chosen over (a) presentation-only categorical color (drops the headline per-row coloring) and (b) also wiring the GUI (larger, deferred). GUI stays as-is under a DESIGN §9 temporary parity exception — acceptable because §10 forbids color as the sole signal, so facts/words remain at parity; only the additive color differs between surfaces for now.

2. **Full per-view decomposition (not minimal patching).** `tui_app.cpp` shrinks to a thin shell. New files: `tui/src/theme.{hpp,cpp}`, `tui/src/render_util.hpp` (status glyph, kv row, region frame), `tui/src/views/{tab_bar,status_bar,devices_view,modules_view,updates_view,snapshots_view}.cpp`. Each view exposes pure `render(const XxxVM&, const Theme&, Size) -> ftxui::Element` — no sysfs/D-Bus/filesystem work in render (§8). Rationale: coloring an untested 965-line closure is unverifiable; decomposition is what makes §12.1 render tests possible at all. Alternative (color in place, decompose later) rejected: doubles the touch count and ships untested color.

3. **Glyphs: ASCII default, Unicode opt-in.** `+` enabled, `-` disabled, `?` unavailable, `!` unsigned — width-safe in every terminal (§8 forbids ambiguous-width dependence). Unicode dots (● ○ ◉) only behind the capability flag; mono/plain always ASCII. Alternative (Unicode default with fallback) rejected: default must be the safe path.

4. **Planning = OpenSpec change** (this change), matching the last two phases. New capability `tui-presentation`; `ui-accessibility` main spec extended with a color-independence requirement.

5. **Theme = DESIGN §4.1 roles → FTXUI 16-color ANSI only.** accent→Cyan (+inverted for selection/focus), success→Green, warning→Yellow, danger→Red, info→Blue, muted→dim. Capability modes full→mono→plain resolved once at startup from `NO_COLOR` env, `--no-color`/`--ascii` flags, `TERM=dumb`. Color decorators become identity functions in mono/plain — every colored signal pairs with a glyph AND existing text, so meaning never depends on color (§8/§11). Never emit ANSI strings directly; FTXUI decorators only.

**Layout:** keep master-detail (§2.3). Borders only on major regions (list/detail/status); separators + muted group headers for sub-regions (§4.3); one legend line, one status line (§8). 80x24 keeps the existing list/detail switching (§3.2); borders degrade to ASCII in plain mode.

**Per-view coloring:**

| View | Mapping |
| --- | --- |
| Devices | enabled→success, disabled→danger, unavailable→warning, unknown→muted; guard refusal→danger on status line |
| Modules | signed→success, NO→danger, `?`→muted, blacklisted→warning; Secure Boot/lockdown banner info, →warning only when it explains a likely refusal (§5.5) |
| Updates | available→info, up-to-date→muted-success, error→danger |
| Snapshots | healthy→default, corrupt→danger, unsupported→warning; HEAD and last-good markers→accent (SnapshotHealth = Ok/Corrupt/Unsupported) |
| Status line | success/warning/danger/info by task outcome |

**Tests (§12.1 — none exist today):** new `tui/tests/` — `test_theme.cpp` (role→color mapping + capability downgrade) and `test_<view>_render.cpp` per view: render to fixed `ftxui::Screen` at 120x32 / 100x28 / 80x24; assert no row exceeds width and no out-of-bounds writes; glyph+text present in MONO (color-independence proof); selection/focus markers; states matrix (empty/loading/prompt/confirmation/refusal/failure). Wired into ctest; format + gated clang-tidy cover `tui/src` + `tui/tests` (host clang-tidy is fine for `tui/`, unlike `gui/`).

## Risks / Trade-offs

- [Behavior drift during extraction of a 965-line closure] → extract one view at a time with the closure delegating to the new function; app stays working and green at every step (see Migration Plan).
- [Color applied before tests exist would be unverifiable] → sequencing puts theme+tests and per-view extraction before any semantic coloring lands.
- [Terminal theme variance makes ANSI colors unpredictable] → 16-color roles only (user's palette decides exact rendering), every signal paired with glyph+text, mono/plain modes always available.
- [clang-tidy/format gates newly covering tui/ may surface latent noise] → gates extended in the same step as the file moves; fix-forward within the change.
- [GUI/TUI color divergence from the TUI-only accessors] → documented DESIGN §9 temporary parity exception; §10 bars color-as-sole-signal so facts/words stay at parity; accessors are designed GUI-consumable so a later change adopts them without rework.
- [Per-row state accessors mis-map a row (header/placeholder) to a state] → accessors return `optional`/`nullopt` for non-data rows and are unit-tested against header/placeholder/empty/filtered row layouts.

## Migration Plan

Low-risk sequencing — each step leaves the build green and the app working:

1. `theme` + `render_util` + `test_theme` — no UI change.
2. Extract each view as a pure `render(VM, Theme, Size)` (behavior-preserving, no color yet); closure delegates. Render tests per view.
3. Add read-only per-row state accessors to the VMs (app/) + unit tests — no wording/behavior change.
4. Apply semantic coloring (views map accessor state → theme role) + border policy.
5. Full states-matrix render tests + mono/plain color-independence proof.

Rollback: presentation-only change confined to `tui/`; reverting the change restores the prior closure with no data or IPC implications.

## Open Questions

None — the five decisions above are locked and user-approved.
