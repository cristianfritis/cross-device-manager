#!/usr/bin/env bash
# Upgrade matrix (acceptance-suite failure-path spec, beta-06 task 6.2) — run
# INSIDE the disposable VM as root, NEVER on a host. Drives the full package
# lifecycle for ONE format (deb OR rpm) against a PINNED previous release, so
# the same scenarios cover both the Ubuntu/deb and the Fedora/rpm paths.
#
# The previous release is sourced from a pinned local artifact (design decision
# 10): the repo is private until the public flip and CI cannot always fetch old
# release assets, so the owner keeps the v0.5.0-beta.1 packages and passes them
# with --previous. A missing pinned artifact fails LOUDLY (never skips silently)
# — a skipped upgrade test reads as "upgrade works" when it was never run.
#
# Scenarios (each failure fails the run):
#   1. UPGRADE preserves config + snapshots: install <previous>, create a
#      labeled snapshot + a devmgr-owned modprobe file, install <candidate> over
#      it — the daemon reports the candidate version, the pre-upgrade snapshot
#      lists AND restores, and the modprobe config survives.
#   2. DOWNGRADE outcome: install <previous> back over <candidate>. State format
#      v1 is forward-compatible (design decision 10), so the downgrade MUST
#      start and list the pre-existing snapshot; anything else would be a
#      regression from that guarantee (a clear refusal would be re-specified
#      here, not silently tolerated).
#   3. INTERRUPTED-INSTALL recovery: deb leaves an unpacked-but-unconfigured
#      package (the on-disk state a killed postinst leaves); re-configuring
#      recovers cleanly and bus activation works. rpm transactions are atomic
#      (no unconfigured state exists), so this step is a documented N/A for rpm.
#   4. TARBALL -> package replacement (only if --previous-tarball is given):
#      uninstall the tarball FIRST (state preserved), THEN install the package
#      onto the now-clean shared paths — never the reverse (shared unit/dbus/
#      polkit paths, see deb-upgrade-smoke header).
#   5. PURGE / erase residue: deb `apt purge` removes everything INCLUDING the
#      state dir (postrm purge); rpm `rpm -e` removes package files but
#      deliberately PRESERVES /var/lib/devmgrd (rpm has no purge concept — the
#      README documents the manual `rm -rf`), which this step then performs and
#      confirms leaves no residue.
# Ends with an explicit UPGRADE MATRIX OK (all scenarios passed).
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
usage: upgrade-matrix.sh --previous <pkg> --candidate <pkg> [options]
  --previous <path>          PINNED previous-release package (.deb or .rpm) [required]
  --candidate <path>         candidate package of the same format             [required]
  --format deb|rpm           override format (default: inferred from candidate extension)
  --previous-tarball <path>  previous-release .tar.* for the tarball->package replacement step
  --module <name>            module name for the blacklist config artifact (default: dummy)
EOF
    exit 2
}

PREVIOUS="" CANDIDATE="" FORMAT="" PREV_TARBALL="" MODULE=dummy
while [ $# -gt 0 ]; do
    case "$1" in
        --previous)          PREVIOUS=${2:?}; shift 2 ;;
        --candidate)         CANDIDATE=${2:?}; shift 2 ;;
        --format)            FORMAT=${2:?}; shift 2 ;;
        --previous-tarball)  PREV_TARBALL=${2:?}; shift 2 ;;
        --module)            MODULE=${2:?}; shift 2 ;;
        -h|--help)           usage ;;
        *) echo "unknown argument: $1" >&2; usage ;;
    esac
done

[ "$(id -u)" -eq 0 ] || { echo "run as root"; exit 1; }
[ -n "$PREVIOUS" ]  || { echo "--previous is required"; usage; }
[ -n "$CANDIDATE" ] || { echo "--candidate is required"; usage; }

# --- LOUD failure if the pinned previous artifact is absent (task 6.2) --------
if [ ! -f "$PREVIOUS" ]; then
    echo "FATAL: pinned previous-release artifact not found: $PREVIOUS" >&2
    echo "       The upgrade matrix cannot run without it. Point --previous at the" >&2
    echo "       kept v0.5.0-beta.1 package; do NOT skip this test." >&2
    exit 1
fi
[ -f "$CANDIDATE" ] || { echo "FATAL: candidate artifact not found: $CANDIDATE" >&2; exit 1; }
PREVIOUS=$(readlink -f "$PREVIOUS"); CANDIDATE=$(readlink -f "$CANDIDATE")

# Infer the format from the candidate extension unless overridden.
if [ -z "$FORMAT" ]; then
    case "$CANDIDATE" in
        *.deb) FORMAT=deb ;;
        *.rpm) FORMAT=rpm ;;
        *) echo "cannot infer format from '$CANDIDATE'; pass --format deb|rpm" >&2; exit 1 ;;
    esac
