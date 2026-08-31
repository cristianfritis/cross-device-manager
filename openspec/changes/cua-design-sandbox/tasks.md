## 1. Prove the accessibility bridge (gate on D1)

**COMPLETE — gate passed 2026-08-30. Evidence: `spike-evidence/RESULT.md`.**

D1 chose the a11y tree over pixel+OCR, and the bridge was unproven on this stack
— on 2026-07-27 `get_window_state` returned only the top-level window on the
host. It is proven now: 305 nodes, 255 named, driven by element with no pointer.

- [x] 1.1 Build a throwaway image on top of the unit image adding `xvfb`,
      `at-spi2-core`, `dbus-x11`, `qt6-qpa-plugins`, and `tmux`
- [x] 1.2 Launch `devmgr-gui` inside it under `Xvfb` + Openbox and
      `dbus-run-session` with `QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1`, and confirm
      the process stays up. (`QT_ACCESSIBILITY=1` alone does NOT register it)
- [x] 1.3 ~~Install `cua-driver`~~ — DROPPED. The spike showed AT-SPI covers
      perception and input; the client is `python3-pyatspi` (D1)
- [x] 1.4 **GATE** — confirm the returned tree contains named, addressable child
      elements (toolbar actions, the device list, the filter), not just the
      top-level window. Record the raw tree in the change directory as evidence
- [x] 1.5 Stop-on-failure path — not triggered; 1.4 passed
- [x] 1.6 Confirm an element-driven action is delivered. `doAction` switched
      Devices → Modules and the toolbar recomposed, with no pointer, no
      `/dev/uinput`, and no foreground activation — the uinput risk does not bind
- [x] 1.7 Record the states finding: hidden actions stay in the tree with
      `SHOWING=False`, so assertions filter on showing state (D10)

## 2. Image and compose service

- [x] 2.1 Write `Dockerfile.design` layering the group-1 packages onto the unit
      image, so the build-and-test hot path is untouched (D9). Start from
      `spike-evidence/Dockerfile.spike`, which is known to work
- [x] 2.2 ~~Pin the driver~~ — N/A, driver dropped in D1; every input is an
      apt package pinned by the base image
- [x] 2.3 ~~Add the driver to `docs/REPRODUCIBILITY.md`~~ — N/A, driver dropped
      in D1; no network fetch at image build, so nothing new to pin
- [x] 2.4 Add a `design` service to `test/docker-compose.yml` whose default
      command runs the harness
- [x] 2.5 Note in the service comment that the compose file has no volume mount,
      so the image must be rebuilt for source changes to take effect

## 3. Posture fixture

- [x] 3.1 Write an entry-point wrapper that starts `dbus-run-session`, `Xvfb`,
      and the a11y bus, then runs a named posture
- [x] 3.2 Implement the `devmgrd` up / down posture by starting or not starting
      the daemon on the session bus
- [x] 3.3 fwupd present/absent posture. `tests/fwupd/devmgr_fake_fwupd` reuses
      `FakeFwupdDaemon` — the same double the `devmgr_fwupd` suite tests the real
      provider against — claimed on the private system bus with a fixed
      inventory. Healthy shows `1.0.0 -> 1.1.0`; degraded shows the fwupd
      unavailable sentence. Asserted by `test_fixtures.py` and by the
      posture-difference check
- [x] 3.4 Implement the DKMS present / missing posture by populating or omitting
      `/var/lib/dkms` and the modules root — no product seam, per D4
- [x] 3.5 Deterministic device set. `test/design/fixtures/devices.umockdev` (5
      devices) is loaded via `umockdev-run`, which replaces `/sys` for the
      wrapped process. Asserted to be exactly the fixture with no host leakage,
      and it makes §10.1's elision rule checkable: the 86-character name elides
      in the list row and is present in full in the detail pane
- [x] 3.6 For each of the four postures, assert the app's own rendered sentence
      confirms the intended state, so a posture that silently fails to apply is
      caught rather than producing a false pass

## 4. TUI capture

- [x] 4.1 Run `devmgr-tui` in a `tmux` pane at exactly `120x32`, `100x28`, and
      `80x24`
- [x] 4.2 Extract rendered text with `capture-pane` and compare as bytes
- [x] 4.3 Assert the status sentence is byte-identical under `NO_COLOR=1`, via
      `od -c`, satisfying the colour-independence scenario
- [x] 4.4 Source every expected string from the shared accessor the view renders
      from, never re-authored in the harness (D5)

## 5. GUI capture

- [x] 5.1 Run `devmgr-gui` under `Xvfb` at `1024x640` and at the `800x520`
      minimum
- [x] 5.2 Extract the accessibility tree per view and per size, filtering on
      `STATE_SHOWING` for every on-screen claim (D10)
