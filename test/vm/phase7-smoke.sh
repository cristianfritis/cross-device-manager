#!/usr/bin/env bash
# Phase 7 dangerous E2E — run INSIDE the disposable VM as root, NEVER on a host.
# Usage: phase7-smoke.sh <sysfs path of a spare USB device>. Root is implicitly
# polkit-authorized, so no agent is needed.
#
# Covers the backup-rollback-engine change end to end:
#   1. auto snapshot exists before a mutating verb (disable)
#   2. restore converges hardware (entry removed -> device re-enabled)
#   3. module blacklist round-trip through a manual snapshot (config-level)
#   4. undo-a-restore via the safety snapshot restore takes first
#   5. CLI recovery path: restores run through the devmgr binary; daemon-down
#      exits 4 (snapshot-cli spec)
set -euo pipefail
USBDEV=${1:?usage: phase7-smoke.sh <sysfs path of a spare USB device>}
[ -f "$USBDEV/authorized" ] || { echo "no authorized attr at $USBDEV"; exit 1; }

install -m644 daemon/data/org.devmgr.Manager1.conf /etc/dbus-1/system.d/
install -m644 daemon/data/org.devmgr.policy /usr/share/polkit-1/actions/
rm -rf /var/lib/devmgrd                 # clean slate: no state, no snapshots
rm -f /etc/modprobe.d/devmgr-p7*.conf   # leftovers from earlier runs

# The recovery CLI (snapshot-cli spec). The VM build puts it in build/cli;
# host preset runs use build/linux-debug/cli.
CLI=./build/cli/devmgr
[ -x "$CLI" ] || CLI=./build/linux-debug/cli/devmgr
[ -x "$CLI" ] || { echo "devmgr CLI binary not found"; exit 1; }

start_daemon() {
    ./build/linux-debug/daemon/devmgrd &
    DPID=$!
    for _ in $(seq 1 50); do
        busctl status org.devmgr.Manager1 >/dev/null 2>&1 && return 0
        sleep 0.2
    done
    echo "devmgrd did not claim org.devmgr.Manager1 within 10s"; exit 1
}
stop_daemon() { kill "$DPID" 2>/dev/null || true; wait "$DPID" 2>/dev/null || true; }
trap 'stop_daemon' EXIT
call() { busctl call org.devmgr.Manager1 /org/devmgr/Manager1 org.devmgr.Manager1 "$@"; }
start_daemon

echo "== auto snapshot exists before disable =="
call SetDeviceEnabled sb "$USBDEV" false
[ "$(cat "$USBDEV/authorized")" = "0" ] || { echo "disable did not stick"; exit 1; }
ls /var/lib/devmgrd/snapshots/*.json >/dev/null 2>&1 || {
    echo "no snapshot file written before mutation"; exit 1; }
"$CLI" snapshot list --json > /tmp/p7-list.json
grep -q '"trigger":"auto"' /tmp/p7-list.json || {
    echo "no auto snapshot in list:"; cat /tmp/p7-list.json; exit 1; }
grep -q '"verb":"SetDeviceEnabled"' /tmp/p7-list.json || {
    echo "auto snapshot reason does not name the verb:"; cat /tmp/p7-list.json; exit 1; }

echo "== CLI restore re-enables the device =="
# The only snapshot is the pre-disable auto one; restore it by its short id
# (list row column 1), exercising unique-prefix resolution E2E.
SID=$("$CLI" snapshot list | awk 'NR==1{print $1}')
"$CLI" snapshot restore "$SID" > /tmp/p7-restore.log
cat /tmp/p7-restore.log
grep -q "re-enable" /tmp/p7-restore.log || {
    echo "restore outcome does not report the re-enable"; exit 1; }
[ "$(cat "$USBDEV/authorized")" = "1" ] || { echo "restore did not re-enable device"; exit 1; }
grep -q '"entries": \[\]' /var/lib/devmgrd/state.json || {
    echo "restore did not empty the state store"; exit 1; }

echo "== module blacklist round-trip via manual snapshot =="
echo "blacklist dummy" > /etc/modprobe.d/devmgr-p7.conf
BID=$("$CLI" snapshot create --label p7-blacklist)
rm /etc/modprobe.d/devmgr-p7.conf
"$CLI" snapshot restore "${BID:0:12}" > /tmp/p7-restore2.log
cat /tmp/p7-restore2.log
grep -q "modprobe-write" /tmp/p7-restore2.log || {
    echo "restore outcome does not report the modprobe write"; exit 1; }
[ "$(cat /etc/modprobe.d/devmgr-p7.conf)" = "blacklist dummy" ] || {
    echo "blacklist file not restored"; exit 1; }

echo "== undo the restore via its safety snapshot =="
# The newest snapshot is the auto safety one the restore just took (captured
# with the blacklist file absent); restoring it must remove the file again.
UID_ROW=$("$CLI" snapshot list | awk 'NR==1{print $1}')
"$CLI" snapshot restore "$UID_ROW" > /tmp/p7-restore3.log
cat /tmp/p7-restore3.log
grep -q "modprobe-remove" /tmp/p7-restore3.log || {
    echo "undo outcome does not report the modprobe remove"; exit 1; }
[ ! -f /etc/modprobe.d/devmgr-p7.conf ] || { echo "undo did not remove blacklist file"; exit 1; }

echo "== CLI exits 4 when the daemon is down =="
stop_daemon
set +e
"$CLI" snapshot list >/dev/null 2>/tmp/p7-down.err
RC=$?
set -e
[ "$RC" = "4" ] || { echo "daemon-down exit was $RC, want 4:"; cat /tmp/p7-down.err; exit 1; }

echo "PHASE7 VM SMOKE OK"
