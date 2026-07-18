#!/bin/sh
# devmgr tarball uninstaller (packaging-tarball spec contract): removes exactly
# the paths install.sh writes (shared manifest.sh list), stopping the daemon
# first. /var/lib/devmgrd is preserved unless --purge is passed.
set -eu

HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=manifest.sh
. "$HERE/manifest.sh"

fail() {
    echo "uninstall.sh: $*" >&2
    exit 1
}

[ "$(id -u)" -eq 0 ] || fail "must run as root"

PURGE=0
if [ "${1:-}" = "--purge" ]; then
    PURGE=1
elif [ -n "${1:-}" ]; then
    fail "unknown argument '$1' (only --purge is supported)"
fi

# --- stop the daemon before its files disappear ------------------------------
if [ -d /run/systemd/system ]; then
    systemctl stop devmgrd.service 2>/dev/null || true
elif command -v rc-service >/dev/null 2>&1; then
    rc-service devmgrd stop 2>/dev/null || true
    rc-update del devmgrd default 2>/dev/null || true
fi

# --- remove phase (same list install.sh writes) ------------------------------
REMOVED=""
zap() {
    if [ -e "$1" ]; then
        rm -f "$1"
        REMOVED="$REMOVED  $1
"
    fi
}

for b in $BINARIES; do
    zap "$BIN_DIR/$b"
done
zap "$DBUS_POLICY_DST"
zap "$DBUS_SERVICE_DST"
zap "$POLKIT_DST"
zap "$SYSTEMD_UNIT_DST"
zap "$OPENRC_DST"

if [ "$PURGE" -eq 1 ]; then
    rm -rf "$STATE_DIR"
    REMOVED="$REMOVED  $STATE_DIR (purged)
"
fi

# --- refresh init/bus state ---------------------------------------------------
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || true
fi
if command -v dbus-send >/dev/null 2>&1; then
    dbus-send --system --type=method_call \
        --dest=org.freedesktop.DBus / org.freedesktop.DBus.ReloadConfig \
        2>/dev/null || true
fi

# --- summary -----------------------------------------------------------------
if [ -n "$REMOVED" ]; then
    echo "devmgr uninstalled. Files removed:"
    printf '%s' "$REMOVED"
else
    echo "devmgr uninstalled: nothing to remove."
fi
if [ "$PURGE" -eq 0 ] && [ -d "$STATE_DIR" ]; then
    echo "State preserved at $STATE_DIR (re-run with --purge to delete it)."
fi
