#pragma once
#include <functional>
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
    // Identity is the device's canonical sysfs path — the wire format devmgrd
    // receives and the coordinate SysfsDeviceController acts on. Phase 4
    // mechanism: USB `authorized` only (non-USB → Error::Unsupported).
    virtual core::Result<void> setEnabled(const std::string& sysfsPath, bool enabled) = 0;
};

class IDriverManager {
   public:
    virtual ~IDriverManager() = default;
    virtual core::Result<std::vector<core::Driver>> driversFor(const core::DeviceId& id) = 0;
    virtual core::Result<void> loadModule(const std::string& name) = 0;
    virtual core::Result<void> unloadModule(const std::string& name) = 0;
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
};

}  // namespace devmgr::pal
