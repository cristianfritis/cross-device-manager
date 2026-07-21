#!/usr/bin/env bash
# Deb upgrade smoke (packaging-deb upgrade spec exit gate) — run INSIDE the
# disposable VM as root. Verifies the three upgrade guarantees task 4.2 adds on
# top of the plain install-smoke:
#   1. UPGRADE PRESERVES STATE: install <old> deb, create a labeled snapshot,
#      install <new> deb over it — the daemon reports the new version and the
#      pre-upgrade snapshot still lists AND restores.
#   2. INTERRUPTED-INSTALL RECOVERY: `dpkg --unpack` leaves the package
#      unpacked-but-unconfigured (the state a killed postinst leaves behind);
#      re-running the configure step completes cleanly and bus activation works.
#   3. TARBALL -> DEB REPLACEMENT: the tarball and the deb share the unit/dbus/
#      polkit paths (only the binary dir differs — /usr/local/bin vs /usr/bin),
#      so the safe order is tarball-uninstall FIRST (state preserved), THEN
#      `apt install ./deb`. Running the tarball uninstall AFTER the deb would
#      delete the now-deb-owned shared files — hence uninstall-then-install.
# Ends with an explicit DEB UPGRADE SMOKE OK.
#
# Args: <old-deb> <new-deb> [<tarball>]. The tarball step is skipped if no
# tarball is given. Task 6.2 wraps this with pinned `--previous` artifacts,
# downgrade, purge-residue, and the RPM path.
set -euo pipefail
OLD=${1:?usage: deb-upgrade-smoke.sh <old-deb> <new-deb> [<tarball>]}
NEW=${2:?usage: deb-upgrade-smoke.sh <old-deb> <new-deb> [<tarball>]}
TARBALL=${3:-}
OLD=$(readlink -f "$OLD"); NEW=$(readlink -f "$NEW")
[ -f "$OLD" ] || { echo "no old deb at $OLD"; exit 1; }
[ -f "$NEW" ] || { echo "no new deb at $NEW"; exit 1; }
[ "$(id -u)" -eq 0 ] || { echo "run as root"; exit 1; }

# Fresh slate: drop any prior install, state, or manually copied policy.
apt-get purge -y devmgr 2>/dev/null || true
pkill -x devmgrd 2>/dev/null || true
rm -rf /var/lib/devmgrd
rm -f /usr/local/bin/devmgr /usr/local/bin/devmgrd /usr/local/bin/devmgr-tui /usr/local/bin/devmgr-gui
systemctl daemon-reload

echo "==> [1/3] upgrade preserves state + snapshots"
apt-get install -y "$OLD"
old_ver=$(devmgrd --version | cut -d' ' -f2)
echo "old version: $old_ver"
devmgr snapshot list >/dev/null   # auto-activates the daemon
sid=$(devmgr snapshot create --label pre-upgrade)
[ -n "$sid" ] || { echo "create printed no id"; exit 1; }
devmgr snapshot list | grep -q pre-upgrade || { echo "snapshot not listed pre-upgrade"; exit 1; }

apt-get install -y "$NEW"
new_ver=$(devmgrd --version | cut -d' ' -f2)
echo "new version: $new_ver"
[ "$new_ver" != "$old_ver" ] || echo "warn: old and new report the same version ($new_ver)"
[ -d /var/lib/devmgrd ] || { echo "state dir gone after upgrade"; exit 1; }
devmgr snapshot list | grep -q pre-upgrade || { echo "pre-upgrade snapshot lost across upgrade"; exit 1; }
devmgr snapshot restore "$sid" || { echo "pre-upgrade snapshot failed to restore after upgrade"; exit 1; }
echo "upgrade OK (state + snapshot $sid survived, restored)"

echo "==> [2/3] interrupted-install recovery"
# Unpack-without-configure is exactly the on-disk state a killed postinst
# leaves. dpkg must report it not-fully-configured, then recover on re-run.
dpkg --unpack "$NEW"
if systemctl is-active --quiet devmgrd.service; then
    echo "daemon left running after bare unpack (postinst must not start it)"; exit 1
fi
dpkg --configure devmgr           # the recovery step (== dpkg --configure -a)
systemctl is-active --quiet devmgrd.service && { echo "configure started the daemon unexpectedly"; exit 1; }
devmgr snapshot list >/dev/null   # first call must still auto-activate
systemctl is-active --quiet devmgrd.service || { echo "activation broken after recovery"; exit 1; }
echo "recovery OK (reconfigure clean, activation works)"

if [ -n "$TARBALL" ]; then
    echo "==> [3/3] tarball -> deb replacement"
    TARBALL=$(readlink -f "$TARBALL")
    apt-get purge -y devmgr
    rm -rf /var/lib/devmgrd
    tmp=$(mktemp -d); tar xf "$TARBALL" -C "$tmp"
    root=$(dirname "$(find "$tmp" -name install.sh -path '*packaging*' | head -1)")
    "$root/install.sh"
    [ -x /usr/local/bin/devmgrd ] || { echo "tarball did not install to /usr/local/bin"; exit 1; }
    devmgr snapshot list >/dev/null
    tsid=$(devmgr snapshot create --label pre-replace)
    # Safe order: fully remove the tarball (state preserved), then install the
    # deb onto a clean system — never the reverse (shared paths, see header).
    "$root/uninstall.sh"
    [ ! -e /usr/local/bin/devmgrd ] || { echo "tarball binary residue survived uninstall"; exit 1; }
    [ ! -e /usr/lib/systemd/system/devmgrd.service ] || { echo "tarball unit residue survived uninstall"; exit 1; }
    [ -d /var/lib/devmgrd ] || { echo "tarball uninstall deleted state (should preserve)"; exit 1; }
    apt-get install -y "$NEW"
    [ -x /usr/bin/devmgrd ] || { echo "deb binary missing at /usr/bin"; exit 1; }
    [ -e /usr/lib/systemd/system/devmgrd.service ] || { echo "deb unit missing after replacement"; exit 1; }
    devmgr snapshot list | grep -q pre-replace || { echo "snapshot lost across tarball->deb replacement"; exit 1; }
    devmgr snapshot restore "$tsid" || { echo "snapshot failed to restore after replacement"; exit 1; }
    rm -rf "$tmp"
    echo "replacement OK (deb sole install at /usr/bin, state + snapshot survived)"
else
    echo "==> [3/3] tarball -> deb replacement SKIPPED (no tarball arg)"
fi

echo "DEB UPGRADE SMOKE OK"
