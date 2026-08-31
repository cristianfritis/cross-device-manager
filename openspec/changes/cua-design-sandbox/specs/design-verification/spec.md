## ADDED Requirements

### Requirement: Verification targets the running application
Design verification SHALL exercise the built `devmgr-gui` and `devmgr-tui`
binaries as a user runs them, and SHALL NOT satisfy any of its assertions from a
ViewModel, an offscreen widget, or a fixed-width `ftxui::Screen`.

This tier is additive to `docs/DESIGN.md` §12.1. The offscreen Qt tests and the
fixed-screen FTXUI render tests remain required; a requirement covered here
SHALL NOT be used as grounds to delete coverage below it.

#### Scenario: Assertions come from the real surfaces
- **WHEN** the harness checks any design requirement
- **THEN** the observed value is read from the GUI's accessibility tree or from
  `tmux capture-pane` output of the running TUI, and never from a ViewModel
  accessor or an in-process render

#### Scenario: The lower tier is preserved
- **WHEN** the harness gains coverage of a behaviour that an existing offscreen
  or fixed-screen test also covers
- **THEN** the existing test remains in place

### Requirement: The GUI is read through its accessibility tree
The harness SHALL read GUI text, roles, and geometry from the AT-SPI
accessibility tree exposed by the running `devmgr-gui`, driving it by element
rather than by pixel coordinate. Screenshots SHALL be captured for every step
and retained as evidence, and SHALL NOT be the source of any assertion.

The verification environment SHALL provide the Qt accessibility bridge, a
session bus, and a real X display with window geometry, so that window-size
requirements are exercised against actual windows.

#### Scenario: Text assertions read the tree
- **WHEN** the harness asserts that a string is present on, or absent from, a
  GUI surface
- **THEN** the string is matched against the accessibility tree's text, and no
  optical character recognition is performed

#### Scenario: The bridge is absent
- **WHEN** the accessibility tree cannot be obtained for `devmgr-gui`
- **THEN** the run fails and reports the bridge as the cause, and SHALL NOT fall
  back to pixel-plus-OCR assertions

#### Scenario: Screenshots are evidence only
- **WHEN** a verification step completes, whether it passed or failed
- **THEN** its screenshot is written to the run's artifacts, and no assertion
  compares it to a reference image

#### Scenario: The application is not registered
- **WHEN** the running GUI has not registered on the accessibility bus
- **THEN** the run reports a harness fault and SHALL NOT report the app's
  controls as absent, so a misconfigured accessibility bridge is distinguishable
  from a genuinely empty surface

### Requirement: On-screen means showing, not present in the tree
Every assertion about what a user can see SHALL be evaluated against the
element's showing state. Membership in the accessibility tree SHALL NOT be
treated as evidence that a control is visible, and absence from the tree SHALL
NOT be required as evidence that it is hidden.

A control that is present but not showing SHALL be reported as not on screen.

#### Scenario: An off-tab verb is hidden
- **WHEN** a toolbar carries actions belonging to a tab that is not active, and
  those actions remain in the accessibility tree without a showing state
- **THEN** the harness reports them as not on screen, so a requirement that a
  verb be hidden rather than merely disabled can actually fail

#### Scenario: A disabled verb is still on screen
- **WHEN** a verb on the active tab is showing but not enabled
- **THEN** the harness reports it as on screen and disabled, distinguishing it
  from a hidden verb

### Requirement: The TUI is read from a fixed-size terminal
The harness SHALL run `devmgr-tui` in a terminal pane of an exact declared size
and read its rendered text back as bytes. It SHALL NOT introduce a driver for
the TUI.

#### Scenario: Rendered text is captured verbatim
- **WHEN** the harness inspects a TUI view
- **THEN** it captures the pane's text at the declared size and compares bytes,
  so that a truncated string differs from a complete one

#### Scenario: Colour is not required to carry meaning
- **WHEN** the TUI is captured with colour disabled
- **THEN** the status text is byte-identical to the coloured capture, satisfying
  `docs/DESIGN.md` §10's rule that state is never communicated by colour alone

### Requirement: Backend posture is reproducible
The harness SHALL place the backends into a chosen availability state — all
healthy, `devmgrd` unreachable, fwupd absent, DKMS missing — and SHALL do so by
controlling the verification environment rather than the host's incidental
condition.

