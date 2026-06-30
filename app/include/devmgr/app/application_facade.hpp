#pragma once
#include <future>
#include <optional>
#include <vector>

#include "devmgr/app/device_service.hpp"
#include "devmgr/core/models.hpp"
#include "devmgr/pal/interfaces.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"

namespace devmgr::app {

// The single command/read surface the frontends use. refresh() runs enumeration
// on the TaskScheduler so the UI thread never blocks on I/O.
class ApplicationFacade {
   public:
    ApplicationFacade(pal::IDeviceEnumerator& enumerator, runtime::TaskScheduler& scheduler,
                      runtime::EventBus& bus, DeviceService& service)
        : enumerator_(enumerator), scheduler_(scheduler), bus_(bus), service_(service) {}

    // Runs enumeration on the TaskScheduler. The caller MUST wait on (or get)
    // the returned future before destroying this facade — the worker task
    // captures `this`, so discarding the future and destroying the facade would
    // dereference a dangling pointer.
    std::future<void> refresh();
    std::vector<core::Device> devices() const { return service_.devices(); }
    std::optional<core::Device> findById(const core::DeviceId& id) const {
        return service_.findById(id);
    }

   private:
    pal::IDeviceEnumerator& enumerator_;
    runtime::TaskScheduler& scheduler_;
    runtime::EventBus& bus_;
    DeviceService& service_;
};

}  // namespace devmgr::app
