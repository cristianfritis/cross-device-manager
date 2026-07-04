#include "devmgr/app/application_facade.hpp"

#include <utility>

#include "devmgr/core/events.hpp"

namespace devmgr::app {

std::future<void> ApplicationFacade::refresh() {
    return scheduler_.submit([this] {
        auto result = enumerator_.enumerate();
        if (result) {
            service_.applyEnumeration(std::move(*result));
        } else {
            bus_.publish(
                core::ErrorEvent{.source = "enumerate", .message = result.error().message});
        }
    });
}

std::future<void> ApplicationFacade::setDeviceEnabled(const core::DeviceId& id, bool enabled) {
    return scheduler_.submit([this, id, enabled] {
        const std::string taskId = "set-enabled:" + id.value;
        const auto device = service_.findById(id);  // resolve at execution time
        if (!device) {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = taskId, .ok = false, .message = "device no longer present"});
            return;
        }
        if (channel_ == nullptr) {
            bus_.publish(
                core::TaskCompletedEvent{.taskId = taskId,
                                         .ok = false,
                                         .message = "built without privileged-helper support"});
            return;
        }
        auto result = channel_->setDeviceEnabled(*device, enabled);
        if (result) {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = taskId,
                .ok = true,
                .message = (enabled ? "Enabled " : "Disabled ") + device->name});
        } else {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = taskId, .ok = false, .message = result.error().message});
        }
    });
}

services::GuardVerdict ApplicationFacade::canDisable(const core::DeviceId& id) const {
    if (prober_ == nullptr) return {};
    const auto device = service_.findById(id);
    if (!device) return {};
    auto facts = prober_->probe();
    if (!facts) return {};  // advisory unavailable → allowed; daemon is authoritative
    return services::evaluateDisable(*facts, device->sysfsPath);
}

}  // namespace devmgr::app