Posture SHALL be produced without modifying product source. A requirement that
cannot be verified without a new seam in `gui/`, `tui/`, `app/`, `core/`, or
`platform/` SHALL be reported as an uncovered requirement, and SHALL NOT be
made verifiable by adding a test-only branch to a shipped binary.

#### Scenario: A degraded state is requested
- **WHEN** the harness is asked for the `devmgrd` unreachable posture
- **THEN** the run reaches that state deterministically, independent of whether
  the host machine happens to be degraded

#### Scenario: Posture needs a product seam
- **WHEN** a posture cannot be produced from the environment alone
- **THEN** the harness reports that requirement as uncovered and the product
  source is left unchanged

#### Scenario: The healthy posture is actually healthy
- **WHEN** the sweep runs the posture in which every backend is available
- **THEN** the surfaces show the firmware inventory that posture serves, and a
  run in which the service failed to start is reported as a fault rather than
  passing as a healthy result

#### Scenario: A posture does not leak into the next one
- **WHEN** more than one posture is swept in the same container
- **THEN** each posture's backends are released before the next begins, and a
  degraded run that can still reach a previous run's service fails

### Requirement: The device set is a fixture, not the host's
The harness SHALL enumerate a fixed device set that it supplies, and SHALL NOT
depend on the devices of the machine it runs on. The fixture SHALL contain a
device name long enough to elide in a list row and a device path deep enough to
exercise long-path handling, so that those requirements are checked on every
machine rather than only where such hardware happens to exist.

#### Scenario: The same devices everywhere
- **WHEN** the sweep runs on two different machines
- **THEN** both enumerate the same devices, and no device of either host appears

#### Scenario: A long value is elided but reachable
- **WHEN** a device name is too long for its list row
- **THEN** the row elides it and the detail pane presents it in full, per
  `docs/DESIGN.md` §10.1

#### Scenario: The fixture fails to apply
- **WHEN** the device fixture cannot be loaded and the host's own devices would
  be enumerated instead
- **THEN** the run reports a fault rather than sweeping the wrong device set

### Requirement: Completeness is the assertion, not absence of clipping
The harness SHALL verify that a string a view means to display is present in
full on the captured surface. It SHALL NOT infer truncation from the shape of a
row, the occupancy of a final column, or any other property that a
correctly-fitting row also exhibits.

Expected strings SHALL be derived from the same shared accessors the
application renders from, so that a wording change updates one place.

A control SHALL be identified by its role and its position relative to the
element it serves, never by its visible wording. Where a check needs a control
to exist rather than a string to match, it SHALL NOT name that control by label.

#### Scenario: A legend loses a key off-screen
- **WHEN** a view's legend does not fit the terminal width and a documented key
  is cut
- **THEN** the harness fails, naming the key that is missing in full

#### Scenario: A legend fits exactly
- **WHEN** a view's legend occupies the full terminal width with no character to
  spare
- **THEN** the harness passes, because every advertised string is present in
  full

#### Scenario: Wording changes in one place
- **WHEN** a displayed sentence is reworded in its shared accessor
- **THEN** the harness's expectation follows from that accessor rather than from
  a separately authored copy of the sentence

#### Scenario: A control's visible label differs from its accessible name
- **WHEN** a check requires that a control accompanying a message exists, and
  that control's visible label and accessible name differ
- **THEN** the check identifies it by role and adjacency and passes, rather than
  matching either label and reporting a control that is present as missing

### Requirement: Every surface is checked at every declared size
The harness SHALL sweep the full cross product of view, size, and posture. It
SHALL NOT report a size result derived from a single view.

TUI sizes SHALL be `120x32`, `100x28`, and `80x24`, matching
`docs/DESIGN.md` §12.1. GUI window sizes SHALL be `1024x640` and `800x520`,
matching §12.3 and the §10.1 minimum.

#### Scenario: A second view overflows unnoticed
- **WHEN** two views both exceed the terminal width and only one is inspected
- **THEN** the sweep still fails on both, because every view is captured at
  every size

#### Scenario: Minimum window size is exercised
- **WHEN** the GUI is verified at `800x520`
- **THEN** no primary control is missing from the accessibility tree, satisfying
  §10.1's rule that primary controls can never be squeezed off-screen

