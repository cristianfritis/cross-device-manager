# Phase 5 — Driver/Module Management: Design Spec

**Date:** 2026-07-06
**Status:** Approved (brainstorming session 2026-07-06)
**Branch:** stacks on `feature/phase4` (stacked-phase pattern; Phases 1–4 unmerged)
**Predecessors:** `2026-07-03-phase4-enable-disable-design.md` (devmgrd pipeline, polkit, guard, IPC v1)

---

## 1. Overview

Phase 5 delivers driver/module management: the `KmodDriverManager` read side
(driver info per device, loaded-module list, modprobe.d read-only display,
Secure Boot/signature indicators), privileged module load/unload, and — the
centerpiece — a **persistence engine** that makes `setDeviceEnabled` universal
across buses and makes "disabled" a durable, actively-enforced state that
survives replug and daemon restart. On top of that engine, explicit
**bind/unbind driver verbs** give power users surgical, non-persistent control.

Product bar (user's words): *a world-class device manager more powerful than
Windows'* — the polished, foolproof abstraction of Windows combined with the
raw, scriptable, surgical control of Linux.

### Goals

1. **Persistence engine:** `setDeviceEnabled` works on all buses (USB
   `authorized` + non-USB driver unbind), with daemon-persisted desired state
   and **active enforcement** (startup sweep + udev watch re-applies disable on
   device reappearance).
2. **Module management:** load/unload kernel modules via devmgrd (guard +
   polkit), full info read side via libkmod (version, path, signature/signer,
   dependencies, holders, size, refcount), modprobe.d options/blacklist shown
   read-only.
3. **Surgical driver verbs:** per-device `UnbindDriver`/`BindDriver` —
   one-shot, never persisted, labeled *advanced*.
4. **Truthful security posture display:** Secure Boot state (efivars) + kernel
   lockdown mode, signature status per module with signer identity.
5. **UI parity:** device detail gains a Driver section (with compatible-driver
   candidates via modalias); both UIs gain a global Modules view.

### Non-goals (deferred)

- **Writing modprobe.d** (blacklist/options editing) → Phase 7, where
  snapshot/rollback makes config edits safe. Phase 5 is read-only on modprobe.d.
- **udev-rule-generated enforcement** (survives daemon-down) → possible Phase
  7/8 layer on top of this engine; rejected for Phase 5 (config mutation before
  snapshots exist).
- **Boot-time daemon autostart** → Phase 8 (OpenRC/systemd service unit).
  Until then enforcement runs while devmgrd runs (manual foreground, as in
  Phase 4).
- `rebootPending` logic → Phase 6 (honest `false` stub in Phase 5).
- Executing modprobe.d `install` commands → never (see §7 taxonomy).

## 2. Locked decisions (brainstorming outcomes)

| Decision | Choice |
|---|---|
| Scope level | Core mutations: info + load/unload + SB/signature display; modprobe.d **read-only** |
| Phase 4 deferral (bind/unbind + PCI) | **Hybrid:** full PCI enable/disable persistence engine first, explicit bind/unbind verbs layered on top |
| Persistence contract | **Active enforcement:** startup sweep + udev watch re-apply; guard re-checked per re-apply |
| UI surface | Device-detail Driver section **plus** new global Modules view, TUI+GUI parity |
| Mechanism | **libkmod in-process** (read side in frontends, write side in daemon). No fork/exec from the privileged daemon; shell-out approach rejected |
| Persistent vs surgical | `SetDeviceEnabled` = persistent desired state (StateStore + enforcement). `UnbindDriver`/`BindDriver` = one-shot, **no** store entry |
| JSON | **nlohmann-json via vcpkg** introduced now (StateStore; Phase 7 snapshots will reuse) |
| IPC | `org.devmgr.Manager1` `ApiVersion` 1 → 2; three polkit actions (1 existing + 2 new), all `auth_admin_keep` |

## 3. Architecture

```
                     ┌─ frontends (unprivileged) ─────────────────────┐
                     │  TUI / GUI                                     │
                     │   ├─ DeviceDetail: driver section + bind/unbind│
                     │   ├─ NEW ModulesVM → Modules view (both UIs)   │
                     │   └─ ApplicationFacade (async, TaskScheduler)  │
                     │        │ reads                    │ mutations  │
                     │        ▼                          ▼            │
                     │  KmodDriverManager (libkmod, RO)  DbusPrivChannel
                     └────────│──────────────────────────────│────────┘
                          /sys, libkmod                 D-Bus system bus
                                                             │
                     ┌─ devmgrd (root) ───────────────────────▼───────┐
                     │  ManagerAdaptor (org.devmgr.Manager1, ApiV=2)  │
                     │  RequestProcessor: validate→guard→authorize→act│
                     │   ├─ IDeviceController (SysfsDeviceController: │
                     │   │    USB authorized + NEW unbind/bind, all buses)
                     │   ├─ NEW IDriverManager (KmodDriverManager, RW)│
                     │   ├─ NEW StateStore (/var/lib/devmgrd, atomic) │
                     │   └─ NEW EnforcementService                    │
                     │        ├─ startup: re-apply desired state      │
                     │        └─ LinuxHotplugMonitor: re-disable on   │
                     │           reappearance (guard re-checked)      │
                     └────────────────────────────────────────────────┘
```

Structural principles:

- **devmgrd gains `pal::IDriverManager&` exactly as it has `IDeviceController&`.**
  libkmod is quarantined in `platform/linux`; all daemon verb logic stays
  unit-testable with `FakeDriverManager`.
- **One `KmodDriverManager` class serves both sides:** frontends call read
  methods unprivileged; the daemon calls write methods as root (mirrors the
  `SysfsDeviceController` pattern).
- **Daemon owns disabled-state truth.** Frontends merge it in via one bulk
  read-only IPC call per refresh (§6.1), never per-device queries.

## 4. Model & PAL changes

### 4.1 `core/models.hpp`

- `Driver` gains `std::optional<std::string> signer;` (modinfo `signer`, e.g.
  "Build time autogenerated kernel key"). Existing fields (`isSigned`,
  `dependencies`, `version`, `path`, `loaded`, `kind`) finally get filled.
- New `LoadedModule { std::string name; std::uint64_t sizeBytes; int refCount;
  std::vector<std::string> holders; }` — runtime view; `Driver` remains the
  static view.
- New `ModprobeInfo { std::optional<std::string> options; bool blacklisted; }`
  — the read-only modprobe.d surface.

### 4.2 `pal/interfaces.hpp`

- `IDriverManager` (existing stub, finally implemented) gains:
  - `listLoadedModules() → Result<std::vector<LoadedModule>>`
  - `moduleInfo(name) → Result<Driver>`
  - `modprobeInfo(name) → Result<ModprobeInfo>`
  - `driversFor(DeviceId)` semantics fixed: returns the **modalias candidate
    list** (all modules matching the device's modalias via
    `kmod_module_new_from_lookup` — the exact resolution modprobe performs).
    The bound entry is identified by the UI via `Device::boundDriver` (no
    model change needed). This feeds the bind-driver candidates display now
    and a dropdown later.
- `IDeviceController::setEnabled` contract widens: non-USB no longer
  `Unsupported`. Return type becomes
  `core::Result<std::optional<std::string>>` — set to the unbound driver's
  name when the disable used the unbind mechanism (`nullopt` for USB
  `authorized` and for enables), so RequestProcessor can record `lastDriver`.
  Enable = targeted rebind via `driver_override` + `drivers_probe` (§5.4).
- `IPrivilegedChannel` gains: `loadModule(name)`, `unloadModule(name)`,
  `bindDriver(device, driverName)`, `unbindDriver(device)`,
  `listDisabledDevices() → Result<std::vector<DisabledDeviceEntry>>`.
- New core types in `core/models.hpp` (shared by daemon, channel, mapper):
  `DeviceKey` (§5.1) and
  `DisabledDeviceEntry { DeviceKey key; std::string lastSysfsPath;
  bool guardSuspended; }`.
- `ISystemInfo::Info` gains `std::string lockdownMode;`
  (`"none"|"integrity"|"confidentiality"`). First real Linux implementation:
  `LinuxSystemInfo` (§7.2).

## 5. State engine

### 5.1 DeviceKey — tiered identity

```
DeviceKey {
  bus:       "usb" | "pci" | ...
  vendorId:  e.g. "046d"
  productId: e.g. "c52b"
  serial:    "ABC123"   // "" ⇒ positional matching
  position:  bus-specific stable locator
}
```

- **Tier 1 — strong identity (serial tuple):** non-empty serial ⇒ match on
  `(bus, vendorId, productId, serial)`. Device is recognized at any port,
  after any reboot or bus re-enumeration.
- **Tier 2 — positional identity:** empty serial ⇒ match on `(bus, position)`
  **validated by** `(vendorId, productId)`:
  - USB: port chain from `devpath` (e.g. `2-1.4`) — stable per physical port
    across reboots. A serial-less device moved to another port is a **new
    device** (Windows semantics; physically indistinguishable devices are
    treated honestly).
  - PCI: sysfs address (`0000:03:00.0`); vid:pid+class validate against BIOS
    renumbering. **Validation failure ⇒ do not enforce, log** — never unbind a
    different device at a stored address.
- **Cloned-serial guard:** at disable time, if another *present* device shares
  the serial tuple, the entry is downgraded to positional immediately
  (deterministic). A tuple matching multiple devices at enforcement time
  enforces on all matches (matches user intent for identical-serial hardware).

Non-key operational fields per entry: `mechanism` (`"authorized"|"unbind"`),
`lastDriver` (targeted rebind), `lastSysfsPath` (display/debug),
`disabledAtUtc`, `guardSuspended`.

### 5.2 StateStore

- Location: `/var/lib/devmgrd/state.json` (dir `0700`, file `0600`, root).
  Path injectable for tests.
- **Only non-default state stored** — enabling a device deletes its entry.
- Atomic writes: tmp file + `fsync` + `rename`.
- Corrupt file on load → moved to `state.json.bad`, start empty, loud log.
  Never silently destroy evidence.
- Serialization: nlohmann-json (vcpkg). Schema versioned (`"version": 1`).

### 5.3 EnforcementService (daemon)

- **Startup sweep:** enumerate via `UdevDeviceEnumerator` (daemon reuses
  platform lib), match all entries, re-apply disable where actual ≠ desired.
- **Live watch:** subscribes `LinuxHotplugMonitor` (Phase 2 component reused
  in-daemon). On Add event: match → guard re-check → re-apply.
- **Flicker race (accepted kernel reality):** the kernel natively probes/binds
  a replugged device milliseconds before the udev event reaches devmgrd; the
  surgical unbind follows. The frontend **never shows the flicker** because:
  (a) status merge uses the daemon's *desired* state (§6.1) — a replugged
  disabled device renders Disabled even while transiently bound; (b) the Phase
  2 hotplug debounce means enforcement typically completes before the
  debounced refresh runs. Suppression by source-of-truth merge, not timing.
- **Guard re-check on every re-apply:** topology may have changed (the
  disabled mouse may now be the sole pointer). Guard refusal ⇒ entry marked
  `guardSuspended`, surfaced in UI ("disabled — enforcement suspended:
  <reason>"), not enforced. Enforcement must never brick input.
- All failures **log-and-continue**; the loop never crashes the daemon.
- Threading: a single **apply mutex** serializes every controller+StateStore
  action; both the hotplug callback path and D-Bus verb handlers acquire it.
  Operations are short sysfs writes, so a mutex suffices — no extra worker
  thread/queue.

### 5.4 Controller mechanics (`SysfsDeviceController` v2)

- **USB disable:** `authorized=0` (unchanged from Phase 4 — device stays
  visible/enumerable) + StateStore entry (`mechanism: "authorized"`).
- **Non-USB disable:** read `driver` symlink → record name → write device id
  to the driver's `unbind` (`mechanism: "unbind"`).
- **Non-USB enable:** write `lastDriver` to the device's `driver_override` →
  write device id to bus `drivers_probe` → **clear `driver_override`**
  (scope-guarded: cleared even if probe fails; never left sticky). No
  `lastDriver` recorded ⇒ plain `drivers_probe`. Rationale: targeted rebind
  cannot accidentally bind a different driver when multiple match.

## 6. Privileged pipeline — IPC v2

### 6.1 D-Bus contract (`org.devmgr.Manager1`, ApiVersion 1 → 2)

| Method | Signature | Polkit action | Guard |
|---|---|---|---|
| `SetDeviceEnabled` | `(s path, b enabled)` | `org.devmgr.set-device-enabled` | disable-only (Phase 4), now all buses + StateStore |
| `UnbindDriver` | `(s path)` | `org.devmgr.manage-drivers` *(new)* | same `evaluateDisable` (unbind ≡ disable risk) |
| `BindDriver` | `(s path, s driver)` | `org.devmgr.manage-drivers` | none (additive) |
| `LoadModule` | `(s name)` | `org.devmgr.manage-modules` *(new)* | none (additive; kernel/SB arbitrates) |
| `UnloadModule` | `(s name)` | `org.devmgr.manage-modules` | `evaluateModuleUnload` (§6.3) |
| `ListDisabledDevices` | `() → aa{sv}` | none (read-only) | none |

- All actions `auth_admin_keep` (Phase 4 UX). Separate action IDs so admins
  can policy modules independently of device toggles.
- `ListDisabledDevices` is a **bulk fetch**: full entry array (key fields +
  `lastSysfsPath` + `guardSuspended`) in one call. Frontends call it once per
  refresh cycle and merge locally — never per-device D-Bus queries. Allowed
  without auth in the D-Bus policy (discloses nothing sysfs doesn't).
- Client `DbusPrivilegedChannel` checks `ApiVersion ≥ 2` once and caches;
  against an old daemon the new verbs fail with "daemon too old (API 1 < 2) —
  restart devmgrd", Phase 4 verbs keep working.

### 6.2 RequestProcessor

Gains `pal::IDriverManager&` and `StateStore&` alongside controller / prober /
authority. Pipeline order preserved for every verb: **validate → guard →
authorize → act** (a refused request never triggers a password prompt).

- Validation: module names must match `[A-Za-z0-9_-]+` (no separators/paths);
  sysfs paths get Phase 4 canonicalize+containment.
- `setDeviceEnabled(disable)`: controller success **then** StateStore write.
- `setDeviceEnabled(enable)`: StateStore entry deleted **then** rebind — a
  rebind failure leaves "enabled-but-unbound" with a clear error, never a
  lying store.
- `UnbindDriver`/`BindDriver` **never touch the StateStore** (surgical
  contract): a surgically unbound device that replugs comes back bound — by
  design. UIs label these "advanced, not persistent".

### 6.3 Module-unload guard — `evaluateModuleUnload`

Pure function beside `evaluateDisable`; authoritative in daemon, advisory in
frontends.

1. Map module → affected devices: `/sys/module/<name>/drivers/*/` → each
   driver's bound-device links (probed fresh per request).
2. Any affected device failing `evaluateDisable` (root/boot chain, sole
   keyboard/pointer) ⇒ **refuse** with that reason (e.g. unloading `xhci_hcd`
   holding the only keyboard).
3. `refCount > 0` / holders present ⇒ refuse pre-flight: "in use by
   <holders>" (friendlier than raw `EBUSY`; TOCTOU still mapped in §8).

Load has no guard: additive, and SB/lockdown enforcement is the kernel's job —
we surface its verdict, never duplicate it.

## 7. Read side

### 7.1 `KmodDriverManager` (`platform/linux`)

RAII `KmodContext` (kmod_new/kmod_unref). Injectable roots: `sysfsRoot` (as
`SysfsDeviceController`) + libkmod's own `dirname`/`config_paths` — config
parsing and modalias lookup are testable against plain-text fixtures
(`modules.alias`, `modules.dep`, fixture modprobe.d) with no real `.ko` files.

- `driversFor(id)`: device → `driver` symlink → `/sys/bus/.../drivers/<d>/module`
  link → bound module; **plus** modalias lookup for the full candidate list.
  No module link ⇒ `kind=Builtin` ("built into kernel", no unload offered).
  Fill: version, path, dependencies, loaded (initstate), `isSigned`+`signer`
  (modinfo `sig_id`/`signer`).
- `listLoadedModules()`: `kmod_module_new_from_loaded` → name, size, refcount,
  holders.
- `moduleInfo(name)` / `modprobeInfo(name)`: lookup + modinfo; options and
  blacklist via **libkmod's config iterators** — we never parse modprobe.d
  ourselves; what we display is what the system would do.
- Write side (daemon-only callers):
  - `loadModule` = `kmod_module_probe_insert_module` with
    `KMOD_PROBE_APPLY_BLACKLIST | KMOD_PROBE_IGNORE_COMMAND`.
  - `unloadModule` = `kmod_module_remove_module`.
- **Perf (S77 low-end discipline):** signature/version read the `.ko` from
  disk (~1–5 ms × ~100 modules) — never in the refresh loop. Modules view
  fills the SIGNED column **asynchronously**: runtime fields render instantly;
  one background task fills a name+path-keyed signature cache (invalidated on
  load/unload). Selected-module detail fetches eagerly.

### 7.2 `LinuxSystemInfo` (`platform/linux`)

- `secureBoot`: efivars `SecureBoot-8be4df61-…` variable, byte 4 (past the
  4-byte attribute header) == 1; no efivars ⇒ `false` (BIOS boot).
- `lockdownMode`: `/sys/kernel/security/lockdown` bracket parse
  (`[none] integrity confidentiality`); file absent ⇒ `"none"`.
- `osVersion`: `/etc/os-release` `PRETTY_NAME`; `kernelVersion`: `uname(2)`.
- `rebootPending`: honest `false` stub (Phase 6).
- All paths injectable for fixture-file tests.

## 8. Error handling

### 8.1 Module load taxonomy

| Condition | Detection | User-facing result |
|---|---|---|
| Blacklisted | positive return with `KMOD_PROBE_APPLY_BLACKLIST` | "blacklisted by modprobe.d" — distinct from failure |
| Has `install` command | pre-flight `kmod_module_get_install_commands`; `KMOD_PROBE_IGNORE_COMMAND` as belt-and-braces | Refused: "module has a modprobe.d install rule (`<cmd>`); devmgrd does not execute shell commands — use modprobe". Preserves no-fork/exec posture (libkmod v33 docs: default probe executes install commands via shell; custom handling recommended for security-sensitive apps) |
| Dependency failed | negative errno → walk `kmod_module_get_dependencies`, check each dep's initstate to identify the culprit | "dependency '<dep>' rejected: unsigned module (Secure Boot / lockdown: <mode>)" — dependency failures bubble up named |
| Unsigned under SB/lockdown | `EKEYREJECTED`/`ENOKEY`/`EPERM` | Message includes actual lockdown mode from `LinuxSystemInfo` |
| Not found | `ENOENT` | "module not found for kernel `<uname -r>`" |
| Already loaded | probe returns success | Idempotent no-op, reported "already loaded" |

### 8.2 Other paths

- **Unload:** holders pre-flighted by guard; TOCTOU `EBUSY` still mapped to
  "in use by <holders>". Builtin ⇒ refused upfront ("built into the kernel").
- **Bind/unbind:** device vanished mid-op ⇒ clean `ENOENT`; unwilling driver ⇒
  "driver <x> rejected the device". `driver_override` cleared via scope guard
  even on probe failure.
- **Enforcement:** log-and-continue; guard-suspension (§5.3); StateStore write
  failure keeps desired state in memory, retries at next mutation.
- **Daemon down:** reads unaffected; mutations get Phase 4-style
  unavailability messages; status merge degrades — disabled PCI devices show
  as driverless (documented).
- **Stale API-1 daemon:** "devmgrd too old (API 1 < 2) — restart the daemon."
- Daemon refusal messages always win over frontend advisory predictions
  (Phase 4 pattern).

## 9. UI surface (TUI + GUI, strict parity)

### 9.1 `ApplicationFacade`

- Reads (sync): `driverInfo(id)` (bound + candidates), `listModules()`,
  `moduleDetail(name)` (incl. `ModprobeInfo`), `systemInfo()` (cached per
  refresh).
- Mutations (async via TaskScheduler + `TaskCompletedEvent`, Phase 4 pattern):
  `loadModule`, `unloadModule`, `bindDriver`, `unbindDriver`.
- Advisory guards: existing `canDisable` reused for unbind; new
  `canUnloadModule` (holders + criticality advisory).
- Refresh pipeline: one bulk `listDisabledDevices()` per refresh; mapper
  merge: matched ⇒ `Disabled`; `guardSuspended` ⇒ `Disabled` + detail note
  "enforcement suspended: <reason>".

### 9.2 `ModulesVM`

New toolkit-agnostic VM beside `DeviceListVM` (same seam pattern): rows,
name-substring filter, selection, signature-cache join, rebuild hooks.
Mutations publish events → auto-refresh both UIs (Phase 4 pattern).

### 9.3 TUI

- `m` toggles Device list ⇄ Modules screen. Banner:
  `Secure Boot: ON · Lockdown: integrity — unsigned modules will be rejected`.
- Table `NAME | SIZE | REFS | USED BY | SIGNED`; `/` filter; Enter → detail
  pane (version, path, signer, depends, modprobe.d options, blacklisted);
  `l` load (FTXUI input, charset-validated); `u` unload (confirm + advisory
  refusal reason).
- Device detail: Driver section (driver, module, version, signed+signer,
  depends, **compatible candidates from modalias**); `U` unbind / `B` bind
  (input prefilled with last known driver; candidates listed) — labeled
  *advanced, not persistent*.

### 9.4 GUI

- `ModuleListModel` (QAbstractTableModel over ModulesVM, mirrors
  `DeviceListModel`) in a Modules tab/dock; toolbar **Load Module…** (input
  dialog), **Unload** (confirm + advisory text), filter box, SB/lockdown
  banner label.
- Device detail: Driver group box + "Unbind driver (advanced)" / "Bind
  driver…" (editable combo pre-populated with modalias candidates) with
  confirm dialogs and advisory-guard tooltips (Phase 4 interaction pattern).
- Identical advisory wording across both UIs (Phase 3/4 parity principle).

## 10. Testing & exit gate

### 10.1 Unit (host, all fakes)

- `evaluateModuleUnload` matrix (critical device via module, holders,
  refcount, clean unload).
- RequestProcessor new verbs: validation → guard → authorize ordering, store
  writes, surgical-verbs-don't-persist (FakeDriverManager, FakeController,
  FakeAuthority, tmp StateStore).
- StateStore: round-trip, atomic rename, corruption → `.bad` sidecar, entry
  removal on enable, cloned-serial downgrade.
- DeviceKey matching: serial tier, positional tier, vid:pid validation
  failure, multi-match enforcement.
- EnforcementService: startup sweep, hotplug re-apply, guard-suspension,
  log-and-continue (FakeMonitor, FakeController).
- Mapper merge: `listDisabledDevices` ⇒ Disabled, `guardSuspended` note,
  daemon-down degradation.
- ModulesVM: rows, filter, async signature-cache join.
- Facade methods via extended `FakePrivilegedChannel` + `FakeDriverManager`.
- `LinuxSystemInfo` parsers via fixture files (efivar bytes, lockdown
  brackets, os-release).

### 10.2 KmodDriverManager focused tests (host, no real .ko)

libkmod's injectable `dirname`/`config_paths` accept plain-text fixtures:
modalias lookup (`modules.alias`), dependency listing (`modules.dep`),
blacklist/options parsing (fixture modprobe.d), sysfs driver→module resolution
+ builtin detection (tmp sysfs root). Signature parsing requires real modules
→ container/VM.

### 10.3 Container integration

- Extended IPC round-trip (tests/ipc, private bus, FakeAuthority): all new
  verbs + bulk `ListDisabledDevices`.
- Docker adds `libkmod-dev`; format/tidy gates extended to new files
  (sdbus purity guard unchanged).

### 10.4 VM smoke (extends `test/vm` + `./test-vm.sh`)

Load `dummy` → verify in `/sys/module` → unload; blacklist-fixture refusal;
disable a virtio device via unbind → inspect `state.json` → restart devmgrd →
verify startup-sweep re-applied → re-enable (verify `driver_override` path) →
print `PHASE5 VM SMOKE OK`.

### 10.5 Host manual smoke (exit gate, Gentoo/OpenRC)

1. Modules view truthfulness: real signatures, SB/lockdown banner.
2. Load/unload `dummy` with real polkit prompt + `auth_admin_keep` caching.
3. Guard refusal on a root-critical module (clear reason shown).
4. **Disable spare USB → physically replug → stays Disabled, no visible
   flicker.**
5. PCI-safe device unbind/rebind round-trip (surgical verbs).
6. Persistence: disable → restart devmgrd → still Disabled.
7. Daemon-down negatives (reads fine, mutations fail cleanly).
8. TUI/GUI parity on all of the above.

### 10.6 Perf gate (S77 discipline)

Modules view first paint never waits on signature fill; device refresh adds
exactly one D-Bus bulk call.

## 11. Dependencies & build

- **libkmod** via pkg-config: Gentoo host `sys-apps/kmod` (already installed —
  it provides modprobe); Docker/Ubuntu `libkmod-dev`. Linked by
  `platform/linux` and (transitively) devmgrd + frontends.
- **nlohmann-json** via vcpkg (header-only): StateStore serialization.
- CI: Dockerfile + docs updated; VM script extended.

## 12. Risks & accepted limitations

- **Flicker race** (kernel binds before enforcement unbinds): unavoidable with
  netlink-based udev; hidden from UI by desired-state merge + debounce (§5.3).
- **USB bus numbers** can theoretically change across reboots (controller
  enumeration order), which would perturb Tier-2 positional matching for
  serial-less devices; vid:pid validation prevents wrong-device enforcement.
  Accepted; rare on fixed hardware.
- **Daemon-down = no enforcement** until Phase 8 autostart; disabled PCI
  devices display as driverless when daemon is down. Documented degradation.
- **Identical serial-less devices** at different ports are distinct entries
  (honest; matches Windows).
- **Enforcement vs D-Bus concurrency:** serialized in-daemon by the apply
  mutex (§5.3).
- Cadence unchanged: user commits every task; container/VM runs are the
  user's (no Docker daemon or real hardware in-agent).
