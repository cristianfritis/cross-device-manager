## ADDED Requirements

### Requirement: One platform backend set, resolved in one place
The application SHALL obtain every platform-dependent implementation from a single backend set produced by one factory entry point declared in `core` and defined exactly once per platform target. Frontends and the application layer SHALL NOT name, include, or construct a platform-specific implementation type. Exactly one platform definition SHALL be linked into any binary.

#### Scenario: No frontend names a platform
- **WHEN** the GUI, TUI, or CLI entry point is compiled
- **THEN** it includes no header under a platform implementation directory and constructs no platform-specific type, obtaining all backends from the factory instead

#### Scenario: Adding a platform touches no frontend
- **WHEN** a new platform backend directory and its factory definition are added to the tree
- **THEN** no source file in `core`, `app`, `gui`, `tui`, or `cli` requires modification for that platform to be selectable

#### Scenario: Backend lifetime spans the process
- **WHEN** the factory returns a backend owner to an entry point
- **THEN** that owner keeps every backend alive until after all consumers have been shut down, and destruction order is the reverse of construction

### Requirement: An unimplemented capability is a refusing implementation, never a null
Every backend reference in the backend set SHALL be valid to call on every platform. A platform that supplies no real implementation for an interface SHALL be given an implementation whose every method returns a failure carrying `core::Error::Code::Unsupported`. Consumers SHALL NOT test a backend reference for nullity, and SHALL NOT require a platform check before calling one.

#### Scenario: Calling an unimplemented backend refuses rather than crashes
- **WHEN** any method of a backend that the running platform does not implement is called
- **THEN** it returns a failure whose code is `Unsupported`, performs no work, and does not crash, throw, or block

#### Scenario: Consumers contain no platform branches
- **WHEN** application-layer or frontend code invokes a backend
- **THEN** the call site contains no conditional on the running platform and no null check on the backend reference

### Requirement: Capabilities are declared, not discovered by attempting
The backend set SHALL be accompanied by a capability descriptor stating, per interface, whether the running platform supplies a real implementation. Presentation code that decides whether to offer a verb, populate a view, or show a tab's contents SHALL read that descriptor. It SHALL NOT determine capability by calling a backend and inspecting the failure.

#### Scenario: An unsupported verb is never offered
- **WHEN** the capability descriptor reports that device control is not implemented on the running platform
- **THEN** the enable/disable verb is not offered to the user, and no call to the device controller is made in order to reach that conclusion

#### Scenario: Descriptor and implementations agree
- **WHEN** the capability descriptor reports an interface as implemented
- **THEN** the corresponding backend in the set is a real implementation, and when it reports the interface as not implemented the backend is the refusing implementation

### Requirement: Layers above the PAL carry no platform vocabulary
Identifiers and user-visible strings in `core`, `app`, `gui`, `tui`, and `cli` SHALL NOT name a platform-specific mechanism. The shared device model's platform identity field SHALL be named and documented as an opaque, platform-native, stable identifier, and its primary hardware-matching string SHALL be named neutrally. Code above the PAL SHALL NOT assume a particular path separator when deriving a display or identity fragment from that identifier.

Existing published contracts are explicitly exempt and SHALL NOT be renamed: the D-Bus interface's member and argument names, and the persisted snapshot store's JSON keys. Each place where a neutral in-memory name is marshalled to or from a platform-named contract field SHALL carry a comment stating that the contract name is frozen.

