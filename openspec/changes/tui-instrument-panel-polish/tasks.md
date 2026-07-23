# Tasks: tui-instrument-panel-polish

Sequencing follows design.md Migration Plan: each numbered group leaves the build green and the app working. Verify each step with the container unit gate (rebuild the `unit` image first — no volume mount) before marking done. Coloring path is Option C (design.md Decision 1a): VMs gain read-only per-row state accessors, TUI colors from them, GUI unchanged under a DESIGN §9 parity exception.

## 1. Theme foundation (no UI change)

- [x] 1.1 Create `tui/src/theme.hpp`/`theme.cpp`: semantic role enum (accent/success/warning/danger/info/muted), `Theme` struct exposing FTXUI decorators, capability mode enum (full/mono/plain) resolved from `NO_COLOR`, `--no-color`/`--ascii`, `TERM=dumb`; decorators are identity in mono/plain
- [x] 1.2 Plumb `--no-color`/`--ascii`/`--unicode` flags through the TUI entry point into theme resolution (theme reaches `runTuiApp`; consumed by views in group 2)
- [x] 1.3 Create `tui/src/render_util.hpp`: status glyph helper (ASCII default `+ - ? !`, Unicode `● ○ ◉` opt-in, ASCII forced in mono/plain), kv row helper, region frame helper (ASCII border fallback in plain)
- [x] 1.4 Create `tui/tests/` with `test_theme.cpp` (8 tests: role→color mapping, capability downgrade, flag/env resolution, glyph policy, plain-ASCII frame); new `devmgr_tui_render` lib + `devmgr_tui_tests` exe wired into CMake + ctest
- [x] 1.5 Extend gates to cover `tui/`: format gate covers `tui/` (container clang-format-18: 214 files clean). clang-tidy gate line extended to `tui/src/*.cpp tui/src/views/*.cpp` in `.github/workflows/ci.yml`; container clang-tidy-18 exit 0 over all 10 tui files. `runTuiApp` size/cognitive-complexity + the nested event-loop lambda suppressed with `NOLINTBEGIN/END` (GUI `main_window.cpp` precedent — event-loop composition, render logic already extracted to `views/`); `marqueeWindow` swappable-params `NOLINTNEXTLINE`; the 150 ms tick literal named.

## 2. Per-view extraction (pure render functions, behavior-preserving, no color yet)

- [x] 2.1 Extract `tui/src/views/tab_bar.cpp` and `tui/src/views/status_bar.cpp` as pure `render(...)` functions + render tests at 120x32/100x28/80x24 (`test_bars_render.cpp`, 3 tests); shell delegates via `views::renderTabBar`/`renderStatusBar`, behaviour-preserving, `theme` now consumed (no more `[[maybe_unused]]`)
- [x] 2.2 Extract `tui/src/views/devices_view.cpp` + `test_devices_view_render.cpp` (three sizes, no row exceeds width, no out-of-bounds writes, selection/focus markers)
- [x] 2.3 Extract `tui/src/views/modules_view.cpp` + render tests (same assertions)
- [x] 2.4 Extract `tui/src/views/updates_view.cpp` + render tests (same assertions)
- [x] 2.5 Extract `tui/src/views/snapshots_view.cpp` + render tests (same assertions)
- [x] 2.6 Reduce `tui_app.cpp` to a thin shell composing the view functions; confirm no behavior change (manual smoke + full ctest)

## 3. VM per-row state accessors (app/; no wording/behavior change)

- [x] 3.1 `DeviceListVM::statusForRow(int) -> std::optional<core::DeviceStatus>` (nullopt for header/placeholder/oob) + unit tests over grouped/filtered/empty layouts
- [x] 3.2 `ModulesVM::signedForRow(int)` (signed/unsigned/undetermined; blacklisted if available) returning an optional state enum + unit tests
- [x] 3.3 `UpdatesVM::stateForRow(int)` (available/up-to-date/error, optional) + unit tests
- [x] 3.4 `SnapshotsVM::healthForRow(int) -> std::optional<core::SnapshotHealth>` plus HEAD / last-good row-marker predicates + unit tests
- [x] 3.5 `StatusLineVM::severity()` (ok/success/warning/danger/info tracked when the message is set) + unit tests
- [x] 3.6 Run `devmgr_tests` (container unit gate) green with the new accessor tests

## 4. Semantic coloring and layout polish (TUI maps accessor state → theme role)

- [x] 4.1 Devices: Active→success, Disabled→danger, Transitioning→warning, Error→danger, Unknown→muted; guard refusal→danger on status line; glyph+text paired everywhere
- [x] 4.2 Modules: signed→success, unsigned→danger, undetermined→muted, blacklisted→warning; security banner info in steady state, warning only when it explains a likely refusal
- [x] 4.3 Updates: available→info, up-to-date→muted success, error→danger
- [x] 4.4 Snapshots: healthy→default, corrupt→danger, unsupported→warning; HEAD + last-good markers accent
- [x] 4.5 Status line: success/warning/danger/info by `severity()`
- [x] 4.6 Border discipline: borders on major regions only (list/detail/status); separators + muted group headers for sub-regions; one legend line, one status line; verify 80–109 col switching layout intact

## 5. States matrix and color-independence proof

- [x] 5.1 Per-view states-matrix render tests: empty, loading, prompt, confirmation, refusal, failure
- [x] 5.2 Mono-mode assertions per view: state glyph AND state text present for every represented state (color-independence proof); plain-mode ASCII border check
- [x] 5.3 Update coloring tests to assert semantic role decorators in full mode

## 6. Exit gate

- [x] 6.1 Full local CI mirror (all green 2026-07-22): `check-format.sh --container` (clang-format-18, 214 clean); Qt + sdbus purity guards PASS; container unit gate rebuilt + `ctest` 533/533; container clang-tidy-18 over the full gate set incl `tui/src` + `tui/src/views` exit 0
- [x] 6.2 Document the DESIGN §9 temporary GUI parity exception (GUI color deferred; accessors GUI-ready) where the project records parity exceptions — added as a durable requirement in `specs/tui-presentation/spec.md` (syncs to the main spec on archive; honors the "no DESIGN.md edits" non-goal); `openspec validate --strict` green
- [ ] 6.3 Manual matrix (DESIGN §12.2 TUI rows): 120x32 / 100x28 / 80x24, `NO_COLOR`, `TERM=dumb`, keyboard-only Devices+Modules workflow; record results
- [ ] 6.4 `openspec validate` change + specs green; user commits
