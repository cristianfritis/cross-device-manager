#!/usr/bin/env bash
# Install smoke (packaging-deb spec exit gate) — run INSIDE the disposable VM
# as root: tests the ARTIFACT testers receive, never the build tree.
#   1. apt install ./devmgr_*.deb resolves and places every file
#   2. D-Bus system-bus activation starts devmgrd via SystemdService= on the
#      first call — no manual start
#   3. both UIs enumerate devices (--self-test, offscreen for the GUI)
#   4. snapshot create -> restore round-trip through the recovery CLI
#   5. apt remove leaves no unit/policy/binary residue, preserves state dir
#   6. apt purge removes the state dir
# Ends with an explicit INSTALL SMOKE OK (all steps passed).
set -euo pipefail
DEB=${1:?usage: install-smoke.sh <path to devmgr_*.deb>}
DEB=$(readlink -f "$DEB")
[ -f "$DEB" ] || { echo "no deb at $DEB"; exit 1; }
[ "$(id -u)" -eq 0 ] || { echo "run as root"; exit 1; }

# Reconcile anything a previous source-based phase smoke left behind: a
# foreground daemon, manually copied policies (Phase 5 stale-policy lesson),
# old state.
pkill -x devmgrd 2>/dev/null || true
rm -f /etc/dbus-1/system.d/org.devmgr.Manager1.conf
rm -f /usr/share/polkit-1/actions/org.devmgr.policy
rm -rf /var/lib/devmgrd
systemctl daemon-reload

echo "==> [1/6] apt install"
apt-get install -y "$DEB"
for f in /usr/bin/devmgrd /usr/bin/devmgr /usr/bin/devmgr-tui /usr/bin/devmgr-gui \
         /usr/lib/systemd/system/devmgrd.service \
         /usr/share/dbus-1/system-services/org.devmgr.Manager1.service \
         /usr/share/dbus-1/system.d/org.devmgr.Manager1.conf \
         /usr/share/polkit-1/actions/org.devmgr.policy; do
    [ -e "$f" ] || { echo "missing after install: $f"; exit 1; }
done
devmgrd --version

echo "==> [2/6] D-Bus activation (no manual start)"
if systemctl is-active --quiet devmgrd.service; then
    echo "daemon active before any call — activation test would be vacuous"; exit 1
fi
devmgr snapshot list   # first org.devmgr.Manager1 call must auto-activate
systemctl is-active --quiet devmgrd.service || { echo "devmgrd.service not active after first call"; exit 1; }
echo "activation OK (unit active via SystemdService=)"

echo "==> [3/6] both UIs enumerate"
tui_rows=$(devmgr-tui --self-test | grep -oE 'self-test rows: [0-9]+' | grep -oE '[0-9]+$')
[ "${tui_rows:-0}" -gt 0 ] || { echo "TUI enumerated no devices"; exit 1; }
gui_rows=$(QT_QPA_PLATFORM=offscreen devmgr-gui --self-test | grep -oE 'self-test rows: [0-9]+' | grep -oE '[0-9]+$')
[ "${gui_rows:-0}" -gt 0 ] || { echo "GUI enumerated no devices"; exit 1; }
echo "TUI rows: $tui_rows, GUI rows: $gui_rows"

echo "==> [4/6] snapshot/restore round-trip (recovery CLI)"
sid=$(devmgr snapshot create --label install-smoke)
[ -n "$sid" ] || { echo "create printed no id"; exit 1; }
devmgr snapshot list | grep -q install-smoke || { echo "manual snapshot not listed"; exit 1; }
devmgr snapshot restore "$sid"
devmgr snapshot delete "$sid"
echo "round-trip OK (id $sid)"

echo "==> [5/6] apt remove: residue-free, state preserved"
apt-get remove -y devmgr
for f in /usr/bin/devmgrd /usr/bin/devmgr /usr/bin/devmgr-tui /usr/bin/devmgr-gui \
         /usr/lib/systemd/system/devmgrd.service \
         /usr/share/dbus-1/system-services/org.devmgr.Manager1.service \
         /usr/share/dbus-1/system.d/org.devmgr.Manager1.conf \
         /usr/share/polkit-1/actions/org.devmgr.policy; do
    [ ! -e "$f" ] || { echo "residue after remove: $f"; exit 1; }
done
systemctl is-active --quiet devmgrd.service && { echo "daemon survived remove"; exit 1; }
[ -d /var/lib/devmgrd ] || { echo "state dir deleted by plain remove"; exit 1; }
echo "remove OK (state preserved)"

echo "==> [6/6] apt purge: state gone"
apt-get purge -y devmgr
[ ! -e /var/lib/devmgrd ] || { echo "state dir survived purge"; exit 1; }
echo "purge OK"

echo "INSTALL SMOKE OK"
