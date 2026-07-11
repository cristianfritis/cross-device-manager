#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/core/models.hpp"
#include "devmgr/core/result.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/runtime/progress.hpp"

namespace devmgr::pal {

class IDeviceEnumerator {
   public:
    virtual ~IDeviceEnumerator() = default;
    virtual core::Result<std::vector<core::Device>> enumerate() = 0;
};

class IHotplugMonitor {
   public:
    using Callback = std::function<void(const HotplugEvent&)>;
    virtual ~IHotplugMonitor() = default;
    virtual core::Result<void> start(Callback callback) = 0;
    // Implementations must block until the reader thread is joined and must
    // guarantee no further callbacks fire after stop() returns —
    // HotplugService::stop()'s correctness depends on this.
    virtual void stop() = 0;
};

class IDeviceController {
   public:
    virtual ~IDeviceController() = default;
    // Identity is the device's canonical sysfs path. Phase 5 mechanisms:
    //  - `authorized` attr present (USB): write 0/1. Returns nullopt.
    //  - otherwise: disable = unbind current driver (returns its name, ""
    //    if none was bound — the value signals mechanism "unbind");
    //    enable = write rebindDriverHint to driver_override (when non-empty
    //    and the attr exists), then bus drivers_probe; override cleared even
    //    on failure. Returns nullopt on enables and authorized-mechanism ops.
    virtual core::Result<std::optional<std::string>> setEnabled(
        const std::string& sysfsPath, bool enabled, const std::string& rebindDriverHint) = 0;
    // Surgical verbs (never persisted by callers).
    virtual core::Result<void> bindDriver(const std::string& sysfsPath,
                                          const std::string& driverName) = 0;
    virtual core::Result<void> unbindDriver(const std::string& sysfsPath) = 0;
};

class IDriverManager {
   public:
    virtual ~IDriverManager() = default;
    // Takes the full Device: modalias lookup + driver-symlink resolution need
    // modalias and sysfsPath (spec §4.2 refinement; same rationale as
    // IPrivilegedChannel taking Device). Returns the modalias candidate list
    // ordered per the pinned contract (kmod_driver_manager.hpp): the FIRST
    // element is the currently-bound (or builtin) driver when one exists —
    // both frontends' bind-prefill depends on that ordering.
    virtual core::Result<std::vector<core::Driver>> driversFor(const core::Device& device) = 0;
    virtual core::Result<void> loadModule(const std::string& name) = 0;
    virtual core::Result<void> unloadModule(const std::string& name) = 0;
    virtual core::Result<std::vector<core::LoadedModule>> listLoadedModules() = 0;
    virtual core::Result<core::Driver> moduleInfo(const std::string& name) = 0;
    virtual core::Result<core::ModprobeInfo> modprobeInfo(const std::string& name) = 0;
    // Canonical sysfs device paths bound via any of the module's drivers.
    virtual core::Result<std::vector<std::string>> devicesUsingModule(const std::string& name) = 0;
};

class IUpdateProvider {
   public:
    virtual ~IUpdateProvider() = default;
    virtual core::Result<std::vector<core::Driver>> checkUpdates() = 0;
    virtual core::Result<void> applyUpdate(const std::string& id,
                                           runtime::ProgressReporter reporter) = 0;
};

class ISystemInfo {
   public:
    struct Info {
        std::string osVersion;
        std::string kernelVersion;
        bool secureBoot = false;
        bool rebootPending = false;
        std::string lockdownMode = "none";
    };
    virtual ~ISystemInfo() = default;
    virtual core::Result<Info> query() = 0;
};

class IPrivilegedChannel {
   public:
    virtual ~IPrivilegedChannel() = default;
    // Takes the full Device (the channel needs sysfsPath on the wire and name
    // for messages). Blocking: interactive polkit auth may take ~minutes —
    // call from a TaskScheduler worker, never a UI thread.
    virtual core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) = 0;
    virtual core::Result<void> loadModule(const std::string& name) = 0;
    virtual core::Result<void> unloadModule(const std::string& name) = 0;
    virtual core::Result<void> bindDriver(const core::Device& device,
                                          const std::string& driverName) = 0;
    virtual core::Result<void> unbindDriver(const core::Device& device) = 0;
    virtual core::Result<std::vector<core::DisabledDeviceEntry>> listDisabledDevices() = 0;
};

}  // namespace devmgr::pal
