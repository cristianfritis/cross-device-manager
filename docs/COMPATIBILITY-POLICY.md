# devmgr compatibility & versioning policy

The rules that let a devmgr client and daemon of different ages talk to each
other, and the rules for evolving the on-disk snapshot format without stranding
data. Two independent version lines are covered:

1. **D-Bus `ApiVersion`** — the client↔daemon wire contract.
2. **Snapshot `formatVersion`** — the on-disk snapshot document format.

This document is authoritative. The constants it cites are the single source of
truth; if code and this doc disagree, the code wins and this doc is the bug.

Scope: the `org.devmgr.Manager1` interface and the `JsonSnapshotStore` on-disk
layout. Client-side firmware/DKMS queries (run unprivileged in the client, not
over IPC) are out of scope.

---

## 0. Supported platforms

| Platform | Surfaces | What works |
|---|---|---|
| Linux | `devmgr-gui`, `devmgr-tui`, `devmgr`, `devmgrd` | Everything: inventory, enable/disable, driver and module management, firmware updates, snapshots |
| Windows 10 version 1809+ / Windows 11 | `devmgr-gui`, `devmgr` | **Read-only.** Device inventory with detail, live hotplug, system information |
| Windows 10 versions 1607–1803 | `devmgr` | **Read-only, command line only.** The GUI is not supported below 1809 |
| Windows 8.1 and earlier | — | **Unsupported** |
| macOS | — | Unsupported |

### What "read-only" means on Windows

The Windows backend implements device enumeration, hotplug notification, and
system information. It implements **no** device control, driver management,
privileged channel, update providers, or criticality probing. Those are not
disabled features waiting to be switched on — the code paths do not exist, and
any such call returns `Unsupported` without contacting the operating system.

In the application this shows as: Devices populated and live, and the Modules,
Updates, and Snapshots views each carrying a sentence naming why they are not
available on this platform, at information severity. The Devices toolbar offers
only `Refresh`; the mutating verbs are **absent**, not greyed out (see
`docs/DESIGN.md` §5.3 — platform inapplicability hides a verb, runtime
unavailability disables it).

No device-mutating capability may be added on Windows until a real criticality
prober exists there. Without one every device classifies at the lowest tier and
the shared safety guard cannot refuse anything — including a request to disable
the only keyboard attached to the machine.

### The two Windows floors, and why there are two

- **Windows 10 version 1607** is the floor for the backend and the CLI. The
  device notification interface the hotplug monitor requires
  (`CM_Register_Notification`) does not exist before it, and the alternatives —
  a hidden window with `WM_DEVICECHANGE`, or polling — were rejected for needing
  a message pump the CLI has not got, or for being visibly laggy. The Windows
  build refuses to configure for an earlier target, naming this minimum.
- **Windows 10 version 1809** is the floor for the GUI, because that is the
  minimum Qt 6 officially supports. Qt will very likely run at 1607, but a
  defect there has no upstream recourse, and a support statement this project
  cannot honour is worse than a narrower one that it can.

Between 1607 and 1803 the CLI is supported and the GUI is not. Formally dropping
Windows 8.1 and pre-1607 Windows 10 is the intended reading of this table, not
an accident of which API was convenient.

---

## 1. D-Bus `ApiVersion` — additive-only

`ApiVersion` is a single monotonically-increasing `uint32`, exposed by the
daemon as a read-only D-Bus property and defined once at
`platform/linux/include/devmgr/platform/linux/dbus_contract.hpp:23`
(`kApiVersion`). It is **not** semver: there is no minor/patch split and no
"major" break is planned. Every bump is additive.

### Version history

| Version | Release | Added (all prior verbs unchanged) |
|---|---|---|
| 2 | Phase 4/5 | Enumeration, enable/disable, driver bind/unbind, `ListDisabledDevices`, module load/unload |
| 3 | Phase 7 | Snapshot verbs — `SnapshotList() -> s`, `SnapshotCreate(s label) -> s`, `SnapshotRestore(s id) -> s`, `SnapshotDelete(s id)` |
| 4 | beta-06 | `SnapshotDiff(s base_id, s target_id) -> s` (empty `target_id` ⇒ live state); the `org.devmgr.Error.InvalidArgs` wire error name |

### The additive-only rule

A new `ApiVersion` MAY only:

- **add a verb** with a fresh method name, or
- **add a read-only property**, or
- **add a wire error name** to the `org.devmgr.Error.*` table.

A new `ApiVersion` MUST NOT, without a coordinated flag-day the beta explicitly
does not budget for:

- change the signature or argument order of an existing verb,
- change the observable semantics of an existing verb,
- remove or rename a verb, property, or error name,
- repurpose an existing error name for a different failure class.

