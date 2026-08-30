# cli-inventory Specification

## Purpose

Daemon-free, read-only device inventory commands. The CLI reads directly from the
platform enumerator, provides stable human and structured output, and preserves the
existing exit-code contract.

## Requirements

### Requirement: Inventory verbs read through the platform backend, never a helper
The command-line tool SHALL provide read-only inventory verbs that list devices and show one device's detail, obtaining them from the platform device enumerator directly. These verbs SHALL NOT require the privileged helper, a message bus, elevation, or any running service, and SHALL be available on every platform that implements device enumeration.

#### Scenario: Listing works with no helper present
- **WHEN** the list verb runs on a system where the privileged helper is not installed or not running
- **THEN** devices are listed successfully and the exit code indicates success

#### Scenario: Inventory verbs are available wherever enumeration is
- **WHEN** the tool is built for a platform whose capability descriptor reports device enumeration as implemented
- **THEN** the inventory verbs are present in the usage text and are dispatchable

### Requirement: The helper connection is established only when a verb needs it
The tool SHALL NOT establish a connection to the privileged helper in order to parse arguments, print usage, or run a verb that does not use the helper. The connection SHALL be established only on the path of a verb that requires it.

#### Scenario: Usage costs nothing
- **WHEN** the tool is invoked with no arguments, with an unknown verb, or with a help request on a system with no message bus
- **THEN** it prints usage or the unknown-command message and exits without attempting any connection

#### Scenario: Inventory costs nothing
- **WHEN** an inventory verb runs on a system with no message bus
- **THEN** it completes normally and no connection attempt is made

### Requirement: Inventory output is stable and machine-readable on request
The list verb SHALL emit one device per line in a documented, column-stable human format on standard output, and SHALL emit a machine-readable structured form when the structured-output flag is given. The show verb SHALL emit one device's fields, including its property map, in the same two forms. Diagnostics SHALL go to standard error and SHALL NOT be interleaved into either output form.

#### Scenario: Structured output parses
- **WHEN** either inventory verb runs with the structured-output flag
- **THEN** standard output is a single well-formed structured document containing only device data, and every diagnostic appears on standard error

#### Scenario: Human output is stable across runs
- **WHEN** the list verb runs twice with an unchanged device set
- **THEN** the two outputs are identical, including ordering

#### Scenario: Device presentation matches the graphical surfaces
- **WHEN** a device's name and bus are rendered by an inventory verb
- **THEN** they use the same shared presentation helpers the other surfaces use, so the name and bus casing match what those surfaces show for the same device

### Requirement: Inventory verbs reuse the published exit-code contract
The inventory verbs SHALL use the tool's existing exit codes and SHALL NOT introduce new ones: success on success, the usage code for malformed arguments, and the not-found code when the show verb's argument matches no device. The helper-unreachable and authorization-denied codes SHALL be unreachable for these verbs.

#### Scenario: Unmatched device reports not-found
- **WHEN** the show verb is given an identifier matching no present device
- **THEN** it exits with the not-found code and writes an explanatory line to standard error

#### Scenario: Helper-related codes cannot occur
- **WHEN** any inventory verb runs under any system condition
- **THEN** its exit code is never the helper-unreachable code and never the authorization-denied code

### Requirement: Enumeration failure is reported as a failure, not as an empty inventory
When the device enumerator itself fails, the list verb SHALL report the failure on standard error and exit with the failure code. It SHALL NOT print an empty list and exit successfully, because a caller scripting the tool cannot distinguish those outcomes.

#### Scenario: Enumeration error is distinguishable from no devices
- **WHEN** enumeration fails
- **THEN** the tool exits with the failure code and standard output contains no device list; and when enumeration succeeds with no devices, it exits with success and standard output contains an empty list
