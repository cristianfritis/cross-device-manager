#!/usr/bin/env bash
# test/vm/lib/fwupd-test-device.sh
#
# Shared helpers for enabling and discovering fwupd's synthetic "test"
# plugin device at runtime, instead of hardcoding its Device ID/GUID/name
# in every smoke script.
#
# We confirmed firsthand that these are NOT stable across fwupd
# versions/distro packaging:
#   fwupd 2.0.20 / Ubuntu 22.04 gave us:
#     name "Integrated Webcam™"
#     id   08d460be0f1f9f128413f816022a6439e0078018
#     guid b585990a-003e-5270-89d5-3705a17f9a43
# A future fwupd bump (this box already resolved 1.7->2.0.20 just from
# `apt-get update` once) could change any of these. Discover, don't
# hardcode — that's the whole point of this file.
#
# Usage from any smoke script:
#   source "$(dirname "$0")/lib/fwupd-test-device.sh"
#   fwupd_test_device_ensure_enabled
#   fwupd_test_device_discover || { fwupd_test_device_dump_diagnostics; exit 1; }
#   echo "$FWUPD_TEST_DEVICE_ID"

set -u

# --- enablement ----------------------------------------------------------

fwupd_test_device_ensure_enabled() {
    if ! dpkg -s fwupd-tests >/dev/null 2>&1; then
        echo "-- installing fwupd-tests (ships fake device fixtures)" >&2
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y fwupd-tests
    fi

    sudo systemctl start fwupd || true
    sleep 1

    if sudo fwupdtool enable-test-devices >/tmp/fwupd-test-enable.log 2>&1; then
        echo "-- fwupdtool enable-test-devices OK" >&2
    else
        echo "-- fwupdtool enable-test-devices failed/unavailable; writing TestDevices=true directly" >&2
        local conf=/etc/fwupd/fwupd.conf
        if sudo test -f "$conf"; then
            if sudo grep -qE '^(EnableTestDevices|TestDevices)=' "$conf"; then
                sudo sed -i -E 's/^(EnableTestDevices|TestDevices)=.*/TestDevices=true/' "$conf"
            elif sudo grep -q '^\[fwupd\]' "$conf"; then
                sudo sed -i '/^\[fwupd\]/a TestDevices=true' "$conf"
            else
                printf '\n[fwupd]\nTestDevices=true\n' | sudo tee -a "$conf" >/dev/null
            fi
        else
            printf '[fwupd]\nTestDevices=true\n' | sudo tee "$conf" >/dev/null
        fi
        printf 'y\n' | sudo fwupdmgr modify-config fwupd TestDevices true \
            >/tmp/fwupd-test-modify-config.log 2>&1 || true
    fi

    local remote
    for remote in /etc/fwupd/remotes.d/fwupd-tests.conf /usr/share/fwupd/remotes.d/fwupd-tests.conf; do
        [ -f "$remote" ] && sudo sed -i 's/^Enabled=false/Enabled=true/' "$remote" 2>/dev/null || true
    done
    if [ ! -f /etc/fwupd/remotes.d/fwupd-tests.conf ] && [ -f /usr/share/fwupd/remotes.d/fwupd-tests.conf ]; then
        sudo mkdir -p /etc/fwupd/remotes.d
        sudo cp /usr/share/fwupd/remotes.d/fwupd-tests.conf /etc/fwupd/remotes.d/fwupd-tests.conf
        sudo sed -i 's/^Enabled=false/Enabled=true/' /etc/fwupd/remotes.d/fwupd-tests.conf
    fi

    sudo systemctl restart fwupd
    local i
    for i in $(seq 1 50); do
        busctl status org.freedesktop.fwupd >/dev/null 2>&1 && break
        sleep 0.2
    done

    fwupdmgr refresh --force >/tmp/fwupd-test-refresh.log 2>&1 || true
}

# --- discovery -------------------------------------------------------------

# Populates FWUPD_TEST_DEVICE_ID / FWUPD_TEST_DEVICE_GUID / FWUPD_TEST_DEVICE_NAME
# by asking the live daemon which device(s) belong to the "test" plugin.
# Returns 1 (and leaves vars empty) if none found.
fwupd_test_device_discover() {
    FWUPD_TEST_DEVICE_ID=""
    FWUPD_TEST_DEVICE_GUID=""
    FWUPD_TEST_DEVICE_NAME=""

    local json
    json="$(fwupdmgr get-devices --json 2>/dev/null || true)"
    if [ -z "$json" ]; then
        echo "-- fwupdmgr get-devices --json returned nothing" >&2
        return 1
    fi

    if command -v jq >/dev/null 2>&1; then
        FWUPD_TEST_DEVICE_ID="$(echo "$json" | jq -r \
            '.Devices[] | select(.Plugin=="test") | .DeviceId' 2>/dev/null | head -1)"
        FWUPD_TEST_DEVICE_GUID="$(echo "$json" | jq -r \
            '.Devices[] | select(.Plugin=="test") | .Guid[0]' 2>/dev/null | head -1)"
        FWUPD_TEST_DEVICE_NAME="$(echo "$json" | jq -r \
            '.Devices[] | select(.Plugin=="test") | .Name' 2>/dev/null | head -1)"
    else
        echo "-- jq not found; falling back to crude text parsing (install jq for reliability)" >&2
        local text
        text="$(fwupdmgr get-devices 2>/dev/null || true)"
        FWUPD_TEST_DEVICE_ID="$(echo "$text" | grep -oE '[0-9a-f]{40}' | head -1)"
        FWUPD_TEST_DEVICE_GUID="$(echo "$text" | grep -oE '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}' | head -1)"
    fi

    if [ -z "$FWUPD_TEST_DEVICE_ID" ] && [ -z "$FWUPD_TEST_DEVICE_GUID" ]; then
        echo "-- no device with Plugin==\"test\" found in fwupdmgr get-devices output" >&2
        return 1
    fi

    export FWUPD_TEST_DEVICE_ID FWUPD_TEST_DEVICE_GUID FWUPD_TEST_DEVICE_NAME
    echo "-- discovered fwupd test device: name='${FWUPD_TEST_DEVICE_NAME:-?}' id=${FWUPD_TEST_DEVICE_ID:-?} guid=${FWUPD_TEST_DEVICE_GUID:-?}" >&2
    return 0
}

# --- diagnostics -------------------------------------------------------------

fwupd_test_device_dump_diagnostics() {
    echo "-- /etc/fwupd/fwupd.conf:" >&2
    sudo cat /etc/fwupd/fwupd.conf >&2 2>/dev/null || true
    echo "-- plugins (test*):" >&2
    fwupdmgr get-plugins 2>&1 | grep -A3 -E '^test' >&2 || true
    echo "-- get-devices:" >&2
    fwupdmgr get-devices >&2 2>&1 || true
}
