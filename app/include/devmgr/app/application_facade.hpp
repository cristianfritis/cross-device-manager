#pragma once
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/app/device_service.hpp"
#include "devmgr/core/models.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/interfaces.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "devmgr/services/critical_device_guard.hpp"

namespace devmgr::app {

// The single command/read surface the frontends use. refresh() runs enumeration
// on the TaskScheduler so the UI thread never blocks on I/O.
class ApplicationFacade {
   public:
    // channel/prober/drivers/systemInfo are optional (null in sdbus-less or
    // kmod-less builds): without a channel setDeviceEnabled reports
    // Unsupported; without a prober canDisable/canUnloadModule are
    // advisory-unavailable and answer "allowed" (devmgrd remains
    // authoritative); without drivers/systemInfo the read methods degrade to
    // {} / nullopt / Unsupported.
    ApplicationFacade(pal::IDeviceEnumerator& enumerator, runtime::TaskScheduler& scheduler,
                      runtime::EventBus& bus, DeviceService& service,
                      pal::IPrivilegedChannel* channel = nullptr,
                      pal::ICriticalityProber* prober = nullptr,
                      pal::IDriverManager* drivers = nullptr,
                      pal::ISystemInfo* systemInfo = nullptr)
        : enumerator_(enumerator),
          scheduler_(scheduler),
          bus_(bus),
          service_(service),
          channel_(channel),
          prober_(prober),
          drivers_(drivers),
          systemInfo_(systemInfo) {}

    // Runs enumeration on the TaskScheduler. The caller MUST wait on (or get)
    // the returned future before destroying this facade — the worker task
    // captures `this`, so discarding the future and destroying the facade would
    // dereference a dangling pointer.
    std::future<void> refresh();

    // Phase 4 mutation: resolves the device, calls the privileged channel on a
    // worker, publishes exactly ONE TaskCompletedEvent{taskId =
    // "set-enabled:" + id.value} — success and every failure mode alike.
    // Same future-custody contract as refresh(). The channel call may block
    // for ~minutes on interactive polkit auth; never wait on the UI thread.
    std::future<void> setDeviceEnabled(const core::DeviceId& id, bool enabled);

    // Advisory guard for UX (grey-out/annotate): pure core policy over
    // freshly probed facts. devmgrd re-checks authoritatively on every
    // request — this result is never a substitute for that.
    services::GuardVerdict canDisable(const core::DeviceId& id) const;

    std::vector<core::Device> devices() const { return service_.devices(); }
    std::optional<core::Device> findById(const core::DeviceId& id) const {
        return service_.findById(id);
    }

    // Reads (sync; {} / advisory-allowed degradation when seams are null).
    std::vector<core::Driver> driverInfo(const core::DeviceId& id) const;
    core::Result<std::vector<core::LoadedModule>> listModules() const;
    core::Result<core::Driver> moduleDetail(const std::string& name) const;
    core::Result<core::ModprobeInfo> modprobeDetail(const std::string& name) const;
    std::optional<pal::ISystemInfo::Info> systemInfo() const;
    services::GuardVerdict canUnloadModule(const std::string& name) const;
    // Mutations (async, Phase 4 pattern): ONE TaskCompletedEvent each, taskId
    // prefixes "load-module:", "unload-module:", "bind-driver:", "unbind-driver:".
    // Module mutations ALSO publish ModulesChangedEvent on success.
    std::future<void> loadModule(const std::string& name);
    std::future<void> unloadModule(const std::string& name);
    std::future<void> bindDriver(const core::DeviceId& id, const std::string& driverName);
    std::future<void> unbindDriver(const core::DeviceId& id);

   private:
    std::future<void> runChannelTask(
        std::string taskId, std::string okMessage, bool modulesChanged,
        std::function<core::Result<void>(pal::IPrivilegedChannel&)> call);

    pal::IDeviceEnumerator& enumerator_;
    runtime::TaskScheduler& scheduler_;
    runtime::EventBus& bus_;
    DeviceService& service_;
    pal::IPrivilegedChannel* channel_ = nullptr;
    pal::ICriticalityProber* prober_ = nullptr;
    pal::IDriverManager* drivers_ = nullptr;
    pal::ISystemInfo* systemInfo_ = nullptr;
};

}  // namespace devmgr::app
