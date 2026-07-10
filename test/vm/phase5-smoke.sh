#!/usr/bin/env bash
# Phase 5 dangerous E2E — run INSIDE the disposable VM as root, NEVER on a host.
# Usage: phase5-smoke.sh <sysfs path of a spare USB device> [sysfs path of a
# safe PCI/virtio device, e.g. the virtio balloon]. Root is implicitly
# polkit-authorized, so no agent is needed.
set -euo pipefail
USBDEV=${1:?usage: phase5-smoke.sh <usb device> [pci/virtio device]}
PCIDEV=${2:-}
[ -f "$USBDEV/authorized" ] || { echo "no authorized attr at $USBDEV"; exit 1; }

install -m644 daemon/data/org.devmgr.Manager1.conf /etc/dbus-1/system.d/
install -m644 daemon/data/org.devmgr.policy /usr/share/polkit-1/actions/
rm -rf /var/lib/devmgrd   # clean slate for the persistence checks

start_daemon() {
    ./build/linux-debug/daemon/devmgrd &
    DPID=$!
    sleep 1
}
stop_daemon() { kill "$DPID" 2>/dev/null || true; wait "$DPID" 2>/dev/null || true; }
trap 'stop_daemon' EXIT
call() { busctl call org.devmgr.Manager1 /org/devmgr/Manager1 org.devmgr.Manager1 "$@"; }
start_daemon

echo "== module load/unload =="
call LoadModule s dummy
[ -d /sys/module/dummy ] || { echo "dummy did not load"; exit 1; }
call UnloadModule s dummy
[ ! -d /sys/module/dummy ] || { echo "dummy did not unload"; exit 1; }

echo "== blacklist refusal =="
echo "blacklist dummy" > /etc/modprobe.d/devmgr-smoke.conf
stop_daemon; start_daemon   # fresh kmod config
if call LoadModule s dummy 2>/tmp/blacklist.err; then
    echo "blacklisted load unexpectedly succeeded"; exit 1
fi
grep -q "blacklisted" /tmp/blacklist.err || { echo "wrong refusal:"; cat /tmp/blacklist.err; exit 1; }
rm /etc/modprobe.d/devmgr-smoke.conf

echo "== persistence: disable survives daemon restart + 'replug' =="
call SetDeviceEnabled sb "$USBDEV" false
[ "$(cat "$USBDEV/authorized")" = "0" ] || { echo "disable did not stick"; exit 1; }
grep -q '"mechanism": "authorized"' /var/lib/devmgrd/state.json || {
    echo "state.json missing entry"; exit 1; }
stop_daemon
echo 1 > "$USBDEV/authorized"          # simulate the kernel re-enabling it
start_daemon                            # startup sweep must re-disable
for i in $(seq 1 50); do
    [ "$(cat "$USBDEV/authorized")" = "0" ] && break
    sleep 0.2
done
[ "$(cat "$USBDEV/authorized")" = "0" ] || { echo "sweep did not re-apply"; exit 1; }
call SetDeviceEnabled sb "$USBDEV" true
[ "$(cat "$USBDEV/authorized")" = "1" ] || { echo "enable did not stick"; exit 1; }

if [ -n "$PCIDEV" ] && [ -e "$PCIDEV/driver" ]; then
    echo "== unbind mechanism + surgical verbs on $PCIDEV =="
    DRIVER=$(basename "$(readlink -f "$PCIDEV/driver")")
    call SetDeviceEnabled sb "$PCIDEV" false
    [ ! -e "$PCIDEV/driver" ] || { echo "unbind-disable left a driver"; exit 1; }
    grep -q '"mechanism": "unbind"' /var/lib/devmgrd/state.json || {
        echo "unbind entry missing"; exit 1; }
    call SetDeviceEnabled sb "$PCIDEV" true      # driver_override targeted rebind
    [ -e "$PCIDEV/driver" ] || { echo "targeted rebind failed"; exit 1; }
    call UnbindDriver s "$PCIDEV"                # surgical: no state entry
    [ ! -e "$PCIDEV/driver" ] || { echo "surgical unbind failed"; exit 1; }
    grep -q '"entries": \[\]' /var/lib/devmgrd/state.json || {
        echo "surgical unbind polluted the store"; exit 1; }
    call BindDriver ss "$PCIDEV" "$DRIVER"
    [ -e "$PCIDEV/driver" ] || { echo "surgical bind failed"; exit 1; }
fi

echo "PHASE5 VM SMOKE OK"
