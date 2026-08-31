## Context

`docs/DESIGN.md` §12 split validation in two when this change began. §12.1
*Automated checks* were offscreen Qt tests and fixed-size FTXUI render tests;
§12.2 *Manual matrix* was a human opening the app. Nothing verified a **running**
binary. This change adds that tier, and §12 now names it: the harness is §12.2
*Running-application checks*, and the manual matrix moved to §12.3.

That gap is measured, not theoretical. The `calm-backend-unavailability` §11
manual matrix (2026-07-27) found five defects in about an hour, and their shape
is the whole argument for this change:

- **F1** — the GUI Devices tab carried no availability note at all while the TUI
  Devices tab did. Both surfaces' unit tests passed; no test compares the two
  *running* surfaces.
- **F2** — `main_window.cpp:566` set the Modules banner from `modulesVm_.banner()`
  (plain string) instead of `bannerLine()` (text + severity), losing the glyph,
  the warning weight, and the `Details` disclosure. The ViewModel was correct, so
  the ViewModel tests passed.
- **F3** — the degraded Snapshots legend overflowed 80 columns and lost `q=quit`
  entirely.
- **F4** — the test that would have caught F3 pinned `const Size s{120, 32}` for
  the degraded case.
- **F5** — `expectNoOverflow` **could not fail**. It read rows out of a
  fixed-width `ftxui::Screen`, then asserted the row width was `<= s.w`. True by
  construction.

F4 and F5 are why F1–F3 survived: the automated tier was structurally blind. A
harness that reads a real terminal and a real window is not a nicer version of
those tests, it is the tier that can actually fail.

The matrix that found them was assembled ad-hoc and thrown away. Three changes
since (`tui-instrument-panel-polish`, `tab-contextual-toolbar`,
`windows-readonly-pal` §12.x) each hand-rolled their own matrix again.

## Goals / Non-Goals

**Goals:**

- Verify `docs/DESIGN.md` requirements against the running `devmgr-gui` and
  `devmgr-tui`, not against isolated render trees.
- Make degraded backend states reproducible. The 2026-07-27 matrix only worked
  because the host happened to be degraded already.
- Assert the things only a running app can assert: cross-surface parity on
  rendered text — which is where "this string is nowhere on screen" is actually
  exercised, since a sentence one surface shows and the other omits is precisely
  an absence claim — legend shortcut survival at the declared terminal sizes,
  accessible names on every focusable control, and a verb being hidden rather
  than merely disabled.
- Prove the harness can fail, by replaying it against a historical tree carrying
  a known committed design defect.
- Run identically in CI and on a developer machine.

**Non-Goals:**

- **Windows surfaces.** The container premise is central here and does not carry
  over; the Windows behavioural gate stays the owner-run manual §12.x matrix. A
  later change may extend the capability.
- **Replacing §12.1.** The offscreen and fixed-screen tests stay. This adds a
  tier above them; it does not license deleting the tier below.
- **Replacing the manual matrix (§12.3).** The harness supplements it; it does not
  retire it. Palette, display scale, true-colour and keyboard-only workflow
  judgement remain human. The harness takes the mechanical rows — sizes,
  shortcut survival, cross-surface text parity, accessible names — and
  `spike-evidence/COVERAGE.md` reports row by row which those are: of eight
  rows, two fully automated, two partial, three still human, one blocked on an
  open task. A green harness run is never grounds to skip the manual matrix.
- **Pixel-perfect comparison.** §12.1 already forbids "brittle full-window pixel
  comparisons tied to one desktop style". Screenshots are evidence, never the
  assertion.
- **Product source changes.** No `gui/`, `tui/`, `app/`, `core/`, or `platform/`
  edits. If a requirement cannot be verified without a new seam in product code,
  that is a finding to report, not a licence to add one.

## Decisions

### D1 — The GUI's text comes from the accessibility tree — PROVEN

The harness reads and drives `devmgr-gui` through its AT-SPI tree, by element.
Screenshots are captured every step as evidence artifacts and are never asserted
on.

**Status: proven by the task 1 spike on 2026-08-30**
(`spike-evidence/RESULT.md`). 305 nodes, 255 named; toolbar actions carry their
visible text as their name; a tab switch driven by `doAction` recomposed the
toolbar. Three mechanics the spike settled:

- **The bridge needs no Qt package.** Ubuntu 24.04 compiles the AT-SPI bridge
  into `libQt6Gui.so.6.4.2` rather than shipping a `plugins/accessiblebridge/`
  plugin. The image needs `at-spi2-core` and `dbus-x11`; an absent plugin
  directory is not evidence of a missing bridge.
- **`QT_ACCESSIBILITY=1` is the wrong knob for Qt 6** — with it alone the app
  never registers and `desktop.childCount` is 0. Use
  `QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1`, which is deterministic and needs no
  dconf. Flipping `org.a11y.Status` on the session bus also works; either alone
  suffices.
- **The client is `python3-pyatspi`, not `cua-driver`.** The spike showed AT-SPI
  covers perception *and* input: `queryAction().doAction(0)` is the focus-free
  path, needing no pointer, no `/dev/uinput`, and no foreground activation.
  Screenshots come from ImageMagick `import` against the Xvfb display. Both are
  `apt` packages in the same layer as the rest of the image, so the harness
  fetches and pins no third-party binary and adds no supply-chain surface to a
  build image.

The change keeps the name `cua-design-sandbox` for continuity with its proposal
and history; the driver it was named for is not used.

*Why:* the assertion set is textual — "`q=quit` is present in full", "the GUI
Devices tab shows the same sentence as the TUI Devices tab", "this string is
nowhere on screen". Pixels do not carry text. Further, `docs/DESIGN.md` §10.1
already requires that *"every focusable list, tree, and filter carries an
accessible name; toolbar actions carry their visible text as their name"* — a
requirement that is **only** checkable through the a11y tree. Choosing the tree
turns an unverified design rule into a tested one.

*Alternatives considered:*

- **Pixel + OCR.** Matches the measured host reality — on 2026-07-27
  `get_window_state` returned only the top-level window, so the run fell back to
  pixel coordinates with `delivery_mode:"foreground"`. Rejected: OCR is a new
  dependency and a new flakiness source, hardcoded coordinates break on every
  layout change, and §10.1's accessible-name rule stays unverifiable.
- **Hybrid, a11y with pixel fallback.** Rejected: two assertion vocabularies,
  and CI and the developer machine could silently take different paths — the
  precise failure mode that let F1–F3 through.
- **`cua-driver`, pinned.** Matches the proposal's name and the 2026-07-27
  method, and brings a tested input-escalation ladder. Rejected once the spike
  showed AT-SPI needs no ladder for element-driven work: its installer fetches
  and execs a second remote script, which puts a pinned third-party binary and a
  supply-chain review into a build image for capability the harness does not
  use.

### D2 — Xvfb plus a window manager, not `QT_QPA_PLATFORM=offscreen`

The GUI runs against `Xvfb` at a fixed size with **Openbox** as window manager,
under `QT_QPA_PLATFORM=xcb`.

*Why:* §12.3 requires checking `1024x640` and `800x520` windows, and §10.1
requires the `800x520` minimum enforcement. An offscreen platform plugin has no
window manager and no real geometry, so "no clipped primary controls at the
minimum window size" cannot be exercised. Bare Xvfb is not enough either — with
no WM there is nothing to honour window geometry, and X11 + Openbox is the
configuration the driver's Linux guide names as the proven AT-SPI baseline. The
image needs `qt6-qpa-plugins`; the beta-05 packaging work already established
that Qt's platform plugins are a separate package here.

### D3 — The TUI needs no driver

A fixed-size `tmux` pane plus `capture-pane`, exactly as the 2026-07-27 run did.

*Why:* it produces exact, greppable, byte-comparable text at `120x32`, `100x28`
and `80x24`, and it confirmed the sentence was byte-identical under `NO_COLOR=1`
via `od -c`. Adding a driver to this half would buy nothing and cost a second
failure mode.

### D4 — Posture is the container's filesystem and bus, not a product seam

The four postures — all healthy, `devmgrd` down, fwupd absent, DKMS missing —
are produced by controlling the container's environment:

| Posture input | Mechanism |
| --- | --- |
| `devmgrd` reachable / not | Start or do not start `devmgrd` under `dbus-run-session`. The unit container already runs none, so "down" is the default. |
| fwupd present / absent | Presence of an `org.freedesktop.fwupd` name on the bus, served by a stub. |
| DKMS present / missing | Populate or omit `/var/lib/dkms` and the modules root. |
| Device set | `umockdev`, already in the image. |

