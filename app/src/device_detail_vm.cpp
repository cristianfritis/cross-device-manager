#include "devmgr/app/device_detail_vm.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <algorithm>

#include "devmgr/core/criticality.hpp"
#include "devmgr/core/device_detail_fields.hpp"
#include "devmgr/core/device_presentation.hpp"

namespace devmgr::app {
namespace {

// One "Label:   value" detail row. The label column is a fixed width so every
// value starts in the same place and no value abuts its colon — the widest
// label ("Hardware ID:") sets the width, which is what the pre-3.6 hand-padded
// literals got wrong for that one row (DESIGN.md §10 consistent presentation).
// A label wider than the column — "Device Instance ID:" from the shared detail
// vocabulary — still gets a single separating space rather than padding every
// other row out to match it. The alternative, widening the column to the widest
// label any platform might publish, would indent every Linux row by seven
// characters it does not need, in an 80-column terminal.
constexpr std::size_t kLabelWidth = 13;
// label is a literal or a core::detailFieldLabel() at every call site and value
// never is; the analyzer sees two string_views, a reader sees "Name:" next to a
// device field.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::string kv(std::string_view label, std::string_view value) {
    std::string row(label);
    if (row.size() < kLabelWidth)
        row.resize(kLabelWidth, ' ');
    else
        row += ' ';
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

// The shared detail-field vocabulary (design D13), rendered in core's order
// with core's labels. This VM authors none of them: a backend publishes a field
// by key and every surface shows it under the same word, so two platforms
// cannot drift to "Vendor" and "Manufacturer" for the same row.
//
// The label column is the same one the model rows use, so the section reads as
// part of one table rather than a differently-aligned appendix.
void appendDetailFields(std::vector<std::string>& out,
                        const std::vector<core::DetailFieldValue>& fields) {
    for (const auto& f : fields) out.push_back(kv(std::string(f.label) + ":", f.value));
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

// The model rows plus the shared detail fields: everything that comes from the
// Device record alone, with no facade probe. Split out of lines() so each half
// stays under the analyzer's size threshold and reads on its own.
void appendDeviceRows(std::vector<std::string>& out, const core::Device& d) {
    // Which properties a device carries depends on the platform backend that
    // enumerated it, so a row whose value the backend did not supply is OMITTED
    // rather than shown blank, dashed, or "unknown" (ui-accessibility: "Absent
    // properties are omitted, not blanked"). A device from a backend with fewer
    // properties then reads as a smaller correct record instead of a damaged
    // one. Name, Id, Bus and Status are unconditional because the model
    // guarantees them for every device on every platform.
    const auto row = [&out](const char* label, std::string_view value) {
        if (!value.empty()) out.push_back(kv(label, value));
    };
    // The same canonical formatter the list rows use, so a device cannot be
    // called one thing in the list and another in the detail pane.
    out.push_back(kv("Name:", core::displayDeviceName(d)));
    // The three identity rows sit directly under the canonical name (R1): the
    // address is what correlates a row with lspci/lsusb/dmesg, VID:PID is what
    // identifies the model, and Id is the app's own stable handle. Now that the
    // label above them is a NAME rather than a bare kernel address, the address
    // has to be shown somewhere or it is simply lost.
    row("Address:", core::displayDeviceAddress(d));
    if (!d.vendorId.empty() || !d.productId.empty())
        out.push_back(kv("VID:PID:", d.vendorId + ":" + d.productId));
    out.push_back(kv("Id:", d.id.value));
    out.push_back(kv("Bus:", core::displayBus(d.bus)));
    out.push_back(kv("Status:", core::to_string(d.status)));
    // The shared vocabulary the backend published for this device, read once so
    // the model rows below can stand down where it already covers the same fact.
    // A backend that publishes "Device Instance ID" and one that leaves nativeId
    // as the only identity row must not both appear: the user would read one
    // string twice under two labels and reasonably assume they differ.
    const auto fields = core::detailFields(d);
    const auto published = [&fields](core::DetailField field) {
        return std::ranges::any_of(fields, [field](const auto& f) { return f.field == field; });
    };
    // "Identity" and "Hardware ID", not "Sysfs" and "Modalias": both of those
    // name a Linux mechanism, and this field holds a canonical sysfs path on
    // Linux and a device instance identifier on Windows (design D3). A label
    // that names one platform's mechanism is a lie on the other, and
    // ui-accessibility forbids either surface printing one at all.
    if (!published(core::DetailField::DeviceInstanceId)) row("Identity:", d.nativeId);
    row("Serial:", d.serial);
    if (d.boundDriver.has_value() && !published(core::DetailField::Driver))
        out.push_back(kv("Driver:", *d.boundDriver));
    if (!published(core::DetailField::HardwareIds)) row("Hardware ID:", d.hardwareId);
    if (d.parent.has_value()) out.push_back(kv("Parent:", d.parent->value));
    if (d.errorNote.has_value()) out.push_back(kv("Error:", *d.errorNote));
    appendDetailFields(out, fields);
}

}  // namespace

std::vector<std::string> DeviceDetailVM::lines(const std::optional<core::DeviceId>& id) const {
    if (!id.has_value()) return {"(no device selected)"};
    auto dev = facade_.findById(*id);
    if (!dev.has_value()) return {"(no device selected)"};

    const core::Device& d = *dev;
    std::vector<std::string> out;
    appendDeviceRows(out, d);
    // R4/R6: the list marks a load-bearing device with a glyph (TUI) or the word
    // (GUI); the risk itself is named here, so the marker never has to carry its
    // meaning alone (docs/DESIGN.md §10). Classified from the guard's OWN facts
    // and policy, so a device shown as essential is exactly one the guard would
    // refuse to disable. Probing reads the filesystem — lines() is called on
    // selection change and cached by both frontends, never per frame (§8).
    if (const auto facts = facade_.criticalityFacts()) {
        const auto level = core::classifyDevice(*facts, d.nativeId);
        if (level != core::Criticality::Ordinary) {
            out.push_back(kv("Risk:", std::string(core::displayCriticality(level)) +
                                          " — disabling this may make the system unusable"));
        }
    }
    appendDriverSection(out, d, facade_.driverInfo(d.id));
    return out;
}

}  // namespace devmgr::app
