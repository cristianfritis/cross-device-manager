## ADDED Requirements

### Requirement: Windows 10 version 1607 is the declared minimum
The Windows backend SHALL declare Windows 10 version 1607 as its minimum supported operating-system version. The declaration SHALL be enforced by the Windows build configuration so that targeting an earlier version fails at build time with a message naming the minimum, and SHALL be stated in the project's supported-platform and compatibility documentation. Earlier Windows releases are formally unsupported.

The floor exists because the hotplug notification interface this backend requires is unavailable before that version; the backend SHALL NOT work around it by falling back to an interface with weaker delivery or shutdown guarantees.

The graphical surface SHALL declare a separate, higher minimum of Windows 10 version 1809, because that is the minimum its toolkit officially supports. The supported-platform documentation SHALL state both numbers and SHALL state that between them the command-line surface is supported and the graphical one is not. A surface SHALL NOT be declared supported on a version its toolkit does not support, even where it would run.

#### Scenario: Build refuses an earlier target
- **WHEN** the Windows backend is configured to target a Windows version earlier than 10 version 1607
- **THEN** the build fails with a message naming the required minimum, and no program is produced

#### Scenario: The floor is documented as a support statement
- **WHEN** the supported-platform and compatibility documentation is read
- **THEN** it states Windows 10 version 1607 as the minimum for the backend and command-line surface, Windows 10 version 1809 as the minimum for the graphical surface, and that earlier releases are unsupported

#### Scenario: The graphical surface is not claimed below its toolkit's floor
- **WHEN** a Windows version at or above 1607 but below 1809 is considered
- **THEN** the command-line surface is stated as supported and the graphical surface is stated as unsupported, rather than the graphical surface being claimed on the strength of it happening to run

#### Scenario: No weaker fallback is introduced
- **WHEN** the Windows backend's hotplug implementation is examined
- **THEN** it uses the notification interface requiring the declared minimum, with no alternative path for earlier versions

### Requirement: Windows supplies a read-only backend set
The Windows platform backend SHALL implement device enumeration, hotplug notification, and system information, and SHALL NOT implement device control, driver management, the privileged channel, update providers, or criticality probing. The unimplemented interfaces SHALL be supplied as refusing implementations and SHALL be reported as unimplemented by the capability descriptor.

#### Scenario: Capability descriptor reflects the read-only set
- **WHEN** the backend set is created on Windows
- **THEN** the descriptor reports device enumeration, hotplug, and system information as implemented, and reports device control, driver management, privileged channel, update providers, and criticality probing as not implemented

#### Scenario: No mutation is reachable
- **WHEN** the application runs on Windows
- **THEN** no code path can enable, disable, bind, unbind, load, unload, install, or snapshot, and any such call returns `Unsupported` without contacting the operating system

### Requirement: Device identity is the device instance identifier
A Windows device's platform identity SHALL be its device instance identifier, stored unmodified in the shared device model's platform identity field. It SHALL be stable across enumerations for as long as the device remains attached in the same location, and SHALL be the value the application uses to correlate a hotplug event with an enumerated device.

#### Scenario: Identity round-trips
- **WHEN** a device is enumerated twice without being physically moved
- **THEN** its platform identity field is byte-identical in both results

#### Scenario: Identity is not reinterpreted
- **WHEN** a device instance identifier is stored in the model
- **THEN** it is stored verbatim, with no case change, separator substitution, prefix stripping, or truncation

### Requirement: Windows device properties map onto the shared model
The enumerator SHALL populate the shared device model from Windows device properties as follows: the display name from the friendly name when present and the device description otherwise; the vendor from the manufacturer property; the primary hardware-matching string from the first, most specific hardware identifier.

A property that Windows does not report for a device SHALL be left unset rather than filled with a placeholder, an empty-looking sentinel, or a guess.

#### Scenario: Friendly name preferred, description as fallback
- **WHEN** a device reports both a friendly name and a device description
- **THEN** the display name is the friendly name; and when it reports only a device description, the display name is the device description

#### Scenario: Missing properties stay absent
- **WHEN** a device reports no manufacturer and no bound driver
- **THEN** the vendor field is empty, no driver detail field is populated, and the detail view omits those rows rather than showing blank ones

### Requirement: Native Windows property identifiers do not leave the Windows backend
The Windows backend SHALL map its native property identifiers onto the shared device detail field vocabulary, and SHALL populate at minimum Manufacturer, Driver, Driver Version, Driver Provider, Driver Date, Class, Hardware IDs, and Device Instance ID where Windows reports them. The Hardware IDs field SHALL carry the complete reported list, in its reported order.

The native property-identifier constants and the mapping table SHALL exist only within the Windows backend directory. No native identifier SHALL appear in a shared, application, or surface layer, in a field identifier, in a rendered label, or in any user-visible string. Surfaces SHALL render these fields under the shared labels and in the shared order, and SHALL author no Windows-specific label.

#### Scenario: A user never sees a native property identifier
- **WHEN** a Windows device's detail is rendered on any surface, or emitted by a command-line inventory verb
- **THEN** every field is labelled with its shared product-facing label, and no rendered text contains a native Windows property-identifier prefix

#### Scenario: The mapping is contained in the backend
- **WHEN** the source tree outside the Windows backend directory is searched for native Windows property-identifier constants
- **THEN** none is found

#### Scenario: Hardware identifier list is complete and ordered
- **WHEN** a device reports several hardware identifiers
- **THEN** the primary hardware-matching field holds the first one and the Hardware IDs detail field holds the complete list in its reported order

