# Task 12.3 — Pass-2 manual test matrix (extends 6.3)

Supersedes `manual-test-6.3.md`, which points at a build tree that no longer
exists and describes an 80-column collapse the build never had.

## Binary — read this before launching

```
./build/tui/devmgr-tui          # correct; rebuild: cmake --build build --target devmgr-tui -j24
./build/linux-debug/tui/...     # DO NOT USE — deleted; it was stale at 2026-07-23 09:59
```

`build/linux-debug/` predated the pass-2 commit and carried none of the pass-2
criticality strings. Running it is what produced the 2026-07-24 false report of a
perpetually looping Devices reveal and missing module criticality markers. It has
been removed; confirm it has not come back before starting.

Verify the binary is current before the first cell:

```
stat -c '%y' build/tui/devmgr-tui && git log -1 --format=%cd
strings build/tui/devmgr-tui | grep -c essential   # expect >= 1
```

## How to run each cell

Resize the terminal first (`printf '\e[8;<rows>;<cols>t'` in most emulators),
then launch the matching mode. Quit with `q`.

| Mode | Command | Expect |
| --- | --- | --- |
| Full (16-colour) | `./build/tui/devmgr-tui` | semantic colour on states |
| Mono | `NO_COLOR=1 ./build/tui/devmgr-tui` | no colour, box-drawing borders kept, glyph+word intact |
| Mono (flag) | `./build/tui/devmgr-tui --no-color` | same as NO_COLOR |
| Plain | `TERM=dumb ./build/tui/devmgr-tui` | no colour, ASCII borders |
| Plain (flag) | `./build/tui/devmgr-tui --ascii` | same as TERM=dumb |

## Size × mode grid — record PASS/FAIL

Per cell: no row wraps or overflows; borders enclose only major regions
(list/detail/status); exactly one legend line and one status line; **both panes
render and both carry readable text** (there is no list-only collapse — at 80
columns the split narrows to 44/34, the same proportions the Devices tab uses).

| Size | Full | Mono (NO_COLOR) | Plain (TERM=dumb) |
| --- | --- | --- | --- |
| 120x32 |  |  |  |
| 100x28 |  |  |  |
| 80x24  |  |  |  |

## B1–B6 — re-confirm visibly

- [ ] B1 selection bar, `>` cursor and the detail pane are all on the SAME row —
      including after moving the mouse over a different row without clicking
- [ ] B2 the selection colour is identical across all four tabs and does not
      change with the selected row's own state
- [ ] B3 empty Snapshots shows no cursor at all on the placeholder; a group
      header never takes the cursor (arrow through it)
- [ ] B4 empty Snapshots shows exactly ONE "no snapshots" indicator
- [ ] B5 `Signed` is the second Modules column, and `yes ()` never appears
- [ ] B6 the status line is one row at 80 columns with the long Updates message,
      ending in a visible ellipsis rather than a mid-word cut

## §E additions

- [ ] active tab is unmistakable with no colour (bold + `{n}` braces vs `[n]`)
- [ ] a long name on the SELECTED row reveals its tail, comes to REST, and does
      not loop — in every tab (Devices, Modules, Updates, Snapshots)
- [ ] non-selected long rows elide right with a visible ellipsis
- [ ] Updates rows are coloured by state (available/up-to-date/error) in Full
- [ ] no row or line wraps or overflows in any cell above
- [ ] essential module shows the `#` marker on its row AND the criticality word
      in the detail pane, on one screen, in Mono AND Plain
- [ ] the three markers are distinguishable: focus `>`, criticality `#`,
      active tab `{n}`
- [ ] K3: below 80x24 in either dimension the minimum-size notice replaces the
      UI and names both exits; at exactly 80x24 the full UI returns
- [ ] K4: with the filter open, every command key (`r e U B q l u d s h x m /`
      and digits) types into the filter and does NOT fire its command; `Enter`
      hands focus back; `Esc` clears and hands back

## Edge content

- [ ] long path / modalias in the detail pane is bounded with a visible ellipsis
- [ ] no selection / no matches states render cleanly
- [ ] daemon down → refusal wording on the status line (stop the daemon first)
- [ ] delayed signature lookup shows a stable `…`/`?` state, not a flash
- [ ] operation failure → danger status + guidance where applicable

## Result

Overall: __ / __ cells PASS.

Notes:
