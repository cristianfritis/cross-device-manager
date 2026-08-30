# ui-accessibility Specification

## Purpose

Accessibility and consistency baseline for both frontends: keyboard-only operation, visible focus and accessible names, layout minimums with lossless elision, defined loading/empty/error states, and unified device presentation cosmetics.

## Requirements

### Requirement: Keyboard-only operation
Every user flow in both UIs (navigation, filtering, all verbs, confirmations, detail inspection) SHALL be completable with the keyboard alone. The GUI SHALL have a coherent tab order and shortcuts for tab switching and primary verbs; the TUI's existing hotkey set SHALL cover any newly added views.

#### Scenario: Restore without a pointer
- **WHEN** a user operates the GUI with keyboard only
- **THEN** they can reach the Snapshots tab, select a snapshot, open the restore preview, and confirm or cancel without a mouse

### Requirement: Focus visibility and accessible names
Keyboard focus SHALL be visibly indicated at all times in both UIs. GUI interactive controls SHALL carry accessible names usable by assistive technology; icon-only or ambiguous controls are not permitted without one.

#### Scenario: Focus never disappears
- **WHEN** a user tabs through every control on each GUI tab
- **THEN** the focused control is visually identifiable at every step and each control announces a meaningful name

### Requirement: Layout minimums and long-value handling
Both UIs SHALL remain usable at a defined minimum size (GUI minimum window size; TUI minimum terminal size, degrading gracefully below it). Long values (device names, snapshot reasons, file paths) SHALL elide in rows without data loss — the full value SHALL always be reachable in the detail surface.

That rule governs DATA rows, whose full value the detail surface holds. It SHALL NOT be applied to an explanatory row that stands in place of a list — the shared unavailability sentence a view shows when its backend is unimplemented, which no detail surface repeats. Such a row SHALL be rendered in full, wrapping within the width available to it, and SHALL NOT require horizontal scrolling or elision to be read. A surface that elides or clips that sentence has not stated why the view is empty; it has only claimed to.

#### Scenario: Long reason survives elision
- **WHEN** a snapshot reason exceeds the row width
- **THEN** the row elides it and the detail view shows the full text

#### Scenario: The unsupported sentence is readable without scrolling
- **WHEN** a view whose backend the platform does not implement renders its shared unavailability sentence in place of a list, in a column narrower than the sentence
- **THEN** the sentence wraps and is readable in full without horizontal scrolling, and it is neither elided nor clipped

#### Scenario: The wrapping exception does not reach data rows
- **WHEN** a data row in the same list exceeds the column width on a platform where the backend is implemented
- **THEN** that row still elides, and its full value is still reachable in the detail surface

### Requirement: Defined loading, empty, and error states
Every list and detail view in both UIs SHALL define all three states — loading, empty, and error — with the same wording across GUI and TUI per docs/DESIGN.md. Blank panels with no explanation are not permitted.

Raw backend detail SHALL NOT be presented as the explanation. Exception names, D-Bus names, errno values, and filesystem paths SHALL NOT appear in any primary widget or pane of either UI; the explanation SHALL be the shared translated sentence for that backend, with the raw detail preserved in the log and reachable through an expandable diagnostic affordance.

The empty state and the source-unreachable state SHALL be distinguishable and SHALL NOT co-occur. A view's empty-state string asserts a query that completed and returned nothing, so it SHALL NOT render while a source feeding that view is unreachable.

#### Scenario: Daemon down on any view
- **WHEN** any view loads while the backend it reads from is unreachable
- **THEN** both UIs show the same explanatory error state sourced from shared application state, not an empty list and not a blank panel

#### Scenario: Updates tab with fwupd unreachable
- **WHEN** the Updates view loads on a system where the `fwupd` service is not responding
- **THEN** both UIs show the shared translated sentence for `fwupd`, no primary widget or pane text matches `org\.freedesktop`, `DBus\.Error`, `ServiceUnknown`, or `errno`, and `(no updates available)` is not shown

#### Scenario: Empty is not confused with unreachable
- **WHEN** a view renders while one of its sources is unreachable
- **THEN** that view's empty-state string is absent from the render, and the degraded explanation is present instead