#### Scenario: Device Instance ID is presented under its shared label
- **WHEN** a Windows device's detail is rendered
- **THEN** its platform identity appears as the Device Instance ID field under that shared label, and not under a label naming an enumeration mechanism

### Requirement: Windows device status maps into the shared status taxonomy
A Windows device's operational state, including any reported problem condition, SHALL be mapped into the existing shared device-status taxonomy that the surfaces already colour and label. It SHALL NOT be exposed as a free-text status detail field, and no Windows-specific status wording SHALL be introduced.

#### Scenario: A problem condition uses the shared taxonomy
- **WHEN** a Windows device reports a problem condition
- **THEN** its status is one of the shared taxonomy's values, is coloured and labelled by the same shared mapping every other platform uses, and no additional status row appears in its detail

#### Scenario: No second notion of status exists
- **WHEN** a Windows device's detail fields are examined
- **THEN** none of them is a status field, because status is carried by the shared model's status value alone

### Requirement: Bus classification reuses the shared taxonomy
Windows devices SHALL be classified into the existing shared bus taxonomy by device instance identifier prefix, with no new enumerator values introduced. USB-enumerated devices SHALL classify as USB, PCI-enumerated devices as PCI, ACPI- and root-enumerated devices as the platform bus, and every other prefix as other. The raw prefix SHALL be preserved in the property map.

#### Scenario: Known prefixes classify
- **WHEN** devices with USB, PCI, ACPI, and root prefixes are enumerated
- **THEN** they classify as USB, PCI, platform, and platform respectively, and each carries its raw prefix in the property map

#### Scenario: Unknown prefix does not invent a bus
- **WHEN** a device carries a prefix with no mapping
- **THEN** it classifies as other, its raw prefix is preserved, and the shared bus display helper renders it exactly as it renders other on every other platform

### Requirement: Hotplug delivers arrivals and removals without a message pump
The Windows hotplug monitor SHALL deliver device arrival and removal events without requiring the host process to own a window or run a message pump, so that it functions identically in a graphical program and a console program. Each event SHALL carry the affected device's platform identity.

#### Scenario: Console program receives events
- **WHEN** hotplug monitoring is started from a program with no window and no message loop and a device is attached
- **THEN** an arrival event is delivered carrying that device's platform identity

#### Scenario: Removal is reported
- **WHEN** a monitored device is detached
- **THEN** a removal event is delivered carrying the same platform identity that its arrival carried

### Requirement: Windows hotplug shutdown is safe against in-flight callbacks
The Windows hotplug monitor SHALL satisfy the monitor interface's shutdown contract: stopping SHALL block until no callback is executing and SHALL guarantee that no callback fires afterwards. Stopping SHALL NOT be initiated from within a callback, and the implementation SHALL neither deadlock nor access freed state under any interleaving of callback delivery and shutdown.

#### Scenario: Stop during an in-flight callback
- **WHEN** shutdown begins while a callback is executing
- **THEN** shutdown blocks until that callback returns, no further callback is delivered, and no freed state is accessed

#### Scenario: Stop is not reachable from a callback
- **WHEN** the code paths reachable from a hotplug callback are examined
- **THEN** none of them initiates monitor shutdown directly

#### Scenario: Repeated start and stop cycles are clean
- **WHEN** monitoring is started and stopped repeatedly while devices are being attached and detached
- **THEN** every cycle completes, no callback is delivered outside a started period, and no resource is leaked

### Requirement: Windows system information reports what it can verify
The Windows system information backend SHALL report the operating system version and the kernel or build version, and SHALL report Secure Boot state only when it can be determined. A value that cannot be determined SHALL be reported as unknown rather than guessed. Fields describing Linux-only concepts SHALL be left at their documented neutral value.

#### Scenario: Undeterminable Secure Boot state is not guessed
- **WHEN** Secure Boot state cannot be read on the running system
- **THEN** it is reported as unknown, and it is never reported as disabled in order to represent "could not read"

#### Scenario: Linux-only fields stay neutral
- **WHEN** system information is queried on Windows
- **THEN** fields that describe Linux-only concepts carry their documented neutral value and no Windows concept is substituted into them

### Requirement: A Windows write verb requires a criticality prober first
Because the Windows backend implements no criticality probing, every Windows device classifies at the lowest criticality tier, which means the safety guard cannot refuse anything. No device-mutating capability SHALL be enabled on Windows until a real criticality prober for Windows exists and is wired into the same shared guard policy the other platforms use.

#### Scenario: Mutation without probing is refused at the specification level
- **WHEN** a future change proposes enabling any device-mutating verb on Windows
- **THEN** it is not permitted to ship unless it also supplies a Windows criticality prober, because otherwise the guard would permit disabling a device that is essential to operating the machine

#### Scenario: Absence of probing is recorded, not implied
- **WHEN** the Windows backend set is created
- **THEN** the capability descriptor reports criticality probing as not implemented, so the condition is observable rather than inferred from every device appearing ordinary

### Requirement: Windows presents unsupported views honestly
On Windows, each view whose backing capability is not implemented SHALL present that backend's shared unavailability sentence for the unsupported kind, at information severity. It SHALL NOT present an empty list without explanation, an error, or a retry affordance that cannot succeed.

#### Scenario: Every unsupported view is explained
- **WHEN** the application runs on Windows and the module, update, and snapshot views are opened
- **THEN** each shows its shared unsupported sentence at information severity, and none shows a bare empty list, an error, or a retry control

#### Scenario: The device view is unaffected
- **WHEN** the application runs on Windows and the device view is opened
- **THEN** devices are listed with their detail, selection and navigation work, and no unavailability note is shown for device enumeration
