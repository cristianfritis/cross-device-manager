## Why

The GUI toolbar shows every verb the application has, on every tab, all the
time. Confirmed live on 2026-07-27 against the real host build: standing on
**Devices**, the toolbar offers `Create Snapshot`, `Restore Snapshot`,
`Diff Snapshot`, `Delete Snapshot`, `Install Update`, `Refresh Updates` and
`Dismiss Request` — fourteen buttons, of which five can act on the current
selection.

Two costs follow. The user scans fourteen affordances to find the two that
apply, on every tab, forever. And disabled-ness is doing double duty: a dimmed
`Install Update` on the Devices tab means "wrong tab", while a dimmed `Disable`
on the same tab means "this device cannot be disabled right now" — the same
visual signal for a permanent category error and a live, explainable refusal.
The `calm-backend-unavailability` change leaned on that second meaning heavily,
giving every daemon-backed verb a tooltip explaining why it is unavailable;
those tooltips are attached to buttons that are, on most tabs, simply not
applicable.

`docs/DESIGN.md` §5.3 and §10.1 already specify tab gating. This is the
implementation.

## What Changes

- The toolbar renders only the verbs belonging to the active tab, plus the
  verbs that are genuinely global.
- Disabled therefore recovers its single meaning: *this verb applies here and
  cannot run right now*, and its tooltip says why.
- Tab switches re-compose the toolbar rather than re-running enablement over a
  fixed button set.
- The TUI is already contextual — its per-view legend lists only that view's
  keys — so this change is about bringing the GUI to the parity the TUI
  already holds, not about inventing a new rule. One TUI gap closes with it:
  `d=dismiss` is advertised on Updates even when no dismissible request exists,
  which is a key with no object rather than a key being refused. Keys a guard
  would refuse stay listed and keep explaining themselves on the status line.

Deliberately **not** in scope: changing any verb's own enablement predicate,
its wording, or its keyboard shortcut. This change decides *which verbs are
present*, never *whether a present verb is enabled*.

## Capabilities

### New Capabilities

- `gui-presentation` — no product capability is new; this implements gating
  `docs/DESIGN.md` §5.3/§10.1 already specifies. The spec module is new only
  because the GUI's own presentation rules had no home: they were split between
  `ui-accessibility` and `docs/DESIGN.md`, with no requirement anywhere stating
  which verbs a tab shows.

### Modified Capabilities

- `tui-presentation` — a legend key with no object is unlisted; a key a guard
  would refuse stays listed.

## Impact

- `gui/src/main_window.cpp` / `.hpp` — toolbar construction order, per-tab
  action tagging, and one presentation function owning visibility, enablement,
  text and tooltip.
- `gui/tests/test_main_window.cpp` — per-tab assertions on which actions are
  present, that a verb absent from a tab is absent rather than disabled, order,
  separator hygiene, off-tab shortcut inertness, and visible-with-tooltip while
  the daemon is down. Existing `isEnabled()`-only assertions gain `isVisible()`.
- `tui/src/views/updates_view.cpp` and `tui/tests/test_updates_view_render.cpp`
  — the `d=dismiss` gate and its render tests at 120x32, 100x28 and 80x24.
- No VM, core, or daemon change: this is presentation only.
- Risk is low and contained to the frontends. The main hazard is a verb silently
  disappearing from every tab, which the per-tab presence tests exist to catch.
