## Context

The PAL (`core/include/devmgr/pal/interfaces.hpp`) declares seven interfaces and
has exactly one implementation set, in `platform/linux/`. Nothing has ever
substituted it except `FakePal` in tests, and tests do not exercise startup
wiring. The three real composition roots — `gui/src/gui_app.cpp:64-72`,
`tui/src/tui_app.cpp:87-95`, `cli/src/main.cpp:56` — stack-construct concrete
Linux types and hand references to the facade. `CMakeLists.txt:35` puts every
user-facing target inside `if(UNIX AND NOT APPLE)`.

Two prior pieces of work make this change smaller than it would otherwise be.
`calm-backend-unavailability` built a wording table keyed by
`(BackendId, UnavailabilityKind)` in `core`, with `app::BackendStatusVM` as the
single cross-surface accessor, and mapped `core::Error::Code::Unsupported` to the
`unsupported` kind — a kind no backend emits today. `tab-contextual-toolbar`
built verb gating that already hides verbs whose backing service is unavailable.
A platform with no `devmgrd`, no `fwupd`, and no `dkms` is the case both were
built for; this change supplies the first real producer.

Constraints that shape everything below:

- **The wire and the disk are frozen.** `daemon/src/manager_adaptor.cpp:39,54,59`
  names D-Bus parameters `sysfs_path`; `daemon/src/entry_json.cpp:14,28`
  persists `last_sysfs_path` into `/var/lib/devmgrd`. ApiVersion is 4 and
  `docs/COMPATIBILITY-POLICY.md` governs it. Neither may change here.
- **`core::Criticality` has three values** — `Ordinary`, `Important`,
  `Essential` — and no "unknown". Anything that does not probe classifies as
  `Ordinary`, which renders as no marker at all.
- **`IHotplugMonitor::stop()` has a hard contract** (`interfaces.hpp:29-31`):
  block until the reader is joined, and guarantee no callback fires afterwards.
  The Linux implementation shipped a use-after-free against exactly this in
  Phase 2. Any new implementation inherits the hazard.
- **The owner commits every task and runs every manual gate.** The agent cannot
  commit, and there is no Windows machine inside the agent's reach.

## Goals / Non-Goals

**Goals:**

- A backend seam that is *load-bearing*: adding a platform means adding a
  directory and one factory, not editing three frontends.
- Absent capabilities degrade through the machinery that already exists, with no
  new null checks scattered through `app/`.
- A Windows read-only backend that a user can actually run: Devices populated
  with real detail, hotplug live, other tabs honestly explained.
- Layers above the PAL contain no Linux vocabulary in identifiers or in
  user-visible strings.
- The Linux behaviour after this change is byte-identical to before it.

**Non-Goals:**

- Any Windows write path, privilege model, service, or UAC integration.
- macOS/IOKit.
- Windows packaging, installer, code signing, or release artifacts.
- The TUI on Windows.
- Windows firmware update integration.
- Renaming anything on the D-Bus wire or in the snapshot store.

## Decisions

### D1 — Absence is a null object plus a capability descriptor, not a null pointer

A backend set is a struct of **references to always-valid implementations**. A
platform that cannot supply one gets a shared null-object implementation whose
every method returns `Error{Code::Unsupported}`. Alongside it, the set carries a
`PlatformCapabilities` descriptor of plain bools stating which backends are real.

```
struct PlatformCapabilities {
    bool deviceEnumeration, hotplug, deviceControl,
         driverManagement, privilegedChannel, updateProviders,
         criticalityProbing, systemInfo;
};
```

Call sites never branch on a pointer. Code that *acts* calls through and gets
`Unsupported`, which `BackendStatusVM` already turns into the correct calm
sentence. Code that *presents* — toolbar composition, tab emptiness, legend keys
— reads the descriptor, because hiding a verb must not require attempting it.

