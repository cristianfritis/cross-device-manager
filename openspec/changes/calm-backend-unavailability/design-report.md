# Design report — calm-backend-unavailability

Status at close of the implementation session: groups 1–10 shipped, group 11
(owner manual matrix) and 12.1/12.3 outstanding. 717 host tests green, 718 in
the container; format gate clean at CI's clang-format-18; `openspec validate
--strict` passes.

## What shipped

**The two reported defects are fixed at their shared root.** Nothing owned the
answer to "what do we say, how loud, and where does the raw detail go, when a
backend is not there" — now `core::unavailabilityText()` owns the words,
`app::BackendStatusVM` owns the note, and both surfaces read that one accessor.

- **The D-Bus name is gone from the banner.** `availabilityCell()` concatenated
  `core::Error::message`, so `org.freedesktop.DBus.Error.ServiceUnknown: …`
  reached the GUI and TUI banners. The per-provider segment is now the shared
  sentence; the raw message survives as `BackendNote::diagnostic` and as one
  warn line per `(backend, kind)` transition.
- **`(no updates available)` no longer lies.** It renders only when every
  provider reported available and all returned zero candidates. A new
  `(checking for updates)` row covers the pre-report frame so §6.1's
  first-frame-never-empty rule still holds.
- **DKMS stopped putting a path in user-visible text.** Its availability error
  is now an honest diagnostic naming the root it actually looked for
  (`dkmsRoot_`, which is configurable — the old string hardcoded
  `/var/lib/dkms` and could therefore be wrong as well as leaky).
- **Both surfaces disclose rather than hide.** GUI: a `Details ▾` `QToolButton`
  with an accessible name, in the tab order, toggling an inline read-only
  region. TUI: global `i` toggling a `-- Diagnostics --` region, `Escape`
  closes, listed in the legend only while something is degraded.
- **Role is supplied, not parsed.** `modulesBannerRole()`'s substring match on
  `"will be rejected"` is deleted; `ModulesVM::bannerLine()` returns text and
  severity from one read of the system posture.

## Decisions amended during implementation

1. **The TUI diagnostics region is a muted header, not a box** (task 6.1,
   `tui-presentation` delta amended). §4.3 keeps borders for major regions; a
   subordinate reveal under the banner it explains is not one. `-- Diagnostics
   --` plus a rule, in the same family as the detail pane's `— Driver —`
   headers, and ASCII so PLAIN mode emits no non-ASCII byte.
2. **A loading row was added** (`backend-availability` delta amended).
   Suppressing the placeholder during initial load would otherwise have left
   the first frame blank, contradicting the very §6.1 clause that motivated the
   suppression. `(checking for updates)` is non-selectable and non-actionable.
3. **Task 6.4 dropped its "degraded Devices" render test.** Only the update
   providers are observed in this change; design.md deliberately leaves
   Devices/`snapshot-ui` adoption of `BackendStatusVM` optional, so a Devices
   degraded state does not exist to render yet.
4. **A shared test fixture holds the sentences**
   (`tests/fixtures/backend_sentences.hpp`). The GUI test binary links Qt and
   the TUI render binary links neither app nor core, so neither could assert
   against the wording table directly. Each surface asserts its rendered text
   against the fixture, and `tests/unit/test_backend_parity.cpp` asserts the
   fixture against `core::unavailabilityText()` — editing a sentence without
   editing the fixture fails one test in one place.

## Deliberately not done

- **The Devices and Snapshots views still use their own daemon-down handling.**
  This change makes them *able* to adopt `BackendStatusVM`; design.md's open
  questions leave that to a later change.
- **`fwupd` masked vs. absent still collapse** to one "not responding"
  sentence. The exact cause is one keystroke away in the diagnostic; splitting
  it is a row in the table if a user reports the collapsed wording as
  misleading.
- **The GUI did not adopt semantic colour.** Role rides on weight and the `?`
  glyph; DESIGN §9's GUI colour exception stands.

## Flagged: a pre-existing gap this change did NOT close

`tui-presentation`'s "Steady-state security is calm" scenario reads: Secure Boot
on and no operation blocked → the security banner renders as **information**.
The shipped implementation renders **warning** whenever `secureBoot ||
lockdown != none`, because `ModulesVM::banner()` appends "unsigned modules will
be rejected" in exactly that case and the old substring match warned on it.

This change swapped the *mechanism* only: the role is now supplied by the VM
with the same predicate, so the observable behaviour is byte-identical to
before. Closing the gap would mean either changing the scenario or making the
banner calm until a verb is actually blocked — the second needs UI-transient
"is an operation blocked right now" state that `ModulesVM` cannot compute
purely, which is precisely the cut-condition set for this group. It is
therefore left exactly as found, and is worth its own change with the owner's
input on which side should move.

## The gate that caught something

The container clang-tidy gate (10.3) had never run against the groups 1–2 code,
and it failed on `app/src/backend_status_vm.cpp`:

    const bool transition = !logged_.at(slot) || *logged_.at(slot) != kind;

`bugprone-unchecked-optional-access` — two separate `.at()` calls are two
separate optionals as far as the analyzer can prove, so the guarded dereference
reads as unchecked. Fixed by binding the slot once. Worth remembering that the
groups 1–2 commit went out with host gates only; the container tidy gate is the
one that sees this class of defect.

## Gate results

| Gate | Result |
| --- | --- |
| Host build `-j24` + full suite | 717/717, zero regressions (was 695 at group 2) |
| Container build + unit | 718/718 (one sysfs test skipped as designed) |
| `scripts/check-format.sh --container` | OK, 248 files clean (clang-format-18) |
| Container clang-tidy | clean after one fix — see below |
| `openspec validate --strict` | valid |
