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

#### Scenario: Long reason survives elision
- **WHEN** a snapshot reason exceeds the row width
- **THEN** the row elides it and the detail view shows the full text

### Requirement: Defined loading, empty, and error states
Every list and detail view in both UIs SHALL define all three states — loading, empty, and error — with the same wording across GUI and TUI per docs/DESIGN.md. Blank panels with no explanation are not permitted.

#### Scenario: Daemon down on Snapshots tab
- **WHEN** the Snapshots view loads while devmgrd is unreachable
- **THEN** both UIs show the same explanatory error state, not an empty list

### Requirement: Color independence
No state SHALL be conveyed by color alone in either UI. The TUI SHALL honor `NO_COLOR`, `--no-color`/`--ascii`, and `TERM=dumb` by disabling color while keeping every state distinguishable through paired glyphs and text; the GUI SHALL continue to pair color with text or iconography under light, dark, and high-contrast palettes.

#### Scenario: TUI without color loses no meaning
- **WHEN** the TUI runs with `NO_COLOR` set
- **THEN** enabled/disabled/unavailable devices, signed/unsigned modules, and task outcomes remain distinguishable via glyphs and text

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
Device presentation cosmetics SHALL be unified across UIs: bus names rendered through one shared `displayBus()` helper (single casing everywhere), and the modalias cosmetic defect fixed so identity fields render consistently.

#### Scenario: Bus casing matches
- **WHEN** the same device is shown in TUI and GUI lists and details
- **THEN** the bus name uses identical casing and wording in all four places