- [x] 5.6 Assert the app is registered on the a11y bus before any check, so a
      dead bridge reports as a harness fault, not as an app with no controls
- [x] 5.3 Write a screenshot per step to the run artifacts, asserted on by
      nothing
- [x] 5.4 Assert every focusable list, tree, and filter carries an accessible
      name, and every toolbar action carries its visible text as its name (§10.1)
- [x] 5.5 Assert no primary control is missing from the tree at `800x520`

## 6. Assertions

- [x] 6.1 Implement the completeness assertion: an advertised string must be
      present in full. Do not implement a clipping detector — a clipped row and
      an exactly-fitting row are indistinguishable from the screen alone (F5)
- [x] 6.2 Implement the full sweep: every view × every size × healthy and
      degraded, on both surfaces. No size result may come from one view (F3)
- [x] 6.3 Implement cross-surface parity on rendered text: fail when one surface
      shows a sentence or verb the other omits (§9, F1)
- [x] 6.4 ~~Standalone "this string is nowhere on screen" assertion~~ —
      SUPERSEDED by 6.3. Parity is the absence claim that matters: it fails when
      a sentence one surface shows is missing from the other, which is exactly
      the F1 shape this task was written for. A free-floating "assert X is
      absent" has no requirement behind it in the spec and would need an authored
      string to name X, which D5 forbids
- [x] 6.5 Assert a shared sentence keeps the severity role its accessor carries —
      glyph, weight, and diagnostic disclosure (F2)
- [x] 6.6 Confirm the harness reports an uncovered requirement rather than
      proposing a product seam, when one cannot be verified from the environment

## 7. Prove the harness can fail (D7 replay)

- [x] 7.1 Create a worktree at `dcb25d4` (= `38eaa00^`). Chosen as the tree
      where F1/F2/F3 were believed live; 7.3 established that they were not, and
      that it carries the committed `tab-contextual-toolbar` defect instead
- [x] 7.2 Build it. If the current image's toolchain refuses, retry in an image
      contemporaneous with 2026-07-28
- [x] 7.3 Ran the harness against it. **It fails — 51 findings — but on the
      toolbar defect, not F1/F2/F3.** D7's premise was wrong: `git grep` shows
      `i=diagnostics` first appears in `38eaa00`, the same commit as the legend
      fitting, so F1-F3 were never a committed state. Amended in D7
- [x] 7.4 Evidence recorded in `spike-evidence/replay/` — `RESULT.md`, the 51
      findings, the pre-fix captures, and the empty HEAD result, naming `dcb25d4`
- [x] 7.5 Not triggered — the replay tree built and ran
- [x] 7.6 Confirm the harness PASSES on current `HEAD`, where all five are fixed

## 8. CI

- [x] 8.1 Add a `design-verification` job to `.github/workflows/ci.yml` with
      `continue-on-error: true` (D8)
- [x] 8.2 Upload the run artifacts — a11y trees, captured panes, screenshots — so
      a failure is diagnosable without a local reproduction
- [x] 8.3 Harness green on `HEAD` and red (51 findings) on `dcb25d4`, proving
      the checks fail on the tree under test. **The CI job itself has not run** —
      it is unexercised until pushed

## 9. Close-out

- [x] 9.1 No C++ added, so the format gate does not apply. `shellcheck` run over
      the new scripts
- [x] 9.2 Run `openspec validate --all --strict` and confirm it is green
- [x] 9.3 Coverage reported in `spike-evidence/COVERAGE.md`: of eleven matrix
      rows, six automated, one partial, three human by nature, one uncovered
- [x] 9.4 `docs/DESIGN.md` §12 updated: the harness is now §12.2
      *Running-application checks*, the manual matrix moved to §12.3, and each
      row that stays human says why it cannot be automated

## 10. Determinism fixtures (tasks 3.3 / 3.5 follow-through)

- [x] 10.1 Extend `FakeFwupdDaemon` with a bus selector defaulting to Session,
      so the existing `devmgr_fwupd` suite is bit-for-bit unaffected
- [x] 10.2 Add the `devmgr_fake_fwupd` target and a system-bus policy allowing
      the double to own `org.freedesktop.fwupd` on the harness's private bus
- [x] 10.3 Write `test_fixtures.py` and gate the sweep on it, so a posture that
      failed to apply is a harness fault rather than a wall of design failures
- [x] 10.4 Fix posture leakage found by the sweep: session teardown releases the
      backend names, and the degraded posture now asserts they are unowned
- [x] 10.5 One session per posture rather than per size — the per-size sessions
      raced on bus-name ownership
- [x] 10.6 Exclude the focused row from the colour comparison. FTXUI scrolls the
      focused row horizontally by a varying amount between launches (recorded
      2026-07-27 as pre-existing and out of scope), which carries no colour
      information; every other row is still compared line by line
