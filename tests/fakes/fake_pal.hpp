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
    void seedDriver(const core::DeviceId& id, core::Driver driver) {
        drivers_[id.value].push_back(std::move(driver));
    }
    bool enabled(const std::string& sysfsPath) const {
        const auto it = enabled_.find(sysfsPath);
        return it != enabled_.end() && it->second;
    }

    core::Result<std::vector<core::Device>> enumerate() override { return devices_; }

    core::Result<void> setEnabled(const std::string& sysfsPath, bool enabled) override {
        const auto it = enabled_.find(sysfsPath);
        if (it == enabled_.end())
            return core::makeError(core::Error::Code::NotFound, "no such device: " + sysfsPath);
        it->second = enabled;
        return {};
    }

    core::Result<std::vector<core::Driver>> driversFor(const core::DeviceId& id) override {
        const auto it = drivers_.find(id.value);
        if (it == drivers_.end()) return std::vector<core::Driver>{};
        return it->second;
    }
    core::Result<void> loadModule(const std::string&) override { return {}; }
    core::Result<void> unloadModule(const std::string&) override { return {}; }

    core::Result<Info> query() override { return Info{"fake-os 1.0", "6.1.0-fake", false, false}; }

   private:
    std::vector<core::Device> devices_;
    std::unordered_map<std::string, bool> enabled_;
    std::unordered_map<std::string, std::vector<core::Driver>> drivers_;
};

}  // namespace devmgr::test
