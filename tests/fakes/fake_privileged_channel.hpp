#pragma once
#include <string>
#include <vector>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::test {

// Records channel calls; `next` scripts the outcome.
class FakePrivilegedChannel final : public pal::IPrivilegedChannel {
   public:
    core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) override {
        calls.push_back({device.sysfsPath, enabled});
        return next;
    }
    core::Result<void> loadModule(const std::string& name) override {
        moduleCalls.push_back("load:" + name);
        return next;
    }
    core::Result<void> unloadModule(const std::string& name) override {
        moduleCalls.push_back("unload:" + name);
        return next;
    }
    core::Result<void> bindDriver(const core::Device& device, const std::string& driver) override {
        moduleCalls.push_back("bind:" + device.sysfsPath + ":" + driver);
        return next;
    }
    core::Result<void> unbindDriver(const core::Device& device) override {
        moduleCalls.push_back("unbind:" + device.sysfsPath);
        return next;
    }
    core::Result<std::vector<core::DisabledDeviceEntry>> listDisabledDevices() override {
        return disabledEntries;
    }
    struct Call {
        std::string sysfsPath;
        bool enabled;
    };
    std::vector<Call> calls;
    std::vector<std::string> moduleCalls;
    core::Result<void> next = {};
    core::Result<std::vector<core::DisabledDeviceEntry>> disabledEntries =
        std::vector<core::DisabledDeviceEntry>{};
};

}  // namespace devmgr::test