#### Scenario: Identity fragment survives either separator
- **WHEN** a platform identity fragment is derived from an identifier that uses `\` as its separator
- **THEN** the derived fragment is the same as it would be for the equivalent `/`-separated identifier, and no empty or whole-string result is produced

#### Scenario: Published contracts are unchanged
- **WHEN** the D-Bus interface is introspected and a snapshot store written before this change is read
- **THEN** the interface version is unchanged, every argument name is as it was, and every stored record loads without migration

### Requirement: Device detail fields are a shared vocabulary owned above the PAL
Device facts that a surface displays SHALL be carried as a closed set of stable field identifiers defined in `core`, each with one product-facing label and a defined display order, both owned by `core`. A platform backend SHALL map its native property names into that set inside its own directory. A native platform property name SHALL NOT appear as a field identifier, as a label, or in any string a surface renders.

Surfaces SHALL render the fields present on a device in the shared order using the shared labels, and SHALL NOT author, reword, reorder, or abbreviate a label. A backend SHALL be free to populate a subset of the fields; a field it does not populate is simply absent.

The set of fields is extensible, but a new field SHALL be added to the shared vocabulary rather than passed through as a raw platform key.

#### Scenario: No native property name reaches a surface
- **WHEN** any device detail rendering is produced on any platform
- **THEN** no field identifier and no rendered label is a platform-native property name, and searching the rendered output for a platform property-key prefix finds nothing

#### Scenario: Two backends agree on one label
- **WHEN** two platform backends both populate the same detail field
- **THEN** each surface renders that field under the identical shared label, in the identical position relative to the other fields

#### Scenario: Native key mapping is contained
- **WHEN** the source tree is searched for a platform's native property-key identifiers
- **THEN** they appear only within that platform's own backend directory, and in no shared, application, or surface code

#### Scenario: A backend populating a subset renders correctly
- **WHEN** a backend populates only some of the shared detail fields
- **THEN** the populated fields render in shared order and the unpopulated ones are absent, with no gap, placeholder, or reordering

### Requirement: A supported platform declares a minimum operating-system version
Each platform backend SHALL declare a minimum supported operating-system version, and that declaration SHALL be enforced by the build and stated in user-facing documentation, not left implicit in the choice of a platform interface. A build targeting an older version SHALL fail at configure or compile time with a message naming the minimum, rather than producing a program that fails at load or at first use.

#### Scenario: Below-minimum target fails loudly at build time
- **WHEN** the platform backend is configured to target an operating-system version below its declared minimum
- **THEN** the build fails with a message naming the required minimum version, and no program is produced

#### Scenario: The minimum is documented, not inferred
- **WHEN** the supported-platform documentation is read
- **THEN** it states the minimum version for each supported platform, matching what the build enforces

### Requirement: The build gates targets on capability, not on operating system
Build inclusion of each target SHALL be conditioned on the capability it requires — a platform backend exists, a GUI toolkit is present, a privileged-channel transport is present — rather than on an operating-system test. The platform backend selection SHALL auto-detect from the host, SHALL be overridable, and SHALL accept a value meaning no platform backend.

#### Scenario: Configuring with no platform backend succeeds
- **WHEN** the project is configured with the platform backend selection set to none
- **THEN** configuration succeeds, the platform-independent libraries and their unit tests build and pass, and no user-facing program target is defined

#### Scenario: A GUI-capable non-Linux host builds the GUI
- **WHEN** the project is configured on a host that has a platform backend and the GUI toolkit but no privileged-channel transport
- **THEN** the GUI and the command-line tool are built, and the daemon and its transport-dependent tests are not

#### Scenario: Linux configurations are unchanged
- **WHEN** the project is configured on Linux in each of its existing configurations
- **THEN** the same set of targets is built as before this change, with the same options

### Requirement: A capability with no implementation degrades, it does not fail
When a view's backing capability is not implemented on the running platform, the view SHALL present the shared unavailability sentence for that backend rather than an empty result, an error dialog, or a crash. Views whose capabilities are implemented SHALL remain fully functional. The application SHALL start and remain usable when only device enumeration is implemented.

#### Scenario: Minimum viable platform starts and works
- **WHEN** a platform implements only device enumeration and system information
- **THEN** the application starts, the device view populates and is fully navigable, and every other view presents its unavailability sentence

#### Scenario: Unsupported is not an error state
- **WHEN** a view's capability is not implemented on the running platform
- **THEN** nothing in that view is presented in the danger role, no error dialog is raised, and no log entry is emitted at error severity
