#!/usr/bin/env bash
# probe-canonical-names.sh — empirical Step-0 for tasks.md 7.1 (displayDeviceName).
# PURPOSE: decide, from THIS box, whether a human-readable name already exists for a
#   device/module and whether the daemon DTO already carries it — i.e. whether 7.1 is a
#   pure-formatter / wiring fix (GO) or needs an additive DTO field (STOP/escalate).
# VERDICT BAKED IN (why there is no "internal DB" and no sqlite here):
#   - hw-name resolution ALREADY lives in the OS/daemon: udev/hwdb (compiles pci.ids/
#     usb.ids into a binary trie), libpci/pciutils, libkmod (the .ko's own description),
#     and fwupd (firmware names — already canonical in your Updates tab).
#   - core::displayDeviceName MUST be a PURE FORMATTER over daemon DTO fields. It must NOT
#     call libudev/libpci, bundle pci.ids, or add sqlite/any dependency (locked: no new
#     dependency, no ApiVersion change). A bundled DB goes stale + carries a license line
#     + duplicates a DB the distro keeps current; sqlite is the wrong shape for prefix-key
#     hw lookups and belongs ONLY to app-owned mutable data (audit log / aliases / cache),
#     decided separately. So: if these probes return names, there is NOTHING left to build.
# USAGE:
#   ./scripts/probe-canonical-names.sh              # inventory this PC
#   ./scripts/probe-canonical-names.sh --pci BDF   # deep probe one PCI device
#   ./scripts/probe-canonical-names.sh BDF         # legacy spelling of --pci BDF
set +e +u

usage() {
  cat <<'EOF'
Usage:
  probe-canonical-names.sh [--all]
  probe-canonical-names.sh --pci DOMAIN:BUS:SLOT.FUNCTION
  probe-canonical-names.sh DOMAIN:BUS:SLOT.FUNCTION

--all (the default) inventories human-readable component names from the
authoritative source available for each hardware class. --pci retains the
original detailed PCI/DTO diagnostic. No mode changes the system.
EOF
}

section() {
  printf '\n=== %s ===\n' "$1"
}

print_dmi_field() {
  label="$1"
  field="$2"
  path="/sys/devices/virtual/dmi/id/$field"
  value=$(tr -d '\000' <"$path" 2>/dev/null)
  [ -n "$value" ] && printf "  %-20s %s\n" "$label" "$value"
}

