## 1. Core wording table

- [x] 1.1 Add `core/include/devmgr/core/backend_wording.hpp` with `BackendId { Devmgrd, Fwupd, Dkms }`, `UnavailabilityKind { Absent, Unreachable, NotPermitted, Unsupported }`, `kindFor(Error::Code)`, and `unavailabilityText(BackendId, UnavailabilityKind)`. `unavailabilityText` takes no error object and no string — the signature is what enforces the raw-detail ban (design D1).
- [x] 1.2 Write `tests/unit/test_backend_wording.cpp` first: assert the three table sentences byte-for-byte, assert `kindFor` maps `NotFound→Absent`, `Io→Unreachable`, `Permission→NotPermitted`, `Unsupported→Unsupported`, and assert every `(backend, kind)` pair returns a non-empty sentence containing no `org.freedesktop`, `DBus.Error`, `errno`, or `/`.
- [x] 1.3 Implement `core/src/backend_wording.cpp` with the table and a calm generic fallback naming the backend. Register the sources in `core/CMakeLists.txt` and the new test in the unit-test target.
- [x] 1.4 Run the new test alone and confirm it passes before any consumer exists.

## 2. Shared availability accessor

- [x] 2.1 Add `app/include/devmgr/app/backend_status_vm.hpp` with `BackendNote { backend, kind, text, diagnostic, role }` and `std::vector<BackendNote> notes() const`. Keep `text` and `diagnostic` as separate fields with no combined accessor (design D2).
- [x] 2.2 Write `tests/unit/test_backend_status_vm.cpp` first: `notes()` is empty when every backend is healthy; a degraded backend yields exactly one note; `text` equals the core table entry; `diagnostic` carries the raw error message verbatim; no note's `text` contains any substring of its own `diagnostic`.
- [x] 2.3 Implement the role mapping as a total function — `Unreachable`/`NotPermitted` → `Warning`, blocked-verb context → `Warning`, otherwise `Information` — with no `Danger` branch (design D3). Add a test that enumerates every `(kind, blockedVerb)` pair and asserts the result is never `Danger`.
- [x] 2.4 Implement `app/src/backend_status_vm.cpp`, wire it into `app/CMakeLists.txt`, and have it log the raw diagnostic at warn level once per `(backend, kind)` transition. Add a test that repeated identical observations produce one log line, not one per observation.

## 3. UpdatesVM — stop leaking, stop lying

- [ ] 3.1 Write the failing tests first in the updates VM test file: with a provider whose `availability.error->message` is `"org.freedesktop.DBus.Error.ServiceUnknown: The name ..."`, `banner()` contains none of `org.freedesktop`, `DBus.Error`, `ServiceUnknown`; and with that provider unreachable, the row set contains no `(no updates available)`.
- [ ] 3.2 Replace `availabilityCell()` (`app/src/updates_vm.cpp:71-76`) so the per-provider cell comes from the shared accessor and never concatenates `error->message`.
- [ ] 3.3 Gate `kPlaceholderRow` (`app/src/updates_vm.cpp:12`) on the whole-view predicate: emitted only when every provider reported available and all returned zero candidates (design D5).
- [ ] 3.4 Add the mixed-availability test: one provider available with candidates, one unreachable → candidate rows present, degraded note present, no empty-result string.
- [ ] 3.5 Add the genuinely-empty test: all providers available, zero candidates → `(no updates available)` present, no degraded note.
- [ ] 3.6 Confirm the per-provider notices path (`updates_vm.cpp:284`) still appends notices in provider order for available providers.

## 4. DKMS provider text

- [ ] 4.1 Change `platform/linux/src/dkms_status_provider.cpp:163` so the availability error message is treated as diagnostic only, and the presented sentence comes from the table. The `/var/lib/dkms` path stays in the diagnostic, never in the presented text.
- [ ] 4.2 Add or update a provider test asserting the presented text contains no `/`.

## 5. GUI — sentence visible, raw detail behind a disclosure

- [ ] 5.1 Point `updatesBannerLabel_` (`gui/src/main_window.cpp:635`, `:631`) at the shared accessor's `text`, and express the note's role through weight and iconography as the existing Warning-role handling does (`main_window.cpp:38`) — no color adoption, the §9 exception stands.
- [ ] 5.2 Add a `Details ▾` `QToolButton` on the banner row toggling an inline read-only region containing `diagnostic`. Give the button an accessible name and confirm it sits in the tab order. Render both only while a degraded note exists.
- [ ] 5.3 Add an offscreen GUI test: with fwupd unreachable, no primary widget's text matches `org\.freedesktop|DBus\.Error|ServiceUnknown|errno`, and `(no updates available)` is absent from the Updates page.
- [ ] 5.4 Extend that test: after activating the disclosure, the raw text is present in the revealed region — proving the detail is demoted, not deleted.
- [ ] 5.5 Add a keyboard-only test path: reach and toggle the disclosure using key events alone.

