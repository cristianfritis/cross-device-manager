#include "devmgr/platform/linux/kmod_driver_manager.hpp"

#include <libkmod.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <optional>
#include <utility>

namespace devmgr::platform_linux {
namespace fs = std::filesystem;

namespace {
// Kernel treats '-' and '_' as equivalent in module names.
std::string normalized(std::string name) {
    std::ranges::replace(name, '-', '_');
    return name;
}

// Parses the .ko's modinfo section (version/signer/sig_id) into `d`. Absent
// for builtins and for fixture indexes without real files — stay defaults.
void applyModinfo(kmod_module* mod, core::Driver& d) {
    kmod_list* info = nullptr;
    if (kmod_module_get_info(mod, &info) < 0) return;
    kmod_list* i = nullptr;
    kmod_list_foreach(i, info) {
        const char* key = kmod_module_info_get_key(i);
        const char* value = kmod_module_info_get_value(i);
        if (key == nullptr || value == nullptr) continue;
        if (std::strcmp(key, "version") == 0) d.version = value;
        if (std::strcmp(key, "signer") == 0) {
            d.isSigned = true;
            d.signer = value;
        }
        if (std::strcmp(key, "sig_id") == 0) d.isSigned = true;
    }
    kmod_module_info_free_list(info);
}

// Reads `options <module> ...` entries from modprobe.d config into `info`.
void applyOptionsConfig(kmod_ctx* ctx, const std::string& wanted, core::ModprobeInfo& info) {
    kmod_config_iter* it = kmod_config_get_options(ctx);
    if (it == nullptr) return;
    while (kmod_config_iter_next(it)) {
        const char* key = kmod_config_iter_get_key(it);
        const char* value = kmod_config_iter_get_value(it);
        if (key != nullptr && normalized(key) == wanted && value != nullptr) {
            info.options = info.options ? *info.options + " " + value : std::string(value);
        }
    }
    kmod_config_iter_free_iter(it);
}

// Reads `blacklist <module>` entries from modprobe.d config into `info`.
void applyBlacklistConfig(kmod_ctx* ctx, const std::string& wanted, core::ModprobeInfo& info) {
    kmod_config_iter* it = kmod_config_get_blacklists(ctx);
    if (it == nullptr) return;
    while (kmod_config_iter_next(it)) {
        const char* key = kmod_config_iter_get_key(it);
        if (key != nullptr && normalized(key) == wanted) info.blacklisted = true;
    }
    kmod_config_iter_free_iter(it);
}
}  // namespace

struct KmodDriverManager::Impl {
    Options options;
    kmod_ctx* ctx = nullptr;

