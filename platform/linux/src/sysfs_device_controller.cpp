#include "devmgr/platform/linux/sysfs_device_controller.hpp"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

namespace devmgr::platform_linux {
namespace fs = std::filesystem;

namespace {
// Canonical containment: `path` must resolve to a directory at or below
// `root`. Rejects symlink/".." escapes — the daemon trusts nothing from the
// client (spec: validate before guard/auth/act).
bool isContained(const fs::path& canonicalPath, const fs::path& canonicalRoot) {
    const auto rel = canonicalPath.lexically_relative(canonicalRoot);
    return !rel.empty() && !rel.native().starts_with("..");
}

core::Result<void> writeAttr(const fs::path& attr, const std::string& value) {
    std::ofstream out(attr);
    if (!out)
        return core::makeError(core::Error::Code::Io, "cannot open " + attr.string() + ": " +
                                                          std::generic_category().message(errno));
    out << value;
    out.flush();
    if (!out) return core::makeError(core::Error::Code::Io, "write failed: " + attr.string());
    return {};
}

// <dev>/subsystem -> <root>/bus/<bus>; returns the bus directory.
core::Result<fs::path> busDirFor(const fs::path& device) {
    std::error_code ec;
    fs::path bus = fs::weakly_canonical(device / "subsystem", ec);
    if (ec || !fs::is_directory(bus, ec))
        return core::makeError(core::Error::Code::Unsupported,
                               "no subsystem link at " + device.string());
    return bus;
}

std::optional<std::string> boundDriverName(const fs::path& device) {
    std::error_code ec;
    const fs::path link = device / "driver";
    if (!fs::is_symlink(link, ec)) return std::nullopt;
    const fs::path target = fs::read_symlink(link, ec);
    if (ec) return std::nullopt;
    return target.filename().string();
}

// Mechanism 1 (Phase 4, USB): the authorized attribute.
core::Result<std::optional<std::string>> viaAuthorized(const fs::path& authorized, bool enabled) {
    auto w = writeAttr(authorized, enabled ? "1" : "0");
    if (!w) return tl::unexpected(w.error());
    return std::optional<std::string>{};  // nullopt => authorized mechanism
}

// Mechanism 2 disable (spec §5.4): unbind the currently bound driver.
core::Result<std::optional<std::string>> disableViaUnbind(const fs::path& device,
                                                          const fs::path& bus) {
    auto driver = boundDriverName(device);
    if (!driver) return std::optional<std::string>{""};  // nothing bound: no-op disable
    auto w = writeAttr(bus / "drivers" / *driver / "unbind", device.filename().string());
    if (!w) return tl::unexpected(w.error());
    return driver;
}

// Mechanism 2 enable (spec §5.4): driver_override (when hinted and supported)
// -> drivers_probe -> ALWAYS clear the override, success or failure (never sticky).
core::Result<std::optional<std::string>> enableViaProbe(const fs::path& device, const fs::path& bus,
                                                        const std::string& rebindDriverHint) {
    std::error_code ec;
    const fs::path override = device / "driver_override";
    const bool useOverride = !rebindDriverHint.empty() && fs::exists(override, ec);
    if (useOverride) {
        auto w = writeAttr(override, rebindDriverHint);
        if (!w) return tl::unexpected(w.error());
    }
    auto probe = writeAttr(bus / "drivers_probe", device.filename().string());
    if (useOverride) {
        auto clear = writeAttr(override, "");  // scope-guard semantics
        if (probe && !clear) return tl::unexpected(clear.error());
    }
    if (!probe) return tl::unexpected(probe.error());
    return std::optional<std::string>{};
}
}  // namespace

SysfsDeviceController::SysfsDeviceController(std::string sysfsRoot)
    : sysfsRoot_(std::move(sysfsRoot)) {}

core::Result<fs::path> SysfsDeviceController::canonicalDevice(const std::string& sysfsPath) const {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(fs::path(sysfsPath), ec);
    if (ec) return core::makeError(core::Error::Code::NotFound, "cannot resolve " + sysfsPath);
    const fs::path root = fs::weakly_canonical(fs::path(sysfsRoot_), ec);
    if (ec || !isContained(canonical, root))
        return core::makeError(core::Error::Code::NotFound,
                               "path outside sysfs root: " + sysfsPath);
    if (!fs::is_directory(canonical, ec))
        return core::makeError(core::Error::Code::NotFound,
                               "device no longer present: " + sysfsPath);
    return canonical;
}

core::Result<std::optional<std::string>> SysfsDeviceController::setEnabled(
    const std::string& sysfsPath, bool enabled, const std::string& rebindDriverHint) {
    auto device = canonicalDevice(sysfsPath);
    if (!device) return tl::unexpected(device.error());
    std::error_code ec;

    const fs::path authorized = *device / "authorized";
    if (fs::exists(authorized, ec)) return viaAuthorized(authorized, enabled);

    auto bus = busDirFor(*device);
    if (!bus) return tl::unexpected(bus.error());

    if (!enabled) return disableViaUnbind(*device, *bus);
    return enableViaProbe(*device, *bus, rebindDriverHint);
}

core::Result<void> SysfsDeviceController::unbindDriver(const std::string& sysfsPath) {
    auto device = canonicalDevice(sysfsPath);
    if (!device) return tl::unexpected(device.error());
    const auto driver = boundDriverName(*device);
    if (!driver)
        return core::makeError(core::Error::Code::NotFound,
                               "no driver bound at " + device->string());
    auto bus = busDirFor(*device);
    if (!bus) return tl::unexpected(bus.error());
    return writeAttr(*bus / "drivers" / *driver / "unbind", device->filename().string());
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — IDeviceController signature
core::Result<void> SysfsDeviceController::bindDriver(const std::string& sysfsPath,
                                                     const std::string& driverName) {
    auto device = canonicalDevice(sysfsPath);
    if (!device) return tl::unexpected(device.error());
    auto bus = busDirFor(*device);
    if (!bus) return tl::unexpected(bus.error());
    std::error_code ec;
    const fs::path bindAttr = *bus / "drivers" / driverName / "bind";
    if (!fs::exists(bindAttr, ec))
        return core::makeError(core::Error::Code::NotFound,
                               "no such driver on this bus: " + driverName);
    return writeAttr(bindAttr, device->filename().string());
}

}  // namespace devmgr::platform_linux
