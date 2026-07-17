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
    core::Result<std::vector<core::SnapshotMeta>> snapshotList() override { return snapshotMetas; }
    core::Result<std::string> snapshotCreate(const std::string& label) override {
        snapshotCalls.push_back("create:" + label);
        return nextCreate;
    }
    core::Result<core::RestoreOutcome> snapshotRestore(const std::string& id) override {
        snapshotCalls.push_back("restore:" + id);
        return nextRestore;
    }
    core::Result<void> snapshotDelete(const std::string& id) override {
        snapshotCalls.push_back("delete:" + id);
        return next;
    }
    struct Call {
        std::string sysfsPath;
        bool enabled;
    };
    std::vector<Call> calls;
    std::vector<std::string> moduleCalls;
    std::vector<std::string> snapshotCalls;
    core::Result<void> next = {};
    core::Result<std::vector<core::DisabledDeviceEntry>> disabledEntries =
        std::vector<core::DisabledDeviceEntry>{};
    core::Result<std::vector<core::SnapshotMeta>> snapshotMetas = std::vector<core::SnapshotMeta>{};
    core::Result<std::string> nextCreate = std::string{"fake-snapshot-id"};
    core::Result<core::RestoreOutcome> nextRestore = core::RestoreOutcome{};
};

}  // namespace devmgr::test
