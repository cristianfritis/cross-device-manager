## Why

`openspec/specs/tui-presentation/spec.md` currently specifies Devices as
`enabled→success` and Modules as `signed→success`. On a real machine almost
every device is enabled and almost every module is signed, so both lists render
as an unbroken wall of green — the "wall of green" the owner flagged during the
v0.6.0-beta.1 review.

Colour that is always present carries no information. Worse, it spends the
palette's strongest signal on the least interesting fact, so the one genuinely
notable row — a disabled device, an unsigned module — has to compete with a
hundred neighbours shouting the same hue. The rule that makes the security
banner and the availability notes calm (state of the world → information;
something the user should act on → warning) has not been applied to the
steady state of a list.

There is a second, quieter problem. "Normal" has three different answers in the
app today: Devices enabled and Modules signed take `success`, Updates
up-to-date takes `muted`, and Snapshots healthy takes no role at all. The spec
calls the Updates case "muted success", but `Role::Muted` renders as the dim
attribute with no hue (`tui/src/theme.cpp:43`), so that phrase describes
nothing that exists. One rule should cover all four.

`calm-backend-unavailability` established the calm principle for backends and
deliberately stopped there: its design report records the Devices colour
direction as a parked decision needing its own `enabled→success` amendment,
because changing it is a wording-and-palette decision for the owner rather than
something to slip into an availability change.

## What Changes

Settled with the owner on 2026-08-08:

- **Add a `Nominal` role to `docs/DESIGN.md` §4.1**, bringing the table to
  thirteen roles. In the TUI it renders as green plus the dim attribute
  (`color(Color::Green) | dim`). It means *verified normal* — a quiet
  affirmative, neither silence nor a shout.
- **Narrow `Success`** to what §4.1 already says it means: a completed
  operation. It stays on the status line for transient task outcomes and leaves
  resting row state entirely.
- **Apply `Nominal` to the normal state of all four collection views** —
  Devices enabled, Modules signed, Updates up-to-date, Snapshots healthy — so
  one rule holds across the app and the spec's "muted success" phrase becomes
  literally true for the first time.
- **Remap Devices disabled from `danger` to `muted`.** `DeviceStatus::Error`
  already maps to danger, so today a device the user deliberately turned off
  paints identically to one that faulted. Freeing red to mean only "something
  broke" is the same calm principle applied one row further down the table.
- Keep the non-colour signals exactly as they are. Glyph and state text already
  carry the state in MONO and PLAIN modes, and they remain the primary channel —
  this change re-weights a redundant signal, it does not remove the only one.
  `Theme::decorate` returns the identity decorator outside full-colour mode
  (`tui/src/theme.cpp:30`), so `Nominal` is invisible under `NO_COLOR` and
  `--ascii` with no extra work.
- Update the render tests that currently assert success colour on a normal row.

Accepted trade-off: Devices disabled and Devices unknown both land on `muted`,
so they are separable by glyph (`-` versus `?`) and by the state word, never by
colour. That is within what §10 requires.

Resulting table:

| View | Normal | Other states |
| --- | --- | --- |
| Devices | enabled → nominal | disabled → muted, transitioning → warning, error → danger, unknown → muted |
| Modules | signed → nominal | unsigned → danger, undetermined → muted, blacklisted → warning |
| Updates | up-to-date → nominal | available → information, failed install → danger |
| Snapshots | healthy → nominal | corrupt → danger, unsupported → warning; HEAD and last-good keep accent, which wins |
| Status line | — | success/warning/danger/information by task outcome, unchanged |

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `tui-presentation` — the role→ANSI mapping requirement gains `Nominal`; the
  per-view semantic colouring requirement's table is replaced wholesale, along
  with the scenario that asserts `enabled→success`.
- `ui-accessibility` — reconfirm that re-weighting the colour leaves every state
  identifiable from glyph and words, which is what the accessibility
  requirement already demands.

## Impact

Two corrections to this proposal's first draft. The per-view colour table lives
in `openspec/specs/tui-presentation/spec.md`, not `docs/DESIGN.md` §9 — §9 is
Cross-Surface Parity, and §4.1 is the role palette. And a grep of `gui/src`
finds no device-row colouring at all, so this is a TUI-only change; the GUI
needs no edit.

- `docs/DESIGN.md` §4.1 — the role table gains `Nominal`, and the TUI mapping
  bullet gains its ANSI treatment. Reference values for the GUI column still
  need choosing, since `dim` has no Qt equivalent; the cell is reserved and
  unused for now.
- `tui/src/semantics.hpp` — the `Role` enum.
- `tui/src/theme.cpp` — one `decorate()` case.
- `tui/src/state_roles.hpp` — all four mapping functions. `roleForSnapshotRow`
  changes shape slightly: it returns `Nominal` where it currently returns
  `nullopt` for a healthy non-marker row.
- `tui/src/tui_app.cpp` — audit for direct `Role::Success` uses that are really
  resting state.
- Tests: `tui/tests/test_theme.cpp`, `test_states_matrix.cpp` (its
  `kRoleColours` fixture already models a role as a `(foreground, dim)` pair, so
  the new role slots in), `test_selection_render.cpp`, `test_bars_render.cpp`,
  including `Scenario: Device states colored semantically`.

Risks the design must answer:

- **`dim` plus green may be indistinguishable from plain green on a 16-colour
  terminal.** The owner accepted this risk knowingly. If it cannot be told apart
  on a real terminal, the role choice is a dead end and the change stalls rather
  than shipping an invisible signal.
- **Selection inverts a row's semantic colour**
  (`tui/tests/test_selection_render.cpp:153`). Whether `dim` survives inverted
  video and stays readable needs checking on a real terminal, not only in a
  render test.
- **Snapshots gain paint they have never had.** Healthy rows become dim-green in
  a column that also carries cyan HEAD and last-good markers; the markers must
  stay unmissable.
- This is a visible change to the app's resting appearance. It wants the owner's
  eye on a real machine before it lands, not just a passing test.
