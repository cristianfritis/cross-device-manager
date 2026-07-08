#!/bin/sh
# Generates three tiny fake .ko files for the KmodDriverManager unit tests
# (see tests/unit/test_kmod_driver_manager.cpp). libkmod's module lookup
# (kmod_module_new_from_lookup) reads modules.dep.bin / modules.alias.bin
# exclusively -- it has never had a text-index fallback -- so the tests need
# real depmod-processed .ko files, not hand-written text indexes.
#
#   helper.ko  -- defines helper_fn, exported via a fake __ksymtab_strings
#                 section so depmod can resolve a dependency onto it.
#   dummy.ko   -- undefined reference to helper_fn (=> depmod records a
#                 dependency on helper.ko) plus a USB modalias.
#   usbhid.ko  -- a bare module so byName("usbhid") resolves.
#
# Usage: generate_fixtures.sh <output_dir> [cc] [objcopy]
set -eu

OUT=$1
CC=${2:-cc}
OBJCOPY=${3:-objcopy}

mkdir -p "$OUT"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

# helper.ko -- defines helper_fn and exports it via __ksymtab_strings.
printf 'int helper_fn;\n' | "$CC" -x c -c - -o helper_base.o
printf 'helper_fn\0' >ksymtab_strings.bin
"$OBJCOPY" --add-section __ksymtab_strings=ksymtab_strings.bin helper_base.o helper_sym.o
printf 'name=helper\0license=GPL\0' >helper.modinfo
"$OBJCOPY" --add-section .modinfo=helper.modinfo helper_sym.o "$OUT/helper.ko"

# dummy.ko -- undefined ref to helper_fn (=> depmod records dep on helper) + modalias.
printf 'extern int helper_fn; int *dummy_uses = &helper_fn;\n' | "$CC" -x c -c - -o dummy.o
printf 'name=dummy\0alias=usb:v046DpC52B*\0license=GPL\0' >dummy.modinfo
"$OBJCOPY" --add-section .modinfo=dummy.modinfo dummy.o "$OUT/dummy.ko"

# usbhid.ko -- bare module so byName("usbhid") resolves.
echo '' | "$CC" -x c -c - -o empty.o
printf 'name=usbhid\0license=GPL\0' >usbhid.modinfo
"$OBJCOPY" --add-section .modinfo=usbhid.modinfo empty.o "$OUT/usbhid.ko"
