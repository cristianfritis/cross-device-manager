# shellcheck shell=sh
# shellcheck disable=SC2034  # consumed by install.sh AND uninstall.sh
# Shared path manifest (packaging-tarball spec): uninstall.sh removes exactly
# what install.sh writes, so both source this single list.
BIN_DIR=/usr/local/bin
BINARIES="devmgrd devmgr devmgr-tui devmgr-gui"
DBUS_POLICY_DST=/usr/share/dbus-1/system.d/org.devmgr.Manager1.conf
DBUS_SERVICE_DST=/usr/share/dbus-1/system-services/org.devmgr.Manager1.service
POLKIT_DST=/usr/share/polkit-1/actions/org.devmgr.policy
SYSTEMD_UNIT_DST=/usr/lib/systemd/system/devmgrd.service
OPENRC_DST=/etc/init.d/devmgrd
STATE_DIR=/var/lib/devmgrd
