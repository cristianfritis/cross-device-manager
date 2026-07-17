#include "daemon/src/manager_adaptor.hpp"

#include <map>
#include <string>
#include <vector>

#include "devmgr/core/snapshot_json.hpp"
#include "devmgr/platform/linux/dbus_contract.hpp"

namespace devmgr::daemon {
namespace {
void throwIfFailed(const core::Result<void>& result) {
    if (!result)
        throw sdbus::Error(
            sdbus::Error::Name{platform_linux::dbusErrorNameFor(result.error().code)},
            result.error().message);
}

// Unwraps Result<T> or throws the mapped D-Bus error (value-returning verbs).
template <class T>
T valueOrThrow(core::Result<T> result) {
    if (!result)
        throw sdbus::Error(
            sdbus::Error::Name{platform_linux::dbusErrorNameFor(result.error().code)},
            result.error().message);
    return std::move(*result);
}
}  // namespace

ManagerAdaptor::ManagerAdaptor(sdbus::IConnection& connection, RequestProcessor& processor)
    : processor_(processor) {
    object_ = sdbus::createObject(connection, sdbus::ObjectPath{platform_linux::kObjectPath});
    auto sender = [this] {
        return std::string{object_->getCurrentlyProcessedMessage().getSender()};
    };
    object_
        ->addVTable(
            sdbus::registerMethod("SetDeviceEnabled")
                .withInputParamNames("sysfs_path", "enabled")
                .implementedAs([this, sender](const std::string& path, const bool enabled) {
                    throwIfFailed(processor_.setDeviceEnabled(sender(), path, enabled));
                }),
            sdbus::registerMethod("LoadModule")
                .withInputParamNames("name")
                .implementedAs([this, sender](const std::string& name) {
                    throwIfFailed(processor_.loadModule(sender(), name));
                }),
            sdbus::registerMethod("UnloadModule")
                .withInputParamNames("name")
                .implementedAs([this, sender](const std::string& name) {
                    throwIfFailed(processor_.unloadModule(sender(), name));
                }),
            sdbus::registerMethod("BindDriver")
                .withInputParamNames("sysfs_path", "driver")
                .implementedAs([this, sender](const std::string& path, const std::string& driver) {
                    throwIfFailed(processor_.bindDriver(sender(), path, driver));
                }),
            sdbus::registerMethod("UnbindDriver")
                .withInputParamNames("sysfs_path")
                .implementedAs([this, sender](const std::string& path) {
                    throwIfFailed(processor_.unbindDriver(sender(), path));
                }),
            sdbus::registerMethod("ListDisabledDevices")
                .withOutputParamNames("entries")
                .implementedAs([this] {
                    std::vector<std::map<std::string, sdbus::Variant>> out;
                    for (const auto& e : processor_.listDisabledDevices()) {
                        out.push_back({{"bus", sdbus::Variant(e.key.bus)},
                                       {"vendor_id", sdbus::Variant(e.key.vendorId)},
                                       {"product_id", sdbus::Variant(e.key.productId)},
                                       {"serial", sdbus::Variant(e.key.serial)},
                                       {"position", sdbus::Variant(e.key.position)},
                                       {"mechanism", sdbus::Variant(e.mechanism)},
                                       {"last_driver", sdbus::Variant(e.lastDriver)},
                                       {"last_sysfs_path", sdbus::Variant(e.lastSysfsPath)},
                                       {"disabled_at_utc", sdbus::Variant(e.disabledAtUtc)},
                                       {"guard_suspended", sdbus::Variant(e.guardSuspended)}});
                    }
                    return out;
                }),
            sdbus::registerMethod("SnapshotList")
                .withOutputParamNames("metadata_json")
                .implementedAs([this] {
                    return core::snapshotListToJson(valueOrThrow(processor_.snapshotList()));
                }),
            sdbus::registerMethod("SnapshotCreate")
                .withInputParamNames("label")
                .withOutputParamNames("id")
                .implementedAs([this, sender](const std::string& label) {
                    return valueOrThrow(processor_.snapshotCreate(sender(), label));
                }),
            sdbus::registerMethod("SnapshotRestore")
                .withInputParamNames("id")
                .withOutputParamNames("outcome_json")
                .implementedAs([this, sender](const std::string& id) {
                    return core::restoreOutcomeToJson(
                        valueOrThrow(processor_.snapshotRestore(sender(), id)));
                }),
            sdbus::registerMethod("SnapshotDelete")
                .withInputParamNames("id")
                .implementedAs([this, sender](const std::string& id) {
                    throwIfFailed(processor_.snapshotDelete(sender(), id));
                }),
            sdbus::registerProperty("ApiVersion").withGetter([] {
                return platform_linux::kApiVersion;
            }))
        .forInterface(sdbus::InterfaceName{platform_linux::kInterfaceName});
}

}  // namespace devmgr::daemon
