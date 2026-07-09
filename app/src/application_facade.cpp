#include "devmgr/app/application_facade.hpp"

#include <utility>

#include "devmgr/app/disabled_overlay.hpp"
#include "devmgr/core/events.hpp"

namespace devmgr::app {

std::future<void> ApplicationFacade::refresh() {
    return scheduler_.submit([this] {
        auto result = enumerator_.enumerate();
        if (!result) {
            bus_.publish(
                core::ErrorEvent{.source = "enumerate", .message = result.error().message});
            return;
        }
        if (channel_ != nullptr) {
            // ONE bulk fetch per refresh (spec §6.1); daemon-down or API-1
            // degrades silently to Phase 4 rendering.
            if (auto disabled = channel_->listDisabledDevices(); disabled)
                applyDisabledOverlay(*result, *disabled);
        }
        service_.applyEnumeration(std::move(*result));
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

std::vector<core::Driver> ApplicationFacade::driverInfo(const core::DeviceId& id) const {
    if (drivers_ == nullptr) return {};
    const auto device = service_.findById(id);
    if (!device) return {};
    auto result = drivers_->driversFor(*device);
    return result ? *result : std::vector<core::Driver>{};
}

core::Result<std::vector<core::LoadedModule>> ApplicationFacade::listModules() const {
    if (drivers_ == nullptr)
        return core::makeError(core::Error::Code::Unsupported, "built without kmod support");
    return drivers_->listLoadedModules();
}

core::Result<core::Driver> ApplicationFacade::moduleDetail(const std::string& name) const {
    if (drivers_ == nullptr)
        return core::makeError(core::Error::Code::Unsupported, "built without kmod support");
    return drivers_->moduleInfo(name);
}

core::Result<core::ModprobeInfo> ApplicationFacade::modprobeDetail(const std::string& name) const {
    if (drivers_ == nullptr)
        return core::makeError(core::Error::Code::Unsupported, "built without kmod support");
    return drivers_->modprobeInfo(name);
}

std::optional<pal::ISystemInfo::Info> ApplicationFacade::systemInfo() const {
    if (systemInfo_ == nullptr) return std::nullopt;
    auto info = systemInfo_->query();
    if (!info) return std::nullopt;
    return *info;
}

services::GuardVerdict ApplicationFacade::canUnloadModule(const std::string& name) const {
    if (drivers_ == nullptr || prober_ == nullptr) return {};  // advisory unavailable
    services::ModuleUnloadFacts moduleFacts;
    auto loaded = drivers_->listLoadedModules();
    if (!loaded) return {};
    for (const auto& m : *loaded) {
        if (m.name != name) continue;
        moduleFacts.holders = m.holders;
        moduleFacts.refCount = m.refCount;
    }
    if (auto affected = drivers_->devicesUsingModule(name); affected)
        moduleFacts.affectedDevicePaths = *affected;
    auto facts = prober_->probe();
    if (!facts) return {};
    return services::evaluateModuleUnload(*facts, moduleFacts);
}

std::future<void> ApplicationFacade::runChannelTask(
    std::string taskId, std::string okMessage, bool modulesChanged,
    std::function<core::Result<void>(pal::IPrivilegedChannel&)> call) {
    return scheduler_.submit([this, taskId = std::move(taskId), okMessage = std::move(okMessage),
                              modulesChanged, call = std::move(call)] {
        if (channel_ == nullptr) {
            bus_.publish(
                core::TaskCompletedEvent{.taskId = taskId,
                                         .ok = false,
                                         .message = "built without privileged-helper support"});
            return;
        }
        auto result = call(*channel_);
        if (result) {
            bus_.publish(
                core::TaskCompletedEvent{.taskId = taskId, .ok = true, .message = okMessage});
            if (modulesChanged) bus_.publish(core::ModulesChangedEvent{});
        } else {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = taskId, .ok = false, .message = result.error().message});
        }
    });
}

std::future<void> ApplicationFacade::loadModule(const std::string& name) {
    return runChannelTask("load-module:" + name, "Loaded module " + name, true,
                          [name](pal::IPrivilegedChannel& c) { return c.loadModule(name); });
}

std::future<void> ApplicationFacade::unloadModule(const std::string& name) {
    return runChannelTask("unload-module:" + name, "Unloaded module " + name, true,
                          [name](pal::IPrivilegedChannel& c) { return c.unloadModule(name); });
}

std::future<void> ApplicationFacade::bindDriver(const core::DeviceId& id,
                                                const std::string& driverName) {
    const auto device = service_.findById(id);
    if (!device) {
        return runChannelTask("bind-driver:" + id.value, "", false,
                              [](pal::IPrivilegedChannel&) -> core::Result<void> {
                                  return core::makeError(core::Error::Code::NotFound,
                                                         "device no longer present");
                              });
    }
    return runChannelTask("bind-driver:" + id.value, "Bound " + driverName + " to " + device->name,
                          false, [device = *device, driverName](pal::IPrivilegedChannel& c) {
                              return c.bindDriver(device, driverName);
                          });
}

std::future<void> ApplicationFacade::unbindDriver(const core::DeviceId& id) {
    const auto device = service_.findById(id);
    if (!device) {
        return runChannelTask("unbind-driver:" + id.value, "", false,
                              [](pal::IPrivilegedChannel&) -> core::Result<void> {
                                  return core::makeError(core::Error::Code::NotFound,
                                                         "device no longer present");
                              });
    }
    return runChannelTask(
        "unbind-driver:" + id.value, "Unbound driver from " + device->name, false,
        [device = *device](pal::IPrivilegedChannel& c) { return c.unbindDriver(device); });
}

}  // namespace devmgr::app
