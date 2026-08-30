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
