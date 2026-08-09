## MODIFIED Requirements

### Requirement: Consistent device presentation
Device presentation cosmetics SHALL be unified across every surface — graphical, terminal, and command-line — and SHALL be produced by the shared presentation helpers rather than reimplemented per surface. Bus names SHALL be rendered through the one shared bus-display helper, giving a single casing everywhere. Identity fields SHALL render consistently and SHALL NOT expose a platform-specific mechanism name to the user.

A surface SHALL NOT assume that any particular identity or descriptive property is present, because which properties a device carries depends on the platform backend that enumerated it. A property the backend did not supply SHALL be omitted from the rendering rather than shown as an empty, placeholder, or literal-unknown row, so that a device enumerated by a backend with fewer properties reads as a smaller correct record rather than a damaged one.

#### Scenario: Bus casing matches
- **WHEN** the same device is shown in the terminal and graphical lists and details
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