Because every change is additive, an **older client keeps working against a
newer daemon** unchanged (it simply never calls the verbs it doesn't know), and
that is the guarantee the version line exists to protect.

### How a client negotiates — `ensureApi(N)`

Each client verb declares the *minimum* `ApiVersion` that introduced it and
gates on it before the call:

```
snapshotList/Create/Restore/Delete → ensureApi(3)
snapshotDiff                        → ensureApi(4)
module/driver/disabled-list verbs   → ensureApi(2)
```

`DbusPrivilegedChannel::ensureApi(minVersion)`
(`platform/linux/src/dbus_privileged_channel.cpp:57`) reads the daemon's
`ApiVersion` property once, caches it, and returns
`core::Error::Code::Unsupported` (with `apiTooOldMessage`) when the daemon is
older than the verb needs. **Rules for authors:**

- A verb gates on the version that *introduced* it — never a higher one. Adding
  a verb at v5 must not raise the floor of a v3 verb.
- A newer client against an older daemon degrades to a clean `Unsupported`
  refusal on the missing verb, not a crash or a hang.

### How error-name compatibility stays additive

`coreErrorFor()` (`dbus_contract.hpp:53`) maps a wire error name back to a
domain `Error::Code`, and **collapses any name it does not recognise to `Io`**.
This is what makes a new error name additive: when the v4 daemon throws
`org.devmgr.Error.InvalidArgs` at a pre-v4 client, that client falls through to
`Io` and reports an I/O failure — degraded but safe — instead of
misclassifying it. The throw side (`dbusErrorNameFor()`, same header) and the
catch side must be edited together, and a new name is only ever *added* to the
table, never reassigned.

The same function also normalises daemon-unavailable transport errors (masked
unit, no bus owner, spawn failure, timeout) to `Busy`, which the CLI routes to
exit code 4 (unreachable). Those are transport names owned by systemd/D-Bus, not
part of the `ApiVersion` contract, but they follow the same discipline: match on
the stable error *name*, never on message text.

---

## 2. Snapshot `formatVersion` — migration policy

Snapshot documents carry an integer `formatVersion`. The current value is
`core::kSnapshotFormatVersion = 1`
(`core/include/devmgr/core/snapshot_models.hpp:15`).

### The reject-forward rule

> Readers MUST reject documents whose `formatVersion` is greater than the
> reader's `kSnapshotFormatVersion`.

Enforced at `daemon/src/snapshot_store.cpp:231`. A too-new document is surfaced
in listings with `SnapshotHealth::Unsupported`
(`snapshot_models.hpp:39-42`): it is **never** restored, **never** deleted, and
its file is **left untouched**. This is deliberate — an older daemon that meets
a newer snapshot (e.g. after a downgrade) must not corrupt or discard data it
does not understand. Corruption is a separate axis: a content-hash mismatch
yields `SnapshotHealth::Corrupt` and quarantines the file to
`<name>.bad-<timestamp>` (evidence rule — never deleted).

### Forward/backward guarantees

- **Format v1 is forward-compatible for read.** A v0.5 daemon must start and
  `SnapshotList` after a downgrade; anything beyond that (restore of a snapshot
  it wrote) is the only expectation. This is the downgrade contract the
  acceptance upgrade-matrix checks.
- Snapshot ids are the **SHA-256 of the canonical payload serialization**, and
  parents link snapshots into a linear history. Id-addressing is what makes
  migration safe: re-serializing a payload under a new format yields a *new* id,
  so migration writes new snapshots and leaves the originals in place as
  evidence rather than mutating them.

### Rules for bumping `formatVersion` to v2+

Bump only when the payload shape must change in a way a v1 reader cannot ignore.
A bump ships with **all** of:

1. A one-way migrator (or dual-read support) that re-reads v1 documents. Because
   ids are content hashes, a migrator that changes bytes produces new ids; it
   MUST NOT delete the v1 originals — leave them for the reject-forward path.
2. The reject-forward guarantee preserved: the new reader still refuses
   documents newer than *it* knows, with the file untouched.
3. An acceptance check that an older daemon meeting the new format degrades to
   `Unsupported`, not corruption.

### What is *not* a format change

The storage backend swap planned post-beta (content-addressed backend behind the
`ISnapshotStore` interface — `daemon/include/devmgr/daemon/snapshot_store.hpp`)
is an *implementation* change, not a format change, as long as the on-disk
document format stays v1. It requires no `formatVersion` bump.

---

## Change checklist

Adding a verb, property, or error name:

- [ ] `kApiVersion` bumped by exactly one; the history table above updated.
- [ ] New verb gates on the version that introduced it via `ensureApi(N)`; no
      existing verb's floor changed.
- [ ] New error name added to *both* `dbusErrorNameFor()` and `coreErrorFor()`;
      no existing name repurposed.
- [ ] No existing verb signature or semantics touched.

Changing the snapshot format:

- [ ] `kSnapshotFormatVersion` bumped; reject-forward test still passes.
- [ ] Migrator/dual-read in place; v1 originals preserved (new id, not in-place
      mutation).
- [ ] Downgrade acceptance check confirms an older daemon reports `Unsupported`,
      not corruption.