*Alternatives considered.* Nullable pointers: rejected — it pushes a null check
into every consumer, and the first missed one is a crash on a platform CI can't
fully exercise. `std::optional<std::reference_wrapper<>>`: same problem with
worse ergonomics. Throwing stubs: rejected outright, `IUpdateProvider` already
mandates exception-freedom and the codebase is `Result`-based throughout.

*Why both mechanisms rather than one.* The descriptor alone would leave
mis-gated call paths crashing; the null object alone would force the UI to call
a backend to discover it should not offer it, which is exactly the "attempt then
apologise" pattern `docs/DESIGN.md` §5.5 forbids.

### D2 — One owning factory per platform, selected at link time

```
// core/include/devmgr/pal/platform_backends.hpp   (declaration, no platform types)
namespace devmgr::pal {
class PlatformBackends {            // owns storage; non-copyable
  public:
    static Result<std::unique_ptr<PlatformBackends>> create(const BackendOptions&);
    const BackendSet& backends() const;
    PlatformCapabilities capabilities() const;
    virtual ~PlatformBackends();
};
}
```

`create()` is declared once in `core` and **defined once per platform target**
(`platform/linux/src/platform_backends_linux.cpp`,
`platform/windows/src/platform_backends_windows.cpp`). Exactly one is linked
into any binary. Frontends call `create()`, hold the returned owner for the
process lifetime, and pass `backends()` to the facade — the same references the
facade takes today.

*Alternatives considered.* A registry with runtime registration: rejected, there
is never more than one platform in a process, so the indirection buys nothing and
costs static-initialisation-order risk. Compile-time policy templates: rejected,
it would put platform types in headers `app/` includes, re-coupling the layers
this change exists to decouple. `#ifdef` in the existing `main()` files:
rejected, that is the status quo with more lines.

*What this deliberately keeps.* Ownership stays in `main()`'s scope, so the
current shutdown ordering — which the Phase 2 UAF fix depends on — is preserved
rather than re-derived.

### D3 — `Device::sysfsPath` → `Device::nativeId`; the wire and disk keep their names

`sysfsPath` becomes `nativeId`, documented as an opaque, platform-native, stable
device identifier: a canonical sysfs path on Linux, a device instance ID on
Windows. `modalias` becomes `hardwareId`, documented as the platform's primary
hardware-matching string, with the full Windows hardware-ID list preserved in
`Device::properties`.

The rename stops at the C++ boundary. `manager_adaptor.cpp`'s `sysfs_path`
parameter names, `entry_json.cpp`'s `last_sysfs_path` key, and
`DisabledDeviceEntry::lastSysfsPath` all keep their spelling, because they are
Linux-only surfaces under a published compatibility policy. Marshalling code
gains a one-line comment at each crossing saying so.

*Alternatives considered.* Renaming everything including the wire: rejected —
it forces ApiVersion 5, a snapshot-store migration, and a deb/rpm upgrade path,
for a cosmetic gain. Renaming nothing: rejected — `sysfsPath` on a Windows
device is a lie in the one place a future contributor is most likely to read, and
the user chose the full seam refactor precisely to avoid that debt.

*Cost, stated plainly.* This is the largest mechanical edit in the change,
touching `core`, `app`, `daemon`, `platform/linux`, all three frontends, and the
test suites. It is pure rename plus the D4 helper; it should land as its own
task with a green build before any Windows code exists.

### D4 — Core stops assuming `/`

