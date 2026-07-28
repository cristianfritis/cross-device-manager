# Design report — calm-backend-unavailability

Status: groups 1–10 and 13 shipped; group 11 (owner manual matrix), 12.1's
container gates, and 12.3 outstanding. 745 host tests green; host format gate
clean over 249 files; `openspec validate --strict` passes. The container format
and clang-tidy gates have **not** been run against the group 13 code — the
Docker daemon is not running on this machine — and both must pass before push.

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

## Group 13 — `devmgrd` observation (scope correction, 2026-07-25)

Groups 1–10 wired only the update providers. `BackendId::Devmgrd` existed in the
table and in tests but nothing ever observed it, which left three delta
scenarios unimplemented and made task 11.1 untestable. Owner decision: wire it
rather than narrow the specs, against the owner's 11.1 misread catalog as the
acceptance spec.

- **Ownership sits on `ApplicationFacade`, not on a view.** `backendStatus()`
  exposes one `BackendStatusVM` per application, written through the single
  point `observeDaemonRead()` and read through `daemonAvailability()`. Three
  views each owning a copy would have logged the once-per-transition warn line
  three times per outage.
- **The scope grew mid-group, correctly.** The first pass wired Snapshots
  alone, on the reasoning that Snapshots is the only view `devmgrd` feeds. The
  catalog showed that reasoning was too narrow: the daemon also owns every
  mutation verb on Devices and Modules and the disabled-state overlay behind
  the Devices rows. Devices and Modules therefore carry the note too, withhold
  `(no devices)` / `(no modules)` while the daemon is unreachable, and gate
  their daemon-backed verbs — which is where the TUI's danger-loud
  `helper devmgrd is not available` actually lived.
- **Verbs stay visible and disabled**, with the shared sentence as the tooltip
  reason. A guard refusal outranks it as the more specific reason. The
  local-only History toggle is deliberately not gated.
- **`Could not refresh devices; showing the previous result.`** existed only as
  prose in `docs/DESIGN.md:376`; it is implemented now.
- **Liveness is re-observed on every unprivileged read**, and any successful
  call clears the note, so down→up→down produces the note twice instead of
  going quiet after the first recovery.
- **`(no matches)` still renders while degraded** — a filter message is about
  what the user typed, not about what the daemon knows.
- **Parity is now asserted through rendered surfaces.** `devmgrd` is checked on
  both frontends against real render output, not only through the wording
  table; `tests/unit/test_backend_parity.cpp` names which file closes which
  surface. TUI fixed-screen render tests at 120x32, 100x28 and 80x24 in FULL
  and MONO live in `tui/tests/test_backend_availability_render.cpp`.

Found by the suite, not by review: `ModulesVM::restoreSelection()` lacked the
empty-rows guard the other VMs already had. A withheld empty-state row can
leave the list genuinely empty, and `std::clamp(current, 0, -1)` is UB — the
GUI suite aborted.

## Group 14 — what the live matrix caught (2026-07-27)

The §11 matrix was run by driving the real GUI through `cua-driver` and the real
TUI through fixed-size `tmux` panes, on a machine that happened to already be in
a mixed-degraded posture. It passed on substance — the D-Bus name is gone, the
sentence is byte-exact across surfaces, the raw detail is demoted not deleted,
nothing is painted danger, no empty-state string lies — and found five defects
that every existing test agreed did not exist.

The through-line: **the tests were checking the parts, and the parts were
right.** `ModulesVM` returned the correct role; the GUI just never asked for it.
`DeviceListVM::availabilityNotes()` worked; the GUI had no widget to show it in.
The legend content was correct; nothing checked whether it fit. Each defect
lives in the seam between a component and its surface, which is exactly the
region unit tests do not cover and a running app does.

- **The GUI carried the note on two pages of four.** Devices had no banner
  widget at all; Modules had one, wired to the plain `banner()` string, so the
  role never arrived and the glyph, the weight and the disclosure were all
  missing. The TUI carried it fully on all three. Fixed by building the banner
  row once — `MainWindow::makeAvailabilityBanner()` — rather than hand-rolling
  it per page, which is what allowed a page to be wired with two thirds of it.
- **The degraded legend overflowed the minimum terminal.** Adding
  `i=diagnostics` took Devices from 74 to 89 columns and Snapshots from 81 to
  96. At 80x24 the key was cut mid-word and `q=quit` left the screen. Fixed by
  `views::fitLegend()`, which composes the legend to fit — spending typography
  (separator width, then abbreviations) before it will spend a shortcut, and
  never dropping the way out.
- **Two assertions could not fail.** `expectNoOverflow` read rows out of a
  fixed-width `ftxui::Screen` and asserted their width was within the terminal —
  true by construction. And the one test that rendered the degraded legend ran
  at 120x32 only, asserting `q=quit)` survived on the *healthy* renders. Fixed
  by asserting completeness instead of non-overflow, and by looping every size.

Worth keeping: the *first* replacement for the tautology was also wrong. It
tried to detect clipping from the last column's contents and immediately failed
the healthy 80-column Snapshots legend, which fits exactly. A clipped row and an
exactly-fitting row are indistinguishable from the rendered screen; only the
intended string can settle it.

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
3. **Task 6.4 dropped its "degraded Devices" render test** — *superseded by
   group 13.* At the time only the update providers were observed, so a Devices
   degraded state did not exist to render. Group 13 created one; the render
   test it asked for now exists in
   `tui/tests/test_backend_availability_render.cpp`.
4. **A shared test fixture holds the sentences**
   (`tests/fixtures/backend_sentences.hpp`). The GUI test binary links Qt and
   the TUI render binary links neither app nor core, so neither could assert
   against the wording table directly. Each surface asserts its rendered text
   against the fixture, and `tests/unit/test_backend_parity.cpp` asserts the
   fixture against `core::unavailabilityText()` — editing a sentence without
   editing the fixture fails one test in one place.
5. **Mutation failures never mark the daemon unavailable** (group 13). Only
   unprivileged reads — `listDisabledDevices`, `snapshotList` — can set the
   note. A mutation that fails may have been refused by a daemon that answered
   perfectly well (polkit denial, guard refusal), and reporting that as
   "service unavailable" would be a confident falsehood. Mutations only ever
   *clear* the note, on success.
6. **A null channel is not an outage** (group 13). It is a build without the
   privileged seam, so it reports no error and the existing degradation path is
   untouched.

## Deliberately not done

- **Catalog #16 is left as `docs/DESIGN.md` §6.1 specifies.** The owner's
  catalog expects a distinct `No devices match "x"` string on Devices; the §6.1
  table specifies `(no devices)` for both the empty-system and the
  no-filter-match state there (Modules and Snapshots do have distinct strings).
  That is a `med` row and a wording decision for the owner — not something to
  slip into an availability change.
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
| Host build `-j24` + full suite | 745/745, zero regressions (695 at group 2, 717 at group 10) |
| `scripts/check-format.sh` (host) | OK, 249 files clean (clang-format-22) |
| `openspec validate --strict` | valid |
| Container build + unit | **not run since group 10** (718/718 then) — Docker daemon down |
| `scripts/check-format.sh --container` | **not run since group 10** (248 files clean at clang-format-18 then) — Docker daemon down |
| Container clang-tidy | **not run since group 10** (clean after one fix — see below) — Docker daemon down |

The three container rows are the outstanding half of task 12.1. Host
clang-format is v22 and CI pins 18, so the host run is a smoke check, not
parity; the container run is the gate that counts before push.
