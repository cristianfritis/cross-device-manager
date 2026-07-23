# Proposal: tui-instrument-panel-polish

## Why

The FTXUI TUI is functionally complete but visually flat: one 965-line `runTuiApp` closure with no color (only bold/inverted/separator), no per-view decomposition, and zero render tests. DESIGN.md §4.1 defines semantic color roles and §12.1 defines TUI render checks that the TUI does not yet implement — including `NO_COLOR`/monochrome operation, which DESIGN.md §4.1 requires "when support is added." With v0.6.0-beta.1 shipped and Phase 9 deferred, this is the stabilization window to bring the TUI to instrument-panel quality within the existing design contract.

**Pass 2 (2026-07-22).** A manual pass over the pass-1 build (5 screenshots across Devices/Modules/Updates/Snapshots plus the <80x24 message) confirmed the foundation but found three classes of remaining work: (a) defects that violate the existing spec (selection incoherence, per-tab selection color, cursor on placeholders, duplicate empty indicator, Modules column order, status-line overflow); (b) locked behaviors that did not fully land (Updates/Snapshots per-view color, status severity color, unverified 80-col toggle and filter no-leak); and (c) new usability scope the product owner now wants for non-technical users (canonical device names, active-tab highlight, cross-tab marquee, per-row criticality markers, column headers). Only (c) adds spec requirements; (a) and (b) are regression-tested fixes against the existing contract.

## What Changes

- Decompose `tui/src/tui_app.cpp` into a thin shell plus pure per-view render functions: new `tui/src/theme.{hpp,cpp}`, `tui/src/render_util.hpp`, and `tui/src/views/{tab_bar,status_bar,devices_view,modules_view,updates_view,snapshots_view}.cpp`. Each view is `render(const XxxVM&, const Theme&, Size) -> ftxui::Element` with no I/O (DESIGN §8).
- Add a semantic 16-color ANSI theme mapping DESIGN §4.1 roles (accent→cyan, success→green, warning→yellow, danger→red, info→blue, muted→dim). Never true-color/256. Capability modes full→mono→plain resolved from `NO_COLOR`, `--no-color`/`--ascii` flags, and `TERM=dumb`; color decorators become identity in mono/plain so meaning never depends on color.
- Add status glyphs: ASCII default (`+` enabled, `-` disabled, `?` unavailable, `!` unsigned); opt-in Unicode dots behind the capability flag; mono/plain always ASCII.
- Apply per-view semantic coloring (Devices, Modules, Updates, Snapshots, status line) and restrict borders to major regions per DESIGN §4.3.
- Add the first TUI render-test suite (`tui/tests/`): theme downgrade tests plus per-view fixed-screen renders at 120x32 / 100x28 / 80x24, wired into ctest, with format and clang-tidy gates covering `tui/src` + `tui/tests`.
- No `app/`/`core/` behavior change; GUI/TUI parity preserved (color is native TUI presentation; GUI already uses QPalette).
- Pass 2 — bug-fixes B1–B6 (presentation-only, each with a regression test): selection invariant `selectedIndex == cursorIndex == detailIndex`; one accent/inverted-cyan selection treatment across all four views; placeholder/header rows non-selectable (empty list → no cursor); a single empty indicator on Snapshots; Modules column order Name, Signed, Ref, Size, Used-by with `(signer)` only when non-empty; status line hard-clipped to one row with right-elide.
- Pass 2 — completions K1–K4 (wire what pass 1 specified but did not fully land): Updates/Snapshots per-view colour from the existing VM accessors; status line coloured by `StatusLineVM::severity()` (max severity for composite lines); the 80-col master-detail toggle and the filter no-leak behaviour verified as tests.
- Pass 2 — new scope R1–R6: canonical device names via new `core::displayDeviceName(...)`; active-tab highlight (accent, never yellow); a bounded reveal of overflowing selected-row names across all four tabs (extracted to `render_util`); a per-row criticality marker backed by a new core classifier + `criticalityForRow` accessors; muted column headers on Modules/Updates; canonical name + criticality TEXT reaching the GUI (parity, not exception).

## Capabilities

### New Capabilities

- `tui-presentation`: TUI presentation layer — semantic theme with capability degradation (full/mono/plain), pure per-view render decomposition, status glyph policy, and fixed-screen render test coverage.

### Modified Capabilities

- `ui-accessibility`: add a color-independence requirement — state is never conveyed by color alone; the TUI honors `NO_COLOR`/`--no-color`/`TERM=dumb` with glyph+text pairing intact in every mode.

## Impact

- **Code**: `tui/src/**` (decomposition + new files), `tui/tests/**` (new), `tui/CMakeLists.txt`, top-level CMake/ctest wiring, CLI flag plumbing for `--no-color`/`--ascii` in the TUI entry point.
- **Code (pass 2)**: add to `core/` (`displayDeviceName`, criticality classifier + unit tests); add to `app/` (`DeviceListVM` canonical-name field + `criticalityForRow`, `ModulesVM::criticalityForRow`, optional segmented `StatusLineVM` accessor + unit tests); add to `gui/` (canonical name + criticality text in device list/detail + offscreen test); extend `tui/tests/`.
- **App layer**: read-only per-row **state** accessors added to `DeviceListVM`/`ModulesVM`/`UpdatesVM`/`SnapshotsVM`/`StatusLineVM` (surfacing already-computed state for the TUI to color from). No wording, filtering, identity, or behavior change; no ApiVersion change. (This revises the initial "no app change" intent — see design.md Decision 1a.)
- **GUI**: colour remains deferred under the DESIGN §9 temporary parity exception; canonical name + criticality TEXT land in the GUI from the same shared VM/core fields (= parity, not a presentation change). The accessors remain GUI-consumable so a later change can adopt them for GUI coloring without rework.
- **Untouched**: daemon, IPC, ApiVersion. (`core/` is no longer untouched — see Code pass 2.)
- **Dependencies**: none new (FTXUI already present).
- **Gates**: extends format + clang-tidy coverage to new TUI files; new ctest targets join the container unit gate; VM-accessor unit tests join `devmgr_tests`. Pass 2: format + clang-tidy extend to the touched `core/`, `app/`, `gui/`, and `tui/` files (GUI checks run in the container gate).
- **Rollback**: additive VM/core fields are ignored by the old renderers; reverting restores the pass-1 views; no IPC or data-format change.
