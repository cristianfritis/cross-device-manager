#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::test {

// In-memory PAL double for unit tests. Implements the read/control interfaces.
class FakePal final : public pal::IDeviceEnumerator,
                      public pal::IDeviceController,
                      public pal::IDriverManager,
                      public pal::ISystemInfo {
   public:
    void seedDevice(core::Device device) {
        enabled_[device.sysfsPath] = true;
        devices_.push_back(std::move(device));
    }
    void seedDriver(const std::string& sysfsPath, core::Driver driver) {
        drivers_[sysfsPath].push_back(std::move(driver));
    }
    void seedLoadedModule(core::LoadedModule m) { loaded_.push_back(std::move(m)); }
    bool enabled(const std::string& sysfsPath) const {
        const auto it = enabled_.find(sysfsPath);
        return it != enabled_.end() && it->second;
    }

    core::Result<std::vector<core::Device>> enumerate() override { return devices_; }

    core::Result<std::optional<std::string>> setEnabled(const std::string& sysfsPath, bool enabled,
                                                        const std::string& hint) override {
        setEnabledCalls.push_back({sysfsPath, enabled, hint});
        const auto it = enabled_.find(sysfsPath);
        if (it == enabled_.end())
            return core::makeError(core::Error::Code::NotFound, "no such device: " + sysfsPath);
        it->second = enabled;
        return unboundDriverResult;
    }
    core::Result<void> bindDriver(const std::string& sysfsPath,
                                  const std::string& driver) override {
        bindCalls.push_back({sysfsPath, driver});
        return nextVoid;
    }
    core::Result<void> unbindDriver(const std::string& sysfsPath) override {
        unbindCalls.push_back(sysfsPath);
        return nextVoid;
    }

    core::Result<std::vector<core::Driver>> driversFor(const core::Device& device) override {
        const auto it = drivers_.find(device.sysfsPath);
        if (it == drivers_.end()) return std::vector<core::Driver>{};
        return it->second;
    }
    core::Result<void> loadModule(const std::string& name) override {
        loadedModules.push_back(name);
        return nextVoid;
    }
    core::Result<void> unloadModule(const std::string& name) override {
        unloadedModules.push_back(name);
        return nextVoid;
    }
    core::Result<std::vector<core::LoadedModule>> listLoadedModules() override { return loaded_; }
    core::Result<core::Driver> moduleInfo(const std::string& name) override {
        for (const auto& [path, list] : drivers_)
            for (const auto& d : list)
                if (d.name == name) return d;
        return core::makeError(core::Error::Code::NotFound, "module not found: " + name);
    }
    core::Result<core::ModprobeInfo> modprobeInfo(const std::string&) override {
        return modprobeResult;
    }
    core::Result<std::vector<std::string>> devicesUsingModule(const std::string& name) override {
        const auto it = moduleDevices_.find(name);
        if (it == moduleDevices_.end()) return std::vector<std::string>{};
        return it->second;
    }
    void seedModuleDevices(const std::string& name, std::vector<std::string> paths) {
        moduleDevices_[name] = std::move(paths);
    }

    core::Result<Info> query() override { return info; }

    struct SetEnabledCall {
        std::string sysfsPath;
        bool enabled;
        std::string hint;
    };
    struct BindCall {
        std::string sysfsPath;
        std::string driver;
    };
    std::vector<SetEnabledCall> setEnabledCalls;
    std::vector<BindCall> bindCalls;
    std::vector<std::string> unbindCalls;
    std::vector<std::string> loadedModules;
    std::vector<std::string> unloadedModules;
    core::Result<void> nextVoid = {};
    core::Result<std::optional<std::string>> unboundDriverResult = std::optional<std::string>{};
    core::ModprobeInfo modprobeResult{};
    Info info{"fake-os 1.0", "6.1.0-fake", false, false, "none"};

   private:
    std::vector<core::Device> devices_;
    std::unordered_map<std::string, bool> enabled_;
    std::unordered_map<std::string, std::vector<core::Driver>> drivers_;
    std::unordered_map<std::string, std::vector<std::string>> moduleDevices_;
    std::vector<core::LoadedModule> loaded_;
};

}  // namespace devmgr::test
