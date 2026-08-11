#pragma once
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "devmgr/core/models.hpp"

namespace devmgr::core {

// The product-facing vocabulary for per-device detail rows (design D13).
//
// Device::properties is a raw, platform-native map: udev keys on Linux,
// property-store keys on Windows. Nothing above the PAL may render one of those
// keys, because a key name is an implementation detail of one platform and
// would read as noise — or as a different word for the same thing — on another.
// This header is the vocabulary those maps land in: a CLOSED set of stable
// field identifiers, one product-facing label each, and a fixed display order.
//
// Where each concern lives:
//   platform/*   maps its native keys into these identifiers, inside its own
//                directory; no native key name crosses the PAL boundary
//   core         owns the identifiers, the labels, and the order (this header)
//   gui/tui/cli  render whatever fields are present, in this order, and author
//                no label of their own
//
// Closed on purpose. A backend cannot introduce a field by writing a new key:
// there is no pass-through, so the only way to show something new is to add it
// here, once, for every surface at the same time.
//
// Device STATUS is deliberately not a field here. A devnode problem condition
// maps onto the shared device-status taxonomy that roleForDeviceStatus already
// colours; a second, differently-worded "Status" row is exactly the divergence
// the shared presentation helpers exist to prevent.
enum class DetailField {
    Manufacturer,
    Driver,
    DriverVersion,
    DriverProvider,
    DriverDate,
    Class,
    HardwareIds,
    DeviceInstanceId,
};

// Display order, and the enumeration a total-mapping test walks. Identity-ish
// facts last: a user scanning the pane reads who made it and what drives it
// before the long matching strings.
inline constexpr std::array<DetailField, 8> kDetailFieldOrder{
    DetailField::Manufacturer,   DetailField::Driver,          DetailField::DriverVersion,
    DetailField::DriverProvider, DetailField::DriverDate,      DetailField::Class,
    DetailField::HardwareIds,    DetailField::DeviceInstanceId};

// The one label a surface renders for this field, on every platform and in
// every surface. Total: an unlisted value cannot occur, because the set is
// closed and this switch is exhaustive.
std::string_view detailFieldLabel(DetailField field);

// The stable identifier a backend writes into Device::properties to populate
// the field. Deliberately NOT a native key on any platform — it is this
// vocabulary's own name, so the same string means the same row everywhere.
std::string_view detailFieldKey(DetailField field);

// One populated field, ready to render.
struct DetailFieldValue {
    DetailField field = DetailField::Manufacturer;
    std::string_view label;  // detailFieldLabel(field) — carried so a surface never re-derives it
    std::string value;
};

// A device's populated detail fields, in kDetailFieldOrder. A field the backend
// did not supply is ABSENT from the result rather than present-and-empty, so a
// surface that renders every entry it is handed cannot produce a blank row
// (ui-accessibility: "Absent properties are omitted, not blanked").
//
// This is the only way a surface reaches Device::properties. No surface
// iterates the raw map, so no raw key can reach a screen even by accident.
std::vector<DetailFieldValue> detailFields(const Device& device);

}  // namespace devmgr::core
