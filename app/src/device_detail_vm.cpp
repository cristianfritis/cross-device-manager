#include "devmgr/app/device_detail_vm.hpp"

#include <string>
#include <vector>

namespace devmgr::app {
namespace {

std::string joinDeps(const std::vector<std::string>& deps) {
    std::string joined;
    for (const auto& dep : deps) {
        if (!joined.empty()) joined += ", ";
        joined += dep;
    }
    return joined;
}

// One rendered driver row: "* usbhid v1.0 — signed: Build key" (bound marker,
// builtin/version/signature). Kernel-module signature is shown; builtins carry
// no signature note.
std::string driverLine(const core::Device& d, const core::Driver& drv) {
    const bool bound = d.boundDriver.has_value() && drv.name == *d.boundDriver;
    std::string line = (bound ? "* " : "  ") + drv.name;
    if (drv.kind == core::DriverKind::Builtin) line += " (builtin)";
    if (!drv.version.empty()) line += " v" + drv.version;
    if (drv.kind != core::DriverKind::Builtin)
        line +=
            drv.isSigned ? " — signed: " + drv.signer.value_or("unknown signer") : " — UNSIGNED";
    return line;
}

void appendDriverSection(std::vector<std::string>& out, const core::Device& d,
                         const std::vector<core::Driver>& drivers) {
    if (drivers.empty()) return;
    out.emplace_back("— Driver —");
    for (const auto& drv : drivers) {
        out.push_back(driverLine(d, drv));
        if (!drv.dependencies.empty()) out.push_back("    depends: " + joinDeps(drv.dependencies));
    }
}

}  // namespace

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
    appendDriverSection(out, d, facade_.driverInfo(d.id));
    return out;
}

}  // namespace devmgr::app
