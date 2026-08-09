## 1. Neutral identity in the shared model

Mechanical rename plus one helper. No behaviour change. Any line in this group
that is not a rename or a call-site reroute is a bug. Ends with a green Linux
build before group 2 starts.

- [x] 1.1 Add `core::identityTail(std::string_view)` (header + impl in `core`) returning the substring after the last `/` or `\`, whichever appears later; empty input and no-separator input return the input unchanged
- [x] 1.2 Unit-test `identityTail` for `/`-separated, `\`-separated, mixed, trailing-separator, no-separator, and empty inputs
- [x] 1.3 Rename `core::Device::sysfsPath` → `nativeId` in `core/include/devmgr/core/models.hpp:37` and rewrite its comment as "opaque, platform-native, stable device identifier"
- [x] 1.4 Rename `core::Device::modalias` → `hardwareId` (`models.hpp:38`) and document it as the platform's primary hardware-matching string
- [x] 1.5 Reroute `core/src/device_key.cpp:29-32` (`positionFor`) and `core/src/device_presentation.cpp:98,206,213` through `identityTail`; keep every other line of those functions unchanged
- [x] 1.6 Update the remaining `core` call sites: `device_key.cpp:40,48,61`, `criticality.cpp:79-80`, `services/device_key.hpp:13`, `services/critical_device_guard.hpp`
- [x] 1.7 Update `pal/interfaces.hpp` parameter names and comments (`IDeviceController::setEnabled`, `IDriverManager::driversFor`, `devicesUsingModule`) to the neutral vocabulary
- [x] 1.8 Update `platform/linux/` call sites; the Linux enumerator now writes the canonical sysfs path into `nativeId` and the modalias into `hardwareId`
- [x] 1.9 Update `daemon/` call sites (`sysfs_device_probe.cpp:31`, `snapshot_service.cpp:45,47,194,221,231,235,239,244,250,256-260`)
- [x] 1.10 Leave `manager_adaptor.cpp:39,54,59,75` D-Bus parameter names and `entry_json.cpp:14,28` JSON keys spelled exactly as they are; add a one-line comment at each stating the contract name is frozen while the in-memory name is neutral
- [x] 1.11 Leave `core::DisabledDeviceEntry::lastSysfsPath` named as it is (it mirrors a persisted key) and comment it accordingly
- [x] 1.12 Update `app/`, `gui/`, `tui/`, `cli/`, and every test file; confirm `grep -rn "sysfsPath\|modalias" core app gui tui cli` returns only frozen-contract comments
- [x] 1.13 Green build + full unit suite on Linux, unchanged results

## 2. Backend set, capabilities, and refusing implementations

- [x] 2.1 Add `pal::PlatformCapabilities` (plain bools: `deviceEnumeration`, `hotplug`, `deviceControl`, `driverManagement`, `privilegedChannel`, `updateProviders`, `criticalityProbing`, `systemInfo`)
- [x] 2.2 Add `pal::BackendSet` holding references to all seven interfaces plus the update-provider list, matching what the facade takes today
- [x] 2.3 Add refusing implementations for every interface, each method returning `Error{Code::Unsupported}` with an empty message, doing no work and never throwing
- [x] 2.4 Unit-test that every method of every refusing implementation returns `Unsupported` and that the set is exhaustive (one refusing type per PAL interface)
- [x] 2.5 Declare `pal::PlatformBackends` in `core/include/devmgr/pal/platform_backends.hpp` — owning, non-copyable, `create()` returning `Result<std::unique_ptr<PlatformBackends>>`, exposing `backends()` and `capabilities()`; the header names no platform type
- [x] 2.6 Unit-test the invariant that `capabilities()` reporting an interface as implemented implies `backends()` does not hold the refusing instance for it, and vice versa

## 3. Linux factory and frontend rewiring

- [x] 3.1 Define `PlatformBackends::create()` in `platform/linux/src/platform_backends_linux.cpp`, constructing exactly what `gui_app.cpp:64-72` constructs today, owning it, and reporting all capabilities implemented (update providers implemented; individual provider availability stays a runtime concern)
- [x] 3.2 Preserve the current construction and destruction ordering exactly; document that the Phase 2 hotplug shutdown fix depends on it
- [x] 3.3 Rewire `gui/src/gui_app.cpp` to call `create()` and drop lines 24-32 and 64-72; the file must include no `devmgr/platform/linux/` header
- [x] 3.4 Rewire `tui/src/tui_app.cpp` the same way, dropping lines 34-42 and 87-95
- [x] 3.5 Rewire `cli/src/main.cpp:10,17,30,43,45,56`; keep `--bus` selection working by passing it through `BackendOptions`
- [x] 3.6 Handle `create()` failure in each entry point: a diagnostic on stderr and a non-zero exit, no crash, no partially wired application
- [x] 3.7 Thread `capabilities()` into the facade / ViewModel layer so presentation code can read it without reaching for a platform header
- [x] 3.8 Confirm `grep -rn "platform/linux" gui tui cli app core` returns only the diagnostic comments already present in `gui/tests/test_main_window.cpp:61` and `cli/src/cli.cpp:24`
- [x] 3.9 Green build + full unit suite on Linux; both UIs start and populate

## 4. Build restructure

- [x] 4.1 Add `DEVMGR_PLATFORM` cache variable (`linux` / `windows` / `none`) auto-detected from the host, documented in the root `CMakeLists.txt`
- [x] 4.2 Replace `CMakeLists.txt:35`'s `if(UNIX AND NOT APPLE)` with the per-target gates from design D5, keeping `daemon`, `tui`, `packaging`, and the sdbus detection Linux-only
- [x] 4.3 Move `gui` and `cli` out of the Linux block; gate `gui` on `DEVMGR_BUILD_GUI` alone and `cli` on a platform backend existing
- [x] 4.4 Make `cli`'s privileged-channel-dependent sources conditional on `DEVMGR_WITH_SDBUS` so the binary builds without a transport
- [ ] 4.5 Verify `DEVMGR_PLATFORM=none` configures and builds `core`, `app`, `tests` only, with no user-facing target defined
- [x] 4.6 Verify the existing `nosdbus` configuration still configures, builds, and passes
- [ ] 4.7 Verify `DEVMGR_PACKAGED_BUILD` still validates the full stack and still refuses a shared sdbus-c++ (`CMakeLists.txt:64-79`)
- [ ] 4.8 Confirm the default Linux configuration builds the identical target set as before this change

## 5. Presentation: unsupported wording and capability gating

- [ ] 5.1 Add the `unsupported` rows from the `backend-availability` delta to the shared wording table in `core`, including the new `snapshots` backend identity
- [ ] 5.2 Unit-test that no `unsupported` sentence contains an install/start/enable/retry instruction or a platform mechanism name, and that `absent` and `unsupported` sentences differ for the same backend
- [ ] 5.3 Fix `unsupported` at information severity permanently in the severity mapping, including the attempted-verb path; unit-test that no input yields warning or danger for it
- [ ] 5.4 Extend `app::BackendStatusVM` so a capability reported unimplemented resolves to `unsupported` for that backend without a call being attempted
- [ ] 5.5 Apply platform capability once at GUI toolbar construction: a verb whose backing capability is unimplemented is never created as a candidate (`gui-presentation` delta)
- [ ] 5.6 Ensure the presentation function does not re-derive platform capability per frame and calls no backend; keep its existing enablement predicates untouched
- [ ] 5.7 Suppress the shortcut binding for any verb excluded by platform capability
- [ ] 5.8 Make separator collapsing survive an emptied group and a wholly empty toolbar
- [ ] 5.9 Make a view whose every source is unsupported render the unsupported sentence in place of content, with no empty-result string, no loading string, and no retry control, while staying tab-selectable and focusable
- [ ] 5.10 GUI tests with a synthetic capability descriptor: read-only descriptor leaves only `Refresh` on Devices; a wholly unsupported tab shows no verb, no separator, and carries the explanation; excluded verbs never reappear across tab/selection/availability changes
- [ ] 5.11 Apply the `ui-accessibility` delta: absent properties are omitted rather than blanked in both detail panes; confirm no surface labels the identity field with a platform mechanism name

## 5a. Shared device detail field vocabulary (design D13)

Prerequisite for group 7 — the Windows backend maps into this, so it must exist
first. `Device::properties` is currently internal-only: nothing in `gui/` or
`tui/` reads it, and its sole consumer is `core/src/device_presentation.cpp:13-19`.

- [ ] 5a.1 Define in `core` a closed device-detail-field set with stable identifiers, one product-facing label each, and a fixed display order: Manufacturer, Driver, Driver Version, Driver Provider, Driver Date, Class, Hardware IDs, Device Instance ID
- [ ] 5a.2 Add a shared accessor returning a device's populated fields in display order, so no surface iterates a raw property map
- [ ] 5a.3 Render detail fields in the GUI detail pane through that accessor, in shared order, authoring no labels locally
- [ ] 5a.4 Render detail fields in the TUI detail pane the same way; confirm labels and order are byte-identical to the GUI's
- [ ] 5a.5 Emit the same fields from `devmgr devices show`, using the same labels and order
- [ ] 5a.6 Unit-test: a backend populating a subset renders those fields in order with no gap or placeholder; two backends populating the same field render one identical label; the field set is closed (a raw key cannot be passed through)
- [ ] 5a.7 Leave the five udev lookup keys in `core/src/device_presentation.cpp:13-19` untouched — they are a derivation input, not a display vocabulary — and comment them to say so, recording Linux detail-field publication as a deliberate residual

## 6. CLI inventory verbs

- [ ] 6.1 Make the privileged channel lazy in `cli/src/main.cpp` — constructed only on the path of a verb that needs it
- [ ] 6.2 Verify no-args, unknown-verb, and help paths make no connection attempt (test on a host with no bus, or with the transport disabled)
- [ ] 6.3 Add `devmgr devices list [--json]` reading through `IDeviceEnumerator` from the backend set
- [ ] 6.4 Add `devmgr devices show <id> [--json]` including the device property map
- [ ] 6.5 Render name and bus through the shared presentation helpers so output matches the GUI and TUI byte for byte
- [ ] 6.6 Map outcomes onto the existing exit codes (`cli/src/cli.hpp:12-17`) with no new codes; enumeration failure returns `kFailed`, never success with an empty list
- [ ] 6.7 Extend `kUsageText` (`cli/src/cli.cpp:58`) with the inventory verbs
- [ ] 6.8 Unit-test: structured output parses and contains only device data; diagnostics go to stderr; two runs over an unchanged device set are byte-identical; `show` with no match exits `kNotFound`; enumeration failure is distinguishable from zero devices
- [ ] 6.9 Gate the inventory verbs on `capabilities().deviceEnumeration` so a platform without an enumerator does not advertise them

## 7. Windows backend — enumeration

- [ ] 7.1 Create `platform/windows/` with its `CMakeLists.txt`, linking `cfgmgr32`, `setupapi`, and `version`
- [ ] 7.2 Enforce the Windows 10 1607 floor in that `CMakeLists.txt` (design D14): set the target version macros and fail configuration with a message naming the minimum if an earlier target is requested; verify by configuring an earlier target and confirming the failure
- [ ] 7.3 Implement `IDeviceEnumerator` over `CM_Get_Device_ID_List_SizeW` / `CM_Get_Device_ID_ListW` / `CM_Locate_DevNodeW` / `CM_Get_DevNode_PropertyW`, present devices only
- [ ] 7.4 Store the device instance identifier verbatim in `nativeId` — no case change, separator substitution, prefix stripping, or truncation
- [ ] 7.5 Map the model fields: friendly name then device description for the display name, manufacturer for vendor, first hardware identifier into `hardwareId`
- [ ] 7.6 Map native properties onto the group-5a detail fields — Manufacturer, Driver, Driver Version, Driver Provider, Driver Date, Class, Hardware IDs, Device Instance ID — with the complete hardware-identifier list in reported order; keep every `DEVPKEY_*` constant and the mapping table inside `platform/windows/`
- [ ] 7.7 Map devnode status and problem conditions onto the existing shared device-status taxonomy; do not add a status detail field and do not introduce Windows-specific status wording
- [ ] 7.8 Leave fields Windows does not report unpopulated — no placeholder, sentinel, or guess
- [ ] 7.9 Implement bus classification by identifier prefix: `USB\`→USB, `PCI\`→PCI, `ACPI\` and `ROOT\`→platform, everything else→other, preserving the raw prefix internally
- [ ] 7.10 Unit-test the mapper against captured property fixtures — no live device access — covering all four bus classes, an unknown prefix, missing friendly name, missing manufacturer, missing driver, multi-entry hardware-identifier lists, and a device reporting a problem condition
- [ ] 7.11 Add a build-time or test-time check that no `DEVPKEY_` identifier appears outside `platform/windows/`, and that no rendered label or field identifier anywhere contains a native Windows property-key prefix

## 8. Windows backend — hotplug and system information

- [ ] 8.1 Implement `IHotplugMonitor` over `CM_Register_Notification` with `CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE` and all-interface-classes, no window and no message pump
- [ ] 8.2 Emit arrival and removal events carrying the device instance identifier, matching what the enumerator writes into `nativeId`
- [ ] 8.3 Implement `stop()` to satisfy `interfaces.hpp:29-31`: `CM_Unregister_Notification` blocks until in-flight callbacks return, and no callback fires afterwards
- [ ] 8.4 Guarantee `stop()` is never reachable from inside a callback (it deadlocks); document the invariant at the call boundary and verify by inspecting every path reachable from the callback
- [ ] 8.5 Add a generation counter so a callback that entered before unregistration cannot observe torn state
- [ ] 8.6 Unit-test against a fake notification source: stop during an in-flight callback, repeated start/stop cycles under event pressure, and no callback delivered outside a started period
- [ ] 8.7 Implement `ISystemInfo`: OS version and build; Secure Boot only when determinable, otherwise unknown — never reported as disabled to mean unreadable; Linux-only fields left at their neutral values
- [ ] 8.8 Define `PlatformBackends::create()` in `platform/windows/src/platform_backends_windows.cpp` wiring the three real backends and the refusing implementations, reporting exactly three capabilities implemented
- [ ] 8.9 Unit-test that no Windows code path can reach a mutating verb and that each unimplemented interface returns `Unsupported` without touching the OS

## 9. Windows application surfaces

- [ ] 9.1 Build the Qt GUI on Windows; fix any Linux-only assumption in `gui/` surfaced by the compiler or by startup
- [ ] 9.2 Verify the Devices tab populates with real detail and that Modules, Updates, and Snapshots each show their unsupported sentence at information severity, with no empty list, error, or retry control
- [ ] 9.3 Verify the Devices toolbar shows only `Refresh`, with the mutating verbs absent rather than disabled, and no orphan separators
- [ ] 9.4 Build and run the CLI on Windows; verify `devices list` and `devices show` including `--json`
- [ ] 9.5 Document the `windeployqt` run-from-build-tree procedure in the developer docs; no installer, no packaging (design D11)

## 10. CI and documentation

- [ ] 10.1 Add a `windows-latest` job to `.github/workflows/ci.yml` building `core`, `app`, `platform/windows`, `gui`, `cli` and running unit tests
- [ ] 10.2 Have that job provision Qt itself, pinned to an exact version from a declared source recorded in the workflow (design D12). The job must not read any pre-existing local Qt, and no task or document may resolve the Qt version by inspecting an installed one
- [ ] 10.3 Choose the pinned Qt version so it supports the Windows 10 1607 floor; record the version and its provisioning source in the developer documentation
- [ ] 10.4 Document that the owner's local Qt is expected to match the pinned version, and give the command that prints the local version so the match is checkable — the owner machine is the acceptance gate, never the dependency source
- [ ] 10.5 Add an `x64-windows` triplet to the vcpkg manifest configuration; confirm `gtest`, `spdlog`, `nlohmann-json`, `tl-expected` resolve
- [ ] 10.6 Confirm the Windows build inputs satisfy `docs/REPRODUCIBILITY.md` — every dependency version declared, none resolved from ambient machine state
- [ ] 10.7 State in the release records that the Windows CI job covers compilation and unit tests only, naming the device behaviours it does not cover (`acceptance-suite` delta)
- [ ] 10.8 Add the CLI inventory gate — listing with no helper present — to the Linux gate set as well as the Windows one
- [ ] 10.9 Update `README.md` and `docs/COMPATIBILITY-POLICY.md` with the supported-platform matrix, the Windows 10 1607 minimum as a support statement, and what read-only means on Windows
- [ ] 10.10 Record in `docs/DESIGN.md` that platform inapplicability hides a verb while runtime unavailability disables it, so the rule has one home
- [ ] 10.11 Record in `docs/DESIGN.md` that device detail fields come from the shared vocabulary with shared labels and order, and that native platform property names never reach a surface

## 11. Linux regression gate (owner)

This group exists because groups 1–4 rewrite every program's startup path, which
unit tests do not cover. A Linux regression must not be discoverable only on
Windows. If any row fails, stop and fix before touching group 12.

- [ ] 11.1 GUI starts, Devices populates, one enable/disable round-trip through the real polkit prompt succeeds, application exits cleanly
- [ ] 11.2 TUI starts, Devices populates, one enable/disable round-trip succeeds, hotplug reacts to a physical plug and unplug, exits cleanly
- [ ] 11.3 CLI: existing snapshot verbs still work against a running `devmgrd`; new `devices list` works with `devmgrd` stopped
- [ ] 11.4 With `devmgrd` stopped, both UIs still show the unreachable sentence at warning severity and keep mutating verbs visible-and-disabled — confirming unreachable was not accidentally reclassified as unsupported
- [ ] 11.5 Container unit suite green (rebuild first — the compose service has no volume mount)
- [ ] 11.6 VM smoke scripts for the phases that have them still print their OK markers

## 12. Windows verification gate (owner)

Automated Windows CI proves compilation only. These rows are the behavioural
gate and cannot be satisfied by a green run.

- [ ] 12.1 GUI starts on real Windows and Devices lists devices whose names, vendors, and buses match Device Manager for the same machine
- [ ] 12.2 Plug a USB device: it appears without a manual refresh. Unplug it: it disappears. Repeat at least three times
- [ ] 12.3 Close the application while a device is being plugged and unplugged repeatedly; confirm no hang and no crash on exit (this is the D8 hazard)
- [ ] 12.4 Modules, Updates, and Snapshots each show their unsupported sentence; each tab is reachable and its explanation readable with the keyboard alone
- [ ] 12.5 Devices toolbar shows only `Refresh`; no disabled mutating verb and no orphan separator is present anywhere
- [ ] 12.6 `devmgr devices list` and `devmgr devices show` produce correct output; `--json` parses; names and buses match what the GUI shows
- [ ] 12.7 Device detail omits rows for properties Windows does not report, rather than showing blank or unknown values
- [ ] 12.8 Read the detail pane for several devices and confirm every row is labelled in product-facing words — Manufacturer, Driver Version, Device Instance ID, Hardware IDs, Class — with no `DEVPKEY`-shaped text anywhere on screen or in `devices show --json`
- [ ] 12.9 Confirm a device with a problem condition shows its state through the shared status colour and word, with no extra status row
- [ ] 12.10 Confirm the owner machine's Qt version matches the version CI pins (task 10.4), and record both
- [ ] 12.11 Record the result of every row above with the Windows build number and version tested, confirming it is at or above Windows 10 1607

## 13. Gates

- [ ] 13.1 `openspec validate windows-readonly-pal --strict` passes
- [ ] 13.2 Container unit tests green
- [ ] 13.3 `scripts/check-format.sh --container` passes under clang-format-18
- [ ] 13.4 Container clang-tidy gate exits 0 with no user diagnostics
- [ ] 13.5 Windows CI job green
- [ ] 13.6 Sync delta specs to main specs and create the three new main specs
