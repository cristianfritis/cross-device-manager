#pragma once
#include <string>

#include "devmgr/core/models.hpp"

namespace devmgr::core {

// The single source of truth for how a device reads to a user, so the list, the
// detail pane, the GUI, and status prose can never disagree on what a device is
// called (DESIGN.md §10 consistent device presentation; §9 "visible nouns match"
// across surfaces). Every caller renders these two strings; nobody re-derives a
// label from Device fields.
//
// PURE FORMATTER over fields the enumerator already populated. It does NOT call
// libudev/libpci, bundle pci.ids, or add any dependency — hardware-name
// resolution belongs to the platform layer, which already ran it: mapDevice()
// copies the device's whole udev property map into Device::properties, so the
// hwdb-curated strings this reads are DTO data like any other field. The
// property keys are read defensively (absent -> empty), so a Device built by a
// test fake or a non-udev backend degrades to the raw-id tiers instead of
// misreporting.
//
// Precedence — first tier that yields a name wins:
//   1. Device::name when it is a real name (the mapper's ID_MODEL_FROM_DATABASE
//      / ID_MODEL / product chain), plus a direct ID_MODEL_FROM_DATABASE read as
//      a backstop.
//   2. the device's self-reported product string, minus udev's underscores and
//      minus the junk case where it is merely the hex product id repeated.
//   3. vendor + PCI subclass  ("AMD USB controller")
//   4. vendor + PCI class     ("AMD Serial bus controller")
//   5. vendor alone           ("Synaptics device")
//   6. bus + "device"         ("PCI device") — never a bare kernel address,
//      which is what displayDeviceIdentity() is for.
// The vendor joins only in tiers 3-5, where the remainder is a category rather
// than a product and would not identify anything on its own. Tiers 1-2 keep the
// catalogue's own wording so labels stay stable and stay inside the row budget.
//
// Device::name is REJECTED when it is the kernel's positional name (a PCI
// address, a USB port chain, or literally the last sysfs path segment): that is
// the mapper's last-resort fallback, not a name, and showing it as the label is
// the defect this formatter exists to fix.
std::string displayDeviceName(const Device& device);

// The stable-identity line that accompanies the name: "1022:151b · 0000:c5:00.4".
// Two devices legitimately share a name (a machine has six identically named
// bridges), so this is what tells them apart, and it is the only place a raw
// address is a correct thing to print. Either half is omitted when unknown;
// empty when the device carries neither ids nor a sysfs path.
std::string displayDeviceIdentity(const Device& device);

}  // namespace devmgr::core
