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

### Requirement: Consistent device presentation
Device presentation cosmetics SHALL be unified across UIs: bus names rendered through one shared `displayBus()` helper (single casing everywhere), and the modalias cosmetic defect fixed so identity fields render consistently.

#### Scenario: Bus casing matches
- **WHEN** the same device is shown in TUI and GUI lists and details
- **THEN** the bus name uses identical casing and wording in all four places
