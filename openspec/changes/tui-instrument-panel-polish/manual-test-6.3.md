# Task 6.3 — Manual test matrix (DESIGN §12.2 TUI rows)

Binary: `./build/linux-debug/tui/devmgr-tui` (host build; rebuild with
`cmake --build build/linux-debug --target devmgr-tui -j24`).

Automated coverage already proves width-safety, mono glyph+text pairing, and
plain-mode ASCII borders (`tui/tests/`, 33 render tests). This pass is the human
confirmation the automated suite cannot give: real terminal, real keyboard.

## How to run each cell

Resize the terminal to the target size first (`printf '\e[8;<rows>;<cols>t'`
in most emulators, or resize the window). Then launch the matching mode:

| Mode | Command | Expect |
| --- | --- | --- |
| Full (16-color) | `./build/linux-debug/tui/devmgr-tui` | semantic colour on states |
| Mono | `NO_COLOR=1 ./build/linux-debug/tui/devmgr-tui` | no colour, box-drawing borders kept, glyph+word intact |
| Mono (flag) | `./build/linux-debug/tui/devmgr-tui --no-color` | same as NO_COLOR |
| Plain | `TERM=dumb ./build/linux-debug/tui/devmgr-tui` | no colour, ASCII `+ - │`→`|` borders |
| Plain (flag) | `./build/linux-debug/tui/devmgr-tui --ascii` | same as TERM=dumb |

Quit with `q`.

## Size × mode grid — record PASS/FAIL

For each cell confirm: no row wraps/overflows, borders enclose only major
regions (list/detail/status), one legend line + one status line, master-detail
switching intact (80-col collapses to list/detail toggle).

| Size | Full | Mono (NO_COLOR) | Plain (TERM=dumb) |
| --- | --- | --- | --- |
| 120x32 |  |  |  |
| 100x28 |  |  |  |
| 80x24  |  |  |  |

## Keyboard-only workflows (no mouse)

Devices tab:
- [ ] `Tab`/arrows move selection; focus marker visible without colour
- [ ] `/` filters; typing narrows; `Esc` clears; filter key does not leak into list
- [ ] enabled→success, disabled→danger, unavailable→warning, unknown→muted (Full)
- [ ] every coloured state still shows its ASCII glyph + state word in Mono/Plain
- [ ] a guard-refused action drives the status line to danger (red in Full, worded in Mono/Plain)

Modules tab (`m` or tab control):
- [ ] arrows move selection; signed→success, unsigned(`!`/NO)→danger, `?`→muted, blacklisted→warning
- [ ] security banner reads Info (blue) in steady state; Warning only when it explains a likely refusal
- [ ] `!`/`?` glyphs + words present in Mono/Plain

## Edge content (spot-check, any one size)

- [ ] long device name marquee-scrolls inside its region (no wrap/overflow)
- [ ] long path in detail pane bounded (no overflow)
- [ ] no selection / no matches states render cleanly
- [ ] daemon down → refusal wording on status line (run with daemon stopped)
- [ ] delayed signature lookup shows a stable `?`/loading state, not a flash
- [ ] operation failure → danger status + guidance where applicable

## Result

Overall: __ / __ cells PASS. Notes:
