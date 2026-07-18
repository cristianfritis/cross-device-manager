#!/bin/sh
# devmgr tarball installer (packaging-tarball spec contract):
#   - requires root
#   - detects systemd vs OpenRC; with neither, installs the init-agnostic files
#     and prints manual steps (D-Bus activation still auto-starts the daemon)
#   - idempotent: re-running converges to the single-run state
#   - aborts BEFORE writing anything when an existing file at a target path is
#     not devmgr's (unreconcilable foreign/partial state)
#   - prints a summary naming every file it wrote
# uninstall.sh removes exactly the paths listed in manifest.sh.
set -eu

HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH='' cd -- "$HERE/.." && pwd)
# shellcheck source=manifest.sh
. "$HERE/manifest.sh"

fail() {
    echo "install.sh: $*" >&2
    exit 1
}

[ "$(id -u)" -eq 0 ] || fail "must run as root"

# --- detect init system ------------------------------------------------------
INIT=none
if [ -d /run/systemd/system ]; then
    INIT=systemd
elif [ -e /run/openrc ] || command -v rc-service >/dev/null 2>&1; then
    INIT=openrc
fi

# --- pre-flight: all sources present, all existing targets reconcilable ------
for b in $BINARIES; do
    [ -x "$ROOT/bin/$b" ] || fail "missing binary $ROOT/bin/$b (incomplete tarball?)"
done
for f in devmgrd.service devmgrd.initd org.devmgr.Manager1.service \
         org.devmgr.Manager1.conf org.devmgr.policy manifest.sh; do
    [ -f "$HERE/$f" ] || fail "missing $HERE/$f (incomplete tarball?)"
done
reconcilable() {
    if [ -e "$1" ] && ! grep -q devmgr "$1" 2>/dev/null; then
        fail "existing $1 is not devmgr's — refusing to touch it; remove it manually and re-run"
    fi
}
reconcilable "$DBUS_POLICY_DST"
reconcilable "$DBUS_SERVICE_DST"
reconcilable "$POLKIT_DST"
reconcilable "$SYSTEMD_UNIT_DST"
reconcilable "$OPENRC_DST"

# --- write phase -------------------------------------------------------------
WRITTEN=""
put() { # put SRC DST MODE
    install -D -m "$3" "$1" "$2"
    WRITTEN="$WRITTEN  $2
"
}

for b in $BINARIES; do
    put "$ROOT/bin/$b" "$BIN_DIR/$b" 0755
done
put "$HERE/org.devmgr.Manager1.conf" "$DBUS_POLICY_DST" 0644
put "$HERE/org.devmgr.policy" "$POLKIT_DST" 0644

# The shipped unit/activation files point at the deb's /usr/bin; the tarball
# installs to /usr/local/bin, so rewrite the exec paths on the way in.
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
sed "s|/usr/bin/devmgrd|$BIN_DIR/devmgrd|" "$HERE/org.devmgr.Manager1.service" > "$TMP"
put "$TMP" "$DBUS_SERVICE_DST" 0644

case "$INIT" in
    systemd)
        sed "s|/usr/bin/devmgrd|$BIN_DIR/devmgrd|" "$HERE/devmgrd.service" > "$TMP"
        put "$TMP" "$SYSTEMD_UNIT_DST" 0644
        systemctl daemon-reload || true
        ;;
    openrc)
        put "$HERE/devmgrd.initd" "$OPENRC_DST" 0755
        ;;
esac

# Make the system bus rescan activation files and bus policy.
if command -v dbus-send >/dev/null 2>&1; then
    dbus-send --system --type=method_call \
        --dest=org.freedesktop.DBus / org.freedesktop.DBus.ReloadConfig \
        2>/dev/null || true
elif command -v busctl >/dev/null 2>&1; then
    busctl call org.freedesktop.DBus / org.freedesktop.DBus ReloadConfig \
        2>/dev/null || true
fi

# --- summary -----------------------------------------------------------------
echo "devmgr installed. Files written:"
printf '%s' "$WRITTEN"
case "$INIT" in
    systemd)
        echo "Init: systemd — no manual start needed; the daemon bus-activates on first use."
        ;;
    openrc)
        echo "Init: OpenRC script installed at $OPENRC_DST."
        echo "Optional boot-time supervision: rc-update add devmgrd default && rc-service devmgrd start"
        echo "(without it, D-Bus activation still starts the daemon on first use)"
        ;;
    none)
        echo "Init: neither systemd nor OpenRC detected — no init integration installed."
        echo "Manual steps: D-Bus activation auto-starts $BIN_DIR/devmgrd on first use;"
        echo "for supervised operation wire $HERE/devmgrd.service or $HERE/devmgrd.initd"
        echo "into your init system by hand."
        ;;
esac
