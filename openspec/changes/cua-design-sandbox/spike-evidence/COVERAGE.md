# What the harness covers of the `docs/DESIGN.md` manual matrix

Required by §13's agent contract: *"Before finishing, an agent must report …
which manual visual checks remain and why they could not be automated."*

`docs/DESIGN.md` §12 now names three tiers: §12.1 offscreen/fixed-screen checks,
§12.2 the running-application harness, §12.3 the manual matrix. This table is
the evidence behind that split. Every "Automated" row below is asserted by
`test/design/assertions.py` and was observed passing on a real sweep.

| Original matrix row | Status | Evidence / why |
| --- | --- | --- |
| GUI: light, dark, high-contrast palette | **Human** | The accessibility tree carries no colour, and §12.1 forbids asserting on pixels. |
| GUI: 100%, 125%, 200% display scale | **Human** | Reachable via `QT_SCALE_FACTOR`, but the failure mode is visual crowding, not a missing element. Not attempted. |
| GUI: `1024x640` and `800x520` windows | **Automated** | Both swept; every showing toolbar action's extents checked against the window frame. |
| TUI: true colour, 256 colour, monochrome | **Partial** | Monochrome automated: the coloured and `NO_COLOR` renders are compared line by line. The two colour depths are not distinguished. |
| TUI: `120x32`, `100x28`, `80x24` | **Automated** | All three swept, all four views; legend shortcut keys compared across widths. |
| Both: keyboard-only Devices and Modules workflows | **Human** | The harness drives by accessibility action, not key events, so it does not exercise a keyboard path. |
| Both: long device names, long paths | **Automated** | The `umockdev` fixture guarantees an 86-character name and a five-level syspath on every machine. The long name is asserted present **in full** in the detail pane while elided in the list row — §10.1's elision rule, previously unverified. |
| Both: no selection | **Automated** | The unselected detail placeholder is captured on every tab of every sweep. |
| Both: daemon down | **Automated** | The degraded posture, asserted to genuinely have no `devmgrd` and no fwupd on the bus. |
| Both: no matches, guard refusal, delayed signature lookup, operation failure | **Not covered** | No posture drives these; they need input or a scripted backend failure. |

## Summary

Of the eleven original rows: **six automated, one partial, three human by
nature, one uncovered.**

The harness supplements the manual matrix; it does not retire it. A green
harness run is not grounds to skip §12.3.

## What the fixtures made possible

Two rows moved from "uncheckable" to "automated" only because the sweep stopped
depending on the host:

- **Devices** — `test/design/fixtures/devices.umockdev` replaces `/sys` for the
  wrapped process. Before it, whether any device name was long enough to elide,
  or any path deep enough to matter, depended on the machine's hardware.
- **fwupd** — `tests/fwupd/devmgr_fake_fwupd` serves a fixed inventory on the
  private system bus, so "fwupd is present and has an update" renders
  identically everywhere. The healthy posture shows `1.0.0 -> 1.1.0` for the
  seeded device; the degraded posture shows *"Firmware updates unavailable — the
  fwupd service is not responding."*

Both are asserted by `test/design/test_fixtures.py`, which runs before the sweep
so a posture that failed to apply is reported as a harness fault rather than as
a wall of design failures.