## 6. TUI — `i` diagnostics region

- [ ] 6.1 Bind `i` globally in `tui/src/tui_app.cpp` to toggle a bordered Diagnostics region; `Escape` also closes it. Do not touch `d` (bound at `tui_app.cpp:798` and `:837`).
- [ ] 6.2 Render each degraded backend's note as the `?` glyph plus the shared sentence in its own bounded region — not a full-bleed bar — with the role from the accessor.
- [ ] 6.3 Add the diagnostics key to the shortcut legend while any backend is degraded, keeping the single-legend and single-status-line rules intact.
- [ ] 6.4 Add fixed-screen render tests at 120x32, 100x28, and 80x24 in FULL and MONO: degraded Updates and degraded Devices render the sentence and glyph, no row overflows, and the note's role is never `Danger`.
- [ ] 6.5 Add a closed-state test at each size: with diagnostics closed, no rendered cell contains `org.freedesktop`, `DBus.Error`, `ServiceUnknown`, `errno`, or a filesystem path.
- [ ] 6.6 Add an open-state test at 80x24 with a long raw diagnostic: the region renders, the raw text elides, and no row overflows the terminal width.
- [ ] 6.7 Add a MONO/PLAIN test asserting the sentence is byte-identical to FULL mode and the state is identifiable by `?` plus the sentence with no color.

## 7. Banner role seam

- [ ] 7.1 Add role-carrying banner accessors for the banners this change touches, so text and role arrive together from the VM.
- [ ] 7.2 Remove the substring match in `modulesBannerRole()` (`tui/src/state_roles.hpp:204`) for those banners and read the VM-supplied role instead.
- [ ] 7.3 Add a test: changing a banner's wording while its underlying state is unchanged leaves the rendered role unchanged.
- [ ] 7.4 Confirm the existing "Steady-state security is calm" scenario still passes.

## 8. Cross-surface parity

- [ ] 8.1 Add a parity test asserting that for the same degraded backend, the string the GUI renders and the string the TUI renders are both equal to `notes()[i].text` — one accessor, no frontend rewording.
- [ ] 8.2 Assert the parity test covers `devmgrd`, `fwupd`, and `dkms`.

## 9. Docs

- [ ] 9.1 Add the source-unreachable row to the `docs/DESIGN.md` §6.1 shared-wording table. Cite §6's existing prose as the source of truth; do not rewrite it.
- [ ] 9.2 Document the TUI `i` diagnostics key wherever the TUI key set is listed for users.

## 10. Gates

- [ ] 10.1 Build with `-j24` and run the full test suite; record the pass count and confirm zero regressions.
- [ ] 10.2 Run `scripts/check-format.sh --container` before any push — host clang-format 21 and CI/container 18 diverge.
- [ ] 10.3 Run the container clang-tidy gate. Rebuild the container image first: docker-compose has no volume mount, so a stale image reports phantom results.
- [ ] 10.4 Run `openspec validate --strict` for this change and confirm it passes.

## 11. Manual verification — user-gated

- [ ] 11.1 **Owner runs:** stop `devmgrd`, open both surfaces. Confirm reads still work, the note is calm and bounded (not a red bar), the sentence matches on both surfaces, and the disclosure/`i` reveals the raw detail.
- [ ] 11.2 **Owner runs:** stop or mask `fwupd`, open the Updates tab on both surfaces. Confirm the translated sentence appears, `(no updates available)` does not, and no D-Bus name is visible until the diagnostic is opened.
- [ ] 11.3 **Owner runs:** repeat 11.1 and 11.2 with `NO_COLOR=1` and under a high-contrast GUI palette. Confirm the state is identifiable from glyph and words alone.
- [ ] 11.4 **Owner runs:** confirm the raw diagnostic appears in the log exactly once per transition, not once per poll, over a few minutes with the backend down.
- [ ] 11.5 Record the matrix results in a manual-test note in this change directory.

## 12. Close-out

- [ ] 12.1 Re-run all gates in section 10 after the manual matrix.
- [ ] 12.2 Write the design report recording what shipped, what was deferred, and any decision amended during implementation.
- [ ] 12.3 Confirm the three parked follow-ups are captured as their own changes before archive: `cua-design-sandbox`, `calm-normal-state-color`, `tab-contextual-toolbar`.
