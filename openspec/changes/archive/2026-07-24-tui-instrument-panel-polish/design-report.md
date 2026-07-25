# DESIGN §13 report — tui-instrument-panel-polish

Closes task 12.4. Covers both passes (pass 1: theme, decomposition, semantic
colour, render tests; pass 2: bug-fixes B1–B6, completions K1–K4, new scope
R1–R6, plus B7 found at apply time).

## 1. Shared states considered, and both surfaces

The states this change touches are the ones §6 makes shared between the GUI and
the TUI. For each: where the fact originates, and what each surface does with it.

| Shared state | Source of truth | TUI | GUI | Parity |
| --- | --- | --- | --- | --- |
| Device status (enabled / disabled / unavailable / unknown) | `DeviceListVM::statusForRow` | glyph + state word + semantic colour | glyph + state word, no colour | §9 temporary colour exception; words at parity |
| Module signature (signed / unsigned / undetermined / blacklisted) | `ModulesVM::signedForRow` | `yes`/`NO`/`…` cell + colour | same cell text | same exception |
| Update state (available / up-to-date / error) | `UpdatesVM::stateForRow` | row colour + version cells | version cells | same exception |
| Snapshot health + HEAD/last-good markers | `SnapshotsVM::healthForRow` + marker predicates | colour + `*` marker | marker text | same exception |
| Status-line severity | `StatusLineVM::severity()`, composed by `composeStatus` | colour by max severity | message text | same exception |
| **Canonical device name** | `core::displayDeviceName` | primary list label + detail `Name:` | primary list label + detail `Name:` | **full parity, same core formatter** |
| **Criticality (essential / important / ordinary)** | `core::classifyDevice` / `classifyModule` via `criticalityForRow` | `#`/`~` marker + `Risk:` detail line | criticality WORD in list text + `Risk:` detail line | **full parity in words**; colour TUI-only |
| Minimum-size refusal | `views::belowMinimumSize` | notice naming the minimum + both exits | GUI has its own resize behaviour (untouched) | n/a — TUI-only condition |

