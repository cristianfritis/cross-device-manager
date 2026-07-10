## Enable/Disable (Phase 4)

Cross-device-manager can disable and re-enable a **USB** device from either UI.
It toggles the kernel's per-device `authorized` sysfs attribute only — no other
bus is mutated this phase. The mutation always travels through a polkit-gated
root helper (`devmgrd`); the UIs never write sysfs directly.

## Driver & Module Management (Phase 5)

Phase 5 extends enable/disable to every bus and adds kernel-module management:

- **Universal, persistent enable/disable with active enforcement.** Non-USB
  devices (PCI, virtio, …) are disabled by unbinding their driver; USB keeps
  the `authorized` mechanism. Re-enable performs a targeted `driver_override`
  rebind. A disable now *sticks*: the daemon records the device identity in
  its state store, re-applies it when the device reappears (replug/hotplug),
  and sweeps on startup — so a disabled device stays disabled across replugs
  and daemon restarts.
- **Module load/unload with Secure Boot/lockdown awareness.** A **Modules**
  view in both UIs lists loaded kernel modules (the SIGNED column fills
  asynchronously) under a banner showing the machine's Secure Boot and kernel
  lockdown state. Load/unload travel through the same polkit-gated helper;
  unloading a module that backs a critical device is refused with a reason,
  and modules blacklisted in `modprobe.d` refuse to load.
- **Surgical bind/unbind.** Advanced per-device driver bind/unbind verbs that
  deliberately leave no persistent state — a replug returns the kernel
  default binding.

New runtime dependency: **libkmod** (module enumeration and load/unload). The
daemon persists its state under `/var/lib/devmgrd`; override the directory
with `--state-dir PATH` for testing (never in production).

### Running it on a real host

The helper is not installed by a service manager yet — run it in the
foreground:

1. Install the D-Bus and polkit data files:
   ```sh
   sudo install -m644 daemon/data/org.devmgr.Manager1.conf /etc/dbus-1/system.d/
   sudo install -m644 daemon/data/org.devmgr.policy /usr/share/polkit-1/actions/
   ```
2. Start the helper (watch its log lines):
   ```sh
   sudo ./build/linux-debug/daemon/devmgrd
   ```
3. In a **graphical session**, launch either UI and select a device:
   - TUI: `./build/linux-debug/tui/devmgr-tui` — press `e`, confirm with `y`.
   - GUI: `./build/linux-debug/gui/devmgr-gui` — use the **Disable/Enable**
     toolbar (or right-click context) action and confirm the dialog.

### Authorization

The polkit action uses `auth_admin_keep`: the first disable/enable prompts for
an administrator password, and the grant is cached (~5 minutes) so the reverse
toggle needs no new prompt. **A desktop polkit agent must be running** —
pure-console authentication is not supported this phase, so run the UIs from a
graphical session.

### Safety guard

Before sending a *disable*, both UIs consult an advisory guard (the daemon
re-checks authoritatively on every request). It refuses, with a reason, to
disable devices that would break the running system:

- a device backing the root filesystem ("backs the root filesystem"),
- the only keyboard or only pointer ("would disable the only keyboard").

Refused devices show the reason on the TUI status line and as a greyed-out
action with a tooltip in the GUI. Re-enabling is never guarded.
