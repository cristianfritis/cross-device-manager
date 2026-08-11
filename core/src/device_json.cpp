#include "devmgr/core/device_json.hpp"

#include <nlohmann/json.hpp>

#include "devmgr/core/device_detail_fields.hpp"
#include "devmgr/core/device_presentation.hpp"

namespace devmgr::core {
namespace {

using nlohmann::json;

// nlohmann's ordered_json preserves insertion order, so the key sequence below
// IS the emitted order — the byte-identical-across-runs requirement is a
// property of this function rather than of a sort somewhere downstream.
nlohmann::ordered_json toObject(const Device& device) {
    nlohmann::ordered_json obj;
    obj["id"] = device.id.value;
    // The SAME canonical name and bus label the GUI and TUI rows show. A second
    // spelling here would be a third surface disagreeing with the other two,
    // which is exactly what the shared helpers exist to prevent.
    obj["name"] = displayDeviceName(device);
    obj["bus"] = displayBus(device.bus);
    obj["status"] = to_string(device.status);
    // Omitted rather than null when the backend did not supply them: a JSON
    // null would be the structured form of a blank row, and the same rule
    // applies to a parser as to a reader (ui-accessibility).
    if (!device.nativeId.empty()) obj["identity"] = device.nativeId;
    if (!device.hardwareId.empty()) obj["hardwareId"] = device.hardwareId;
    if (!device.vendorId.empty()) obj["vendorId"] = device.vendorId;
    if (!device.productId.empty()) obj["productId"] = device.productId;
    if (!device.serial.empty()) obj["serial"] = device.serial;
    if (device.boundDriver.has_value()) obj["driver"] = *device.boundDriver;
    if (device.parent.has_value()) obj["parent"] = device.parent->value;

    // The shared detail vocabulary, in display order, under the same labels
    // every surface prints. Raw platform keys never appear — detailFields() is
    // the only reader of the property map, and it is closed.
    auto details = nlohmann::ordered_json::array();
    for (const auto& field : detailFields(device))
        details.push_back(nlohmann::ordered_json{{"label", field.label}, {"value", field.value}});
    if (!details.empty()) obj["details"] = std::move(details);
    return obj;
}

}  // namespace

std::string deviceToJson(const Device& device) {
    return toObject(device).dump(2);
}

std::string deviceListToJson(const std::vector<Device>& devices) {
    auto array = nlohmann::ordered_json::array();
    for (const auto& device : devices) array.push_back(toObject(device));
    return array.dump(2);
}

}  // namespace devmgr::core