fi
case "$FORMAT" in
    deb) command -v apt-get >/dev/null || { echo "format deb but apt-get missing"; exit 1; } ;;
    rpm) command -v rpm     >/dev/null || { echo "format rpm but rpm missing"; exit 1; } ;;
    *) echo "unsupported --format '$FORMAT'"; exit 1 ;;
esac
echo "upgrade matrix: format=$FORMAT previous=$PREVIOUS candidate=$CANDIDATE"

MODPROBE=/etc/modprobe.d/devmgr-accept.conf   # the "config" artifact (scenario 1)

# --- per-format package-manager verbs ----------------------------------------
# Each verb wraps the native tool so the scenario body stays format-agnostic.
pm_install() {   # install/upgrade the given package file
    case "$FORMAT" in
        deb) apt-get install -y "$1" ;;
        rpm) dnf install -y "$1" ;;
    esac
}
pm_downgrade() { # install an OLDER package over a newer one, running scriptlets
    case "$FORMAT" in
        deb) apt-get install -y --allow-downgrades "$1" ;;
        rpm) rpm -Uvh --oldpackage "$1" ;;   # dnf downgrade needs a repo; -U --oldpackage takes a file
    esac
}
pm_erase() {     # plain removal — PRESERVES the state dir on both formats
    case "$FORMAT" in
        deb) apt-get remove -y devmgr ;;
        rpm) rpm -e devmgr ;;
    esac
}
pm_purge_or_clean() {  # full teardown incl. state, for a clean slate
    case "$FORMAT" in
        deb) apt-get purge -y devmgr 2>/dev/null || true ;;
        rpm) rpm -e devmgr 2>/dev/null || true ;;
    esac
    rm -rf /var/lib/devmgrd
}
pkg_version() { devmgrd --version | cut -d' ' -f2; }

clean_slate() {  # drop any prior install, state, and stray tarball residue
    pm_purge_or_clean
    pkill -x devmgrd 2>/dev/null || true
    rm -f /usr/local/bin/devmgr /usr/local/bin/devmgrd \
          /usr/local/bin/devmgr-tui /usr/local/bin/devmgr-gui
    rm -f "$MODPROBE"
    systemctl daemon-reload 2>/dev/null || true
}

# =============================================================================
echo "==> [1/5] upgrade preserves config + snapshots"
clean_slate
pm_install "$PREVIOUS"
old_ver=$(pkg_version); echo "previous version: $old_ver"
devmgr snapshot list >/dev/null           # first call auto-activates the daemon
sid=$(devmgr snapshot create --label pre-upgrade)
[ -n "$sid" ] || { echo "create printed no id"; exit 1; }
devmgr snapshot list | grep -q pre-upgrade || { echo "snapshot not listed pre-upgrade"; exit 1; }
# A devmgr-owned modprobe file is runtime config, not a packaged file, so it
# must survive every package operation untouched.
echo "blacklist $MODULE" > "$MODPROBE"

pm_install "$CANDIDATE"
new_ver=$(pkg_version); echo "candidate version: $new_ver"
[ "$new_ver" != "$old_ver" ] || echo "warn: previous and candidate report the same version ($new_ver)"
[ -d /var/lib/devmgrd ] || { echo "state dir gone after upgrade"; exit 1; }
devmgr snapshot list | grep -q pre-upgrade || { echo "pre-upgrade snapshot lost across upgrade"; exit 1; }
devmgr snapshot restore "$sid" >/dev/null || { echo "pre-upgrade snapshot failed to restore after upgrade"; exit 1; }
[ "$(cat "$MODPROBE" 2>/dev/null)" = "blacklist $MODULE" ] || { echo "devmgr modprobe config lost across upgrade"; exit 1; }
echo "upgrade OK ($old_ver -> $new_ver; snapshot $sid + config survived, restored)"

# =============================================================================
echo "==> [2/5] downgrade outcome (forward-compatible state must still list)"
pm_downgrade "$PREVIOUS"
down_ver=$(pkg_version); echo "downgraded version: $down_ver"
[ "$down_ver" = "$old_ver" ] || echo "warn: downgrade version $down_ver != previous $old_ver"
[ -d /var/lib/devmgrd ] || { echo "state dir gone after downgrade"; exit 1; }
# The daemon must START and LIST the snapshot written under the newer version
# (state format v1 is forward-compatible — design decision 10).
if ! devmgr snapshot list | grep -q pre-upgrade; then
    echo "downgrade lost the pre-upgrade snapshot — regression from the v1 forward-compat guarantee"; exit 1
