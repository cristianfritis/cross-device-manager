#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "devmgr/core/models.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/runtime/event_bus.hpp"

namespace devmgr::app {

// Owns the in-memory device model and turns enumeration snapshots into
// EventBus deltas. Thread-safe; events are published WITHOUT holding the mutex.
class DeviceService {
   public:
    explicit DeviceService(runtime::EventBus& bus) : bus_(bus) {}

    void applyEnumeration(std::vector<core::Device> snapshot);
    void applyDelta(const pal::HotplugEvent& event);
    std::vector<core::Device> devices() const;
    std::optional<core::Device> findById(const core::DeviceId& id) const;

   private:
    runtime::EventBus& bus_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, core::Device> model_;  // keyed by DeviceId.value
};

}  // namespace devmgr::app
