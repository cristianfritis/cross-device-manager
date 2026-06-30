#include "devmgr/app/device_service.hpp"

#include <unordered_map>
#include <utility>

#include "devmgr/core/events.hpp"

namespace devmgr::app {

void DeviceService::applyEnumeration(std::vector<core::Device> snapshot) {
    std::vector<core::Device> added;
    std::vector<core::Device> changed;
    std::vector<core::DeviceId> removed;

    {
        std::scoped_lock lock(mutex_);
        std::unordered_map<std::string, core::Device> next;
        next.reserve(snapshot.size());
        for (auto& d : snapshot) {
            const std::string key = d.id.value;
            auto prev = model_.find(key);
            if (prev == model_.end()) {
                added.push_back(d);
            } else if (!(prev->second == d)) {
                changed.push_back(d);
            }
            next.emplace(key, std::move(d));
        }
        for (const auto& [key, dev] : model_) {
            if (next.find(key) == next.end()) removed.push_back(dev.id);
        }
        model_.swap(next);
    }

    // Publish outside the lock: EventBus invokes handlers synchronously and a
    // handler may call back into devices()/findById().
    for (const auto& id : removed) bus_.publish(core::DeviceRemovedEvent{id});
    for (auto& d : added) bus_.publish(core::DeviceAddedEvent{std::move(d)});
    for (auto& d : changed) bus_.publish(core::DeviceChangedEvent{std::move(d)});
}

std::vector<core::Device> DeviceService::devices() const {
    std::scoped_lock lock(mutex_);
    std::vector<core::Device> out;
    out.reserve(model_.size());
    for (const auto& [key, dev] : model_) out.push_back(dev);
    return out;
}

std::optional<core::Device> DeviceService::findById(const core::DeviceId& id) const {
    std::scoped_lock lock(mutex_);
    auto it = model_.find(id.value);
    if (it == model_.end()) return std::nullopt;
    return it->second;
}

}  // namespace devmgr::app
