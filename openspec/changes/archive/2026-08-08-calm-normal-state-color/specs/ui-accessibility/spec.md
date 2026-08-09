## MODIFIED Requirements

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
