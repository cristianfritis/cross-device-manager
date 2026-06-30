#include "devmgr/app/device_detail_vm.hpp"

namespace devmgr::app {

std::vector<std::string> DeviceDetailVM::lines(const std::optional<core::DeviceId>& id) const {
    if (!id.has_value()) return {"(no device selected)"};
    auto dev = facade_.findById(*id);
    if (!dev.has_value()) return {"(no device selected)"};

    const core::Device& d = *dev;
    std::vector<std::string> out;
    out.push_back(std::string("Name:    ") + d.name);
    out.push_back(std::string("Id:      ") + d.id.value);
    out.push_back(std::string("Bus:     ") + core::to_string(d.bus));
    out.push_back(std::string("Status:  ") + core::to_string(d.status));
    out.push_back(std::string("Sysfs:   ") + d.sysfsPath);
    out.push_back(std::string("VID:PID: ") + d.vendorId + ":" + d.productId);
    out.push_back(std::string("Serial:  ") + d.serial);
    out.push_back(std::string("Driver:  ") + d.boundDriver.value_or("(none)"));
    out.push_back(std::string("Modalias:") + d.modalias);
    if (d.parent.has_value()) out.push_back(std::string("Parent:  ") + d.parent->value);
    if (d.errorNote.has_value()) out.push_back(std::string("Error:   ") + *d.errorNote);
    return out;
}

}  // namespace devmgr::app
