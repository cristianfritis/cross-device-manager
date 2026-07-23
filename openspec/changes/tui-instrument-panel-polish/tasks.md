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
- [ ] 6.3 Manual matrix (DESIGN §12.2 TUI rows): 120x32 / 100x28 / 80x24, `NO_COLOR`, `TERM=dumb`, keyboard-only Devices+Modules workflow; record results — *folded into pass-2 exit gate 12.3 (re-run in a real terminal)*
- [ ] 6.4 `openspec validate` change + specs green; user commits — *folded into pass-2 exit gate 12.4*

## 7. Pass 2 — core + VM data (no UI change)

- [ ] 7.1 (T1) Add `core::displayDeviceName(const core::Device&)` as a PURE FORMATTER over EXISTING DTO fields only (`name`, `vendorId`, `productId`, `bus`, `driver`, `lastSysfsPath`) — it MUST NOT call libudev/libpci, bundle pci.ids, add sqlite, or add any dependency, and this task makes NO daemon/IPC/ApiVersion change (hw-name resolution already happens daemon-side in the platform `UdevDeviceMapper`; `app/` is deliberately `NO libudev`, so a core/TUI host lookup is architecturally forbidden). **Step 0 (done 2026-07-23):** probe kit + repo audit confirmed the DTO carries hex `vendorId`/`productId` and a `name` that is the udev/hwdb product/subsystem name when catalogued and the kernel positional name (PCI BDF / USB port chain) otherwise; the DTO has NO vendor-name or class strings; the detail pane already reads the same `d.name` as the list (so D9 is "poor label for uncatalogued devices", not a wrong-field bug). **Formatter contract:** primary = `d.name` UNLESS `d.name` is the positional name — detect via `d.name == basename(d.lastSysfsPath)` (the codebase's positional-name definition, device_key.cpp:30-31) or a positional pattern (PCI BDF `^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-9a-fA-F]$`, USB port chain) — in which case primary = a raw-id composition; secondary (muted) = `vendorId:productId · <bdf/position>`; detail rows `Name:`/`VID:PID:`/`Address:`/`Id:` from existing fields. Wire the detail pane to the formatter so list and detail are guaranteed identical (single canonical-name VM field, R1 parity). **Expected render:** catalogued (`name`="Audio Coprocessor") → primary "Audio Coprocessor", secondary "1022:15e2 · <bdf>"; uncatalogued (`name`="0000:c5:00.4") → primary = raw-id composition (NOT a contextless bare address), with full context in the detail rows (Bus/VID:PID/Address/Driver/Id). + unit tests: catalogued → friendly primary + hex secondary; uncatalogued/positional → raw-id composition (assert `name == basename(lastSysfsPath)` is the trigger); USB port-chain case; empty `name` → raw id without error; GUI==TUI parity on the same VM field. (Vendor+class enrichment for uncatalogued rows is OUT of this task — see 7.1b.). Important: the scripts/probe-canonical-name.sh probe kit + repo audit confirmed that the DTO already carries the `name` field (udev/hwdb product/subsystem name when catalogued, kernel positional name otherwise) and that the detail pane already reads the same `d.name` as the list (so D9 is "poor label for uncatalogued devices", not a wrong-field bug). The formatter contract is to use `d.name` unless it is a positional name, in which case it uses a raw-id composition. The expected render for catalogued and uncatalogued devices is specified, and unit tests are required to verify the behavior. You can modify the existing `core::Device` class to add a new method `displayDeviceName()` that implements this logic, and ensure that both the list and detail views use this method for consistency. About scripts/probe-canonical-name.sh: it is a probe kit and repo audit tool that confirms the DTO already carries the `name` field and that the detail pane reads the same `d.name` as the list. This ensures that D9 is "poor label for uncatalogued devices" rather than a wrong-field bug. The formatter contract is to use `d.name` unless it is a positional name, in which case it uses a raw-id composition. The expected render for catalogued and uncatalogued devices is specified, and unit tests are required to verify the behavior. You can modify the existing `core::Device` class to add a new method `displayDeviceName()` that implements this logic, and ensure that both the list and detail views use this method for consistency.
- [ ] 7.1b (OPTIONAL, owner-gated; additive DTO field) Enrich uncatalogued-row labels to `<vendor> <class>` (e.g. "AMD USB controller"): extend the existing platform `UdevDeviceMapper` (already udev-linked) to also read `ID_VENDOR_FROM_DATABASE` + `ID_PCI_CLASS_FROM_DATABASE` (keep `ID_MODEL_FROM_DATABASE` as the `name` source); add `vendorName`/`className` to `core::Device`; bump `displayDeviceName` precedence to (vendor+product) → (vendor+class) → (class) → raw-id. This is a platform+DTO change, NOT a core/TUI host lookup. **Decision gate:** confirm whether the ApiVersion policy requires a bump for additive D-Bus `Device` fields (backward-compatible additions may not); if a bump is required and not accepted, defer this task — Track 1 (7.1) stands alone. + unit tests for the enriched uncatalogued case.
- [ ] 7.2 (T2) Add the core criticality classifier using the curated policy in design Decision 8 (OWNER sign-off pending final string confirmation at apply time). + unit tests.
- [ ] 7.3 (T3) Add VM fields/accessors: `DeviceListVM` canonical-name field + `criticalityForRow`; `ModulesVM::criticalityForRow`; optional segmented `StatusLineVM` accessor. + unit tests. No change to existing shared strings.

## 8. Pass 2 — bug-fixes B1–B6 (presentation-only; one task per bug incl. its regression test) (T4)

- [ ] 8.1 B1 selection invariant `selectedIndex == cursorIndex == detailIndex` + regression test.
- [ ] 8.2 B2 one accent/inverted-cyan selection treatment across all four views + per-view render test.
- [ ] 8.3 B3 placeholder/header rows non-selectable; empty list → no cursor + empty-state test.
- [ ] 8.4 B4 single empty indicator on Snapshots + test.
- [ ] 8.5 B5 Modules column order Name, Signed, Ref, Size, Used-by; `(signer)` only when non-empty + 80-col drop-order test.
- [ ] 8.6 B6 status line hard-clipped to one row with right-elide + >120-char-at-80-cols test.

## 9. Pass 2 — completions (T5)

- [ ] 9.1 K1 per-view colour on Updates/Snapshots from existing accessors + confirm Devices non-green states render + FULL/MONO matrix tests.
- [ ] 9.2 K2 status line coloured by `StatusLineVM::severity()` (max severity for composite lines) + tests.

## 10. Pass 2 — new-scope presentation (T6)

- [ ] 10.1 R2 active-tab highlight (accent+bold; MONO/PLAIN bold + ASCII marker; never yellow).
- [ ] 10.2 R3 bounded reveal: extract to `render_util` as a pure function of an explicit offset; reuse in Modules/Updates/Snapshots; non-selected rows keep `ElideRight`; the live time source is gated to "overflowing row selected/focused" and disabled at rest (design Decision 9); audit + convert pass-1's Devices driver if perpetual.
- [ ] 10.3 R4 criticality marker (Modules required; Devices optional) — marker on name only; never recolours +/- or signed glyph; never danger.
- [ ] 10.4 R5 muted column headers (Modules + Updates); non-selectable; also in the collapsed 80-col list-only view; 80x24 row budget holds.
- [ ] 10.5 render tests for 10.1–10.4 (incl. the three markers pairwise distinct).

## 11. Pass 2 — detail + GUI parity (T7, T8)

- [ ] 11.1 R1 canonical `Name:` in detail + `Address:`/`VID:PID:`/`Id:` rows + bounded long values (wrap/scroll) at 80 cols + render tests.
- [ ] 11.2 R6 GUI canonical name + criticality text in device list/detail + GUI offscreen test (colour deferred; runs in the container gate).

## 12. Pass 2 — verification + exit gate (T9, T10)

- [ ] 12.1 K3 80-col toggle render test; K4 filter no-leak key-routing test. **Key set = the full command-key union** `{ r, e, U, B, q, l, u, d, s, h, x, m, / }` plus nav `{ Tab, ArrowUp, ArrowDown, Esc, Enter }`; assert each *types into* the active filter and does not invoke its command. Include both `U` (unbind) and `u` (unload/install) because the filter is case-insensitive (this case mismatch is the leak surface); drop any stray lowercase `b` (bind is uppercase `B`). Digits `0–9` SHALL type into the filter (the legend binds no digit handler; if code later adds one, re-evaluate). `/` while filtering SHALL not re-trigger/loop; `Esc` SHALL clear and restore list focus. Implementation lever: the active filter is a focused `Input` whose `CatchEvent` consumes `Event::Character` before the list handler sees it.
- [ ] 12.2 Gates over touched `core/`+`app/`+`gui/`+`tui/` (GUI in the container gate); container ctest green.
- [ ] 12.3 Extended manual matrix in a **real terminal** at 120x32 / 100x28 / 80x24 × Full / Mono / Plain. Re-run the original 6.3 matrix AND re-confirm B1–B6 visibly: selection bar == `>` cursor == detail row; selection colour identical across all four tabs; status line is one row at 80 cols with the long Updates line; empty Snapshots has no cursor on the placeholder. Plus the §E additions: active tab distinguishable without colour; long names reveal on the selected row in every tab; Signed is the 2nd column; Updates rows coloured by state; no row/line wraps or overflows; edges — empty Snapshots single indicator, long status at 80 cols one row, essential-module marker + word in Mono/Plain; plus K3/K4.
- [ ] 12.4 `openspec validate tui-instrument-panel-polish --strict` green; DESIGN §13 report (shared states + both surfaces considered, incl. canonical-name/criticality parity; executable checks run; remaining manual checks + why not automatable: real terminal/keyboard, reveal timing, real daemon-down); user commits.