`core/src/device_key.cpp:29` and `core/src/device_presentation.cpp:98,206,213`
extract a "position" by taking the substring after the last `/`. Windows device
instance IDs use `\` (`USB\VID_046D&PID_C52B\5&1234&0&2`). One shared helper
`core::identityTail(std::string_view)` returns the substring after the last `/`
**or** `\`, whichever appears later, and every call site routes through it.

*Alternatives considered.* Having the PAL supply a `position` field: cleaner in
principle, but it widens the model and moves a stable-identity heuristic into
platform code where each platform would drift its own way. Rejected for now;
noted as the escape hatch if `identityTail` proves insufficient for a third
platform.

### D5 — Build gating moves from OS to capability

`if(UNIX AND NOT APPLE)` is replaced by an explicit platform-backend selection
plus per-target capability guards:

| Target | Gate after this change |
| --- | --- |
| `core`, `app`, `tests` | always |
| `platform/linux` | `DEVMGR_PLATFORM STREQUAL "linux"` |
| `platform/windows` | `DEVMGR_PLATFORM STREQUAL "windows"` |
| `gui` | `DEVMGR_BUILD_GUI` (Qt6 found) — **no OS condition** |
| `cli` | a platform backend exists — **no OS condition** |
| `daemon`, `tui`, `packaging` | Linux only, unchanged |
| `tests/ipc`, `tests/fwupd`, `tests/smoke` | `DEVMGR_WITH_SDBUS`, unchanged |

`DEVMGR_PLATFORM` auto-detects (`linux` on `UNIX AND NOT APPLE`, `windows` on
`WIN32`, otherwise `none`) and is overridable. `none` configures cleanly and
builds `core`/`app`/tests only — the current behaviour on unsupported hosts,
now stated rather than implied.

*Why `cli` loses its OS condition.* Once D6 lands, the CLI has verbs that need
no daemon. Keeping it Linux-gated would mean Windows ships a GUI and nothing
scriptable.

### D6 — The CLI gains PAL-direct inventory verbs

`devmgr devices list [--json]` and `devmgr devices show <id>` read through
`IDeviceEnumerator` directly, with no privileged channel and no daemon. They
reuse the existing exit-code table (`cli/src/cli.hpp:12-17`) unchanged:
`kOk` on success, `kUsage` for bad arguments, `kNotFound` when `show` matches no
device. `kUnreachable` and `kNotAuthorized` are structurally unreachable for
these verbs, which is the point.

This requires splitting `cli/src/main.cpp`: today it constructs a
`DbusPrivilegedChannel` before dispatching anything, so on a machine with no bus
even `--help` pays for a connection attempt. The channel becomes lazy —
constructed only when a verb needs it.

*Alternatives considered.* A separate `devmgr-inventory` binary: rejected, two
binaries for one tool is worse UX than one binary with two verb families.
Making the verbs go through `devmgrd`: rejected, it defeats the purpose and is
impossible on Windows.

### D7 — Windows enumeration uses CfgMgr32, not SetupAPI

`CM_Get_Device_ID_List_SizeW` / `CM_Get_Device_ID_ListW` to enumerate present
devnodes, `CM_Locate_DevNodeW` per instance, `CM_Get_DevNode_PropertyW` for
`DEVPKEY_*` values. Required properties: `Device_DeviceDesc`,
`Device_FriendlyName`, `Device_Manufacturer`, `Device_HardwareIds`,
`Device_Class`, `Device_Service`, `Device_Driver`, `Device_DriverVersion`,
`Device_DriverProvider`, `Device_DriverDate`, `Device_LocationInfo`.

*Alternatives considered.* SetupAPI (`SetupDiGetClassDevs`): rejected — it
requires an `HDEVINFO` handle with a fussy lifetime, its property model is the
older `SPDRP_*` set, and its hotplug counterpart needs a window. CfgMgr32 needs
neither a handle nor a message pump, works unelevated, and pairs directly with
D8. WMI (`Win32_PnPEntity`): rejected — COM apartment requirements, far slower,
and it is a query layer over the same data.

*Driver facts arrive as device properties, not through `IDriverManager`.*
Windows has no load/unload counterpart to `modprobe`, so implementing
`IDriverManager` would mean five methods returning `Unsupported` and two that
half-work. `IDriverManager` is a null object on Windows; the bound driver's
name, version, provider, and date land in `Device::properties` and render in the
detail pane.

### D8 — Hotplug via `CM_Register_Notification`, with the Phase 2 hazard designed out

Register `CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE` with
`CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES`. Callbacks arrive on a system
thread-pool thread, so the implementation must satisfy `interfaces.hpp:29-31`:

- `stop()` calls `CM_Unregister_Notification`, which blocks until in-flight
  callbacks return — that satisfies the join half of the contract.
- `CM_Unregister_Notification` **must not** be called from inside the callback;
  doing so deadlocks. `stop()` therefore records the registering thread and the
  callback path never calls `stop()` directly. The existing `HotplugService`
  debounce/claim pattern (the Phase 2 fix) sits above this unchanged.
- A generation counter guards against a callback that entered before
  unregistration observing torn state.

This is the single highest-risk file in the change and the one place where a
Windows-only bug would be invisible to Linux CI.

*Alternatives considered.* A hidden window plus `RegisterDeviceNotification` and
`WM_DEVICECHANGE`: rejected — it needs a message pump, which the CLI does not
have and the Qt GUI would have to share, coupling hotplug to the GUI event loop.
Polling: rejected as a fallback too; a 2-second re-enumeration of several hundred
devnodes is both slow and visibly laggy.

### D9 — No criticality probing on Windows, and that is a gate on the next change

`ICriticalityProber` is a null object on Windows. Because `core::Criticality` has
no "unknown" value, every Windows device classifies `Ordinary` and renders no
marker. That is honest for this change — nothing on Windows can be disabled, so
the guard is never consulted — but it is a loaded gun for the next one.

The Windows spec therefore states it as a precondition: **a Windows write verb
may not ship until a real `ICriticalityProber` exists for Windows.** Shipping
mutation against a prober that says "everything is Ordinary" would let a user
disable their only keyboard, which the Linux guard exists to prevent.

*Alternatives considered.* A minimal prober from `DEVPKEY_Device_Capabilities`
and `CM_DEVCAP_REMOVABLE`: rejected for now — a partial safety claim is worse
than a stated absence, because the UI cannot distinguish "probed and ordinary"
from "not probed" once a value is in the field.

### D10 — Windows bus mapping reuses the existing enum, no new enumerators

`core::BusType` gains nothing. The Windows enumerator maps by device instance ID
prefix: `USB\` → `Usb`, `PCI\` → `Pci`, `ACPI\` and `ROOT\` → `Platform`,
everything else (`HID\`, `SWD\`, `SCSI\`, `BTH\`, …) → `Other`, with the raw
prefix preserved in `Device::properties` so the detail pane can show it.

*Rationale.* `Platform` already means "fixed, on-board, not a removable bus" on
Linux, and `displayBus()` renders it the same way; ACPI and root-enumerated
devices are that. Adding Windows-flavoured enumerators would change
`displayBus()` output and force a `ui-accessibility` bus-casing revision on
Linux for no user benefit.

### D11 — Windows Qt deployment is a documented developer step, not a build feature

The GUI runs from the build tree with `windeployqt`. No installer, no bundling,
no CPack generator for Windows in this change.

*Rationale.* Packaging is a whole capability on Linux (`packaging-deb`,
`packaging-rpm`, `packaging-tarball`, `release-supply-chain`) with signing and
reproducibility requirements. Doing a half-version of that for Windows would
produce an unsigned artifact users might install, which is worse than none.

### D12 — CI owns Qt provisioning; the owner's machine is a gate, not a source

The Windows CI job SHALL provision Qt itself, pinned to an exact version, from a
declared source recorded in the workflow. The owner's machine is the acceptance
gate — it verifies behaviour — and is never the definition of what the build
depends on. No build step, task, or document may resolve a Qt version by
inspecting whatever happens to be installed locally.

*Why this differs from Linux, deliberately.* The Phase 3 spec chose *system* Qt
on Linux and explicitly rejected vcpkg Qt. That decision rests on distributions
shipping a coherent, patched Qt that the packaged `.deb` links against — a
"system Qt" that exists as a concept. Windows has no such thing: there is no
distribution, no system package, and every developer's install is an
independently chosen version at an arbitrary path. Carrying the Linux rule over
would mean the Windows build depends on an unpinned local artifact, which is the
opposite of what that rule was protecting.

*Consequence for reproducibility.* `docs/REPRODUCIBILITY.md` and
`release-supply-chain` require that a build be re-creatable from declared
inputs. A pinned CI-provisioned Qt satisfies that; "whatever the owner has"
does not. The pinned version is recorded in the workflow and in the developer
documentation, and the owner's local Qt is documented as needing to match it —
a checkable statement, rather than the source of truth.

*Alternatives considered.* Owner's local Qt as the reference: rejected per the
above — it makes CI a mirror of one machine. Building Qt from source in CI:
rejected, it costs hours per run for no benefit over a pinned prebuilt.

### D13 — Device detail fields are a shared product-facing vocabulary; native keys stay in the PAL

`Device::properties` is today a purely internal lookup table: nothing in `gui/`
or `tui/` reads it, and its only consumer is `core/src/device_presentation.cpp`,
which pulls five udev keys (`ID_VENDOR_FROM_DATABASE`, `ID_MODEL`, …) to derive
a display name. Windows would be the first backend whose properties reach a
user, and there is no vocabulary for them to land in.

Rather than let raw `DEVPKEY_Device_*` names become that vocabulary by default,
`core` gains an explicit **device detail field** set: a closed list of stable
field identifiers, each with a product-facing label and a defined display order.
The initial set is Manufacturer, Driver, Driver Version, Driver Provider, Driver
Date, Class, Hardware IDs, and Device Instance ID. Every platform backend maps
its native keys into this set inside its own directory; no raw platform key name
crosses the PAL boundary in a field identifier or a label.

*Where each concern lives.* The `DEVPKEY_*` constants and the mapping from them
exist only in `platform/windows/`. `core` owns the field identifiers, the
labels, and the order. `gui/` and `tui/` render whatever fields are present, in
the shared order, and author no labels of their own.

*Device status is not one of these fields.* Windows devnode problem codes map
into the existing shared device-status taxonomy that
`roleForDeviceStatus` already colours, not into a free-text "Status" detail row.
A second, differently-worded notion of status is exactly the kind of divergence
the shared presentation helpers exist to prevent.

*Linux is not converted in this change.* The five udev keys stay where they are,
because they are an internal derivation input, not a display vocabulary, and
nothing renders them. Linux may later publish detail fields through the same
set; the vocabulary is designed for that but this change does not do it. Stated
as a residual rather than left to be discovered.

*Alternatives considered.* A free-form string map rendered as-is: rejected —
it is precisely the leak this decision exists to prevent, and it would put
`DEVPKEY_Device_DriverVersion` in front of a user. Per-platform label tables:
rejected — two backends would drift to "Vendor" and "Manufacturer" for the same
row, which `ui-accessibility` already forbids across surfaces and should equally
forbid across platforms.

### D14 — The minimum Windows version is a declared contract, not an implementation detail

Windows 10 version 1607 is the floor, because `CM_Register_Notification` (D8) is
unavailable before it and the alternatives were rejected there. That floor is
declared in three places that can each be checked: the `windows-device-inventory`
spec states it as a requirement, the Windows build configuration enforces it at
configure or compile time so an older target fails loudly rather than at a
runtime symbol lookup, and the compatibility and README documentation state it
as a supported-platform statement.

*Rationale.* A minimum version that lives only in an API choice is discovered by
a user on an unsupported machine, as a missing-entry-point error. Formally
dropping older Windows is a decision worth making visibly once, not an accident
of which function was convenient.

*What this forecloses.* Windows 8.1 and Windows 10 before 1607 are out of
support for this project. That is the intended reading, not a side effect.

## Risks / Trade-offs

**The seam refactor breaks Linux startup in a way tests do not catch.** The three
`main()` files are the least-covered code in the tree, and D2 rewrites all of
them. → The rename (D3/D4) lands first and separately with a green build; the
seam lands second; a full Linux manual smoke on both UIs is a required gate of
this change, not only the Windows smoke. The container suite and both VM smoke
scripts run before the change is considered done.

**`CM_Register_Notification` misuse deadlocks or use-after-frees, and only on
Windows.** The Linux equivalent shipped exactly this bug once. → D8 states the
two invariants explicitly; the Windows hotplug monitor gets its own unit test
against a fake notification source exercising stop-during-callback and
stop-from-callback; the owner smoke includes a plug/unplug/close-while-plugging
sequence.

**The rename churns the diff so much that a real defect hides in it.** → Rename
is one task, mechanical, no behaviour change, reviewed as such. Any line that is
not a rename in that task is a bug.

**Windows CI can build but cannot verify device behaviour.** A `windows-latest`
runner in Azure sees a synthetic device set, no USB, no hotplug. → CI's job is
compile-and-unit-test only, stated as such in `acceptance-suite` so nobody later
mistakes a green Windows CI for a working Windows build. Behavioural truth comes
from the owner-gated manual smoke.

**Three tabs empty on Windows reads as a broken app.** Modules, Updates, and
Snapshots all resolve to `unsupported`. → This is the `backend-availability`
delta's whole job: each tab carries a specific sentence naming why, at
information severity, never warning. The GUI must not render an empty list with
no explanation — that is the "empty result versus unreachable source" distinction
the spec already requires.

**Scope creep into write verbs.** Windows enumeration makes disable look one API
call away (`CM_Disable_DevNode`). → D9 makes the criticality prober a stated
precondition. It is a spec requirement, not a note.

## Migration Plan

Ordering is load-bearing; each step ends with a green Linux build.

1. **Rename + `identityTail`** (D3, D4). Mechanical, no behaviour change. Linux
   unit + container suites green.
2. **Backend set, capabilities descriptor, null objects** (D1) in `core`, with
   `platform/linux` supplying the real factory (D2). Frontends rewired. Linux
   manual smoke on GUI and TUI.
3. **Build restructure** (D5). Verify: `DEVMGR_PLATFORM=none` configures and
   builds `core`/`app`/tests; the `nosdbus` configuration still builds; the
   packaged-build validation still fires.
4. **Spec deltas** for `backend-availability`, `gui-presentation`,
   `ui-accessibility`, so the Windows behaviour is specified before it is
   written.
5. **`platform/windows`** (D7, D8, D10) with unit tests against fakes.
6. **CLI inventory verbs** (D6) — cross-platform, testable on Linux first.
7. **Windows CI job**, then **owner-gated Windows manual smoke**.

**Rollback.** Steps 1–3 are the risky ones and are Linux-visible, so a
regression surfaces at the step that caused it and reverts cleanly. Steps 5–7 add
files under a build gate that is off on Linux; abandoning them changes no Linux
behaviour.

## Open Questions

The three questions this design opened — Qt provisioning, Windows property
naming, and the minimum Windows version — were resolved by the owner during
planning and are recorded as D12, D13, and D14 rather than left open.

- **Which pinned Qt version CI provisions.** D12 settles that CI pins one and
  that the owner's install is checked against it, not the reverse. The specific
  version and provisioning action are a task-time choice, constrained by needing
  to satisfy the Windows 10 1607 floor (D14). Not a design question.
- **Whether Linux later publishes detail fields through the D13 vocabulary.**
  Out of scope here and stated as a residual in D13. It becomes a real question
  only when a second backend wants to show a field Linux also has.
- **What a third platform does with `identityTail`.** D4 notes the escape hatch
  (a PAL-supplied position field) if separator-splitting proves insufficient.
  Nothing to decide until a third platform exists.