print_udev_database_names() {
  if ! command -v udevadm >/dev/null 2>&1; then
    echo "  (udevadm unavailable)"
    return
  fi

  udevadm info --export-db 2>/dev/null | awk '
    BEGIN { RS=""; FS="\n"; found=0 }
    {
      path=""; subsystem=""
      for (key in property) delete property[key]
      for (line_no=1; line_no<=NF; ++line_no) {
        line=$line_no
        if (line ~ /^P: /) path=substr(line, 4)
        else if (line ~ /^U: /) subsystem=substr(line, 4)
        else if (line ~ /^E: /) {
          assignment=substr(line, 4)
          equals=index(assignment, "=")
          if (equals > 1) property[substr(assignment, 1, equals - 1)]=substr(assignment, equals + 1)
        }
      }
      model=property["ID_MODEL_FROM_DATABASE"]
      if (model == "") next
      if (subsystem != "pci" && !(subsystem == "usb" && property["DEVTYPE"] == "usb_device")) next
      vendor=property["ID_VENDOR_FROM_DATABASE"]
      if (vendor == "") vendor=property["ID_VENDOR"]
      id=path
      sub(/^.*\//, "", id)
      printf "  %-8s %-18s %s%s%s\n", subsystem, id, vendor, vendor == "" ? "" : " ", model
      found=1
    }
    END { if (!found) print "  (no ID_MODEL_FROM_DATABASE properties in the current udev database)" }
  '
}

inventory_all() {
  echo "=== CANONICAL HUMAN-READABLE HARDWARE NAME INVENTORY ==="
  echo "Names are read-only observations. The source shown for each section is the authority;"
  echo "numeric IDs remain the stable fallback when no human-readable name exists."

  section "SYSTEM / FIRMWARE NAMES (DMI/SMBIOS via sysfs)"
  print_dmi_field "System vendor" sys_vendor
  print_dmi_field "System product" product_name
  print_dmi_field "System version" product_version
  print_dmi_field "Board vendor" board_vendor
  print_dmi_field "Board product" board_name
  print_dmi_field "Board version" board_version
  print_dmi_field "BIOS vendor" bios_vendor
  print_dmi_field "BIOS version" bios_version

  section "PROCESSOR NAME (CPUID via lscpu)"
  if command -v lscpu >/dev/null 2>&1; then
    lscpu | grep -E '^(Architecture|Vendor ID|Model name|Socket\(s\)|Core\(s\) per socket|Thread\(s\) per core):' \
      | sed 's/^/  /'
  else
    echo "  (lscpu unavailable)"
  fi

  section "PCI COMPONENT NAMES (pci.ids via lspci; class/name plus stable IDs)"
  if command -v lspci >/dev/null 2>&1; then
    lspci -Dnn | sed 's/^/  /'
  else
    echo "  (lspci unavailable; install the distro pciutils package)"
  fi

  section "USB COMPONENT NAMES (usb.ids/device descriptors via lsusb; stable IDs retained)"
  if command -v lsusb >/dev/null 2>&1; then
    lsusb | sed 's/^/  /'
  else
    echo "  (lsusb unavailable; install the distro usbutils package)"
  fi

  section "STORAGE NAMES (device firmware/udev via lsblk)"
  if command -v lsblk >/dev/null 2>&1; then
    physical_disks=()
    for block_path in /sys/class/block/*; do
      [ -e "$block_path/device" ] || continue
      [ -e "$block_path/partition" ] && continue
      physical_disks+=("/dev/${block_path##*/}")
    done
    if [ "${#physical_disks[@]}" -eq 0 ]; then
      echo "  (no physically backed block devices found)"
    else
      lsblk --nodeps --output NAME,VENDOR,MODEL,REV,TRAN,SIZE,TYPE "${physical_disks[@]}" \
        | sed 's/^/  /'
    fi
  else
    echo "  (lsblk unavailable)"
  fi

  section "UDEV CURATED MODEL NAMES (ID_MODEL_FROM_DATABASE)"
  print_udev_database_names

  section "MEMORY MODULE NAMES (SMBIOS type 17)"
  if ! command -v dmidecode >/dev/null 2>&1; then
    echo "  (dmidecode unavailable)"
  elif [ "$(id -u)" -ne 0 ]; then
    echo "  (DIMM manufacturer/part number requires root: rerun this script with sudo)"
  else
    dmidecode --type 17 2>/dev/null | awk '
      /^Memory Device$/ { if (seen) print ""; seen=1; next }
      seen && /^[[:space:]]+(Locator|Bank Locator|Size|Type|Speed|Manufacturer|Part Number):/ {
        sub(/^[[:space:]]+/, "")
        print "  " $0
      }
    '
  fi

  section "LIMITS / VERDICT"
  echo "  There is no single universal canonical-name database. PCI and USB names come from"
  echo "  distro-maintained ID databases; CPU, board, storage, and DIMM names come from firmware;"
  echo "  kernel module descriptions come from each .ko. Empty or generic firmware strings cannot"
  echo "  be improved reliably without an external vendor-specific database. Raw IDs are final fallbacks."
}

case "${1:-}" in
  ""|--all)
    inventory_all
    exit 0
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  --pci)
    if [ -z "${2:-}" ] || [ -n "${3:-}" ]; then
      usage >&2
      exit 2
    fi
    TEST_BDF="$2"
    ;;
  -*)
    printf "Unknown option: %s\n" "$1" >&2
    usage >&2
    exit 2
    ;;
  *)
    if [ -n "${2:-}" ]; then
      usage >&2
      exit 2
    fi
    TEST_BDF="$1"
    ;;
esac

SYS="/sys/bus/pci/devices/$TEST_BDF"

echo "=== PROBE 0 — tool availability (uname -r: $(uname -r)) ==="
for t in udevadm lspci lsusb lscpu lsblk dmidecode modinfo rg systemd-hwdb; do
  command -v "$t" >/dev/null 2>&1 && printf "  %-12s %s\n" "$t" "$(command -v "$t")" \
    || printf "  %-12s MISSING\n" "$t"
done

echo "=== PROBE 1 — does udev/hwdb resolve $TEST_BDF ? (the curated name, if any) ==="
# ID_*_FROM_DATABASE = the hwdb/pci.ids curated marketing name (prefer this).
# ID_VENDOR/ID_MODEL  = decoded strings (for PCI these ARE the hwdb values; for USB these
#   are the device's self-reported strings, often garbage — hence the _FROM_DATABASE pref).
out=$(udevadm info --query=property --path="$SYS" 2>/dev/null \
      | grep -E 'ID_(VENDOR|MODEL)(_FROM_DATABASE)?=')
if [ -n "$out" ]; then echo "$out" | sed 's/^/  /'; \
else echo "  (no ID_* name props -> hwdb has NO entry for this device, OR udev didn't tag it)"; fi

