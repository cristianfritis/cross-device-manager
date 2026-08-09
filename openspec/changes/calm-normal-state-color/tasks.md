## 1. Source of truth

- [x] 1.1 Add the `Nominal` row to the `docs/DESIGN.md` §4.1 role table, between Accent surface and Success, with its "verified normal, resting state" use text
- [x] 1.2 Add `nominal → green + dim` to the §4.1 TUI mapping bullet list (currently at `docs/DESIGN.md:163`)
- [x] 1.3 Amend the §4.1 Success use text so it reads as a completed operation only, not a healthy resting result
- [x] 1.4 Settle the GUI light/dark reference values for `Nominal` (design Open Question), or record the TUI-only convention in the table

## 2. Role plumbing

- [x] 2.1 Add `Nominal` to the `Role` enum in `tui/src/semantics.hpp`
- [x] 2.2 Add the `Role::Nominal` case to `Theme::decorate` in `tui/src/theme.cpp` returning `color(Color::Green) | dim`
- [x] 2.3 Confirm the non-Full early return at `tui/src/theme.cpp:30` covers `Nominal` with no extra code, and that the switch remains exhaustive with no default case

## 3. Per-view remapping

- [x] 3.1 `roleForDeviceStatus`: `Active` → `Nominal`, `Disabled` → `Muted`; leave `Transitioning`, `Error`, `Unknown` unchanged
- [x] 3.2 `roleForSignature`: `Signed` → `Nominal`
- [x] 3.3 `roleForUpdateState`: `UpToDate` → `Nominal`, and drop the now-inaccurate "muted-success" comment
- [x] 3.4 `roleForSnapshotRow`: return `Nominal` for a healthy row with no marker, keeping the `!health` early return on `nullopt`; update the file-header comment at `state_roles.hpp:23` so `nullopt` documents only the structural case
- [x] 3.5 Audit `tui/src/tui_app.cpp` for direct `Role::Success` uses and move any that describe resting state rather than a task outcome

## 4. Tests

- [x] 4.1 `tui/tests/test_theme.cpp`: assert `Nominal` renders green with `dim` set, and that `Success` still renders green without `dim`
- [x] 4.2 `tui/tests/test_states_matrix.cpp`: extend `kRoleColours` from 6 to 7 entries with `{Role::Nominal, Color::Green, true}`
- [x] 4.3 `tui/tests/test_states_matrix.cpp`: update the per-view row expectations, including the Modules signed row at line 540 and the Devices matrix
- [x] 4.4 `tui/tests/test_selection_render.cpp`: update the enabled-row assertions at lines 411 and 413, and confirm the selection-inversion behaviour recorded at line 153 still holds for a dim foreground
- [x] 4.5 `tui/tests/test_bars_render.cpp`: update any assertion that expects success on a resting row — none found; its only `Role::Success` is a status-bar outcome ("Enabled eth0"), which is exactly where success still belongs
- [x] 4.6 Add a regression test that no collection view emits `Role::Success` when nothing is in flight and nothing recently completed
- [x] 4.7 Add a MONO/PLAIN test asserting `Nominal` produces no color and the normal state stays identifiable from glyph and state word
- [x] 4.8 Add a test asserting disabled and unknown devices are distinguishable by glyph and state word while sharing the muted role

## 5. Owner verification on a real terminal

This group is the gate, not a formality. It cannot be satisfied by a passing
test suite. If 5.1 fails, stop and reopen design decision D1.

- [x] 5.1 Render a Devices list containing enabled, disabled, transitioning, error, and unknown rows; confirm dim-green, plain green, and default foreground are tellable apart on the owner's actual terminal — PASS. All five states rendered via a byte-truthful probe (`\e[32m\e[2m` nominal, `\e[32m` success, `\e[2m` muted); dim-green, plain green, default foreground and dim-only are four distinct paints in wezterm, xterm and xfce4-terminal. Also confirmed in the real app: every Devices row emits `\e[2m\e[32m`
- [x] 5.2 Move the selection across a nominal row and confirm the inverted-video row stays legible — PASS. Selection moved six rows in the live TUI; the selected row is `\e[1;7m\e[36m` (bold reverse cyan) and carries no dim, so the row under the cursor is the brightest thing on screen, not the quietest
- [x] 5.3 Render Snapshots with healthy rows plus HEAD and last-good markers; confirm the cyan markers stay unmissable against dim-green rows — PASS via the probe in all three terminals; cyan markers read as a different channel entirely, not a brighter green. NOT reproducible in the live app on this box: devmgrd is uninstalled, so Snapshots renders empty ("Device service unavailable"). The equivalent live check is the Modules/Devices criticality badge (bright yellow `#`/`~`), which stays loud against dim-green rows
- [x] 5.4 Run with `NO_COLOR=1` and with `--ascii`; confirm every state is still identifiable and that output is byte-identical to the pre-change run in those modes — PASS, measured not argued. Captured the live TUI in both modes from the post-change binary and from a baseline binary built with the three role sources stashed: `diff` reports BYTE-IDENTICAL for mono and for plain. Full mode differs in exactly one way: `\e[32m` on a row became `\e[2m\e[32m`. Zero colour SGR present in either degraded mode
- [x] 5.5 Repeat 5.1 on a second terminal emulator to check the SGR 2 assumption is not specific to one terminal — PASS on three: wezterm, xterm (strongest separation), xfce4-terminal/VTE (weakest but still clear). None dropped SGR 2

## 6. Gates

- [x] 6.1 `openspec validate calm-normal-state-color --strict` passes
- [x] 6.2 Container unit tests green (`docker compose` unit service — rebuild first, the image has no volume mount)
- [x] 6.3 `scripts/check-format.sh --container` passes under clang-format-18
- [x] 6.4 Container clang-tidy gate exits 0 with no user diagnostics
