#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// libkmod-backed IDriverManager (spec §7.1). Reads are unprivileged; the write
// methods (Task 6) need CAP_SYS_MODULE — daemon-only callers. libkmod stays
// quarantined in the .cpp (libudev pattern).
class KmodDriverManager final : public pal::IDriverManager {
   public:
    struct Options {
        std::string sysfsRoot = "/sys";
        std::string moduleDir;  // "" => kernel default (/lib/modules/`uname -r`)
        std::optional<std::vector<std::string>> configPaths;  // nullopt => system modprobe.d
        std::string securityDir = "/sys/kernel/security";     // lockdown, for load errors
    };
    KmodDriverManager();  // defaults to Options{}
    explicit KmodDriverManager(Options options);
    ~KmodDriverManager() override;
    KmodDriverManager(const KmodDriverManager&) = delete;
    KmodDriverManager& operator=(const KmodDriverManager&) = delete;
    KmodDriverManager(KmodDriverManager&&) noexcept = default;             // declared dtor would
    KmodDriverManager& operator=(KmodDriverManager&&) noexcept = default;  // suppress these

    // First element = the bound (or builtin) driver's module when resolvable;
    // the rest are modalias candidates (spec §7.1 / bind-dropdown data).
    core::Result<std::vector<core::Driver>> driversFor(const core::Device& device) override;
    core::Result<void> loadModule(const std::string& name) override;    // Task 6
    core::Result<void> unloadModule(const std::string& name) override;  // Task 6
    core::Result<std::vector<core::LoadedModule>> listLoadedModules() override;
    core::Result<core::Driver> moduleInfo(const std::string& name) override;
    core::Result<core::ModprobeInfo> modprobeInfo(const std::string& name) override;
    core::Result<std::vector<std::string>> devicesUsingModule(const std::string& name) override;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace devmgr::platform_linux
