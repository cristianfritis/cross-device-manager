#include "devmgr/platform/linux/sysfs_device_controller.hpp"

#include <cerrno>
#include <filesystem>
#include <fstream>
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
}  // namespace

SysfsDeviceController::SysfsDeviceController(std::string sysfsRoot)
    : sysfsRoot_(std::move(sysfsRoot)) {}

core::Result<void> SysfsDeviceController::setEnabled(const std::string& sysfsPath, bool enabled) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(fs::path(sysfsPath), ec);
    if (ec) return core::makeError(core::Error::Code::NotFound, "cannot resolve " + sysfsPath);
    const fs::path root = fs::weakly_canonical(fs::path(sysfsRoot_), ec);
    if (ec || !isContained(canonical, root))
        return core::makeError(core::Error::Code::NotFound,
                               "path outside sysfs root: " + sysfsPath);
    if (!fs::is_directory(canonical, ec))
        return core::makeError(core::Error::Code::NotFound,
                               "device no longer present: " + sysfsPath);
    const fs::path attr = canonical / "authorized";
    if (!fs::exists(attr, ec))
        return core::makeError(
            core::Error::Code::Unsupported,
            "enable/disable not supported for this device (no authorized attribute)");
    std::ofstream out(attr);
    if (!out)
        return core::makeError(core::Error::Code::Io, "cannot open " + attr.string() + ": " +
                                                          std::generic_category().message(errno));
    out << (enabled ? '1' : '0');
    out.flush();
    if (!out) return core::makeError(core::Error::Code::Io, "write failed: " + attr.string());
    return {};
}

}  // namespace devmgr::platform_linux
