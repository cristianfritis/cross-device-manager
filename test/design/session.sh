#!/bin/bash
# Brings up one verification session, then execs "$@" inside it.
#
# Posture is the container's environment, never a product seam (D4):
#   POSTURE=degraded  no devmgrd, no fwupd -- the container's own default
#   POSTURE=healthy   a system bus carrying devmgrd AND the fwupd double
#   DKMS=present|missing   populate or omit /var/lib/dkms
#   DEVICES=fixture|host   run the app under umockdev with a fixed device set
# The device set is a fixture by default: enumerating the host's real devices
# makes every captured surface machine-specific, and the 12.2 long-name and
# long-path rows then depend on what hardware happens to be present.
# SCREEN_W / SCREEN_H size the X screen so window-size requirements are
# exercised against a real window under a real window manager (D2).
set -u

: "${POSTURE:=degraded}"
: "${DKMS:=missing}"
: "${DEVICES:=fixture}"
: "${SCREEN_W:=1024}"
: "${SCREEN_H:=640}"
: "${DISPLAY_NUM:=99}"

# Everything this session starts is torn down when it ends. Without this the
# daemons outlive their session, and because run.sh drives several sessions in
# one container a later DEGRADED posture would find the previous HEALTHY
# posture's system bus still up -- silently verifying the wrong state.
SESSION_PIDS=""
# shellcheck disable=SC2317  # invoked via the EXIT/INT/TERM trap below
cleanup() {
    for pid in $SESSION_PIDS; do
        kill "$pid" 2>/dev/null || true
    done
    # The system bus is shared state on a fixed socket path, so it must go too.
    # The system bus is INFRASTRUCTURE and is left running; the posture is
    # which NAMES are owned on it. Tearing the bus down here left a stale
    # /run/dbus/pid behind, so the next session's dbus-daemon refused to start
    # and its clients silently reached the OLD bus, where the previous
    # posture's owners were still registered.
    pkill -f devmgr_fake_fwupd 2>/dev/null || true
    pkill -f "devmgrd --bus system" 2>/dev/null || true
    # Wait for the names to actually be released before the next session claims
    # them -- kill returns long before the bus notices the owner is gone.
    for _ in $(seq 1 40); do
        if ! owns org.freedesktop.fwupd && ! owns org.devmgr.Manager1; then break; fi
        sleep 0.25
    done
}
trap cleanup EXIT INT TERM

track() { SESSION_PIDS="$SESSION_PIDS $!"; }

# Name ownership, not introspection: the double registers a hand-built vtable
# and does not implement org.freedesktop.DBus.Introspectable, so `gdbus
# introspect` fails against a name that is in fact owned and answering.
owns() {
    gdbus call --system --dest org.freedesktop.DBus \
        --object-path /org/freedesktop/DBus \
        --method org.freedesktop.DBus.NameHasOwner "$1" 2>/dev/null |
        grep -q true
}

export DISPLAY=":${DISPLAY_NUM}"
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"

# Qt 6's accessibility knob. QT_ACCESSIBILITY=1 is NOT it -- with that alone the
# app runs and simply never registers on the a11y bus, which reads downstream as
# "this app has no controls" rather than as an error (task 1 spike).
export QT_QPA_PLATFORM=xcb
export QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1

Xvfb ":${DISPLAY_NUM}" -screen 0 "${SCREEN_W}x${SCREEN_H}x24" -nolisten tcp >/dev/null 2>&1 &
track
for _ in $(seq 1 40); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.25; done
xdpyinfo >/dev/null 2>&1 || { echo "session: Xvfb did not come up" >&2; exit 1; }

# A window manager is required, not decoration: with no WM nothing honours
# window geometry, and the size requirements below 800x520 cannot be exercised.
openbox >/dev/null 2>&1 &
track
sleep 1

/usr/libexec/at-spi-bus-launcher --launch-immediately >/dev/null 2>&1 &
track
for _ in $(seq 1 40); do
    gdbus call --session --dest org.a11y.Bus --object-path /org/a11y/bus \
        --method org.a11y.Bus.GetAddress >/dev/null 2>&1 && break
    sleep 0.25
done
/usr/libexec/at-spi2-registryd >/dev/null 2>&1 &
track
sleep 1

# --- DKMS posture: the container filesystem IS the fixture. DkmsStatusProvider
# --- is default-constructed on /var/lib/dkms, so no env override is needed.
rm -rf /var/lib/dkms
if [ "$DKMS" = "present" ]; then
    mkdir -p /var/lib/dkms/acpi_tad/1.0/"$(uname -r)"/x86_64/module
fi

# --- devmgrd posture -------------------------------------------------------
if [ "$POSTURE" = "healthy" ]; then
    mkdir -p /run/dbus /usr/share/dbus-1/system.d
    cp /src/daemon/data/org.devmgr.Manager1.conf /usr/share/dbus-1/system.d/ 2>/dev/null || true
    cp /src/test/design/fixtures/org.freedesktop.fwupd.conf /usr/share/dbus-1/system.d/ 2>/dev/null || true
    if [ ! -S /run/dbus/system_bus_socket ]; then
        rm -f /run/dbus/pid 2>/dev/null || true
        dbus-daemon --system --fork >/dev/null 2>&1
        sleep 1
    fi
    if [ -x /src/build/linux-debug/daemon/devmgrd ]; then
        /src/build/linux-debug/daemon/devmgrd --bus system \
            > /tmp/devmgrd.log 2>&1 &
        track
        sleep 2
    fi

    # --- fwupd posture: the SAME double the devmgr_fwupd suite tests the real
    # --- provider against, claimed on this private system bus with a fixed
    # --- inventory. FwupdUpdateProvider value-initializes useSessionBus to
    # --- false, so the GUI looks for it here exactly as it does in production.
    if [ -x /src/build/linux-debug/tests/fwupd/devmgr_fake_fwupd ] && ! owns org.freedesktop.fwupd; then
        /src/build/linux-debug/tests/fwupd/devmgr_fake_fwupd > /tmp/fake-fwupd.log 2>&1 &
        track
        for _ in $(seq 1 40); do
            owns org.freedesktop.fwupd && break
            sleep 0.25
        done
        owns org.freedesktop.fwupd ||
            echo "session: fwupd double did not claim the name" >&2
    fi
fi

# --- Device set. umockdev-run replaces /sys for the wrapped process, so the app
# --- sees exactly the fixture and nothing of the host.
LAUNCH_PREFIX=""
if [ "$DEVICES" = "fixture" ]; then
    LAUNCH_PREFIX="umockdev-run -d /src/test/design/fixtures/devices.umockdev --"
fi
export LAUNCH_PREFIX

"$@"
status=$?
exit "$status"
