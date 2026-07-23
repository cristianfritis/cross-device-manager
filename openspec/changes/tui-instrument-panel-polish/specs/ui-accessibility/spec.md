# ui-accessibility Delta Specification

## ADDED Requirements

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
- **WHEN** an essential module is rendered in MONO or PLAIN
- **THEN** it is identifiable by its marker glyph and the word "Essential", with no color dependence
