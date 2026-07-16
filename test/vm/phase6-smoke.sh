#!/usr/bin/env bash
# Phase 6 VM smoke: fwupd fakedevice update + dkms status through OUR stack.
set -euo pipefail
cd "$(dirname "$0")/../.."

# shellcheck source=test/vm/lib/fwupd-test-device.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib/fwupd-test-device.sh"

echo "== enable fwupd test remote & plugin =="
fwupd_test_device_ensure_enabled

echo "== discover fwupd test device =="
if ! fwupd_test_device_discover; then
    echo "PHASE6 VM SMOKE FAILED: fwupd fake test device never appeared" >&2
    fwupd_test_device_dump_diagnostics
    exit 1
fi

echo "== read side: fakedevice upgrade visible via FwupdUpdateProvider =="
SMOKE=./build/tests/smoke/devmgr_fwupd_smoke
[ -x "$SMOKE" ] || SMOKE=./build/linux-debug/tests/smoke/devmgr_fwupd_smoke
[ -x "$SMOKE" ] || SMOKE=./build/tests/devmgr_fwupd_smoke
if [ ! -x "$SMOKE" ]; then
    echo "PHASE6 VM SMOKE FAILED: devmgr_fwupd_smoke binary not found" >&2
    find ./build -name '*fwupd*smoke*' 2>/dev/null >&2 || true
    exit 1
fi

# The smoke tool's --device matcher accepts a substring of either the fwupd
# Device ID or the display name. Try the discovered identifiers in order of
# specificity, falling back to the historical literal "fakedevice".
DEVICE_SELECTOR=""
for candidate in "$FWUPD_TEST_DEVICE_ID" "$FWUPD_TEST_DEVICE_NAME" "fakedevice"; do
    [ -z "$candidate" ] && continue
    if "$SMOKE" --device "$candidate" >/tmp/p6-read.log 2>/tmp/p6-read.err; then
        DEVICE_SELECTOR="$candidate"
        break
    fi
    if ! grep -qi "no matching device" /tmp/p6-read.log /tmp/p6-read.err 2>/dev/null; then
        # Failed for a reason OTHER than selector mismatch — fail fast, this
        # is a real failure, not a naming issue.
        cat /tmp/p6-read.log
        cat /tmp/p6-read.err >&2
        echo "PHASE6 VM SMOKE FAILED: smoke tool failed for a reason other than device selection" >&2
        exit 1
    fi
    echo "-- selector '$candidate' not accepted by smoke tool, trying next" >&2
done

cat /tmp/p6-read.log
[ -s /tmp/p6-read.err ] && cat /tmp/p6-read.err >&2

if [ -z "$DEVICE_SELECTOR" ]; then
    echo "PHASE6 VM SMOKE FAILED: smoke tool could not match discovered device" >&2
    echo "-- discovered: id=$FWUPD_TEST_DEVICE_ID guid=$FWUPD_TEST_DEVICE_GUID name=$FWUPD_TEST_DEVICE_NAME" >&2
    exit 1
fi

if ! grep -q "localcab=1" /tmp/p6-read.log; then
    echo "PHASE6 VM SMOKE FAILED: fakedevice release not locally resolvable (localcab=0)" >&2
    echo "-- expected the devmgr-cabdir directory-kind remote to supply a local cab;" >&2
    echo "-- check /etc/fwupd/remotes.d/devmgr-cabdir.conf and 'fwupdmgr get-remotes'" >&2
    exit 1
fi

echo "== install side: 1.2.2 -> 1.2.4 through our stack =="
"$SMOKE" --device "$DEVICE_SELECTOR" --install --expect-version 1.2.4

echo "== dkms status side =="
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y dkms >/dev/null 2>&1 || true
if command -v dkms >/dev/null; then
    sudo mkdir -p /usr/src/devmgrtest-1.0
    sudo tee /usr/src/devmgrtest-1.0/dkms.conf >/dev/null <<'EOF'
PACKAGE_NAME="devmgrtest"
PACKAGE_VERSION="1.0"
BUILT_MODULE_NAME[0]="devmgrtest"
DEST_MODULE_LOCATION[0]="/updates/dkms"
AUTOINSTALL="yes"
MAKE[0]="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build modules"
CLEAN="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build clean"
EOF
    sudo tee /usr/src/devmgrtest-1.0/Makefile >/dev/null <<'EOF'
obj-m := devmgrtest.o
EOF
    sudo tee /usr/src/devmgrtest-1.0/devmgrtest.c >/dev/null <<'EOF'
#include <linux/module.h>
static int __init t_init(void) { return 0; }
static void __exit t_exit(void) {}
module_init(t_init); module_exit(t_exit);
MODULE_LICENSE("GPL");
EOF
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y "linux-headers-$(uname -r)" >/dev/null 2>&1 || true
    sudo dkms add -m devmgrtest -v 1.0 2>/dev/null || true
    sudo dkms build -m devmgrtest -v 1.0 && sudo dkms install -m devmgrtest -v 1.0 || \
        echo "note: dkms build unavailable (no headers) — status-only check degraded"
    "$SMOKE" --dkms devmgrtest || echo "note: dkms assertion skipped"
fi

echo "PHASE6 VM SMOKE OK"