### Requirement: Cross-surface parity is checked on rendered text
The harness SHALL compare what the GUI and the TUI actually display for the same
shared state, and SHALL fail when one surface shows a fact, sentence, or verb
the other omits.

This implements the `docs/DESIGN.md` §9 invariant that visible nouns and verbs
match between surfaces, at the level of what is on screen rather than what the
ViewModels return.

#### Scenario: One surface omits an availability note
- **WHEN** a shared unavailability sentence is displayed on a TUI view and not
  on the corresponding GUI view
- **THEN** the harness fails, naming the view and the missing sentence

#### Scenario: A shared sentence loses its role on one surface
- **WHEN** a surface renders a shared sentence without the severity role its
  accessor carries, losing the glyph, weight, or diagnostic disclosure that the
  other surface shows
- **THEN** the harness fails, naming the surface and the dropped role

### Requirement: Accessible names are verified on the running GUI
The harness SHALL assert that every focusable list, tree, and filter in the
running GUI carries an accessible name, and that each toolbar action carries its
visible text as its name, per `docs/DESIGN.md` §10.1.

#### Scenario: A control has no accessible name
- **WHEN** a focusable control appears in the accessibility tree with an empty
  or absent name
- **THEN** the harness fails, naming the control and the view it appears on

### Requirement: The harness is proven able to fail
Before the harness is accepted, it SHALL be replayed against a tree in which at
least one known design defect was live, and SHALL fail on that defect. It SHALL
also pass against the tree in which the defect is fixed, so that its failures
are attributable to the tree under test rather than to the harness itself.

An assertion that has never been observed to fail SHALL be recorded as
*exercised but not proven*, and SHALL NOT be reported as proven coverage. Such
assertions remain required; the distinction is one of evidence, not of value.

The replay SHALL be recorded as evidence with the commit identified. If the
replay cannot be executed, it SHALL be reported as blocked rather than omitted.

#### Scenario: Replay against a tree with a known defect
- **WHEN** the harness runs against a historical commit carrying a design defect
  that was real, committed, and fixed later
- **THEN** it fails on that defect, and each failure names the surface and the
  element concerned

#### Scenario: The same harness passes on the current tree
- **WHEN** the harness runs against the tree in which that defect is fixed
- **THEN** it passes, so the failures are attributable to the tree under test
  rather than to the harness

#### Scenario: A defect that was never committed
- **WHEN** a defect the harness is designed to catch never existed in committed
  history, having been introduced and fixed within one commit
- **THEN** it SHALL NOT be claimed as replay-proven, and the harness's coverage
  of it SHALL be recorded as exercised but not observed failing

#### Scenario: Replay cannot be built
- **WHEN** the defect tree cannot be built in any available image
- **THEN** the replay is reported as blocked with the reason, and the harness is
  not recorded as proven

### Requirement: Verification runs the same way in CI and locally
The harness SHALL run from a single containerised entry point that produces the
same result on a developer machine and on CI, and SHALL NOT depend on the
operator's desktop session.

Its CI job SHALL be non-blocking on introduction, so that a flaky probe cannot
wedge the pipeline before the harness has a record. Making it blocking SHALL be
a deliberate later decision, not the result of omitting the setting.

#### Scenario: Same command both places
- **WHEN** a developer runs the harness locally and CI runs it
- **THEN** both invoke the same containerised entry point against the same image

#### Scenario: A probe is flaky
- **WHEN** the harness fails on CI while the job is non-blocking
- **THEN** the pipeline result is unaffected and the run's artifacts — a11y
  trees, captured panes, and screenshots — are retained for diagnosis

#### Scenario: The developer's desktop is untouched
- **WHEN** the harness runs on a developer machine
- **THEN** it drives its own display inside the container and does not activate
  or steal focus from the operator's windows

### Requirement: Scope is the Linux surfaces
This capability SHALL cover `devmgr-gui` and `devmgr-tui` on Linux. Windows
surfaces are out of scope and SHALL remain covered by the owner-run manual
acceptance gate. A future change MAY extend the capability to Windows; until it
does, the absence of Windows coverage here SHALL be read as a declared boundary
rather than a defect in the harness.

#### Scenario: Windows verification is requested
- **WHEN** verification of a Windows surface is sought
- **THEN** this capability does not provide it, and the omission is a declared
  scope boundary rather than a gap in the Linux harness