fi
echo "downgrade OK ($new_ver -> $down_ver; daemon starts, snapshot still lists)"

# =============================================================================
echo "==> [3/5] interrupted-install recovery"
if [ "$FORMAT" = deb ]; then
    clean_slate
    pm_install "$PREVIOUS"                 # a real prior install to upgrade over
    devmgr snapshot list >/dev/null
    # Unpack-without-configure is exactly the on-disk state a killed postinst
    # leaves. dpkg must report it not-fully-configured, then recover on re-run.
    dpkg --unpack "$CANDIDATE"
    if systemctl is-active --quiet devmgrd.service; then
        echo "daemon left running after bare unpack (postinst must not start it)"; exit 1
    fi
    dpkg --configure devmgr                # the recovery step (== dpkg --configure -a)
    devmgr snapshot list >/dev/null        # first call must still auto-activate
    systemctl is-active --quiet devmgrd.service || { echo "activation broken after recovery"; exit 1; }
    echo "recovery OK (reconfigure clean, activation works)"
else
    echo "recovery N/A for rpm: transactions are atomic, no unconfigured-package state exists"
fi

# =============================================================================
echo "==> [4/5] tarball -> package replacement"
if [ -n "$PREV_TARBALL" ]; then
    [ -f "$PREV_TARBALL" ] || { echo "FATAL: --previous-tarball not found: $PREV_TARBALL" >&2; exit 1; }
    PREV_TARBALL=$(readlink -f "$PREV_TARBALL")
    clean_slate
    tmp=$(mktemp -d); tar xf "$PREV_TARBALL" -C "$tmp"
    root=$(dirname "$(find "$tmp" -name install.sh -path '*packaging*' | head -1)")
    [ -n "$root" ] || { echo "tarball has no packaging/install.sh"; exit 1; }
    "$root/install.sh"
    [ -x /usr/local/bin/devmgrd ] || { echo "tarball did not install to /usr/local/bin"; exit 1; }
    devmgr snapshot list >/dev/null
    tsid=$(devmgr snapshot create --label pre-replace)
    # Safe order: fully remove the tarball (state preserved), THEN install the
    # package onto a clean system — never the reverse (shared paths).
    "$root/uninstall.sh"
    [ ! -e /usr/local/bin/devmgrd ] || { echo "tarball binary residue survived uninstall"; exit 1; }
    [ -d /var/lib/devmgrd ] || { echo "tarball uninstall deleted state (should preserve)"; exit 1; }
    pm_install "$CANDIDATE"
    [ -x /usr/bin/devmgrd ] || { echo "package binary missing at /usr/bin"; exit 1; }
    devmgr snapshot list | grep -q pre-replace || { echo "snapshot lost across tarball->package replacement"; exit 1; }
    devmgr snapshot restore "$tsid" >/dev/null || { echo "snapshot failed to restore after replacement"; exit 1; }
    rm -rf "$tmp"
    echo "replacement OK (package sole install at /usr/bin, state + snapshot survived)"
else
    echo "tarball -> package replacement SKIPPED (no --previous-tarball)"
fi

# =============================================================================
echo "==> [5/5] purge / erase residue"
clean_slate
pm_install "$CANDIDATE"
devmgr snapshot list >/dev/null            # create state to prove removal behavior
devmgr snapshot create --label pre-purge >/dev/null
# Plain removal PRESERVES state on both formats (reinstall keeps snapshots).
pm_erase
[ ! -e /usr/bin/devmgrd ] && [ ! -e /usr/local/bin/devmgrd ] || { echo "binary residue after removal"; exit 1; }
[ -d /var/lib/devmgrd ] || { echo "plain removal deleted state (should preserve)"; exit 1; }
echo "plain removal OK (files gone, state preserved)"
if [ "$FORMAT" = deb ]; then
    # deb has a purge concept: postrm purge deletes the state dir too.
    apt-get purge -y devmgr
    [ ! -d /var/lib/devmgrd ] || { echo "deb purge left state residue at /var/lib/devmgrd"; exit 1; }
    echo "purge OK (deb postrm removed the state dir, no residue)"
else
    # rpm has no purge: state is deliberately kept, README documents manual rm.
    [ -d /var/lib/devmgrd ] || { echo "rpm erase unexpectedly removed state"; exit 1; }
    rm -rf /var/lib/devmgrd                 # the documented manual cleanup
    [ ! -d /var/lib/devmgrd ] || { echo "manual state removal failed"; exit 1; }
    echo "erase residue OK (rpm preserved state; documented manual rm cleared it)"
fi

echo "UPGRADE MATRIX OK"
