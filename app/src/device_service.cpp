#include "devmgr/app/device_service.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <utility>

#include "devmgr/core/events.hpp"

namespace devmgr::app {
namespace {

// A removal event's DeviceId is not trustworthy: real udev strips the
// ID_VENDOR_ID / ID_MODEL_ID / ID_SERIAL_SHORT properties from a `remove`
// uevent, so the hotplug monitor derives a different id than the one recorded
// at add time. The platform-native id (a sysfs path on Linux, a device
// instance id on Windows) is the one field that is present and identical on
// both events, so a removal that misses on id falls back to matching it.
auto findForRemoval(std::unordered_map<std::string, core::Device>& model,
                    const core::Device& event) {
    auto it = model.find(event.id.value);
    if (it != model.end() || event.nativeId.empty()) return it;
    return std::ranges::find_if(
        model, [&](const auto& entry) { return entry.second.nativeId == event.nativeId; });
}

}  // namespace

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

void DeviceService::applyDelta(const pal::HotplugEvent& event) {
    std::optional<core::DeviceAddedEvent> added;
    std::optional<core::DeviceChangedEvent> changed;
    std::optional<core::DeviceRemovedEvent> removed;

    {
        std::scoped_lock lock(mutex_);
        const std::string key = event.device.id.value;
        if (event.action == pal::HotplugEvent::Action::Removed) {
            if (auto it = findForRemoval(model_, event.device); it != model_.end()) {
                removed = core::DeviceRemovedEvent{it->second.id};
                model_.erase(it);
            }
        } else {  // Added or Changed — reconcile against the live model
            auto it = model_.find(key);
            if (it == model_.end()) {
                model_.emplace(key, event.device);
                added = core::DeviceAddedEvent{event.device};
            } else if (!(it->second == event.device)) {
                it->second = event.device;
                changed = core::DeviceChangedEvent{event.device};
            }
        }
    }

    // Publish outside the lock (same discipline as applyEnumeration).
    if (removed) bus_.publish(*removed);
    if (added) bus_.publish(*added);
    if (changed) bus_.publish(*changed);
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
