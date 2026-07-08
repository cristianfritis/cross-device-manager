#pragma once
#include <mutex>

#include "devmgr/daemon/state_store.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::daemon {

// Active enforcement (spec §5.3): re-applies persisted desired state at
// startup (sweep) and on device reappearance (onHotplug). Guard is re-checked
// on EVERY re-apply — refusal marks the entry guardSuspended instead of
// enforcing. All failures log-and-continue; this class never throws.
class EnforcementService {
   public:
    EnforcementService(pal::IDeviceEnumerator& enumerator, pal::IDeviceController& controller,
                       pal::ICriticalityProber& prober, StateStore& store, std::mutex& applyMutex);
    void sweep();
    void onHotplug(const pal::HotplugEvent& event);  // monitor callback thread

   private:
    void maybeReapply(const core::DisabledDeviceEntry& entry, const core::Device& device);

    pal::IDeviceEnumerator& enumerator_;
    pal::IDeviceController& controller_;
    pal::ICriticalityProber& prober_;
    StateStore& store_;
    std::mutex& applyMutex_;
};

}  // namespace devmgr::daemon
