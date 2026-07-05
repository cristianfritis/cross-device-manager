#!/usr/bin/env bash
# Snapshot / run / revert / logs helper around the disposable test VM.
set -euo pipefail

SNAPSHOT_NAME="devmgr-clean"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERIAL_LOG="${DEVMGR_SERIAL_LOG:-$HERE/devmgr-vm-serial.log}"
USB_TRACE_LOG="${DEVMGR_USB_TRACE_LOG:-$HERE/devmgr-vm-usb-trace.log}"

usage() { echo "usage: $0 {create|run <cmd>|revert|logs}" >&2; exit 2; }

case "${1:-}" in
    create) shift; vagrant snapshot save "$SNAPSHOT_NAME" "$@";;
    run)    shift; vagrant ssh -c "${*:?command required}" ;;
    revert) vagrant snapshot restore "$SNAPSHOT_NAME" ;;
    logs)
        # Follow the host-side logs the Vagrantfile writes. QEMU's own stderr is
        # additionally at /var/log/libvirt/qemu/<domain>.log — find <domain> with
        # `virsh list --all` (vagrant-libvirt names it <dir>_default).
        files=("$SERIAL_LOG")
        [ -f "$USB_TRACE_LOG" ] && files+=("$USB_TRACE_LOG")
        echo "== tail -F ${files[*]} (Ctrl-C to stop) ==" >&2
        tail -n +1 -F "${files[@]}"
        ;;
    *)      usage ;;
esac
