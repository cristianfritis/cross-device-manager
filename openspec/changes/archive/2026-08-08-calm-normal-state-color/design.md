## Context

The TUI paints resting list rows with `success` green. On a real machine nearly
every device is enabled and nearly every module is signed, so two of the four
collection views render as a wall of green — flagged by the owner during the
v0.6.0-beta.1 review.

Underneath that complaint sits an inconsistency. "Normal" has three different
implementations today:

| View | Normal state | Role today | Renders as |
| --- | --- | --- | --- |
| Devices | `DeviceStatus::Active` | `Role::Success` | green |
| Modules | `ModuleSignature::Signed` | `Role::Success` | green |
| Updates | `UpdateRowState::UpToDate` | `Role::Muted` | dim, no hue |
| Snapshots | `SnapshotHealth::Ok`, no marker | `std::nullopt` | default foreground |

`openspec/specs/tui-presentation/spec.md` describes the Updates case as
"up-to-date→muted success", but `Theme::decorate` maps `Role::Muted` to the bare
`dim` attribute (`tui/src/theme.cpp:43`). There is no dim-green role, so that
phrase names something the codebase has never had.

The colour direction was parked deliberately by `calm-backend-unavailability`,
whose design report recorded it as needing its own amendment because the
decision belongs to the owner.

## Goals / Non-Goals

**Goals:**

- One rule for the resting state of every collection view.
- Keep a quiet affirmative on verified-normal rows rather than removing the
  signal entirely — the owner's explicit choice on 2026-08-08.
- Free the loud hues to mean departure from normal, with `danger` narrowed to
  actual faults.
- Make the spec's per-view colour table describe what the code does.

**Non-Goals:**

- No GUI change. A grep of `gui/src` finds no device-row colouring; the GUI's
  §4.1 colour exception already keeps it out of scope.
- No change to glyphs, state words, layout, or key routing. Only the role a
  state maps to moves.
- No change to the status line, the security banner, or the backend
  availability notes — `calm-backend-unavailability` settled those.
- No new colour depth. Still 16-colour ANSI through FTXUI decorators.

## Decisions

### D1: Add a `Nominal` role rather than reuse an existing one

Three options were on the table for what "normal" renders as.

