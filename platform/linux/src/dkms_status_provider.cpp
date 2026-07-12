#include "devmgr/platform/linux/dkms_status_provider.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace devmgr::platform_linux {
namespace fs = std::filesystem;
namespace {

constexpr std::array<const char*, 4> kKoExts = {".ko", ".ko.xz", ".ko.gz", ".ko.zst"};

bool isModuleFile(const fs::path& p) {
    const auto name = p.filename().string();
    return std::ranges::any_of(kKoExts, [&](const char* e) {
        const std::string_view ext{e};
        return name.size() > ext.size() && std::string_view(name).ends_with(ext);
    });
}

// lstat-first entry guards (spec V2 "never throw"; §6 "⊥ follow links outside
// configured roots"): is_symlink() MUST be checked before any type observer —
// is_directory()/is_regular_file() on a symlink entry stat the TARGET, which
// both follows the link and, in the plain (no error_code) overloads, throws
// filesystem_error when the target stat fails (EACCES, ELOOP). Only the
// error_code overloads are used; any error ⇒ the entry is skipped.
bool isRealDir(const fs::directory_entry& e) {
    std::error_code ec;
    if (e.is_symlink(ec) || ec) return false;
    return e.is_directory(ec) && !ec;
}

bool isRealFile(const fs::directory_entry& e) {
    std::error_code ec;
    if (e.is_symlink(ec) || ec) return false;
    return e.is_regular_file(ec) && !ec;
}

// One arch-level directory (<kernelDir>/<arch>/module/*.ko*), dirs only, no
// symlink dirs — fixed depth, no recursive walk (spec §6 "no recursive
// follow"). Appends every recognized module-output basename found.
void collectModuleBasenames(const fs::path& archDir, std::vector<std::string>& basenames) {
    const auto moduleDir = archDir / "module";
    std::error_code ec;
    if (!fs::is_directory(moduleDir, ec) || ec) return;
    for (const auto& f : fs::directory_iterator(moduleDir, ec)) {
        if (ec || !isRealFile(f)) continue;
        if (!isModuleFile(f.path())) continue;
        basenames.push_back(f.path().filename().string());
    }
}

// Scans every arch subdirectory of kernelDir (dirs only, no symlink dirs) for
// module output; returns the union of build-output basenames found.
std::vector<std::string> moduleOutputBasenames(const fs::path& kernelDir) {
    std::vector<std::string> basenames;
    std::error_code ec;
    for (const auto& archDir : fs::directory_iterator(kernelDir, ec)) {
        if (ec || !isRealDir(archDir)) continue;
        collectModuleBasenames(archDir.path(), basenames);
    }
    return basenames;
}

bool anyBasenameInstalled(const fs::path& installedDir, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        std::error_code ec;
        if (fs::exists(installedDir / name, ec) && !ec) return true;
    }
    return false;
}

// kernelState(kernelDir, kernel, modulesRoot):
//   scan <kernelDir>/<arch>/module/*.ko* (one arch level, dirs only, no symlink dirs)
//   no module output          -> "unknown - build residue or unsupported layout"
//   kernel not in modulesRoot -> "<state> - kernel absent" (state still built/...)
//   basename in <modulesRoot>/<kernel>/updates/dkms/ -> "built + installed"
//   else                      -> "built" (spec §6: weak-modules/distro-specific
//     install paths are out of scope -> tri-state only, never claim "not
//     installed" — evidence of absence in updates/dkms/ is not proof of absence)
std::string kernelState(const fs::path& kernelDir, const std::string& kernel,
                        const fs::path& modulesRoot) {
    const auto basenames = moduleOutputBasenames(kernelDir);
    if (basenames.empty()) return "unknown — build residue or unsupported layout";

    std::error_code ec;
    const bool kernelPresent = fs::is_directory(modulesRoot / kernel, ec) && !ec;
    const bool installed =
        anyBasenameInstalled(modulesRoot / kernel / "updates" / "dkms", basenames);
    std::string state = installed ? "built + installed" : "built";
    if (!kernelPresent) state += " — kernel absent";
    return state;
}

bool isVersionMetaSubdir(const std::string& name) {
    return name == "source" || name == "build";
}

// Populates c.details, one entry per kernel dir under verPath; returns
// whether any kernel dir was found (else the version was only registered).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — version dir vs modules root
bool collectKernelDetails(const fs::path& verPath, const fs::path& modulesRoot,
                          core::UpdateCandidate& c) {
    bool sawKernelDir = false;
    std::error_code ec;
    for (const auto& kDir : fs::directory_iterator(verPath, ec)) {
        if (ec || !isRealDir(kDir)) continue;
        const auto kernel = kDir.path().filename().string();
        if (isVersionMetaSubdir(kernel)) continue;
        sawKernelDir = true;
        c.details.emplace_back(kernel, kernelState(kDir.path(), kernel, modulesRoot));
    }
    return sawKernelDir;
}

core::UpdateCandidate makeCandidate(const std::string& module, const std::string& version) {
    core::UpdateCandidate c;
    c.providerId = "dkms";
    c.id = "dkms:" + module + "/" + version;
    c.displayName = module;
    c.currentVersion = version;
    return c;
}

// One module/version pair -> one candidate, appended to out.
void collectVersions(const fs::path& modPath, const std::string& module,
                     const fs::path& modulesRoot, std::vector<core::UpdateCandidate>& out) {
    std::error_code ec;
    for (const auto& verDir : fs::directory_iterator(modPath, ec)) {
        if (ec || !isRealDir(verDir)) continue;
        const auto version = verDir.path().filename().string();
        core::UpdateCandidate c = makeCandidate(module, version);
        if (!collectKernelDetails(verDir.path(), modulesRoot, c)) {
            c.details.emplace_back("(no kernel)", "added — not built");
        }
        out.push_back(std::move(c));
    }
}

}  // namespace

DkmsStatusProvider::DkmsStatusProvider(std::string dkmsRoot, std::string modulesRoot)
    : dkmsRoot_(std::move(dkmsRoot)), modulesRoot_(std::move(modulesRoot)) {}

std::string DkmsStatusProvider::providerId() const {
    return "dkms";
}

pal::UpdateProviderCaps DkmsStatusProvider::capabilities() const {
    return pal::UpdateProviderCaps::Query;
}

core::ProviderAvailability DkmsStatusProvider::availability() const {
    std::error_code ec;
    const bool available = fs::is_directory(dkmsRoot_, ec) && !ec;
    core::ProviderAvailability a;
    a.available = available;
    if (!available) {
        a.error = core::Error{.code = core::Error::Code::NotFound,
                              .message = "no /var/lib/dkms — DKMS not present"};
    }
    return a;
}

core::Result<std::vector<core::UpdateCandidate>> DkmsStatusProvider::enumerate() {
    std::vector<core::UpdateCandidate> out;
    std::error_code ec;
    for (const auto& modDir : fs::directory_iterator(dkmsRoot_, ec)) {
        if (ec || !isRealDir(modDir)) continue;
        const auto module = modDir.path().filename().string();
        collectVersions(modDir.path(), module, modulesRoot_, out);
    }
    return out;
}

core::Result<std::vector<core::PendingAction>> DkmsStatusProvider::pendingActions() {
    return std::vector<core::PendingAction>{};
}

core::Result<core::InstallOutcome> DkmsStatusProvider::install(
    const std::string& /*candidateId*/, const core::ReleaseRef& /*release*/,
    runtime::ProgressReporter /*progress*/) {
    return core::makeError(core::Error::Code::Unsupported, "dkms provider is status-only");
}

}  // namespace devmgr::platform_linux
