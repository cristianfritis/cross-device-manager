#include "devmgr/core/device_detail_fields.hpp"

#include <string>

namespace devmgr::core {

std::string_view detailFieldLabel(DetailField field) {
    switch (field) {
        case DetailField::Manufacturer:
            return "Manufacturer";
        case DetailField::Driver:
            return "Driver";
        case DetailField::DriverVersion:
            return "Driver Version";
        case DetailField::DriverProvider:
            return "Driver Provider";
        case DetailField::DriverDate:
            return "Driver Date";
        case DetailField::Class:
            return "Class";
        case DetailField::HardwareIds:
            return "Hardware IDs";
        case DetailField::DeviceInstanceId:
            return "Device Instance ID";
    }
    return "";  // unreachable: the set is closed and the switch above is total
}

// The keys are this vocabulary's own, prefixed so they cannot collide with a
// platform's native keys sharing the same map. A backend writing
// "devmgr.detail.manufacturer" is stating which ROW it means, not which native
// property it read — which is the whole point of the indirection.
std::string_view detailFieldKey(DetailField field) {
    switch (field) {
        case DetailField::Manufacturer:
            return "devmgr.detail.manufacturer";
        case DetailField::Driver:
            return "devmgr.detail.driver";
        case DetailField::DriverVersion:
            return "devmgr.detail.driver_version";
        case DetailField::DriverProvider:
            return "devmgr.detail.driver_provider";
        case DetailField::DriverDate:
            return "devmgr.detail.driver_date";
        case DetailField::Class:
            return "devmgr.detail.class";
        case DetailField::HardwareIds:
            return "devmgr.detail.hardware_ids";
        case DetailField::DeviceInstanceId:
            return "devmgr.detail.device_instance_id";
    }
    return "";  // unreachable: the set is closed and the switch above is total
}

std::vector<DetailFieldValue> detailFields(const Device& device) {
    std::vector<DetailFieldValue> out;
    out.reserve(kDetailFieldOrder.size());
    for (const auto field : kDetailFieldOrder) {
        const auto it = device.properties.find(std::string(detailFieldKey(field)));
        // Absent AND empty-valued both mean "the backend did not supply this":
        // a backend that writes an empty string has reported nothing, and a
        // blank row would claim otherwise.
        if (it == device.properties.end() || it->second.empty()) continue;
        out.push_back({.field = field, .label = detailFieldLabel(field), .value = it->second});
    }
    return out;
}

}  // namespace devmgr::core
