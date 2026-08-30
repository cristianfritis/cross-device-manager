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

## 1a. Hotplug removal correlation by platform-native identity

Surfaced by the group 11.2 manual matrix: on a physical unplug the device
never left the model. Real udev strips `ID_VENDOR_ID` / `ID_MODEL_ID` /
`ID_SERIAL_SHORT` from a `remove` uevent, so the monitor's `mapDevice()`
derives a different `DeviceId` than the one recorded at add time and
`DeviceService::applyDelta` no-ops the removal. Latent since Phase 2 — unit
fixtures build `remove` events with full identity — and the same hazard the
Windows spec already guards against for its own identity (`windows-device-
inventory`: the instance id "SHALL be the value the application uses to
correlate a hotplug event with an enumerated device").

- [x] 1a.1 In `DeviceService::applyDelta`, on a `Removed` event whose `DeviceId` misses the model, fall back to erasing the entry whose `nativeId` equals the event's `nativeId`; an empty `nativeId` never matches
- [x] 1a.2 Publish the removal carrying the model's own `DeviceId` for that entry, not the event's
- [x] 1a.3 Unit-test: a removal whose derived id differs but whose `nativeId` matches removes the device and publishes one removal with the model's id; a removal with an empty or unknown `nativeId` changes nothing
- [x] 1a.4 Green build + full unit suite on Linux
- [x] 1a.5 Manual re-verify on real hardware: TUI, no keypress — a physical unplug reactively removes the row and shows the shared "device removed" status (paired with 11.2)

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
- [x] 4.5 Verify `DEVMGR_PLATFORM=none` configures and builds `core`, `app`, `tests` only, with no user-facing target defined
- [x] 4.6 Verify the existing `nosdbus` configuration still configures, builds, and passes
- [x] 4.7 Verify `DEVMGR_PACKAGED_BUILD` still validates the full stack and still refuses a shared sdbus-c++ (`CMakeLists.txt:64-79`)
- [x] 4.8 Confirm the default Linux configuration builds the identical target set as before this change

## 5. Presentation: unsupported wording and capability gating

- [x] 5.1 Add the `unsupported` rows from the `backend-availability` delta to the shared wording table in `core`, including the new `snapshots` backend identity
- [x] 5.2 Unit-test that no `unsupported` sentence contains an install/start/enable/retry instruction or a platform mechanism name, and that `absent` and `unsupported` sentences differ for the same backend
- [x] 5.3 Fix `unsupported` at information severity permanently in the severity mapping, including the attempted-verb path; unit-test that no input yields warning or danger for it
- [x] 5.4 Extend `app::BackendStatusVM` so a capability reported unimplemented resolves to `unsupported` for that backend without a call being attempted
- [x] 5.5 Apply platform capability once at GUI toolbar construction: a verb whose backing capability is unimplemented is never created as a candidate (`gui-presentation` delta)
- [x] 5.6 Ensure the presentation function does not re-derive platform capability per frame and calls no backend; keep its existing enablement predicates untouched
- [x] 5.7 Suppress the shortcut binding for any verb excluded by platform capability
- [x] 5.8 Make separator collapsing survive an emptied group and a wholly empty toolbar
- [x] 5.9 Make a view whose every source is unsupported render the unsupported sentence in place of content, with no empty-result string, no loading string, and no retry control, while staying tab-selectable and focusable
- [x] 5.10 GUI tests with a synthetic capability descriptor: read-only descriptor leaves only `Refresh` on Devices; a wholly unsupported tab shows no verb, no separator, and carries the explanation; excluded verbs never reappear across tab/selection/availability changes
- [x] 5.11 Apply the `ui-accessibility` delta: absent properties are omitted rather than blanked in both detail panes; confirm no surface labels the identity field with a platform mechanism name

## 5a. Shared device detail field vocabulary (design D13)

Prerequisite for group 7 — the Windows backend maps into this, so it must exist
first. `Device::properties` is currently internal-only: nothing in `gui/` or
`tui/` reads it, and its sole consumer is `core/src/device_presentation.cpp:13-19`.

- [x] 5a.1 Define in `core` a closed device-detail-field set with stable identifiers, one product-facing label each, and a fixed display order: Manufacturer, Driver, Driver Version, Driver Provider, Driver Date, Class, Hardware IDs, Device Instance ID
- [x] 5a.2 Add a shared accessor returning a device's populated fields in display order, so no surface iterates a raw property map
- [x] 5a.3 Render detail fields in the GUI detail pane through that accessor, in shared order, authoring no labels locally
- [x] 5a.4 Render detail fields in the TUI detail pane the same way; confirm labels and order are byte-identical to the GUI's
- [x] 5a.5 Emit the same fields from `devmgr devices show`, using the same labels and order
- [x] 5a.6 Unit-test: a backend populating a subset renders those fields in order with no gap or placeholder; two backends populating the same field render one identical label; the field set is closed (a raw key cannot be passed through)
- [x] 5a.7 Leave the five udev lookup keys in `core/src/device_presentation.cpp:13-19` untouched — they are a derivation input, not a display vocabulary — and comment them to say so, recording Linux detail-field publication as a deliberate residual

## 6. CLI inventory verbs

- [x] 6.1 Make the privileged channel lazy in `cli/src/main.cpp` — constructed only on the path of a verb that needs it
- [x] 6.2 Verify no-args, unknown-verb, and help paths make no connection attempt (test on a host with no bus, or with the transport disabled)
- [x] 6.3 Add `devmgr devices list [--json]` reading through `IDeviceEnumerator` from the backend set
- [x] 6.4 Add `devmgr devices show <id> [--json]` including the device property map
- [x] 6.5 Render name and bus through the shared presentation helpers so output matches the GUI and TUI byte for byte
- [x] 6.6 Map outcomes onto the existing exit codes (`cli/src/cli.hpp:12-17`) with no new codes; enumeration failure returns `kFailed`, never success with an empty list
- [x] 6.7 Extend `kUsageText` (`cli/src/cli.cpp:58`) with the inventory verbs
- [x] 6.8 Unit-test: structured output parses and contains only device data; diagnostics go to stderr; two runs over an unchanged device set are byte-identical; `show` with no match exits `kNotFound`; enumeration failure is distinguishable from zero devices
- [x] 6.9 Gate the inventory verbs on `capabilities().deviceEnumeration` so a platform without an enumerator does not advertise them

## 7. Windows backend — enumeration

- [x] 7.1 Create `platform/windows/` with its `CMakeLists.txt`, linking `cfgmgr32`, `setupapi`, and `version`
- [x] 7.2 Enforce the Windows 10 1607 floor in that `CMakeLists.txt` (design D14): set the target version macros and fail configuration with a message naming the minimum if an earlier target is requested; verify by configuring an earlier target and confirming the failure
- [x] 7.3 Implement `IDeviceEnumerator` over `CM_Get_Device_ID_List_SizeW` / `CM_Get_Device_ID_ListW` / `CM_Locate_DevNodeW` / `CM_Get_DevNode_PropertyW`, present devices only
- [x] 7.4 Store the device instance identifier verbatim in `nativeId` — no case change, separator substitution, prefix stripping, or truncation
- [x] 7.5 Map the model fields: friendly name then device description for the display name, manufacturer for vendor, first hardware identifier into `hardwareId`
- [x] 7.6 Map native properties onto the group-5a detail fields — Manufacturer, Driver, Driver Version, Driver Provider, Driver Date, Class, Hardware IDs, Device Instance ID — with the complete hardware-identifier list in reported order; keep every `DEVPKEY_*` constant and the mapping table inside `platform/windows/`
- [x] 7.7 Map devnode status and problem conditions onto the existing shared device-status taxonomy; do not add a status detail field and do not introduce Windows-specific status wording
- [x] 7.8 Leave fields Windows does not report unpopulated — no placeholder, sentinel, or guess
- [x] 7.9 Implement bus classification by identifier prefix: `USB\`→USB, `PCI\`→PCI, `ACPI\` and `ROOT\`→platform, everything else→other, preserving the raw prefix internally
- [x] 7.10 Unit-test the mapper against captured property fixtures — no live device access — covering all four bus classes, an unknown prefix, missing friendly name, missing manufacturer, missing driver, multi-entry hardware-identifier lists, and a device reporting a problem condition
- [x] 7.11 Add a build-time or test-time check that no `DEVPKEY_` identifier appears outside `platform/windows/`, and that no rendered label or field identifier anywhere contains a native Windows property-key prefix

## 8. Windows backend — hotplug and system information

- [x] 8.1 Implement `IHotplugMonitor` over `CM_Register_Notification` with `CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE` and all-interface-classes, no window and no message pump
- [x] 8.2 Emit arrival and removal events carrying the device instance identifier, matching what the enumerator writes into `nativeId`
- [x] 8.3 Implement `stop()` to satisfy `interfaces.hpp:29-31`: `CM_Unregister_Notification` blocks until in-flight callbacks return, and no callback fires afterwards
- [x] 8.4 Guarantee `stop()` is never reachable from inside a callback (it deadlocks); document the invariant at the call boundary and verify by inspecting every path reachable from the callback
- [x] 8.5 Add a generation counter so a callback that entered before unregistration cannot observe torn state
- [x] 8.6 Unit-test against a fake notification source: stop during an in-flight callback, repeated start/stop cycles under event pressure, and no callback delivered outside a started period
- [x] 8.7 Implement `ISystemInfo`: OS version and build; Secure Boot only when determinable, otherwise unknown — never reported as disabled to mean unreadable; Linux-only fields left at their neutral values
- [x] 8.8 Define `PlatformBackends::create()` in `platform/windows/src/platform_backends_windows.cpp` wiring the three real backends and the refusing implementations, reporting exactly three capabilities implemented
- [x] 8.9 Unit-test that no Windows code path can reach a mutating verb and that each unimplemented interface returns `Unsupported` without touching the OS

## 9. Windows application surfaces

- [x] 9.1 Build the Qt GUI on Windows; fix any Linux-only assumption in `gui/` surfaced by the compiler or by startup
- [x] 9.2 Verify the Devices tab populates with real detail and that Modules, Updates, and Snapshots each show their unsupported sentence at information severity, with no empty list, error, or retry control
- [x] 9.3 Verify the Devices toolbar shows only `Refresh`, with the mutating verbs absent rather than disabled, and no orphan separators
- [x] 9.4 Build and run the CLI on Windows; verify `devices list` and `devices show` including `--json`
- [x] 9.5 Document the `windeployqt` run-from-build-tree procedure in the developer docs; no installer, no packaging (design D11)

## 10. CI and documentation

- [x] 10.1 Add a `windows-latest` job to `.github/workflows/ci.yml` building `core`, `app`, `platform/windows`, `gui`, `cli` and running unit tests
- [x] 10.2 Have that job provision Qt itself, pinned to an exact version from a declared source recorded in the workflow (design D12). The job must not read any pre-existing local Qt, and no task or document may resolve the Qt version by inspecting an installed one
- [x] 10.3 Choose the pinned Qt version so it supports the Windows 10 1607 floor; record the version and its provisioning source in the developer documentation
- [x] 10.4 Document that the owner's local Qt is expected to match the pinned version, and give the command that prints the local version so the match is checkable — the owner machine is the acceptance gate, never the dependency source
- [x] 10.5 Add an `x64-windows` triplet to the vcpkg manifest configuration; confirm `gtest`, `spdlog`, `nlohmann-json`, `tl-expected` resolve
- [x] 10.6 Confirm the Windows build inputs satisfy `docs/REPRODUCIBILITY.md` — every dependency version declared, none resolved from ambient machine state
- [x] 10.7 State in the release records that the Windows CI job covers compilation and unit tests only, naming the device behaviours it does not cover (`acceptance-suite` delta)
- [x] 10.8 Add the CLI inventory gate — listing with no helper present — to the Linux gate set as well as the Windows one
- [x] 10.9 Update `README.md` and `docs/COMPATIBILITY-POLICY.md` with the supported-platform matrix, the Windows 10 1607 minimum as a support statement, and what read-only means on Windows
- [x] 10.10 Record in `docs/DESIGN.md` that platform inapplicability hides a verb while runtime unavailability disables it, so the rule has one home
- [x] 10.11 Record in `docs/DESIGN.md` that device detail fields come from the shared vocabulary with shared labels and order, and that native platform property names never reach a surface

## 11. Linux regression gate (owner)

This group exists because groups 1–4 rewrite every program's startup path, which
unit tests do not cover. A Linux regression must not be discoverable only on
Windows. If any row fails, stop and fix before touching group 12.

- [x] 11.1 GUI starts, Devices populates, one enable/disable round-trip through the real polkit prompt succeeds, application exits cleanly
- [x] 11.2 TUI starts, Devices populates, one enable/disable round-trip succeeds, hotplug reacts to a physical plug and unplug, exits cleanly
- [x] 11.3 CLI: existing snapshot verbs still work against a running `devmgrd`; new `devices list` works with `devmgrd` stopped
- [x] 11.4 With `devmgrd` stopped, both UIs still show the unreachable sentence at warning severity and keep mutating verbs visible-and-disabled — confirming unreachable was not accidentally reclassified as unsupported
- [x] 11.5 Container unit suite green (rebuild first — the compose service has no volume mount)
- [x] 11.6 VM smoke scripts for the phases that have them still print their OK markers — phase4/5/6/7/8 all print their OK marker in the libvirt VM (GUI/TUI-off build). Two non-code blockers cleared: (a) a stale `/usr/share/dbus-1/system-services/org.devmgr.Manager1.service` from a Jul-22 install was auto-activating an old `devmgrd` and masking phase7's daemon-down exit-4 check — removed it; the current CLI correctly returns `kUnreachable` (4) when the daemon is genuinely down. (b) `phase8-sandbox-smoke.sh:60` checked `/var/lib/devmgrd/HEAD` but the store has written `/var/lib/devmgrd/snapshots/HEAD` since Phase 7 — script was authored in beta-06 and never run green (dev host is OpenRC); fixed the path in the same commit as this change.

### 11a. TUI mutating keys were not gated on daemon availability (found 2026-08-29)

Row 11.4 passed on what it asked for — both surfaces show the unreachable
sentence at warning severity and neither hides a mutating verb — but it exposed
a parity gap one level below the presentation it checks. The GUI disables every
devmgrd-backed action (`MainWindow::gateOnDaemon`) while the helper is
unreachable; the TUI has no disabled state for a key, so `e`/`U`/`B`/`l`/`u`/
`s`/`r`/`d`/`x` still dispatched and the user got whatever error the privileged
channel eventually returned instead of the shared sentence. Same state, two
behaviours.

- [x] 11a.1 Add a `refusedByDaemon` gate in `tui/src/tui_app.cpp` reading `backendStatus().noteFor(BackendId::Devmgrd, blocksAttemptedVerb=true)` — the same accessor and the same escalation the GUI's disabled-reason uses
- [x] 11a.2 Apply it to every devmgrd-backed key and to no other: devices `e`/`U`/`B`, modules `l`/`u`, snapshots `s`/`r`/`d`(diff)/`x`. Devices `r` (a read), snapshots `h` (a local view toggle), and every Updates key (fwupd, not devmgrd) stay ungated, matching the GUI's own exemptions
- [x] 11a.3 Order the gate AFTER each site's existing guard verdict, so a critical-device or placeholder-row refusal keeps outranking the availability note — it is the more specific reason and the one that survives the daemon coming back
- [x] 11a.4 Publish the shared sentence VERBATIM, with no verb prefix, so the spec's "SHALL NOT reword, prefix, or suffix it" holds on this path too
- [x] 11a.5 Record the invocation half of the rule in the `backend-availability` delta and main specs — blocking applies to a verb's invocation, not only its presentation — with a scenario for a surface whose control has no disabled visual state
- [x] 11a.6 Owner re-run of 11.4 against the new build: with `devmgrd` stopped, each key above prints the shared sentence on the status line and dispatches nothing; `r` and `h` still work — VERIFIED in one daemon-up-to-down TUI session with a selected `SnapshotHealth::Ok` fixture. All nine gated keys (`e/U/B`, `l/u`, `s/r/d/x`) printed the exact shared sentence and emitted zero product IPC; device `r` performed its allowed read and snapshot `h` changed the history rendering locally with zero IPC. Evidence: `.pi/vm-artifacts/minimal/11a6-final-results/results.json`

## 12. Windows verification gate (owner)

Automated Windows CI proves compilation only. These rows are the behavioural
gate and cannot be satisfied by a green run.

**Verification runs 2026-08-29 and 2026-08-30 on the `win10-agent` VM (Windows
10.0.19044 / 21H2, VS 2022 BuildTools, Qt `C:\Qt\6.10.3\msvc2022_64`, vcpkg at the
CI-pinned commit).** The full Windows target (core, app, platform/windows, gui,
cli) compiles and links; local CTest is **557/557**. The first run read 456/458,
both environment-only: `devmgr_gui_selftest` needed `qoffscreen.dll` and the CTest
discovery step needed Qt on `PATH`. Neither was a product defect and both are
resolved. 12.11 below is the roll-up; the three code findings this gate turned up
are 12a, 12b, and — via 13.5 — 13a.

- [x] 12.1 GUI starts on real Windows and Devices lists devices whose names, vendors, and buses match Device Manager for the same machine — **VERIFIED by visual comparison on `win10-agent`**: the native Device Manager and `devmgr-gui` were viewed on the same box; representative PCI/USB device categories, names, vendors, buses, and the app's detail fields are consistent. Evidence: `.pi/vm-artifacts/windows/12-1-devmgmt-clean.png` and `.pi/vm-artifacts/windows/12-1-device-manager-app-compare.png`
- [x] 12.2 Plug a USB device: it appears without a manual refresh. Unplug it: it disappears. Repeat at least three times — **VERIFIED by the owner 2026-08-30**, physically, with USB passthrough into the `win10-agent` VM: across repeated plug/unplug cycles the row appeared and disappeared reactively, with no manual `Refresh` and no keypress. This is the Windows half of the reactive-hotplug behaviour 1a.5 verified on Linux, and the live proof of the `CM_Register_Notification` path (D8)
- [x] 12.3 Close the application while a device is being plugged and unplugged repeatedly; confirm no hang and no crash on exit (this is the D8 hazard) — **VERIFIED by the owner 2026-08-30**, physically, with USB passthrough into the `win10-agent` VM: the application was closed during repeated plug/unplug activity and exited cleanly — no hang, no crash. The D8 shutdown contract holds against real callbacks in flight: `stop()` calls `CM_Unregister_Notification` off the callback thread, it blocks until in-flight callbacks return, and the generation counter keeps a late callback off torn state
- [x] 12.4 Modules, Updates, and Snapshots each show their unsupported sentence; each tab is reachable and its explanation readable with the keyboard alone — VERIFIED: all three show their unsupported sentence in place of content (no list, error, retry); each tab reached with Ctrl+Tab alone. The clipping this row found — the sentence rendered inside the fixed-width left list column and was readable only by scrolling — went to the owner as a decision on 2026-08-30 and the verdict was **fix it now, do not accept it**: on these three tabs the unsupported sentence IS the content, so a clipped sentence is a clipped tab. Fixed under 12b — the banner half and the list-row half both, each verified by rendering the Windows-shaped descriptor on Linux and reading the result
- [x] 12.5 Devices toolbar shows only `Refresh`; no disabled mutating verb and no orphan separator is present anywhere — VERIFIED from screenshot: toolbar is `Refresh` only, no greyed verbs, no orphan separator
- [x] 12.6 `devmgr devices list` and `devmgr devices show` produce correct output; `--json` parses; names and buses match what the GUI shows — VERIFIED: `devices list` = 88 rows (name / [bus] / status / id); `devices show` prints the 8 shared detail fields; `--json` parses (ConvertFrom-Json OK) with `label`/`value` pairs
- [x] 12.7 Device detail omits rows for properties Windows does not report, rather than showing blank or unknown values — VERIFIED against under-described live devices `vport0p1` and `vport0p2`: `devices show`/`--json` emitted only Hardware IDs and Device Instance ID, with no blank or unknown detail rows. Evidence: `.pi/vm-artifacts/windows/windows-cli-acceptance/`
- [x] 12.8 Read the detail pane for several devices and confirm every row is labelled in product-facing words — Manufacturer, Driver Version, Device Instance ID, Hardware IDs, Class — with no `DEVPKEY`-shaped text anywhere on screen or in `devices show --json` — VERIFIED via CLI `devices show` + `--json` and owner-reviewed GUI detail pane across several devices: labels are exactly the shared vocabulary; no `DEVPKEY_`/`DEVPROP` text. (`PCI\VEN_...` strings are the hardware-id *values*, correctly under the "Hardware IDs" / "Device Instance ID" labels.) Evidence: `.pi/vm-artifacts/windows/12a3-bare-launch-owner-final-clean.png`
- [x] 12.9 Confirm a device with a problem condition shows its state through the shared status colour and word, with no extra status row — **VERIFIED on real hardware 2026-08-30**, both arms of `statusFor()`, against the passed-through card reader `USB\VID_14CD&PID_1212\121220160204` (`USB Mass Storage Device`, driver `USBSTOR` 10.0.19041.1288). Evidence: `.pi/vm-artifacts/windows/12-9-disabled-evidence/01-disabled-22.json` and `.pi/vm-artifacts/windows/12-9-error-evidence/02-error-service-fault.json`.

    | Arm | Induced | Windows problem code | devmgr status |
    | --- | --- | --- | --- |
    | switch-off | `Disable-PnpDevice` | **22** `CM_PROB_DISABLED` | `Disabled` |
    | fault | driver service `Start=4` | **32** `CM_PROB_DISABLED_SERVICE` | `Error` |

    Code 32 is the better proof the driver-package route would have given: the mapper's rule is "22 or 29 is a switch-off, EVERY other problem is a fault", so a code the change never anticipated resolving to `Error` exercises the rule rather than a hard-coded 28.

    **The two records differ in exactly one line.** Diffed here: `"status": "Disabled"` versus `"status": "Error"`, and nothing else — same `id` `dev-5efd18cf3d60e9fa`, same identity, same eight detail labels. That is the row's claim proved directly: the shared taxonomy carries the state and the fault changes nothing else in the record. Both records carry Manufacturer / Driver / Driver Version / Driver Provider / Driver Date / Class / Hardware IDs / Device Instance ID and **no label matching `Status|Problem|Code` and none matching `DEVPKEY|DEVPROP|CM_PROB`** — re-checked here against the files, not taken on the script's word. The stable `id` across the fault also confirms identity is derived from the instance id and not from the driver.

    One clarification the row's own wording needs: **there is no status colour to check on Windows.** The GUI carries device status as the WORD in the detail pane's single unconditional `Status:` row (`app/src/device_detail_vm.cpp:113`) and adds no colour — docs/DESIGN.md §9's GUI colour exception — and the TUI, where the role colour and glyph live, is not built on Windows.

    Getting here took four attempts and cost two script defects and one wrong assumption, all recorded because the next person to run this gate will hit them: (1) the first target, a VirtIO serial port, cannot be disabled at all — `Disable-PnpDevice` returns `0x80041001` with no explanation — so `-ListCandidates` now decodes `DEVPKEY_Device_Capabilities` and says which branches each device can serve and why not; (2) the reader runs an inbox `usbstor.inf` with no `oemNN.inf`, so the driver-package route was a dead end and `-ErrorViaService` was added to fault the device through its driver service instead, with the original `Start` saved and restored; (3) `Assert-SharedStatus` crashed with `The property 'Count' cannot be found on this object` on a device that was behaving CORRECTLY — under `Set-StrictMode` a `Where-Object` matching nothing yields `$null` and one matching once yields a scalar — fixed with `@()` and verified by lifting the function verbatim into a PowerShell container and exercising six shapes including the zero-match and one-match cases; (4) the restore failed and the script still exited 0, which is the one outcome a gate script must never produce — the restore is now asserted rather than printed, verifies the service `Start` actually went back, cycles the devnode and falls back to a physical replug, and a machine left faulted fails the run. The owner restored `USBSTOR` by hand and confirmed the VM healthy: F: back, problem code 0, PnP OK, DevMgr `Active`
- [x] 12.10 Confirm the owner machine's Qt version matches the version CI pins (task 10.4), and record both — **RESOLVED by moving the pin.** Acceptance machine (`win10-agent`): **Qt 6.10.3**, `C:\Qt\6.10.3\msvc2022_64`. CI pin: **6.10.3** (`.github/workflows/ci.yml`), was 6.8.3. `docs/WINDOWS-DEVELOPMENT.md` and `docs/REPRODUCIBILITY.md` updated to match; the LTS rationale is replaced with the real one (6.10.3 is a feature release — the pin follows the acceptance gate, and the cost of leaving the 6.8 LTS branch is stated). `design.md` D14's "the LTS this change pins" corrected. The 1809 GUI floor is unchanged: it holds for every Qt 6 release.
- [x] 12.11 Record the result of every row above with the Windows build number and version tested, confirming it is at or above Windows 10 1607

**Machine.** `win10-agent`, **Windows 10.0.19044 (21H2)** — above the 1607
backend/CLI floor (14393) and above the 1809 GUI floor (17763), so both declared
floors are exercised by a build that clears them, not merely asserted. Toolchain:
VS 2022 BuildTools, Qt `C:\Qt\6.10.3\msvc2022_64`, vcpkg at the CI-pinned commit
— the same Qt and the same MSVC major the CI job now pins (12.10, 13a.1), so the
build gate and this behavioural gate agree on the compiler and the framework.

**Automated, on that machine.** Full Windows target (core, app, platform/windows,
gui, cli) compiles and links. Local CTest **557/557 passed**, including
`devmgr_gui_version` and `devmgr_gui_selftest` (12a.3) and `DeviceServiceDelta`
6/6 (the group-1a hotplug fix). The earlier 456/458 reading was the same tree
before the Qt `PATH` and `qoffscreen.dll` environment faults were resolved; both
were environment-only and neither was a product defect.

**Rows.**

| Row | Result | How |
| --- | --- | --- |
| 12.1 device list matches Device Manager | PASS | owner visual compare, both apps on the same box |
| 12.2 USB hotplug add/remove, ≥3 cycles | PASS | owner, physical, USB passthrough 2026-08-30 |
| 12.3 close during plug/unplug — no hang, no crash | PASS | owner, physical, USB passthrough 2026-08-30 |
| 12.4 unsupported sentence on the three tabs, keyboard-reachable | PASS | owner; found the clipping → fixed under 12b |
| 12.5 Devices toolbar is `Refresh` only | PASS | owner screenshot |
| 12.6 `devices list` / `show` / `--json` | PASS | 88 rows; 8 shared fields; `ConvertFrom-Json` OK |
| 12.7 absent properties omitted, not blanked | PASS | live under-described devices `vport0p1`, `vport0p2` |
| 12.8 detail labels are product words, no `DEVPKEY` | PASS | CLI + owner-reviewed GUI detail pane |
| 12.9 problem condition through the shared taxonomy | PASS | live codes 22→`Disabled` and 32→`Error`; records differ in one line |
| 12.10 owner Qt version vs the CI pin | PASS | both 6.10.3; resolved by moving the pin |
| 12a.3 no console window on a bare GUI launch | PASS | owner-reviewed launch, `WIN32_EXECUTABLE` |
| 12b.4 banner sentence wraps and reads, both states | PASS | rendered and read on Linux 2026-08-30 |
| 12b.5-8 the list-row half of the clipping | PASS | ProseRowDelegate; h-scroll 363/126/76 → 0 |

**Defects this gate found, all fixed in-change.** The hotplug removal
correlation bug (1a — latent since Phase 2, on Linux too), the GUI linking as a
console-subsystem executable (12a), the Qt version mismatch between CI and the
acceptance machine (12.10), the unpinned CI runner image (13a), and the clipped
unsupported sentence (12b). Every one of them is a thing a green compile-only CI
run reports as success, which is the argument for this gate existing.

**Every row above is now closed.** The gate is complete: 12.1-12.11, 12a and 12b
all pass, on Windows 10.0.19044 (21H2), above both declared floors.

**What this gate was worth.** It found six defects that a green compile-only CI
reports as success — the hotplug removal correlation bug (1a, latent since Phase
2 and present on Linux too), the GUI linking as a console-subsystem executable
(12a), the Qt version mismatch between CI and the acceptance machine (12.10), the
unpinned CI runner image (13a), the clipped unsupported sentence (12b), and, in
12.9, two defects in the gate's own tooling. Not one of them is visible to a
compiler. That is the argument for a behavioural gate existing at all, and it is
why `acceptance-suite` now requires one per platform.

### 12a. Windows GUI is a console-subsystem executable (found 2026-08-29)

`gui/CMakeLists.txt:33` — `add_executable(devmgr-gui src/main.cpp)` has no `WIN32`
keyword and no `WIN32_EXECUTABLE` property, so on Windows the GUI links as a console
subsystem app: every launch pops a stray empty `conhost` window alongside the Qt
window (visible in `.pi/vm-artifacts/windows-gui*.png` too). This is a Linux
assumption carried to Windows (task 9.1 territory).

- [x] 12a.1 Make `devmgr-gui` a `WIN32_EXECUTABLE` on Windows only, keeping it a normal console binary on Linux — `gui/CMakeLists.txt` sets the property under `if(WIN32)`. Qt supplies the `WinMain` shim through `Qt6::Core`, which links `Qt6::EntryPointPrivate` behind a `$<TARGET_PROPERTY:WIN32_EXECUTABLE>` generator expression, so no extra link is needed; the Windows CI job (13.5) is what proves that
- [x] 12a.2 Preserve `--version` and `--self-test` terminal output (a WIN32 app has no attached stdout when run from a shell): `AttachConsole(ATTACH_PARENT_PROCESS)` at startup when a parent console exists, or an equivalent, before any `std::cout`/`std::cerr` — `gui/src/main.cpp` `attachParentConsole()`, called first thing in `main`. It checks `GetStdHandle(STD_OUTPUT_HANDLE)` FIRST and returns if a launcher already supplied one: CTest and shell redirects hand the process a pipe, and reopening `CONOUT$` over it would send the output to the console instead of to the caller reading the pipe. Only with no output handle does it attach the parent console, re-`freopen_s` the three C streams and `sync_with_stdio(true)` so `cout`/`cerr` rebind. From Explorer there is no parent console, `AttachConsole` fails, and the program stays windowed
- [x] 12a.3 Confirm `devmgr_gui_version` and `devmgr_gui_selftest` CTest tests still pass, and that a bare GUI launch shows no console window — Linux: full `ctest` 853/853, both green (the property is a no-op there, which is the point). Windows: local CTest **557/557 passed**, `devmgr-gui --version` and `--self-test` passed, and a bare `devmgr-gui.exe` launch was owner-reviewed on `win10-agent` with no console/conhost window visible. Evidence: `.pi/vm-artifacts/windows/12a3-bare-launch-owner-final-clean.png`

### 12b. The unsupported sentence was clipped in the tab's list column (found 2026-08-30)

On Modules / Updates / Snapshots a read-only platform has no list: the shared
sentence is pushed as the tab's single LIST ROW (`ModulesVM::pushEmptyStateRow`
and its Updates/Snapshots twins) and also appears in the banner above it. Both
sat in the splitter's fixed-width left column and both ran off the edge.

Measured on Linux against `ReadOnlyFixture` — the synthetic descriptor that
reproduces the Windows shape exactly, same `MainWindow`, same widgets — at a
1100x700 window, left column 254 px:

| Tab | item width, before → after | h-scroll max, before → after | banner wrapped, before → after |
| --- | --- | --- | --- |
| Modules | 617 px → 249 px | 363 → 0 | no → yes |
| Updates | 380 px → 249 px | 126 → 0 | no → yes |
| Snapshots | 330 px → 249 px | 76 → 0 | no → yes |

So there were TWO clipped elements, not one, and the row the owner actually
described — "the sentence renders inside the fixed-width left list column and is
horizontally clipped, readable only by scrolling" — is the LIST ROW: a
`QListView` item does not wrap, and the view answers an over-wide item with a
horizontal scrollbar.

- [x] 12b.1 Word-wrap every availability banner sentence label — `makeAvailabilityBanner()` set `setWordWrap(true)` on the *diagnostic* label but never on the *sentence* label beside it. Fixed there, which covers Devices and Modules; `updatesBannerLabel_` and `snapshotsBannerLabel_` are hand-built outside that factory (`main_window.cpp:487,554`) and had never had the wrap at all, so they got it too. All four banners now wrap, which is also what removes the inconsistency of two banners behaving one way and two the other
- [x] 12b.2 Green Linux build + full unit suite, unchanged results — `cmake --build build -j24` clean, `ctest` **853/853 passed, 0 failed**
- [x] 12b.3 Re-run the format and clang-tidy gates over the changed file — `scripts/check-format.sh --container`: **OK, 284 file(s) clean** under clang-format-18. Container clang-tidy over all 81 translation units: **exit 0**, `Suppressed 3007921 warnings (3007106 in non-user code, 815 NOLINT)` — the two numbers account for the total, so no user diagnostic was emitted. Byte-identical to the 13.4 reading, so 13.4 still stands over the changed tree
- [x] 12b.4 Verify the banner half on Linux — VERIFIED here 2026-08-30 by rendering `MainWindow` offscreen and reading the images. Degraded Linux state (`devmgrd` unreachable, Devices and Snapshots): the shared sentence wraps to two lines, stays bold for the Warning role, keeps its `Details ▾` disclosure beside it, the disclosure row below is unmoved, and the device rows are untouched with no horizontal scrollbar. Read-only state: all three banners wrap and read in full
- [x] 12b.5 Wrap the list row, and ONLY the list row — owner decision 2026-08-30, after the measurement above showed the clipped element was the row, not the banner. `main_window.cpp:691` deliberately sets `setWordWrap(false)` plus `ElideRight` on all four views per docs/DESIGN.md §2.4 (rows elide; the full value stays reachable in the detail pane), and that rule is right for a data row and wrong for this one: the unsupported sentence has no detail-pane entry to be reachable in, so eliding it would leave it unreadable. New `gui::ProseRowDelegate` (`gui/src/prose_row_delegate.{hpp,cpp}`) wraps a row the model marks with `kProseRowRole` and defers to the base delegate for every other row; the three list models publish that role as `vm_.unsupportedContent().has_value()`, which is exact because a wholly unsupported tab has no data and `pushEmptyStateRow()` makes the sentence its single row. Installed on Modules / Updates / Snapshots only — Devices always has a real enumerator, so it has no prose row. The elide loop is untouched, so §2.4 still governs every data row
- [x] 12b.6 Verified on Linux against `ReadOnlyFixture`, the descriptor that reproduces the Windows shape, at a 1100x700 window — before → after, per tab: h-scroll maximum **363 → 0** (Modules), **126 → 0** (Updates), **76 → 0** (Snapshots); item width 617/380/330 px → 249 px against a 254 px viewport; row height 14 px → 46/32/32 px, i.e. the sentence wrapped to three/two/two lines and reads in full with no scrolling. The images were rendered and read, not just measured. The control holds too: a module row 76 characters wide stays `prose=false`, one line high, elided, with its full value in the detail pane
- [x] 12b.7 Regression tests, both halves — `UnsupportedSentenceWrapsAndNeedsNoHorizontalScroll` asserts the role, `sizeHintForColumn <= viewport`, `horizontalScrollBar()->maximum() == 0` and a wrapped row height on all three tabs; `LongDataRowStillElidesRatherThanWrapping` asserts a long data row is not prose, stays one line, and keeps `ElideRight`. Each guards its own premise, so neither can pass vacuously if the sentence later becomes short enough to fit
- [x] 12b.9 Amend `ui-accessibility` so the next implementer does not re-introduce this — the requirement the code cited to justify `setWordWrap(false)` on all four views is "Layout minimums and long-value handling", and as written it says long values SHALL elide with the full value reachable in the detail surface. That is right for a data row and silent about a row that IS the view's explanation, so applying it there was a defensible misreading of a spec that did not say enough. The delta now states the carve-out: the elide rule governs DATA rows, an explanatory row standing in place of a list SHALL render in full and SHALL NOT require horizontal scrolling or elision, with a scenario for each side — the sentence wrapping in a narrow column, and a data row in the same list still eliding. Written after 13.6's sync, so it re-opens the sync
- [x] 12b.8 Green Linux build + full unit suite after the delegate — `ctest` **855/855 passed, 0 failed** (853 plus the two new). Container format gate: **OK, 286 file(s) clean** under clang-format-18. Container clang-tidy over all 82 translation units (81 plus `gui/src/prose_row_delegate.cpp`): **exit 0**, `Suppressed 3068074 warnings (3067256 in non-user code, 818 NOLINT)` — the two numbers account for the total, so no user diagnostic was emitted. NOLINT moved 815 → 818: the three `new ProseRowDelegate(view)` expressions, which sit inside the constructor's existing `NOLINTBEGIN(cppcoreguidelines-owning-memory)` region with every other Qt parent-owned widget, so the delta is accounted for rather than merely tolerated

## 13. Gates

- [x] 13.1 `openspec validate windows-readonly-pal --strict` passes
- [x] 13.2 Container unit tests green — re-run 2026-08-29 after 11a/12a: `docker compose -f test/docker-compose.yml run --rm unit` = **854/854**, 0 failed (`SysfsControllerTest.UnwritableAttrIsIo` skipped — it needs a non-root user, as always in this image)
- [x] 13.3 `scripts/check-format.sh --container` passes under clang-format-18 — re-run 2026-08-29 after 11a/12a: **OK, 284 file(s) clean** under `/usr/bin/clang-format-18`. No divergence from the host clang-format 22 pass, which had only reformatted the newly added lines
- [x] 13.4 Container clang-tidy gate exits 0 with no user diagnostics — final re-run 2026-08-30 after 12b, over all 82 translation units: **exit 0**, `Suppressed 3068074 warnings (3067256 in non-user code, 818 NOLINT)` — the two numbers account for the total, so no user diagnostic was emitted (the exit code alone is not the check here: the gate is piped, so the count line is what proves it). The 2026-08-29 reading after 11a/12a was 81 units and `Suppressed 3007921 (3007106 non-user, 815 NOLINT)`; the whole delta is `prose_row_delegate.cpp` and its three NOLINT-covered `new` expressions (12b.8)
- [x] 13.5 Windows CI job green — run [33282695317](https://github.com/cristianfritis/cross-device-manager/actions/runs/33282695317) on `feature/phase_9`, 2026-08-30. Both jobs succeeded. `windows-build-and-test` on the newly pinned `windows-2022`: Provision Qt (pinned) / Provision vcpkg (pinned commit) / Configure / Build (core, app, platform/windows, gui, cli) / Unit tests / CLI inventory gate all green — so the `WIN32_EXECUTABLE` change links (Qt's `WinMain` shim arrives through `Qt6::Core` as expected, 12a.1) and Qt 6.10.3 provisions and builds (12.10). `build-and-test` green too, including the container clang-tidy and clang-format steps. The previous run [33282332775](https://github.com/cristianfritis/cross-device-manager/actions/runs/33282332775) is the `windows-latest` failure that 13a fixes. Step conclusions are the evidence here; the raw ctest counts are not, because `gh run view --log` returns HTTP 403 for this token

### 13a. The Windows CI runner image was not pinned (found 2026-08-29)

First push of 13.5 failed at CONFIGURE, not compile: `Generator Visual Studio 17
2022 could not find any instance of Visual Studio`, after vcpkg had already
resolved every pinned package. `windows-latest` and `windows-2025` both became
Windows Server 2025 with **Visual Studio 2026 (v18)** in the 2026-06-08 → 06-15
rollout, and the `windows-debug` preset names the `Visual Studio 17 2022`
generator. Task 10.2 pinned Qt and task 10.6 checked `docs/REPRODUCIBILITY.md`,
but the runner image itself was left as a moving label — and that document's
own toolchain row declared `windows-latest` AS the value, which is not a value.

- [x] 13a.1 Pin `runs-on` to `windows-2022`, the image GitHub names as the VS 2022 fallback, with the reason recorded beside it. This also makes CI agree with the acceptance machine's VS 2022 BuildTools — the same argument that moved the Qt pin in 12.10, pointed at the compiler
- [x] 13a.2 Correct the `docs/REPRODUCIBILITY.md` toolchain row: the declared value is an image label with a fixed toolchain, not `latest`
- [x] 13a.3 Note in `docs/WINDOWS-DEVELOPMENT.md` that the preset requires a VS 2022 install, so a VS 2026 machine will not configure it
- [x] 13a.4 Moving to Visual Studio 2026 is deliberately NOT done here: MSBuild, the MSVC toolchain, the .NET SDK and the Windows SDK headers all move together, and CI and the acceptance machine must move in one step. Own change — **CONFIRMED by the owner 2026-08-30: keep it separate.** The deliverable of this row is the decision, and it is now recorded in three places that a future reader will actually hit: the `runs-on: windows-2022` pin carries its reason beside it (13a.1), `docs/REPRODUCIBILITY.md` declares the image label as a fixed-toolchain value rather than `latest` (13a.2), and `docs/WINDOWS-DEVELOPMENT.md` states that the preset needs a VS 2022 install so a VS 2026 machine will not configure it (13a.3). Nothing in `windows-readonly-pal` blocks on the migration; it is a toolchain change with its own gate — CI, the acceptance VM and `docs/REPRODUCIBILITY.md` moving in one step — and belongs in its own OpenSpec change, not folded into a PAL change as a side effect. `windows-2022` is supported for three years, so there is no deadline forcing it into this one
- [x] 13.6 Sync delta specs to main specs and create the three new main specs — re-run at close-out after 12b.9 added a `ui-accessibility` carve-out that postdates the first sync
