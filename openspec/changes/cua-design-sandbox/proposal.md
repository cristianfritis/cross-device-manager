## Why

Every design requirement in `docs/DESIGN.md` is verified today either by a
fixed-screen render test — which renders a view in isolation, not the running
app — or by a human opening the app and looking. The gap between those two is
where the `calm-backend-unavailability` manual matrix found all five of its
defects on 2026-07-27, and three of them are instructive:

- The GUI Devices tab has no availability note at all, while the TUI Devices tab
  has one. No unit test compares the two *running* surfaces, so nothing failed.
- The GUI Modules banner takes the plain string instead of the role-carrying
  accessor, losing its glyph, weight, and disclosure. Its VM is correct, so the
  VM tests pass.
- The degraded Snapshots legend overflows 80 columns and loses `q=quit`
  entirely. The render test that would have caught it runs only at 120x32, and
  the overflow helper it relies on reads rows out of a fixed-width
  `ftxui::Screen` and therefore cannot fail.

That matrix was run by driving the real GUI through `cua-driver` and the real
TUI through fixed-size `tmux` panes. It worked, it found real defects in about
an hour, and it was assembled ad-hoc and thrown away. This change makes it a
harness the project keeps.

## What Changes

- A reusable design-review harness that launches the real `devmgr-gui` and
  `devmgr-tui` binaries, drives them, and captures what a user would actually
  see: the GUI's accessibility tree plus screenshots, the TUI's rendered text at
  fixed terminal sizes.
- A posture fixture that puts the backends into a chosen availability state
  (daemon down, fwupd absent, DKMS missing, all healthy) so degraded states are
  reproducible rather than accidents of the host — the 2026-07-27 matrix only
  worked because the machine happened to be degraded already.
- Assertions that only a running app can make: cross-surface parity on rendered
  text, "this string is nowhere on screen", legend and banner fitting inside the
  terminal width, and no danger paint on a readable screen.
- A containerised environment so the harness runs the same way on CI and on a
  developer machine, and so it does not depend on the operator's desktop
  session.

The five defects above are the harness's design rationale — the shapes it must
be able to see. They are **not** its acceptance criteria, and an earlier version
of this proposal was wrong to make them so.

Attempting the replay established why (design D7, `spike-evidence/replay/`).
F1, F2 and F3 were never present together in a committed revision: `git grep`
shows `i=diagnostics`, the addition that overflowed the Snapshots legend, first
appears in `38eaa00` — the same commit as the legend fitting that fixed it. The
three defects lived only in the working tree of 2026-07-27, between the feature
landing and the manual matrix catching it. No commit exhibits them, so no replay
can fail on them.

That sharpens the case for the harness rather than weakening it: those defects
were caught by a human driving a pre-commit tree and by nothing else, which is
exactly the tree a developer would point this harness at.

The acceptance criterion is therefore that the harness be **demonstrably able to
fail on a real committed defect**. It is: against `dcb25d4` it emits 51 findings
for the committed `tab-contextual-toolbar` defect — every tab's verbs visible on
every tab — and against `HEAD` it emits none.

## Capabilities

### New Capabilities

- `design-verification` — automated verification of `docs/DESIGN.md`
  requirements against the running application rather than against isolated
  render trees.

### Modified Capabilities

- None. This adds verification infrastructure; it changes no product behaviour.

## Impact

- New harness directory and its runner; new container image or an extension of
  the existing `test/docker-compose.yml` services.
- CI workflow gains a job, most likely non-blocking at first so a flaky UI probe
  cannot wedge the pipeline before the harness has earned trust.
- No product source changes.

Known constraints, measured on 2026-07-27 on this machine, that the design must
answer:

- **Qt exposes no AT-SPI tree here.** `get_window_state` returned only the
  top-level window with `org.a11y.Bus` reachable and `IsEnabled` true, so
  element-indexed driving was unavailable and the run fell back to pixel
  coordinates. Either the harness ships the Qt accessibility bridge in its
  image, or it drives the GUI by pixel and treats the screenshot as the ground
  truth.
- **`/dev/uinput` is absent here**, so background pixel input could not land and
  the run escalated to `delivery_mode:"foreground"`, which briefly activates the
  window. Acceptable for a headless container; not acceptable for a harness a
  developer runs beside their own work. The container path is what makes this
  tolerable.
- **The TUI needs no driver at all.** A fixed-size `tmux` pane plus
  `capture-pane` gave exact, greppable, byte-comparable output at 120x32 and
  80x24 — including confirming the sentence is byte-identical under `NO_COLOR=1`
  via `od -c`. The TUI half of the harness should stay this simple.
