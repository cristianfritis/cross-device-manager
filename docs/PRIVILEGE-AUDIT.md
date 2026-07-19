# devmgrd privilege audit

What the privileged daemon reads, writes, and executes — and, for each entry, the
capability or sandbox allowance it forces. This document is the justification
side of `daemon/data/devmgrd.service`: every allowance in the unit must appear
here, and every need here must be satisfied by exactly one allowance.

Scope: `devmgrd` only. The GUI/TUI/CLI clients run unprivileged as the calling
user and are not covered — notably **fwupd and DKMS are queried from the client
process**, not from the daemon, so firmware update machinery imposes no
privilege on `devmgrd`.

Audited against the tree at beta-06 task 1.3 (`daemon/`, `platform/linux/`).

## Method

- Write surface: every `ofstream`/`open(2)`/`filesystem::remove|rename` call in
  `daemon/src` and `platform/linux/src`.
- Execute surface: grep for `fork`, `exec*`, `system(`, `popen`, `posix_spawn`.
- Socket surface: every sd-bus connection and every netlink socket.
- Privileged syscalls: libkmod module load/unload.

## Reads

| Path | Why | Allowance |
|---|---|---|
| `/sys/devices/**`, `/sys/bus/**`, `/sys/class/**` | udev enumeration, device attributes, driver links, `authorized`/`driver_override` current values | readable under `ProtectSystem=strict` |
| `/sys/kernel/security/**` | kernel lockdown state — `KmodDriverManager` refuses module ops under lockdown (`kmod_driver_manager.hpp`) | root-owned `0700` securityfs; owner access, no capability |
| `/proc/self/mounts` | `LinuxCriticalityProber` — refuses to disable a device backing a mounted filesystem | readable; `ProtectProc` not set |
| `/etc/modprobe.d/**` | libkmod config resolution (blacklist/options/install rules) and snapshot capture of devmgr-owned files | readable; also in `ReadWritePaths` (see Writes) |
| `/lib/modules/**` (incl. `modules.dep`, `.ko` files) | libkmod module resolution and load | readable under `ProtectSystem=strict` |
| `/run/udev/**` | libudev device database | readable |
| `/var/lib/devmgrd/**` | snapshot store + persisted desired state | `StateDirectory=devmgrd` |

Not read: user home directories (`ProtectHome=yes`), `/tmp` beyond the private
instance (`PrivateTmp=yes`), any network resource.

## Writes

| Path | Operation | Call site |
|---|---|---|
| `<device>/authorized` | enable/disable (USB mechanism) | `sysfs_device_controller.cpp` |
| `<bus>/drivers/<driver>/bind`, `.../unbind` | driver bind/unbind | `sysfs_device_controller.cpp` |
| `<device>/driver_override` | driver pinning | `sysfs_device_controller.cpp` |
| `<bus>/drivers_probe` | re-probe after override | `sysfs_device_controller.cpp` |
| `/etc/modprobe.d/devmgr-*.conf` | write + remove, **devmgr-owned filenames only** (`isDevmgrOwned` gate), during snapshot restore write-back | `snapshot_service.cpp` |
| `/var/lib/devmgrd/{HEAD,state.json,snapshots/*.json}` | atomic write (tmp + fsync + rename) | `atomic_file.cpp`, `snapshot_store.cpp`, `state_store.cpp` |

All targets are root-owned with owner-write permission, so no `CAP_DAC_*` is
required to write them. Device sysfs paths are dynamic (`/sys/devices/<any
topology>`), so `ReadWritePaths` cannot be narrowed below the subtree level.

## Executes

**Nothing.** There is no `fork`, `exec*`, `system()`, `popen`, or `posix_spawn`
anywhere in `daemon/` or `platform/linux/`. Module operations go through libkmod
syscalls, not the `modprobe` binary — which is also why a module carrying a
modprobe.d `install` command rule is refused outright rather than honored
(`kmod_driver_manager.cpp`: "devmgrd does not execute shell commands").

This is what makes `NoNewPrivileges=yes` and `MemoryDenyWriteExecute=yes` free
of cost here.

## Sockets

| Family | Use | Allowance |
|---|---|---|
| `AF_UNIX` | system bus (own `org.devmgr.Manager1`), polkit authorization checks | `RestrictAddressFamilies=AF_UNIX` |
| `AF_NETLINK` | libudev hotplug monitor, `udev` multicast group (`udev_hotplug_monitor.cpp`) — the userspace group, not the kernel one, so no `CAP_NET_ADMIN` | `RestrictAddressFamilies=AF_NETLINK` |

No IP sockets. `PrivateNetwork=` is deliberately **not** set: a private network
namespace would cut the daemon off from systemd-udevd's uevent broadcasts and
silently kill hotplug.

## Capabilities

Required:

- **`CAP_SYS_MODULE`** — `init_module`/`finit_module`/`delete_module` via
  libkmod. Module load/unload is a spec'd product capability.

Dropped from the previous (beta-05) bounding set, with reasons:

| Capability | Why it is not needed |
|---|---|
| `CAP_SYS_ADMIN` | No mount, no namespace, no BPF, no quota, no kexec. Module ops need `CAP_SYS_MODULE`, not this. |
| `CAP_DAC_OVERRIDE` | Every write target is root-owned and owner-writable; permission comes from the mode bits, not the capability. |
| `CAP_DAC_READ_SEARCH` | Same for reads, including the `0700` securityfs directory (owned by root). |
| `CAP_CHOWN` | The daemon never changes ownership — no `chown` call exists. |
| `CAP_FOWNER` | The daemon never changes permissions or overrides owner checks — no `chmod` call exists. |

Correspondingly `CapabilityBoundingSet=CAP_SYS_MODULE`.

## Deliberately-off protections

Two sandbox directives that would otherwise be standard are **off**, because the
product is exactly the thing they block:

- **`ProtectKernelTunables=no`** — it remounts `/sys` read-only. Writing sysfs
  attributes (`authorized`, bind/unbind, `driver_override`) is the core feature.
- **`ProtectKernelModules=no`** — it blocks module load/unload, a spec'd
  capability with its own polkit action.

Both are stated explicitly in the unit rather than merely omitted, so that an
audit of the unit maps line-for-line onto this document.

## Verification

The sandbox is only real if the privileged flows still work under it. Because
the development host runs OpenRC, verification happens in the VM:

    test/vm/phase8-sandbox-smoke.sh <sysfs path of a spare USB device>

It exercises disable/enable, module load/unload, snapshot restore write-back,
and then scans the journal for sandbox denials. The full acceptance suite
(beta-06 task 6.1) repeats this against installed artifacts.

If a sysfs write is denied in the VM, the fallback is widening `ReadWritePaths`
from `/sys/bus /sys/devices` to `/sys` — record the reason here if that happens.
