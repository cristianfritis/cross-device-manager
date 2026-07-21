#include "devmgr/app/device_detail_vm.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace devmgr::app {
namespace {

// One "Label:   value" detail row. The label column is a fixed width so every
// value starts in the same place and no value abuts its colon — the widest
// label ("Modalias:") sets the width, which is what the pre-3.6 hand-padded
// literals got wrong for that one row (DESIGN.md §10 consistent presentation).
constexpr std::size_t kLabelWidth = 10;
std::string kv(const char* label, std::string_view value) {
    std::string row(label);
    if (row.size() < kLabelWidth) row.resize(kLabelWidth, ' ');
    row += value;
    return row;
}

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
    out.push_back(kv("Name:", d.name));
    out.push_back(kv("Id:", d.id.value));
    out.push_back(kv("Bus:", core::displayBus(d.bus)));
    out.push_back(kv("Status:", core::to_string(d.status)));
    out.push_back(kv("Sysfs:", d.sysfsPath));
    out.push_back(kv("VID:PID:", d.vendorId + ":" + d.productId));
    out.push_back(kv("Serial:", d.serial));
    out.push_back(kv("Driver:", d.boundDriver.value_or("(none)")));
    out.push_back(kv("Modalias:", d.modalias));
    if (d.parent.has_value()) out.push_back(kv("Parent:", d.parent->value));
    if (d.errorNote.has_value()) out.push_back(kv("Error:", *d.errorNote));
    appendDriverSection(out, d, facade_.driverInfo(d.id));
    return out;
}

}  // namespace devmgr::app
