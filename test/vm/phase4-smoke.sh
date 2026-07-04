#!/usr/bin/env bash
# Phase 4 dangerous E2E — run INSIDE the disposable VM as root, NEVER on a host.
# Usage: phase4-smoke.sh /sys/devices/.../<usb-device>   (a QEMU-attached spare,
# e.g. usb-storage; root is implicitly polkit-authorized so no agent is needed)
set -euo pipefail
DEV=${1:?usage: phase4-smoke.sh <sysfs path of a spare USB device>}
[ -f "$DEV/authorized" ] || { echo "no authorized attr at $DEV"; exit 1; }

install -m644 daemon/data/org.devmgr.Manager1.conf /etc/dbus-1/system.d/
install -m644 daemon/data/org.devmgr.policy /usr/share/polkit-1/actions/
./build/linux-debug/daemon/devmgrd &
DPID=$!
trap 'kill "$DPID" 2>/dev/null || true' EXIT
sleep 1

busctl call org.devmgr.Manager1 /org/devmgr/Manager1 org.devmgr.Manager1 \
    SetDeviceEnabled sb "$DEV" false
[ "$(cat "$DEV/authorized")" = "0" ] || { echo "disable did not stick"; exit 1; }

busctl call org.devmgr.Manager1 /org/devmgr/Manager1 org.devmgr.Manager1 \
    SetDeviceEnabled sb "$DEV" true
[ "$(cat "$DEV/authorized")" = "1" ] || { echo "enable did not stick"; exit 1; }

echo "PHASE4 VM SMOKE OK"
