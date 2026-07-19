#!/usr/bin/env bash
# Sandbox smoke (beta-06 task 1.3) — run INSIDE the disposable VM as root,
# NEVER on a host. Proves the hardened devmgrd.service still permits every
# privileged flow, and that nothing the sandbox blocks is silently swallowed.
#
# Unlike phase7-smoke.sh this deliberately runs the daemon UNDER SYSTEMD (the
# sandbox only exists there — a foreground ./build/.../devmgrd has no unit and
# would vacuously pass), so it needs the installed package.
#   1. hardened unit is the one systemd loaded (spot-check key directives)
#   2. device disable/enable  -> sysfs writes under ProtectSystem=strict
#   3. module load/unload     -> CAP_SYS_MODULE + SystemCallFilter=@module
#   4. snapshot restore       -> /etc/modprobe.d + state dir write-back
#   5. journal carries no sandbox denial for any of the above
# Ends with an explicit SANDBOX SMOKE OK.
set -euo pipefail
USBDEV=${1:?usage: phase8-sandbox-smoke.sh <sysfs path of a spare USB device> [module]}
MODULE=${2:-dummy}
[ "$(id -u)" -eq 0 ] || { echo "run as root"; exit 1; }
[ -f "$USBDEV/authorized" ] || { echo "no authorized attr at $USBDEV"; exit 1; }
command -v devmgrd >/dev/null || { echo "devmgrd not installed — install the deb first"; exit 1; }

pkill -x devmgrd 2>/dev/null || true   # kill any stray foreground daemon
systemctl daemon-reload
systemctl reset-failed devmgrd.service 2>/dev/null || true
SINCE=$(date '+%Y-%m-%d %H:%M:%S')

echo "==> [1/5] systemd loaded the hardened unit"
show() { systemctl show devmgrd.service -p "$1" --value; }
[ "$(show ProtectSystem)" = "strict" ] || { echo "ProtectSystem not strict"; exit 1; }
[ "$(show ProtectHome)" = "yes" ] || { echo "ProtectHome not yes"; exit 1; }
[ "$(show PrivateTmp)" = "yes" ] || { echo "PrivateTmp not yes"; exit 1; }
caps=$(show CapabilityBoundingSet)
echo "CapabilityBoundingSet=$caps"
show RestrictAddressFamilies | grep -q AF_NETLINK || { echo "AF_NETLINK not allowed"; exit 1; }
show ReadWritePaths | grep -q /etc/modprobe.d || { echo "modprobe.d not writable"; exit 1; }
# Advisory: prints the exposure score, does not gate.
systemd-analyze security devmgrd.service | tail -3 || true

echo "==> [2/5] device disable/enable (sysfs writes)"
devmgr snapshot list >/dev/null   # bus activation starts the unit
systemctl is-active --quiet devmgrd.service || { echo "unit not active"; exit 1; }
call() { busctl call org.devmgr.Manager1 /org/devmgr/Manager1 org.devmgr.Manager1 "$@"; }
call SetDeviceEnabled sb "$USBDEV" false
[ "$(cat "$USBDEV/authorized")" = "0" ] || { echo "disable did not reach sysfs"; exit 1; }
call SetDeviceEnabled sb "$USBDEV" true
[ "$(cat "$USBDEV/authorized")" = "1" ] || { echo "re-enable did not reach sysfs"; exit 1; }
echo "sysfs write surface OK"

echo "==> [3/5] module load/unload (CAP_SYS_MODULE)"
modprobe -r "$MODULE" 2>/dev/null || true
call LoadModule s "$MODULE"
lsmod | grep -qE "^${MODULE}\b" || { echo "$MODULE not loaded — CAP_SYS_MODULE or @module blocked"; exit 1; }
call UnloadModule s "$MODULE"
lsmod | grep -qE "^${MODULE}\b" && { echo "$MODULE still loaded after unload"; exit 1; }
echo "module ops OK"

echo "==> [4/5] snapshot restore write-back (state dir + modprobe.d)"
sid=$(devmgr snapshot create --label sandbox-smoke)
[ -n "$sid" ] || { echo "create printed no id"; exit 1; }
[ -s /var/lib/devmgrd/HEAD ] || { echo "state dir not written under the sandbox"; exit 1; }
call SetDeviceEnabled sb "$USBDEV" false
devmgr snapshot restore "$sid" >/dev/null
[ "$(cat "$USBDEV/authorized")" = "1" ] || { echo "restore did not converge hardware"; exit 1; }
devmgr snapshot delete "$sid"
echo "restore OK (id $sid)"

echo "==> [5/5] journal has no sandbox denial"
DENIALS=$(journalctl -u devmgrd.service --since "$SINCE" --no-pager |
          grep -iE "operation not permitted|permission denied|read-only file system|seccomp|EPERM|EACCES" || true)
if [ -n "$DENIALS" ]; then
    echo "sandbox denials in the journal:"; echo "$DENIALS"; exit 1
fi
echo "journal clean"

echo "SANDBOX SMOKE OK"
