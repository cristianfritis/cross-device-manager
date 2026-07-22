#!/usr/bin/env bash
# Release acceptance suite (acceptance-suite spec, beta-06 task 6.1) — run INSIDE
# the disposable VM as root, NEVER on a host. Exercises the ARTIFACT a tester
# receives (the installed deb), end to end, through OUR stack — never the build
# tree. Root is implicitly polkit-authorized, so no agent is needed.
#
# Usage: acceptance.sh <path to devmgr_*.deb> <sysfs path of a spare USB device> [module]
#
# Scenarios (each failure fails the run):
#   1. install the deb; every packaged file (incl. devmgr-fwupd-smoke) is placed
#   2. D-Bus activation starts devmgrd on the first call (no manual start)
#   3. both UIs enumerate devices (--self-test)
#   4. firmware update check + install through the installed devmgr-fwupd-smoke
#      against fwupd's test remote (fakedevice 1.2.2 -> 1.2.4)
#   5. device disable + snapshot restore round-trip (restore re-enables)
#   6. driver blacklist round-trip through a manual snapshot (config-level)
#   7. hotplug reaction: hot-remove the USB device, re-enumerate, count drops
#   8. CLI recovery path: daemon down (masked, non-activatable) -> the recovery
#      CLI exits 4 on the system bus (snapshot-cli spec)
#   9. journal carries no sandbox denial for any of the above
# Ends with an explicit ACCEPTANCE OK (all scenarios passed).
set -euo pipefail
DEB=${1:?usage: acceptance.sh <deb> <usb sysfs path> [module]}
USBDEV=${2:?usage: acceptance.sh <deb> <usb sysfs path> [module]}
MODULE=${3:-dummy}
DEB=$(readlink -f "$DEB")
[ -f "$DEB" ] || { echo "no deb at $DEB"; exit 1; }
[ "$(id -u)" -eq 0 ] || { echo "run as root"; exit 1; }
[ -f "$USBDEV/authorized" ] || { echo "no authorized attr at $USBDEV"; exit 1; }

# shellcheck source=test/vm/lib/fwupd-test-device.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib/fwupd-test-device.sh"

self_test_rows() {  # $1 = binary, extra args after; echoes the row count
    "$@" | grep -oE 'self-test rows: [0-9]+' | grep -oE '[0-9]+$'
}

# Reconcile anything a previous source-based phase smoke left behind (Phase 5
# stale-policy lesson) so the install is against a clean slate.
pkill -x devmgrd 2>/dev/null || true
rm -f /etc/dbus-1/system.d/org.devmgr.Manager1.conf
rm -f /usr/share/polkit-1/actions/org.devmgr.policy
rm -rf /var/lib/devmgrd
rm -f /etc/modprobe.d/devmgr-accept*.conf
systemctl daemon-reload
SINCE=$(date '+%Y-%m-%d %H:%M:%S')   # journal-scan window start (scenario 9)

echo "==> [1/9] apt install the release deb"
apt-get install -y "$DEB"
for f in /usr/bin/devmgrd /usr/bin/devmgr /usr/bin/devmgr-tui /usr/bin/devmgr-gui \
         /usr/bin/devmgr-fwupd-smoke \
         /usr/lib/systemd/system/devmgrd.service \
         /usr/share/dbus-1/system-services/org.devmgr.Manager1.service \
         /usr/share/dbus-1/system.d/org.devmgr.Manager1.conf \
         /usr/share/polkit-1/actions/org.devmgr.policy; do
    [ -e "$f" ] || { echo "missing after install: $f"; exit 1; }
done
devmgrd --version

echo "==> [2/9] D-Bus activation (no manual start)"
if systemctl is-active --quiet devmgrd.service; then
    echo "daemon active before any call — activation test would be vacuous"; exit 1
fi
devmgr snapshot list   # first org.devmgr.Manager1 call must auto-activate
systemctl is-active --quiet devmgrd.service || { echo "devmgrd.service not active after first call"; exit 1; }
call() { busctl call org.devmgr.Manager1 /org/devmgr/Manager1 org.devmgr.Manager1 "$@"; }

echo "==> [3/9] both UIs enumerate"
tui_rows=$(self_test_rows devmgr-tui --self-test)
[ "${tui_rows:-0}" -gt 0 ] || { echo "TUI enumerated no devices"; exit 1; }
gui_rows=$(self_test_rows env QT_QPA_PLATFORM=offscreen devmgr-gui --self-test)
[ "${gui_rows:-0}" -gt 0 ] || { echo "GUI enumerated no devices"; exit 1; }
echo "TUI rows: $tui_rows, GUI rows: $gui_rows"

echo "==> [4/9] firmware update check + install through devmgr-fwupd-smoke"
fwupd_test_device_ensure_enabled
if ! fwupd_test_device_discover; then
    echo "acceptance: fwupd fake test device never appeared" >&2
    fwupd_test_device_dump_diagnostics
    exit 1