*Why:* `DkmsStatusProvider` is default-constructed on `/var/lib/dkms` at the
wiring site in `platform_backends_linux.cpp`, and there is no `getenv` seam
anywhere in `platform/linux/src/` or `app/src/`. In a container that path *is*
the fixture, so posture needs no env override and no product code change. The
"No product source changes" promise in the proposal holds.

*Alternative considered:* add `DEVMGR_DKMS_ROOT`-style env overrides. Rejected —
it is a product change made solely for the test harness, and it would put a
test-only branch in a shipped binary.

### D5 — Assert completeness, never absence of clipping

The property the harness checks is: **a string the view means to show is present
in full on screen.** It never tries to detect truncation by inspecting a row's
shape.

*Why:* this is F5's lesson, and the first attempt at fixing F5 got it wrong. A
clipping *detector* — flag a row whose last column holds ordinary text —
immediately failed the healthy 80-column Snapshots legend, which fits exactly.
A clipped row and an exactly-fitting one are indistinguishable from the screen
alone. Completeness is decidable; clipping is not.

The harness therefore takes its expected strings from the ViewModel/legend
source of truth and asserts their presence in the captured surface.

**The harness violated this rule once, and it is worth recording.** The first
severity-role check asserted the presence of a control literally named
`Details`, and produced four failures against a `HEAD` where the control exists:
its *visual* label is `Details ▾`, but its accessible name is `Backend
diagnostics`. An authored expected string had been smuggled into a check whose
whole purpose is to avoid them. It was replaced with structural identification —
the control is found by role and by adjacency to the banner, never by its
wording — which is both correct and robust to the label changing.

### D6 — Sweep every tab at every size

No assertion is made from a single tab. F3 was under-reported precisely because
the live check happened to be standing on Snapshots; Devices overflowed too (89
columns degraded, 74 healthy) and went unnoticed. A single-tab spot check is not
a size check.

The matrix is the cross product: {Devices, Modules, Updates, Snapshots} ×
{`120x32`, `100x28`, `80x24`} × {healthy, degraded} for the TUI, and
{Devices, Modules, Updates, Snapshots} × {`1024x640`, `800x520`} ×
{healthy, degraded} for the GUI.

### D7 — Acceptance is a replay against `dcb25d4` — AMENDED

The proposal's acceptance criterion — "fail before the fix, pass after" — is
unreproducible as written because all five defects are fixed. It is satisfied
instead by a one-time replay against `dcb25d4` (= `38eaa00^`), recorded as
evidence.

**Amended 2026-08-30 after running it** (`spike-evidence/replay/RESULT.md`). The
original wording named F1, F2 and F3 as the replay targets on the strength of
`dcb25d4` lacking their *fixes*. That inference was wrong: absence of a fix is
not presence of a defect when the same commit introduced the feature. `git grep`
shows `i=diagnostics` — the addition that pushed the Snapshots legend to 96
columns — first appears in `38eaa00`, the very commit that added the legend
fitting. **F1–F3 were never a committed state**; they lived only in the working
tree on 2026-07-27 between the feature landing and the manual matrix catching
them.

That is not a weakness of the rationale, it sharpens it: those defects were
caught by a human driving a pre-commit tree and by nothing else, which is
precisely the tree a developer would point this harness at.

What the replay does prove is stronger than a synthetic fixture: against
`dcb25d4` the harness produced **51 failures** on the toolbar showing every
tab's verbs on every tab — a real committed defect, recorded in the 2026-07-27
manual note under *Observed, out of scope*, live until `tab-contextual-toolbar`
fixed it on 2026-08-08, and caught by no automated test in that window. Against
`HEAD` the same harness passes. The assertions can fail, and they fail on the
tree under test rather than at random.

That check is only *able* to fail because of D10: the off-tab actions sit in the
accessibility tree in both the broken and the fixed build, so a membership-based
check would have passed both.

Honest limit: the checks aimed at F1–F3 (cross-surface parity, legend key
preservation, severity role) are exercised on `HEAD` but have not been observed
failing on a historical tree, because no such tree exists.

*Alternatives considered:*

- **A standing known-bad fixture** asserted on every run. Rejected: synthetic
  defects drift from the real ones and the fixture is maintained forever.
- **Pass-on-HEAD only.** Rejected: nothing would prove the assertions can fail,
  which is exactly the F5 flaw being corrected.

### D8 — CI job is non-blocking first

A new `design-verification` job runs on the same triggers as `build-and-test`
with `continue-on-error: true`.

