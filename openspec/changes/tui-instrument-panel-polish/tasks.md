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

- [x] 7.1 (T1) Add `core::displayDeviceName(const core::Device&)` + `core::displayDeviceIdentity(...)` (`core/{include/devmgr/core,src}/device_presentation.{hpp,cpp}`) as PURE FORMATTERS over EXISTING DTO fields — no libudev/libpci call, no bundled pci.ids, no sqlite, no new dependency, no daemon/IPC/ApiVersion change. **Step 0 (revised 2026-07-23, supersedes the earlier audit):** `scripts/probe-canonical-names.sh` (now with a `--resolve` tier simulation) measured 51 pci/usb devices on the dev box — `ID_MODEL_FROM_DATABASE` covers 43, `ID_VENDOR_FROM_DATABASE` covers 51/51, `ID_PCI_SUBCLASS_FROM_DATABASE` 38/39 PCI; 5 rows render as a bare kernel address today. The earlier audit's claim that the DTO carries no vendor/class strings was WRONG: `udev_device_mapper.cpp:98-102` copies the device's ENTIRE udev property map into `core::Device::properties`, and TUI/GUI construct `UdevDeviceEnumerator` in-process (`tui_app.cpp:209`, `gui_app.cpp:64`) on both the enumerate and hotplug paths — so the hwdb strings are already DTO data on every real path (test fakes leave `properties` empty and degrade to the raw-id tiers). The DTO field is `sysfsPath`, not `lastSysfsPath`. **Formatter contract (as built):** tier 1 `Device::name` when non-positional (+ direct `ID_MODEL_FROM_DATABASE` backstop) → tier 2 cleaned `ID_MODEL` (underscores→spaces; rejected when it is just the hex product id) → tier 3 `<shortVendor> <ID_PCI_SUBCLASS_FROM_DATABASE>` → tier 4 `<shortVendor> <ID_PCI_CLASS_FROM_DATABASE>` → tier 5 `<shortVendor> device` → tier 6 `<BUS> device`, then `(unknown device)`. Positional detection = `name == basename(sysfsPath)` (device_key.cpp:29-33 definition) OR PCI-BDF shape OR USB port-chain/root-hub shape. `shortVendor` prefers a bracketed pci.ids alias (`... [AMD]` → `AMD`) else strips one legal-form suffix (`Co., Ltd`, `Inc.`, `Corporation`, …); the vendor joins in tiers 3-5 ONLY — OWNER decision 2026-07-23: catalogued names keep the catalogue's wording (no churn on 43/51 rows, widest label 70 not 79 cols). `displayDeviceIdentity` = `vendorId:productId · <position>`, which is what distinguishes the 23 rows that share a name. **Wired:** `DeviceListVM::appendRows` (rows AND sort order), the filter haystack (canonical + raw name), and `DeviceDetailVM` `Name:` — list and detail read the same formatter, so R1 parity is structural. **Verified:** 20 new unit tests in `tests/unit/test_device_presentation.cpp` (tiers, positional shapes, vendor shortening table, junk-model rejection, empty/degenerate devices, identity disambiguation); full ctest 552/552 green; host format gate clean (container parity pending — docker daemon down, folded into 12.2).
- [x] 7.1b Vendor+class enrichment for uncatalogued rows — **folded into 7.1 and shipped there.** The owner-gate premise was false: no additive DTO field, no `UdevDeviceMapper` change and no ApiVersion decision are needed, because `Device::properties` already carries `ID_VENDOR_FROM_DATABASE` / `ID_PCI_{,SUB}CLASS_FROM_DATABASE` on every real path (see 7.1 Step 0). Measured effect on the dev box: rows rendering as a bare kernel address 5 → 0 (e.g. `0000:c5:00.4` → `AMD USB controller`, `3-3` → `Synaptics device`). Re-run `scripts/probe-canonical-names.sh --resolve` on other hardware to confirm the tier coverage generalises.
- [x] 7.2 (T2) Core criticality classifier (`core/{include/devmgr/core,src}/criticality.{hpp,cpp}`): `enum class Criticality {Ordinary, Important, Essential}`, `displayCriticality()` (the word that pairs with the marker in mono/plain), `classifyDevice(facts, sysfsPath)`, `classifyModule(name, refCount, holders)`. **OWNER sign-off 2026-07-23 — Decision 8 AMENDED from a name-list-only policy to facts-first + curated fallback.** Rationale found at apply time: `services::evaluateDisable`/`evaluateModuleUnload` already derive criticality from live system facts (root/boot backing paths, sole keyboard/pointer, refcount, holders) and are what the daemon actually enforces, so a curated-string marker could contradict the refusal the user hits — and Decision 8 itself calls misclassification safety-relevant. Devices therefore classify through the guard's own pure policy (Essential ⇔ the guard would refuse); modules use holders/refcount plus the Decision 8 curated essential list, which covers the case a refcount cannot see (amdgpu at refcount 0 still blanks the session). Curated list adopted as specified, exact-match only (no prefix/substring — it would over-warn); security modules (`apparmor`/`selinux`/`ima`/`integrity`/`lockdown`) map to Important, never Essential. + 15 unit tests in `tests/unit/test_criticality.cpp`, including one that asserts the marker and `evaluateDisable` agree rather than asserting a hardcoded expectation.
- [x] 7.3 (T3) VM fields/accessors: `DeviceListVM::nameForRow` (the canonical-name field — the SAME string the row text and the detail `Name:` render, so R1 parity is structural) and `DeviceListVM::criticalityForRow`; `ModulesVM::criticalityForRow` (classifies from the snapshot entry the row was formatted from, so marker and Ref/Used-by columns cannot disagree). New `ApplicationFacade::criticalityFacts()` returns the probed facts once — `canDisable()` re-probes the filesystem per call, which a per-row loop must never do; `DeviceListVM` probes in `refreshSnapshot()` (once per model change) and NOT in `rebuild()` (which also runs on every filter keystroke). Row vectors are now reset/appended only through `clearRows()`/`pushNonDeviceRow()` so the five parallel vectors cannot drift out of alignment. Filter haystack extended with the canonical name (users type what they read; the kernel name still matches). No existing shared string changed. **Owner decision 2026-07-23: the Devices marker is IN scope** (Decision 8 had it optional) — facts-first makes it exact. Segmented `StatusLineVM` accessor deliberately NOT built: it is optional here and 9.2/K2 is where composite status lines create the actual need. + 11 unit tests in `tests/unit/test_vm_criticality_accessors.cpp` (name/marker alignment, non-device rows, no-prober degradation, survival across filtering, canonical-name filtering). Full ctest 578/578 green; host format gate clean.

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
