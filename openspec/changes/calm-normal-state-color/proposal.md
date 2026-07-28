## Why

`docs/DESIGN.md` §9 currently specifies Devices as `enabled→success`. On a real
machine almost every device is enabled, so the Devices list renders as an
unbroken wall of green — the "wall of green" the owner flagged during the
v0.6.0-beta.1 review.

Colour that is always present carries no information. Worse, it spends the
palette's strongest signal on the least interesting fact, so the one genuinely
notable row — a disabled device, an unavailable one — has to compete with a
hundred neighbours shouting the same hue. The rule that makes the security
banner and the availability notes calm (state of the world → information;
something the user should act on → warning) has not been applied to the
steady state of a list.

`calm-backend-unavailability` established that principle for backends and
deliberately stopped there: its design report records the Devices colour
direction as a parked decision needing its own `enabled→success` amendment,
because changing it is a wording-and-palette decision for the owner rather than
something to slip into an availability change.

## What Changes

- Amend `docs/DESIGN.md` §9's per-view colour table so the **normal** state of a
  list is not painted with a success colour. Normal becomes the default
  foreground; colour is reserved for departures from normal.
- Apply the amended table to Devices first, then to every other view whose
  steady state is currently coloured, so one rule holds across the app.
- Keep the non-colour signals exactly as they are. Glyph and state text already
  carry the state in MONO and PLAIN modes, and they remain the primary channel —
  this change removes a redundant signal, it does not remove the only one.
- Update the render tests that currently assert success colour on a normal row.

Open question for the owner, to settle during design: whether "normal" means
*no colour at all* or *a muted success*. The second keeps a hint of positive
confirmation while dropping the shout; the first is stricter and matches how the
TUI's `default` role already behaves for healthy snapshots.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `tui-presentation` — the per-view semantic colouring requirement's Devices
  row, and the accompanying scenario that asserts `enabled→success`.
- `ui-accessibility` — reconfirm that dropping the colour leaves the state
  identifiable from glyph and words, which is what the accessibility
  requirement already demands.

## Impact

- `docs/DESIGN.md` §9 — the source of truth; amended first, code follows.
- `tui/src/state_roles.hpp` and the view renderers that map device state to a
  role.
- `gui/src/main_window.cpp` only if it colours device rows; the §9 GUI colour
  exception may already make this a TUI-only change.
- Render tests asserting the current mapping, including
  `Scenario: Device states colored semantically`.
- Risk: this is a visible change to the app's resting appearance. It wants the
  owner's eye on a real machine before it lands, not just a passing test.