*No colour at all* (Snapshots' current behaviour) is the strictest reading of
"colour marks departure from normal", costs zero new roles, and has working
precedent in the codebase. Rejected: the owner wants verified-normal to read
as affirmatively checked rather than merely unmarked.

*Reuse `Role::Muted`* matches the spec's existing "muted success" wording and
also costs zero new roles. Rejected on a concrete collision: `muted` is already
the role for `DeviceStatus::Unknown` and `ModuleSignature::Undetermined`. Making
it also mean "verified normal" would collapse *confirmed good* and *could not
determine* onto one paint — the two states a security-adjacent tool most needs
to keep apart. It also inverts hierarchy, since DESIGN §4.1 defines muted as
"secondary metadata": every device row would render quieter than its own bus
group header.

*A new role* was chosen. `docs/DESIGN.md` §4.1 grows from twelve entries to
thirteen. In the TUI it renders `color(Color::Green) | dim`.

### D2: Name it `Nominal`

`Nominal` is instrument-panel vocabulary and matches the register DESIGN §4
already sets ("a modern Linux instrument panel"). It reads as a state of a
system rather than as an evaluation, which is what distinguishes it from
`Success`. Alternatives considered: `SuccessMuted` (describes the rendering, not
the meaning — the sort of name that goes stale the moment the rendering
changes), `Steady` (collides conceptually with "steady state" as already used
for the security banner), `Healthy` (over-claims for Devices, where the role
means enabled, not diagnosed well).

### D3: `Success` narrows to transient task outcomes

DESIGN §4.1 already defines Success as "Completed operation, healthy result".
With `Nominal` carrying resting state, `Success` keeps only the first half: the
status line reporting an operation the user just performed. This is a
tightening of an existing definition, not a new one, and it is what makes the
change coherent rather than merely quieter — the loud green now fires on an
event instead of describing a condition.

### D4: All four views adopt `Nominal`, including Snapshots

Snapshots healthy rows currently take no role and would keep working untouched.
Applying `Nominal` there anyway buys the single-sentence rule and removes the
last of the three-way drift. Accent still wins for HEAD and last-good markers,
so the marker column's meaning is unchanged.

### D5: Devices `disabled` moves from `danger` to `muted`

`DeviceStatus::Error` already maps to `danger` (`state_roles.hpp:37`). With
`Disabled` also mapping there, a device the user deliberately turned off is
indistinguishable by colour from one that faulted. Once `Active` stops being
green, that red becomes the loudest thing in an ordinary list, describing a
state nobody needs to act on. Moving `Disabled` to `muted` leaves `danger`
meaning exactly one thing.

Accepted consequence: `Disabled` and `Unknown` now share `muted`. They stay
separable by glyph (`-` versus `?`, `state_roles.hpp:51,57`) and by the state
word, which is what the `ui-accessibility` colour-independence requirement
demands. The owner accepted this explicitly.

### D6: `roleForSnapshotRow` must separate healthy from structural

`state_roles.hpp:23` documents `nullopt` as covering "group header, placeholder,
out-of-range row". `roleForSnapshotRow` currently also returns `nullopt` for a
healthy non-marker row, overloading the same value with two meanings. Returning
`Nominal` for the healthy case separates them, and the `!health` early return at
line 94 keeps the structural case on `nullopt`. No signature change; the
overload just stops being ambiguous.

### D7: MONO and PLAIN need no work

`Theme::decorate` returns the identity decorator whenever the mode is not
`Full` (`tui/src/theme.cpp:30`), so `Nominal` is automatically invisible under
`NO_COLOR`, `--no-color`, `--ascii`, and `TERM=dumb`. The existing
degradation tests cover it once the role is added to their fixtures.

### D8: Incidental spec↔code correction

The current per-view requirement reads "Devices — enabled→success,
disabled→danger, unavailable→warning, unknown→muted". The code enum is
`Active / Disabled / Transitioning / Error / Unknown`; there is no
`unavailable`, `Transitioning` takes warning, and `Error` — which takes danger —
is absent from the spec entirely. The delta corrects the state names to match
the enum. This is pre-existing drift, not something this change introduces, but
leaving it would make the new table wrong on arrival.

## Risks / Trade-offs

- **`dim` + green may be indistinguishable from plain green on a 16-colour
  terminal** → No mitigation in code; this is a property of the terminal. The
  owner accepted the risk knowingly when choosing D1. Gate it behind the manual
  check in the migration plan: if the three paints cannot be told apart on a
  real terminal, the change **stalls** and D1 is reopened rather than shipping a
  signal nobody can see. Some terminals drop SGR 2 entirely.
- **Selection inverts a row's semantic colour** →
  `tui/tests/test_selection_render.cpp:153` records that pass 1 inverted the
  row's semantic colour. Whether `dim` survives inverted video and stays legible
  is a real-terminal question that a render test asserting pixel attributes
  cannot answer. Manual check required.
- **Snapshots gain paint they have never had** → Healthy rows become dim-green
  in a column that also carries cyan HEAD and last-good markers. Verify the
  markers remain unmissable against the new background of coloured rows; if not,
  D4 falls back to leaving Snapshots plain as a documented exception.
- **`Disabled` and `Unknown` share `muted`** → Accepted (D5). Glyph and state
  word carry the distinction. The `ui-accessibility` delta adds a scenario so
  this is asserted rather than assumed.
- **The wall of green may simply become a wall of dim green** → This is the
  honest limit of D1 versus the no-colour option. It is the owner's call and it
  is recorded as such.

## Migration Plan

Ordered so the source of truth moves first and every step is independently
verifiable:

1. `docs/DESIGN.md` §4.1 — add the `Nominal` row and its TUI ANSI treatment.
2. `tui/src/semantics.hpp` — add `Nominal` to `Role`.
3. `tui/src/theme.cpp` — add the `decorate()` case.
4. `tui/src/state_roles.hpp` — remap all four view functions.
5. `tui/src/tui_app.cpp` — audit direct `Role::Success` uses for resting state.
6. Tests — extend the role fixtures, then fix the assertions that expected green.
7. **Owner verification on a real terminal** (the gate; see Risks).

Rollback is a plain revert. No persisted state, no data format, no IPC surface,
and no CLI output is touched, so nothing is left behind by backing the change
out at any step.

## Open Questions

- **GUI reference values for `Nominal` in DESIGN §4.1.** `dim` has no Qt
  equivalent, and no other row in the table has an unused cell. Either pick a
  desaturated green pair for the light/dark columns, or establish a convention
  for a TUI-only role and apply it. The GUI does not colour rows today, so this
  blocks nothing but the table's completeness.