*Why:* a flaky UI probe must not wedge merges before the harness has a record.
Flipping it to blocking is a later, deliberate decision with green runs behind
it — not something that happens by omission.

**Status: the job is written but has never executed on a GitHub runner.** It is
unexercised until the branch is pushed. Everything claimed about the harness's
behaviour comes from local container runs; nothing here asserts how it behaves
on hosted CI, where the device list, timing, and screen stack all differ.

### D9 — Separate image, all inputs from apt

A new `Dockerfile.design` layered on the unit image, rather than growing the
unit image itself. Every added input is an Ubuntu package.

*Why:* the unit image is on the build-and-test hot path for every push; adding
Xvfb, Openbox, at-spi2-core, tmux, pyatspi and ImageMagick to it taxes every CI
run for a non-blocking job. Keeping every input on `apt` means the image adds no
network fetch at build and nothing new to `docs/REPRODUCIBILITY.md`'s
pinned-inputs table — the packages are pinned by the base image the same way the
existing toolchain is. That was the deciding practical difference against
`cua-driver` in D1.

### D10 — Visibility is a state, not tree membership

Every assertion about what is on screen filters on `STATE_SHOWING`. Presence in
the accessibility tree is never treated as evidence that a control is visible.

*Why:* the spike found the toolbar exposes all 24 children on every tab, with
the off-tab verbs carrying `SHOWING=False, VISIBLE=False`. A harness asserting
on tree membership would pass `tab-contextual-toolbar`'s central requirement —
that an off-tab verb is *hidden*, not merely disabled — no matter what the code
did. That is an assertion that cannot fail, the same defect class as F5, and it
would have been baked in silently had the spike not dumped states.

This is the general form of D5: for every property the harness checks, prefer
the observation that can distinguish the passing case from the failing one, and
verify it can.

## Risks / Trade-offs

- ~~**The Qt AT-SPI bridge may not come up in the container**~~ → **RESOLVED**
  by the task 1 spike, 2026-08-30: 305 nodes, 255 named. See D1.
- ~~**`/dev/uinput` is absent, so synthetic input may not land**~~ → **DOES NOT
  APPLY.** That limit binds the MPX *pixel* path, which D1 rejects. The AT-SPI
  `doAction` path the harness uses is focus-free and needs no pointer device;
  the spike drove a tab switch with no uinput and no foreground activation.
- **The a11y bridge is off by default and fails silently** → with the wrong env
  knob the app runs normally and simply never registers, so a misconfigured run
  looks like an app with no controls rather than an error. The harness asserts
  the app is registered on the a11y bus before running any check, so this
  reports as a harness fault rather than a wall of false failures.
- **A UI harness is a flakiness source** → non-blocking first (D8), every run
  captures screenshots plus the raw a11y tree and `capture-pane` output as
  artifacts, and any retry is recorded rather than hidden.
- **Golden text drifts as wording changes** → expected strings are read from the
  same shared accessors the app uses, not re-authored in the harness. A wording
  change updates one place. This is the D5 discipline applied to maintenance.
- **The replay build may not compile on a current toolchain** → `dcb25d4` is from
  2026-07-28 and the image toolchain has moved. The replay runs in the image
  contemporaneous with that commit if the current one refuses; if neither works,
  the replay is reported as blocked rather than quietly dropped.
- **Scope pressure toward Windows** → named as a non-goal above so a later
  reader does not read the Linux-only capability as an oversight.

## Migration Plan

Additive. No product code changes, no existing gate altered.

1. `Dockerfile.design` + a `design` service in `test/docker-compose.yml`.
2. Harness under `test/design/`, runnable as
   `docker compose -f test/docker-compose.yml run --rm design`.
3. CI job added `continue-on-error: true`.
4. Replay against `dcb25d4` run once; evidence recorded in the change.

Rollback: delete the service, the directory, and the CI job. Nothing else
depends on them.

## Open Questions

- When does the CI job flip to blocking? Deliberately deferred — it needs a
  record of green runs first, and that decision belongs to whoever has it.
- ~~Should `docs/DESIGN.md` §12.1/§12.2 be edited to name the new tier?~~
  **RESOLVED 2026-08-31.** §12 now carries the harness as §12.2
  *Running-application checks*, the manual matrix moved to §12.3, and each row
  that stays human says why it cannot be automated. The rows the harness now
  covers are listed there as no longer requiring a manual pass.