### Requirement: Color independence
No state SHALL be conveyed by color alone in either UI. The TUI SHALL honor `NO_COLOR`, `--no-color`/`--ascii`, and `TERM=dumb` by disabling color while keeping every state distinguishable through paired glyphs and text; the GUI SHALL continue to pair color with text or iconography under light, dark, and high-contrast palettes. Where two states share a single role — including disabled and unknown devices, which both take muted — they SHALL remain distinguishable by glyph and state word in every color mode, so that sharing a role never costs a distinction.

#### Scenario: TUI without color loses no meaning
- **WHEN** the TUI runs with `NO_COLOR` set
- **THEN** enabled/disabled/unavailable devices, signed/unsigned modules, and task outcomes remain distinguishable via glyphs and text

#### Scenario: States sharing a role stay distinct
- **WHEN** the Devices list renders a disabled device and an unknown device in full color mode, both carrying the muted role
- **THEN** they are told apart by their glyphs and by their state words, with no reliance on color

#### Scenario: Normal rows readable without their role
- **WHEN** a view renders normal rows in MONO or PLAIN, where the nominal role produces no color
- **THEN** the normal state is still identifiable from the row's glyph and state word alone

#### Scenario: Monochrome terminal
- **WHEN** the TUI runs on a terminal reporting `TERM=dumb`
- **THEN** the interface renders in plain ASCII with all states, focus, and selection still identifiable

#### Scenario: Active tab identifiable without color
- **WHEN** the tab bar is rendered in MONO or PLAIN
- **THEN** the active tab is identifiable by bold + an ASCII marker, with no color dependence

#### Scenario: Essential module identifiable without color
- **WHEN** an essential module is selected in MONO or PLAIN
- **THEN** it is identifiable by the marker glyph on its list row together with the criticality word in the detail pane on the same screen, with no color dependence

### Requirement: Consistent device presentation
Device presentation cosmetics SHALL be unified across every surface — graphical, terminal, and command-line — and SHALL be produced by the shared presentation helpers rather than reimplemented per surface. Bus names SHALL be rendered through the one shared bus-display helper, giving a single casing everywhere. Identity fields SHALL render consistently and SHALL NOT expose a platform-specific mechanism name to the user.

A surface SHALL NOT assume that any particular identity or descriptive property is present, because which properties a device carries depends on the platform backend that enumerated it. A property the backend did not supply SHALL be omitted from the rendering rather than shown as an empty, placeholder, or literal-unknown row, so that a device enumerated by a backend with fewer properties reads as a smaller correct record rather than a damaged one.

#### Scenario: Bus casing matches
- **WHEN** the same device is shown in TUI and GUI lists and details
- **THEN** the bus name uses identical casing and wording in all four places

#### Scenario: Command-line output matches the other surfaces
- **WHEN** a device's name and bus are rendered by a command-line inventory verb
- **THEN** they are byte-identical to what the graphical and terminal surfaces render for the same device

#### Scenario: Absent properties are omitted, not blanked
- **WHEN** a device's record lacks properties that devices on another platform carry
- **THEN** the detail rendering omits those rows entirely, and no row shows an empty value, a dash, or the word unknown in their place

#### Scenario: No platform mechanism names leak into the UI
- **WHEN** any device list row, detail pane, or command-line inventory line is rendered on any platform
- **THEN** no label or value names a platform-specific enumeration mechanism, and the identity field is presented under a platform-neutral label

### Requirement: Backend diagnostics are keyboard-reachable
The expandable diagnostic that carries raw backend detail SHALL be operable without a pointer on both surfaces, and SHALL carry an accessible name in the GUI. A hover-only affordance such as a bare tooltip is not sufficient, because it leaves the diagnostic unreachable for keyboard and assistive-technology users. The affordance SHALL be discoverable: labelled in the GUI, and present in the TUI's shortcut legend while a degraded backend is being reported.

#### Scenario: Diagnostic opens without a pointer
- **WHEN** a user operating either UI with the keyboard alone encounters a degraded backend note
- **THEN** they can reach and open the diagnostic affordance, read the raw detail, and close it again without a mouse

#### Scenario: Diagnostic affordance announces itself
- **WHEN** the GUI renders the diagnostic disclosure control
- **THEN** the control carries an accessible name describing what it reveals, and is reachable in the tab order

#### Scenario: TUI advertises the diagnostics key
- **WHEN** the TUI is reporting a degraded backend
- **THEN** the shortcut legend documents the diagnostics key, and the key toggles the diagnostics region open and closed