    explicit Impl(Options o) : options(std::move(o)) {
        std::vector<const char*> paths;
        const char* const* configPaths = nullptr;
        if (options.configPaths) {
            for (const auto& p : *options.configPaths) paths.push_back(p.c_str());
            paths.push_back(nullptr);
            configPaths = paths.data();
        }
        ctx =
            kmod_new(options.moduleDir.empty() ? nullptr : options.moduleDir.c_str(), configPaths);
    }
    ~Impl() {
        if (ctx != nullptr) kmod_unref(ctx);
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    [[nodiscard]] core::Result<void> ready() const {
        if (ctx == nullptr)
            return core::makeError(core::Error::Code::Io, "libkmod context creation failed");
        return {};
    }

    // Fills a core::Driver from one kmod_module (does NOT take ownership).
    core::Driver toDriver(kmod_module* mod) const {
        core::Driver d;
        d.name = kmod_module_get_name(mod);
        const char* path = kmod_module_get_path(mod);
        d.path = path != nullptr ? path : "";
        const int state = kmod_module_get_initstate(mod);
        d.loaded = state == KMOD_MODULE_LIVE || state == KMOD_MODULE_COMING;
        d.kind = state == KMOD_MODULE_BUILTIN ? core::DriverKind::Builtin
                                              : core::DriverKind::KernelModule;
        kmod_list* deps = kmod_module_get_dependencies(mod);
        kmod_list* it = nullptr;
        kmod_list_foreach(it, deps) {
            kmod_module* dep = kmod_module_get_module(it);
            d.dependencies.emplace_back(kmod_module_get_name(dep));
            kmod_module_unref(dep);
        }
        kmod_module_unref_list(deps);
        applyModinfo(mod, d);
        return d;
    }

    core::Result<kmod_module*> byName(const std::string& name) {
        if (auto r = ready(); !r) return tl::unexpected(r.error());
        kmod_list* list = nullptr;
        const int err = kmod_module_new_from_lookup(ctx, name.c_str(), &list);
        if (err < 0 || list == nullptr)
            return core::makeError(core::Error::Code::NotFound, "module not found: " + name);
        kmod_module* mod = kmod_module_get_module(list);  // first match
        kmod_module_unref_list(list);
        return mod;  // caller unrefs
    }

    // Bound (or builtin) driver: <dev>/driver -> driver dir; driver/module -> module.
    std::optional<core::Driver> resolveBoundDriver(const std::string& sysfsPath) {
        std::error_code ec;
        const fs::path driverLink = fs::path(sysfsPath) / "driver";
        if (!fs::is_symlink(driverLink, ec)) return std::nullopt;
        const fs::path driverDir = fs::weakly_canonical(driverLink, ec);
        const std::string driverName = driverDir.filename().string();
        const fs::path moduleLink = driverDir / "module";
        if (fs::is_symlink(moduleLink, ec)) {
            const std::string moduleName = fs::read_symlink(moduleLink, ec).filename().string();
            auto mod = byName(moduleName);
            if (!mod) return std::nullopt;
            auto d = toDriver(*mod);
            kmod_module_unref(*mod);
            return d;
        }
        // Driver with no module: built into the kernel.
        core::Driver builtin;
        builtin.name = driverName;
        builtin.kind = core::DriverKind::Builtin;
        builtin.loaded = true;
        return builtin;
    }

    // Modalias candidates — the exact resolution modprobe performs. Appends to
    // `out`, skipping names already present (e.g. the bound driver).
    void appendModaliasCandidates(const std::string& modalias, std::vector<core::Driver>& out) {
        if (modalias.empty()) return;
        kmod_list* list = nullptr;
        if (kmod_module_new_from_lookup(ctx, modalias.c_str(), &list) < 0) return;
        kmod_list* it = nullptr;
        kmod_list_foreach(it, list) {
            kmod_module* mod = kmod_module_get_module(it);
            auto d = toDriver(mod);
            kmod_module_unref(mod);
            const bool dup = std::ranges::any_of(out, [&](const core::Driver& e) {
                return normalized(e.name) == normalized(d.name);
            });
            if (!dup) out.push_back(std::move(d));
        }
        kmod_module_unref_list(list);
    }
};

KmodDriverManager::KmodDriverManager(Options options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
KmodDriverManager::KmodDriverManager() : KmodDriverManager(Options{}) {}
KmodDriverManager::~KmodDriverManager() = default;

core::Result<std::vector<core::Driver>> KmodDriverManager::driversFor(const core::Device& device) {
    if (auto r = impl_->ready(); !r) return tl::unexpected(r.error());
    std::vector<core::Driver> out;
    // Bound (or builtin) driver first, then modalias candidates.
    if (auto bound = impl_->resolveBoundDriver(device.sysfsPath)) out.push_back(std::move(*bound));
    impl_->appendModaliasCandidates(device.modalias, out);
    return out;
}

core::Result<std::vector<core::LoadedModule>> KmodDriverManager::listLoadedModules() {
    if (auto r = impl_->ready(); !r) return tl::unexpected(r.error());
    kmod_list* list = nullptr;
    if (kmod_module_new_from_loaded(impl_->ctx, &list) < 0)
        return core::makeError(core::Error::Code::Io, "cannot read loaded modules");
    std::vector<core::LoadedModule> out;
    kmod_list* it = nullptr;
    kmod_list_foreach(it, list) {
        kmod_module* mod = kmod_module_get_module(it);
        core::LoadedModule m;
        m.name = kmod_module_get_name(mod);
        m.sizeBytes = static_cast<std::uint64_t>(kmod_module_get_size(mod));
        m.refCount = kmod_module_get_refcnt(mod);
        kmod_list* holders = kmod_module_get_holders(mod);
        kmod_list* h = nullptr;
        kmod_list_foreach(h, holders) {
            kmod_module* hm = kmod_module_get_module(h);
            m.holders.emplace_back(kmod_module_get_name(hm));
            kmod_module_unref(hm);
        }
        kmod_module_unref_list(holders);
        kmod_module_unref(mod);
        out.push_back(std::move(m));
    }
    kmod_module_unref_list(list);
    return out;
}

core::Result<core::Driver> KmodDriverManager::moduleInfo(const std::string& name) {
    auto mod = impl_->byName(name);
    if (!mod) return tl::unexpected(mod.error());
    auto d = impl_->toDriver(*mod);
    kmod_module_unref(*mod);
    return d;
}

core::Result<core::ModprobeInfo> KmodDriverManager::modprobeInfo(const std::string& name) {
    if (auto r = impl_->ready(); !r) return tl::unexpected(r.error());
    core::ModprobeInfo info;
    const std::string wanted = normalized(name);
    applyOptionsConfig(impl_->ctx, wanted, info);
    applyBlacklistConfig(impl_->ctx, wanted, info);
    return info;
}

core::Result<std::vector<std::string>> KmodDriverManager::devicesUsingModule(
    const std::string& name) {
    std::error_code ec;
    std::vector<std::string> out;
    const fs::path driversDir = fs::path(impl_->options.sysfsRoot) / "module" / name / "drivers";
    if (!fs::is_directory(driversDir, ec)) return out;  // not loaded / no drivers: empty
    for (const auto& drv : fs::directory_iterator(driversDir, ec)) {
        const fs::path driverDir = fs::weakly_canonical(drv.path(), ec);
        if (ec) continue;
        for (const auto& entry : fs::directory_iterator(driverDir, ec)) {
            if (!fs::is_symlink(entry.path(), ec)) continue;
            const fs::path target = fs::weakly_canonical(entry.path(), ec);
            if (ec) continue;
            // Device links point under .../devices/; skip module/bind/uevent etc.
            if (target.string().find("/devices/") == std::string::npos) continue;
            out.push_back(target.string());
        }
    }
    return out;
}

// loadModule / unloadModule arrive in Task 6.
core::Result<void> KmodDriverManager::loadModule(const std::string&) {
    return core::makeError(core::Error::Code::Unsupported, "loadModule arrives in Task 6");
}
core::Result<void> KmodDriverManager::unloadModule(const std::string&) {
    return core::makeError(core::Error::Code::Unsupported, "unloadModule arrives in Task 6");
}

}  // namespace devmgr::platform_linux
