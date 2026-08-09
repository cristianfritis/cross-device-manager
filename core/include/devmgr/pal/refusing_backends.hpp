#pragma once
#include <optional>
#include <string>
#include <vector>

#include "devmgr/core/models.hpp"
#include "devmgr/core/result.hpp"
#include "devmgr/core/snapshot_diff.hpp"
#include "devmgr/core/snapshot_models.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::pal {

// Null objects for the PAL: one per interface, every method returning
// Error{Code::Unsupported} with an empty message, doing no work, touching no
// operating-system facility, and never throwing.
//
// The empty message is deliberate. core::Error::Code::Unsupported already
// resolves to the shared `unsupported` unavailability sentence through
// app::BackendStatusVM, and a message authored here would be a second, unshared
// wording for the same state — exactly what the wording table exists to prevent.
//
// A platform's factory installs these for the interfaces it does not implement,
// so BackendSet's references are always valid. Code that ACTS calls through and
// gets Unsupported; code that PRESENTS reads PlatformCapabilities instead, so a
// verb is never offered just to be refused.
//
// Each accessor returns a shared, immortal instance (a function-local static).
// Identity is meaningful: `&set.enumerator == &refusingDeviceEnumerator()` is
// how a caller — and the capability/backend agreement test — can tell a refusing
// backend from a real one without calling it.

class RefusingDeviceEnumerator final : public IDeviceEnumerator {
   public:
    core::Result<std::vector<core::Device>> enumerate() override;
};

class RefusingHotplugMonitor final : public IHotplugMonitor {
   public:
    core::Result<void> start(Callback callback) override;
    // Satisfies the interface's shutdown contract vacuously: no callback was
    // ever registered, so there is nothing to join and nothing can fire.
    void stop() override;
};

class RefusingDeviceController final : public IDeviceController {
   public:
    core::Result<std::optional<std::string>> setEnabled(
        const std::string& nativeId, bool enabled, const std::string& rebindDriverHint) override;
    core::Result<void> bindDriver(const std::string& nativeId,
                                  const std::string& driverName) override;
    core::Result<void> unbindDriver(const std::string& nativeId) override;
};

class RefusingDriverManager final : public IDriverManager {
   public:
    core::Result<std::vector<core::Driver>> driversFor(const core::Device& device) override;
    core::Result<void> loadModule(const std::string& name) override;
    core::Result<void> unloadModule(const std::string& name) override;
    core::Result<std::vector<core::LoadedModule>> listLoadedModules() override;
    core::Result<core::Driver> moduleInfo(const std::string& name) override;
    core::Result<core::ModprobeInfo> modprobeInfo(const std::string& name) override;
    core::Result<std::vector<std::string>> devicesUsingModule(const std::string& name) override;
};

class RefusingSystemInfo final : public ISystemInfo {
   public:
    core::Result<Info> query() override;
};

class RefusingCriticalityProber final : public ICriticalityProber {
   public:
    core::Result<CriticalityFacts> probe() override;
};

class RefusingPrivilegedChannel final : public IPrivilegedChannel {
   public:
    core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) override;
    core::Result<void> loadModule(const std::string& name) override;
    core::Result<void> unloadModule(const std::string& name) override;
    core::Result<void> bindDriver(const core::Device& device,
                                  const std::string& driverName) override;
    core::Result<void> unbindDriver(const core::Device& device) override;
    core::Result<std::vector<core::DisabledDeviceEntry>> listDisabledDevices() override;
    core::Result<std::vector<core::SnapshotMeta>> snapshotList() override;
    core::Result<std::string> snapshotCreate(const std::string& label) override;
    core::Result<core::RestoreOutcome> snapshotRestore(const std::string& id) override;
    core::Result<void> snapshotDelete(const std::string& id) override;
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — mirrors the interface
    core::Result<core::SnapshotDiff> snapshotDiff(const std::string& baseId,
                                                  const std::string& targetId) override;
};

IDeviceEnumerator& refusingDeviceEnumerator();
IHotplugMonitor& refusingHotplugMonitor();
IDeviceController& refusingDeviceController();
IDriverManager& refusingDriverManager();
IPrivilegedChannel& refusingPrivilegedChannel();
ISystemInfo& refusingSystemInfo();
ICriticalityProber& refusingCriticalityProber();

}  // namespace devmgr::pal
