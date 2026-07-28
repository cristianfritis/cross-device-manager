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
  already holds, not about inventing a new rule.

Deliberately **not** in scope: changing any verb's own enablement predicate,
its wording, or its keyboard shortcut. This change decides *which verbs are
present*, never *whether a present verb is enabled*.

## Capabilities

### New Capabilities

- None. This implements gating that `docs/DESIGN.md` §5.3/§10.1 already
  specifies.

### Modified Capabilities

- `gui-presentation` — toolbar composition becomes a function of the active
  tab.

## Impact

- `gui/src/main_window.cpp` — toolbar construction and the `currentChanged`
  handler.
- `gui/tests/test_main_window.cpp` — per-tab assertions on which actions are
  present, and that a verb absent from a tab is absent rather than disabled.
- No VM, core, or daemon change: this is presentation only.
- Risk is low and contained to the GUI. The main hazard is a verb silently
  disappearing from every tab, which the per-tab presence tests exist to catch.