Parity statement: this change **preserves parity and carries one documented
temporary exception** — the DESIGN §9 GUI colour exception recorded as a durable
requirement in `specs/tui-presentation/spec.md` ("GUI color parity — temporary
DESIGN §9 exception"). It is bounded by §10: no state is conveyed by colour
alone on either surface, so facts, choices, consequences and wording stay
identical; only the additive colour differs. The accessors were built
GUI-consumable so a later change can lift the exception without reworking the
seam.

The two pass-2 additions that would have *created* a parity gap — canonical
names (R1) and criticality (R4) — were deliberately routed through `core/` and
the ViewModels instead of the TUI, so both surfaces render them from one field
(R6, proven by `gui/tests/test_r6_parity.cpp`).

`app/` and `core/` toolkit independence held throughout: the Qt and sdbus purity
guards pass, `core::displayDeviceName` and the criticality classifier are pure
formatters/policies over existing DTO fields, and the role→colour mapping lives
entirely in `tui/src/state_roles.hpp`.

## 2. Executable checks that ran

Local mirror of CI, all green on 2026-07-24 unless noted:

- **Container unit gate** (image rebuilt first — no volume mount, so the gate is
  stale otherwise): `ctest` **675/675 passed**, 1 skipped
  (`SysfsControllerTest.UnwritableAttrIsIo`). Includes the GUI offscreen suite
  under `QT_QPA_PLATFORM=offscreen`.
- **Host `ctest`**: 674/674 (the container adds the fwupd/integration cases).
- **Container clang-format-18**: 240 files clean over
  `core tests app platform tui gui daemon cli` — exact CI parity (host
  clang-format is v22 and would diverge).
- **Container clang-tidy-18** `--warnings-as-errors='*'` over `core/src app/src
  platform/linux/src gui/src daemon/src cli/src tui/src tui/src/views`: exit 0,
  no user-code diagnostics.
- **Purity guards**: no Qt in `core`/`app`/`platform`/`daemon`/`cli`; no sdbus
  outside the two anchored exemptions. Both PASS.
- **`openspec validate tui-instrument-panel-polish --strict`**: valid.
- **TUI self-test / version test**: `devmgr_tui_selftest` and
  `devmgr_tui_version` pass (full wiring + one enumeration, no alternate screen).

Test coverage added by this change: `tui/tests/` went from **0 to 103 tests**
across 12 files (97 render tests in `devmgr_tui_tests`, 6 key-routing tests in
`devmgr_tui_input_tests`), plus VM/core unit tests in `tests/unit/`
(`test_device_presentation.cpp` 20, `test_criticality.cpp` 15,
`test_vm_criticality_accessors.cpp` 11, `test_tui_state_roles.cpp` 9) and
`gui/tests/test_r6_parity.cpp` (3 offscreen).

The checks proportional to each behaviour: every locked behaviour in the design
has an off-screen assertion — selection invariant, one selection treatment,
non-selectable placeholders, single empty indicator, column order, one-row
status line, per-view colour in FULL, glyph+word in MONO, ASCII in PLAIN, marker
distinctness, reveal bounds and termination, min-size boundary, filter
key-routing (the full command-key union incl. case twins and digits).

## 3. B7 — defect found by the last test written

Task 12.3b asked for the one screen no test covered: an essential module
selected, marker on the row **and** the criticality word in the detail pane, in
MONO, with no colour. Written against the three reference sizes, it failed at
80x24 — not in the test, in the build.

The three wide views (Modules, Updates, Snapshots) pinned the collection pane at
a hard `size(WIDTH, EQUAL, 72)`. At the DESIGN §3.2 minimum of 80 columns that
left the detail pane **six** columns, two of them its own border, so every detail
line rendered as three characters and an ellipsis (`Mod…`, `Ris…`). The pane was
structurally present and informationally empty. Nothing caught it because every
wide-view render test ran at 120 columns, where the pane is comfortable.

Fixed as a pure seam (design Decision 10): `views::wideLeftPaneWidth(cols)`
returns `clamp(cols - 2 - 34, 44, 72)` — unchanged at ≥108 columns, yielding to
44/34 at 80, which is the split the Devices tab already used. The shell reads the
live terminal width and passes the result in as view data, so the view functions
stay pure (§8). Accepted trade-off: at 80 columns a Modules row clips after
`Name` and `Signed` instead of after `Size`; the dropped columns are carried by
the detail pane for the selected row.

This is the second time in this change that a "verified by screenshot" claim
turned out to be verified only at one size or against one build tree (the first
was the 2026-07-24 stale-binary report traced in task 12.3). Both were caught by
writing the off-screen assertion the screenshot had stood in for.

## 4. Manual checks that remain, and why they cannot be automated

Recorded as task 12.3, with the sheet in `manual-test-12.3.md`. These need a real
terminal, a real keyboard, or real system state:

- **Real terminal emulator rendering.** The render tests draw to an
  `ftxui::Screen` buffer and assert on pixels. They cannot prove that a real
  emulator resolves the 16-colour roles to distinguishable hues under the user's
  palette, that box-drawing characters render at the terminal's font, or that
  `NO_COLOR` / `TERM=dumb` are honoured through the actual startup path.
- **Reveal timing and feel.** `revealOffset` is proven monotonic and terminating
  off-screen, and the element is proven width-bounded at offset 0 and offset max.
  Whether the ~0.6 s lead-in and one-glyph-per-tick cadence reads as *deliberate*
  rather than jittery is a judgement about motion over wall-clock time, which the
  fixed-screen harness has no notion of. The same applies to confirming the
  ticker actually stops (observable as no CPU wakeups at rest).
- **Keyboard-only workflow end to end.** `nav::routeFilterKey` is unit-tested
  over the full command-key union against real `ftxui::Event` values, but the
  test drives the routing function, not FTXUI's live focus stack. That the
  `Input` genuinely consumes `Event::Character` before the list handler sees it,
  in the assembled component tree, is a runtime property.
- **Mouse interaction.** B1's root cause was FTXUI's `focused_entry` moving on a
  bare mouse-over. The fix is asserted at the render seam, but reproducing the
  original symptom needs a real pointer.
- **Real daemon-down and real refusal wording.** The guard refusal path is
  covered by unit tests with fakes; confirming the status line's danger valence
  and wording when `devmgrd` is genuinely stopped needs the real service absent.
- **Real hardware variety.** `scripts/probe-canonical-names.sh --resolve`
  measured tier coverage on this dev box (51 pci/usb devices, 5 bare-address rows
  → 0). Whether the `displayDeviceName` tiers generalise needs other hardware.

## 5. Scope discipline

No DESIGN.md edits (a stated non-goal). No ApiVersion, IPC, or daemon change. No
ViewModel wording change except the two parity-mandated additions above. One
premise in the planning artifacts was disproven at apply time and corrected in
place rather than worked around: the 80–109 column "switching layout" the spec
described does not exist in the build (task 10.4), and reconciling DESIGN §3.2
itself is parked as an explicit follow-up rather than silently absorbed here.
