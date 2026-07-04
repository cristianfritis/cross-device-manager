#include "daemon/src/manager_adaptor.hpp"

#include <string>

#include "devmgr/platform/linux/dbus_contract.hpp"

namespace devmgr::daemon {

ManagerAdaptor::ManagerAdaptor(sdbus::IConnection& connection, RequestProcessor& processor)
    : processor_(processor) {
    object_ = sdbus::createObject(connection, sdbus::ObjectPath{platform_linux::kObjectPath});
    object_
        ->addVTable(sdbus::registerMethod("SetDeviceEnabled")
                        .withInputParamNames("sysfs_path", "enabled")
                        .implementedAs([this](const std::string& sysfsPath, const bool enabled) {
                            // getCurrentlyProcessedMessage is only valid inside the
                            // handler — the polkit subject is the caller's unique name.
                            const auto sender =
                                std::string{object_->getCurrentlyProcessedMessage().getSender()};
                            auto result = processor_.setDeviceEnabled(sender, sysfsPath, enabled);
                            if (!result) {
                                throw sdbus::Error(
                                    sdbus::Error::Name{
                                        platform_linux::dbusErrorNameFor(result.error().code)},
                                    result.error().message);
                            }
                        }),
                    sdbus::registerProperty("ApiVersion").withGetter([] {
                        return platform_linux::kApiVersion;
                    }))
        .forInterface(sdbus::InterfaceName{platform_linux::kInterfaceName});
}

}  // namespace devmgr::daemon