echo "=== PROBE 2 — libpci / pci.ids view (incl. subsystem-specific product name) ==="
echo "  -- lspci -vmm -s $TEST_BDF --"
lspci -vmm -s "$TEST_BDF" 2>/dev/null | sed 's/^/  /' \
  || echo "  (lspci unavailable or no entry)"
echo "  -- raw sysfs ids --"
for f in vendor device subsystem_vendor subsystem_device; do
  printf "  %-20s = " "$f"; cat "$SYS/$f" 2>/dev/null || echo "(missing)"
done

echo "=== PROBE 3 — are the system name DBs present at all? ==="
ls -l /usr/share/hwdata/pci.ids /usr/share/hwdata/usb.ids \
      /etc/udev/hwdb.bin /usr/lib/udev/hwdb.bin 2>/dev/null | sed 's/^/  /' \
  || echo "  (one or more DB files absent — note which; on a normal distro all exist)"
command -v systemd-hwdb >/dev/null 2>&1 && echo "  systemd-hwdb: $(systemd-hwdb --version 2>&1 | head -1)"

echo "=== PROBE 4 — module human gloss comes from the .ko itself (NO db needed) ==="
for m in bluetooth amdgpu xhci_hcd ext4; do
  printf "  %-12s -> " "$m"
  d=$(modinfo -F description "$m" 2>/dev/null)
  [ -n "$d" ] && echo "$d" || echo "(no description / module not present)"
done

echo "=== PROBE 5 — DAEMON DTO / VM audit (the crux: is the name already in the DTO?) ==="
# This is a manual+grep audit; the apply-agent runs it against the real repo. The known
# symptom (D9): the LIST shows a friendly name for most rows but the DETAIL 'Name:' shows
# the bare address -> often the list-row builder and the detail builder read DIFFERENT
# fields, OR the DTO has the resolved string but the detail pane reads the address column.
echo "  In repo root, run:"
echo "    rg -n 'ID_MODEL_FROM_DATABASE|ID_MODEL|product_name|productName|->name|\\.name\\b|vendor' daemon/ core/ app/"
echo "    rg -n 'Name:' tui/src/views/devices_view.cpp          # what the detail pane prints"
echo "    rg -n 'rowsRef|rowFor|display' app/ core/             # what the list row prints"
echo "  Then answer: (a) does the device DTO struct have a field holding the hwdb-resolved"
echo "  string? (b) do the list-row builder and the detail 'Name:' builder read the SAME field?"
echo "  (c) for the bare-address rows, is that DTO field empty (daemon didn't copy hwdb) or"
echo "  populated (TUI just reads the wrong column)?"

echo
echo "=== INTERPRETATION -> 7.1 GO / STOP decision ==="
echo "  Combine probe 1/2 (does a human name EXIST on this box?) with probe 5 (does the DTO"
echo "  carry it, and does the TUI read the right field?):"
echo "   * 1/2 EMPTY                          -> no human name exists anywhere; raw id is the"
echo "                                          correct FINAL label. GO: formatter just formats"
echo "                                          the raw id (trivially correct)."
echo "   * 1/2 NON-EMPTY + DTO HAS the field"
echo "     but TUI detail reads the address    -> WIRING FIX. GO: point the detail 'Name:' at the"
echo "                                          same VM field the list uses; no IPC, no ApiVersion."
echo "   * 1/2 NON-EMPTY + DTO LACKS any"
echo "     resolved-name field                 -> STOP: the daemon isn't copying hwdb into the DTO."
echo "                                          Raise the additive-DTO-field / ApiVersion decision."
echo "                                          Do NOT work around it with a host libudev/libpci"
echo "                                          call or a bundled DB in core/TUI."
echo "  RESOLVER PRECEDENCE inside displayDeviceName (over DTO fields ONLY, never a host DB):"
echo "   (1) DTO hwdb-model field (model_from_database / ID_MODEL_FROM_DATABASE-equivalent)"
echo "   (2) DTO general model/name field"
echo "   (3) compose 'Vendor Product' if vendor+product are separate fields"
echo "   (4) raw id fallback (BDF + VID:PID kept as secondary muted text + detail rows)"
echo "  The formatter's OUTPUT is stored in ONE canonical-name VM field that BOTH GUI and TUI"
echo "  read (R1 parity). Per-kind: Modules gloss = libkmod description (technical name stays"
echo "  primary); Updates = fwupd name/vendor/summary as-is; Snapshots = no hw name."
echo "  NO sqlite, NO bundled DB, NO new dependency in any of the above."
# --- USB variant (uncomment / re-run with a USB port id, e.g. 2-1): ---
# TEST_BUS=usb; SYS=/sys/bus/usb/devices/$TEST_PORT
# udevadm info --query=property --path=$SYS | grep -E 'ID_(VENDOR|MODEL)(_FROM_DATABASE)?='
