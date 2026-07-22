# Proposal: tui-instrument-panel-polish

## Why

The FTXUI TUI is functionally complete but visually flat: one 965-line `runTuiApp` closure with no color (only bold/inverted/separator), no per-view decomposition, and zero render tests. DESIGN.md §4.1 defines semantic color roles and §12.1 defines TUI render checks that the TUI does not yet implement — including `NO_COLOR`/monochrome operation, which DESIGN.md §4.1 requires "when support is added." With v0.6.0-beta.1 shipped and Phase 9 deferred, this is the stabilization window to bring the TUI to instrument-panel quality within the existing design contract.

## What Changes

- Decompose `tui/src/tui_app.cpp` into a thin shell plus pure per-view render functions: new `tui/src/theme.{hpp,cpp}`, `tui/src/render_util.hpp`, and `tui/src/views/{tab_bar,status_bar,devices_view,modules_view,updates_view,snapshots_view}.cpp`. Each view is `render(const XxxVM&, const Theme&, Size) -> ftxui::Element` with no I/O (DESIGN §8).
- Add a semantic 16-color ANSI theme mapping DESIGN §4.1 roles (accent→cyan, success→green, warning→yellow, danger→red, info→blue, muted→dim). Never true-color/256. Capability modes full→mono→plain resolved from `NO_COLOR`, `--no-color`/`--ascii` flags, and `TERM=dumb`; color decorators become identity in mono/plain so meaning never depends on color.
- Add status glyphs: ASCII default (`+` enabled, `-` disabled, `?` unavailable, `!` unsigned); opt-in Unicode dots behind the capability flag; mono/plain always ASCII.
- Apply per-view semantic coloring (Devices, Modules, Updates, Snapshots, status line) and restrict borders to major regions per DESIGN §4.3.
- Add the first TUI render-test suite (`tui/tests/`): theme downgrade tests plus per-view fixed-screen renders at 120x32 / 100x28 / 80x24, wired into ctest, with format and clang-tidy gates covering `tui/src` + `tui/tests`.
- No `app/`/`core/` behavior change; GUI/TUI parity preserved (color is native TUI presentation; GUI already uses QPalette).

## Capabilities

### New Capabilities

- `tui-presentation`: TUI presentation layer — semantic theme with capability degradation (full/mono/plain), pure per-view render decomposition, status glyph policy, and fixed-screen render test coverage.

### Modified Capabilities

- `ui-accessibility`: add a color-independence requirement — state is never conveyed by color alone; the TUI honors `NO_COLOR`/`--no-color`/`TERM=dumb` with glyph+text pairing intact in every mode.

## Impact

- **Code**: `tui/src/**` (decomposition + new files), `tui/tests/**` (new), `tui/CMakeLists.txt`, top-level CMake/ctest wiring, CLI flag plumbing for `--no-color`/`--ascii` in the TUI entry point.
- **App layer**: read-only per-row **state** accessors added to `DeviceListVM`/`ModulesVM`/`UpdatesVM`/`SnapshotsVM`/`StatusLineVM` (surfacing already-computed state for the TUI to color from). No wording, filtering, identity, or behavior change; no ApiVersion change. (This revises the initial "no app change" intent — see design.md Decision 1a.)
- **GUI**: unchanged this cycle — documented DESIGN §9 temporary parity exception. The new accessors are designed to be GUI-consumable so a later change can adopt them for GUI coloring without rework.
- **Untouched**: `core/` (existing enums reused), daemon, IPC.
- **Dependencies**: none new (FTXUI already present).
- **Gates**: extends format + clang-tidy coverage to new TUI files; new ctest targets join the container unit gate; VM-accessor unit tests join `devmgr_tests`.
