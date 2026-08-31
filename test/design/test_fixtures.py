"""Focused tests for the two determinism fixtures (tasks 3.3 and 3.5).

These do not go through the GUI or the TUI. They assert the fixtures themselves
behave, so that a sweep failure can be attributed to the surfaces under test
rather than to a posture that silently failed to apply -- the same reasoning as
the harness-fault check in capture.py.

Run inside a session:  session.sh python3 test_fixtures.py
"""
from __future__ import annotations

import json
import os
import shlex
import subprocess
import sys

CLI = "/src/build/linux-debug/cli/devmgr"
FIXTURE = "/src/test/design/fixtures/devices.umockdev"

# Exactly what fixtures/devices.umockdev declares. Asserting the SET, not a
# count, so a fixture edit that drops a row fails loudly instead of passing.
EXPECTED_NAMES = {
    "Fixture Root Hub",
    "Fixture Extremely Long Device Name For Elision Testing Across Both Surfaces 0123456789",
    "Fixture Deeply Nested Device",
    "Fixture Deauthorized Device",
    "Fixture Display Controller",
}
LONG_NAME = "Fixture Extremely Long Device Name For Elision Testing Across Both Surfaces 0123456789"
LONG_PATH = "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-3/1-3.1/1-3.1.4/1-3.1.4.2/1-3.1.4.2.7"

failures: list[str] = []


def check(cond, msg):
    if not cond:
        failures.append(msg)
    print(("  ok   " if cond else "  FAIL ") + msg)


# ---------------------------------------------------------------- 3.5 devices
def test_device_fixture():
    print("[3.5] deterministic umockdev device set")
    out = subprocess.run(
        shlex.split(f"umockdev-run -d {FIXTURE} -- {CLI} devices list --json"),
        capture_output=True, text=True)
    if out.returncode != 0:
        check(False, f"devices list failed: {out.stderr.strip()[:200]}")
        return
    devices = json.loads(out.stdout)
    if isinstance(devices, dict):
        devices = devices.get("devices", [])
    names = {d.get("name", "") for d in devices}
    paths = {d.get("identity", "") for d in devices}
    status = {d.get("name", ""): d.get("status", "") for d in devices}

    check(names == EXPECTED_NAMES,
          f"device set is exactly the fixture (got {len(names)}: {sorted(names)[:2]}…)")
    check(LONG_NAME in names, "12.2 long-device-name row is present")
    check(LONG_PATH in paths, "12.2 long-path row is present")
    # The host machine's own devices must not leak through the sandbox.
    check(not any(n == "PCI device" for n in names),
          "no host-enumerated device leaked into the sandbox")
    # authorized=0 on a usb device maps to Disabled -- a second status value in
    # the sweep, which a host that happens to have none would never exercise.
    check(status.get("Fixture Deauthorized Device") == "Disabled",
          "authorized=0 device reports Disabled, not Active")
    check(status.get("Fixture Root Hub") == "Active",
          "authorized=1 device still reports Active")
    check({d.get("bus", "") for d in devices} == {"USB", "PCI"},
          "both fixture buses are enumerated")


def _owns(name):
    got = subprocess.run(
        ["gdbus", "call", "--system", "--dest", "org.freedesktop.DBus",
         "--object-path", "/org/freedesktop/DBus",
         "--method", "org.freedesktop.DBus.NameHasOwner", name],
        capture_output=True, text=True)
    return got.returncode == 0 and "true" in got.stdout


def test_degraded_is_actually_degraded():
    """The degraded posture must be degraded.

    run.sh drives several sessions in one container, so without teardown a
    previous HEALTHY session's system bus survives and this posture silently
    verifies the wrong state. That happened once; this check is why it cannot
    happen quietly again.
    """
    print("[posture] degraded really has no backends")
    check(not _owns("org.freedesktop.fwupd"),
          "fwupd is NOT on the bus in the degraded posture")
    check(not _owns("org.devmgr.Manager1"),
          "devmgrd is NOT on the bus in the degraded posture")


# ---------------------------------------------------------------- 3.3 fwupd
def test_fwupd_stub():
    print("[3.3] fwupd double on the system bus")
    got = subprocess.run(
        ["gdbus", "call", "--system", "--dest", "org.freedesktop.fwupd",
         "--object-path", "/", "--method", "org.freedesktop.fwupd.GetDevices"],
        capture_output=True, text=True)
    if got.returncode != 0:
        check(False, f"GetDevices failed: {got.stderr.strip()[:200]}")
        return
    payload = got.stdout
    check("fixture-system-firmware" in payload, "seeded updatable device is served")
    check("fixture-dock" in payload, "seeded supported-only device is served")
    check("Fixture Vendor" in payload, "seeded vendor is served")


if __name__ == "__main__":
    if os.environ.get("POSTURE") == "healthy":
        test_fwupd_stub()
    else:
        test_degraded_is_actually_degraded()
    test_device_fixture()
    print()
    if failures:
        print(f"FIXTURE TESTS: {len(failures)} failure(s)")
        for f in failures:
            print("  " + f)
        sys.exit(1)
    print("FIXTURE TESTS: all passed")
