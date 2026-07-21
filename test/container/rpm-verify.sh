#!/usr/bin/env bash
# RPM container verify (packaging-rpm spec exit gate) — runs INSIDE the Fedora
# packaging container (Dockerfile.fedora / the pkg-rpm compose service). The
# container has no PID-1 systemd, so this covers the parts that do not need it:
#   1. build the full stack (linux-packaged) and cut the rpm with `cpack -G RPM`
#   2. dnf install ./devmgr-*.rpm resolves every dependency and runs scriptlets
#   3. every packaged file lands at its /usr FHS path
#   4. `rpm -V devmgr` reports no discrepancies on a fresh install
#   5. all four binaries run `--version` (exits before any bus/display wiring)
#   6. `rpm -e devmgr` removes unit/policy/binary files (state dir is unowned,
#      so it never existed here and nothing to preserve — see install-smoke for
#      the stateful VM path)
# Ends with an explicit RPM VERIFY OK (all steps passed). Scriptlet-level
# systemd/D-Bus behavior that needs PID 1 is exercised by the VM upgrade matrix.
set -euo pipefail
cd "$(dirname "$0")/../.."

DIST=${1:-/tmp/dist-rpm}
rm -rf "$DIST" && mkdir -p "$DIST"

echo "==> [1/6] build linux-packaged + cpack -G RPM"
cmake --preset linux-packaged
cmake --build build/linux-packaged -j"$(nproc)"
cpack --config build/linux-packaged/CPackConfig.cmake -G RPM -B "$DIST"
rm -rf "$DIST/_CPack_Packages"
RPM=$(ls "$DIST"/devmgr-*.x86_64.rpm)
[ -f "$RPM" ] || { echo "no rpm produced in $DIST"; exit 1; }
echo "built $RPM"
echo "--- rpm contents ---"; rpm -qlp "$RPM"; echo "--- rpm requires ---"; rpm -qRp "$RPM"; echo "--------------------"
# File-set parity (packaging-rpm spec): the LICENSE must be in the package. Assert
# it from the package listing directly — independent of any install-time doc
# policy (the Fedora container base sets tsflags=nodocs, which would otherwise
# skip files under /usr/share/doc on install even though they are packaged).
rpm -qlp "$RPM" | grep -qx /usr/share/doc/devmgr/copyright || { echo "LICENSE (copyright) not in the rpm"; exit 1; }

echo "==> [2/6] dnf install (resolves deps, runs %post)"
# tsflags= overrides the container base's tsflags=nodocs so doc files (the
# license) install to disk as they would on a normal Fedora desktop — making
# the on-disk file check and `rpm -V` below meaningful.
dnf install -y --setopt=tsflags= "$RPM"

echo "==> [3/6] every file at its /usr path"
for f in /usr/bin/devmgrd /usr/bin/devmgr /usr/bin/devmgr-tui /usr/bin/devmgr-gui \
         /usr/lib/systemd/system/devmgrd.service \
         /usr/share/dbus-1/system-services/org.devmgr.Manager1.service \
         /usr/share/dbus-1/system.d/org.devmgr.Manager1.conf \
         /usr/share/polkit-1/actions/org.devmgr.policy \
         /usr/share/doc/devmgr/copyright; do
    [ -e "$f" ] || { echo "missing after install: $f"; exit 1; }
done

echo "==> [4/6] rpm -V (no discrepancies on fresh install)"
if ! out=$(rpm -V devmgr); then
    echo "rpm -V reported discrepancies:"; echo "$out"; exit 1
fi
[ -z "$out" ] || { echo "rpm -V output non-empty:"; echo "$out"; exit 1; }
echo "rpm -V clean"

echo "==> [5/6] packaged binaries run --version"
/usr/bin/devmgrd --version
/usr/bin/devmgr --version
/usr/bin/devmgr-tui --version
/usr/bin/devmgr-gui --version

echo "==> [6/6] rpm -e removes package files"
rpm -e devmgr
for f in /usr/bin/devmgrd /usr/bin/devmgr /usr/bin/devmgr-tui /usr/bin/devmgr-gui \
         /usr/lib/systemd/system/devmgrd.service \
         /usr/share/dbus-1/system-services/org.devmgr.Manager1.service \
         /usr/share/dbus-1/system.d/org.devmgr.Manager1.conf \
         /usr/share/polkit-1/actions/org.devmgr.policy; do
    [ ! -e "$f" ] || { echo "residue after erase: $f"; exit 1; }
done
echo "erase OK"

echo "RPM VERIFY OK"