fi
# The installed diagnostic drives OUR FwupdUpdateProvider (never shells out to
# fwupdmgr). Its --device matcher takes a substring of the fwupd Device ID or
# display name; try the discovered ids in order, falling back to "fakedevice".
SEL=""
for candidate in "$FWUPD_TEST_DEVICE_ID" "$FWUPD_TEST_DEVICE_NAME" "fakedevice"; do
    [ -z "$candidate" ] && continue
    if devmgr-fwupd-smoke --device "$candidate" >/tmp/accept-fw.log 2>/tmp/accept-fw.err; then
        SEL="$candidate"; break
    fi
    grep -qi "no matching device" /tmp/accept-fw.log /tmp/accept-fw.err 2>/dev/null || {
        cat /tmp/accept-fw.log; cat /tmp/accept-fw.err >&2
        echo "acceptance: devmgr-fwupd-smoke failed for a reason other than selection" >&2; exit 1; }
    echo "-- selector '$candidate' not matched, trying next" >&2
done
[ -n "$SEL" ] || { echo "acceptance: devmgr-fwupd-smoke matched no discovered device" >&2; exit 1; }
cat /tmp/accept-fw.log
grep -q "localcab=1" /tmp/accept-fw.log || {
    echo "acceptance: fakedevice release not locally resolvable (localcab=0)" >&2; exit 1; }
devmgr-fwupd-smoke --device "$SEL" --install --expect-version 1.2.4
echo "firmware check OK (selector $SEL)"

echo "==> [5/9] device disable + snapshot restore round-trip"
sid=$(devmgr snapshot create --label acceptance-baseline)   # captured while enabled
[ -n "$sid" ] || { echo "create printed no id"; exit 1; }
call SetDeviceEnabled sb "$USBDEV" false
[ "$(cat "$USBDEV/authorized")" = "0" ] || { echo "disable did not reach sysfs"; exit 1; }
devmgr snapshot restore "$sid" >/dev/null
[ "$(cat "$USBDEV/authorized")" = "1" ] || { echo "restore did not re-enable the device"; exit 1; }
devmgr snapshot delete "$sid"
echo "disable+restore OK (id $sid)"

echo "==> [6/9] driver blacklist round-trip via manual snapshot"
echo "blacklist $MODULE" > /etc/modprobe.d/devmgr-accept.conf
bid=$(devmgr snapshot create --label acceptance-blacklist)
rm /etc/modprobe.d/devmgr-accept.conf
devmgr snapshot restore "$bid" >/dev/null
[ "$(cat /etc/modprobe.d/devmgr-accept.conf 2>/dev/null)" = "blacklist $MODULE" ] || {
    echo "blacklist file not restored"; exit 1; }
devmgr snapshot delete "$bid"
rm -f /etc/modprobe.d/devmgr-accept.conf
echo "blacklist round-trip OK (id $bid)"

echo "==> [7/9] hotplug reaction: hot-remove the USB device, re-enumerate"
# Real kernel hot-unplug via the sysfs `remove` attribute. This is the LAST
# device scenario: re-adding needs a physical replug, out of scope for an
# automated pass (the VM is disposable).
before=$(self_test_rows devmgr-tui --self-test)
echo 1 > "$USBDEV/remove"
sleep 1
after=$(self_test_rows devmgr-tui --self-test)
[ "${after:-0}" -lt "${before:-0}" ] || {
    echo "enumeration did not react to hot-remove (before=$before after=$after)"; exit 1; }
echo "hotplug reaction OK (rows $before -> $after)"

echo "==> [8/9] CLI recovery: daemon down (masked) -> recovery CLI exits 4"
# Mask the unit so D-Bus activation CANNOT restart it: systemd refuses a masked
# unit (org.freedesktop.systemd1.UnitMasked), the genuine "unreachable" state a
# tester reaches after `systemctl mask`. Stop first — `systemctl stop` errors on
# an already-masked unit — then mask once it has settled. Scenario 2 already
# proved the activatable path exits 0; this proves the masked path exits 4 (not
# 5). The EXIT trap unmasks on every path so a failure never leaves it wedged.
systemctl stop devmgrd.service
for _ in {1..50}; do systemctl is-active --quiet devmgrd.service || break; sleep 0.1; done
systemctl mask devmgrd.service
trap 'systemctl unmask devmgrd.service >/dev/null 2>&1 || true; \
      systemctl reset-failed devmgrd.service >/dev/null 2>&1 || true' EXIT
set +e
devmgr --bus system snapshot list >/dev/null 2>/tmp/accept-down.err
rc=$?
set -e
# The masked unit must not have been reactivated by the CLI's activation attempt.
if systemctl is-active --quiet devmgrd.service; then
    echo "scenario 8: CLI reactivated the masked daemon"; exit 1
fi
[ "$rc" = "4" ] || { echo "daemon-down (masked) exit was $rc, want 4:"; cat /tmp/accept-down.err; exit 1; }
systemctl unmask devmgrd.service
systemctl reset-failed devmgrd.service >/dev/null 2>&1 || true
trap - EXIT
echo "CLI recovery OK (masked daemon, exit 4)"

echo "==> [9/9] journal has no sandbox denial"
DENIALS=$(journalctl -u devmgrd.service --since "$SINCE" --no-pager |
          grep -iE "operation not permitted|permission denied|read-only file system|seccomp|EPERM|EACCES" || true)
if [ -n "$DENIALS" ]; then
    echo "sandbox denials in the journal:"; echo "$DENIALS"; exit 1
fi
echo "journal clean"

echo "ACCEPTANCE OK"
