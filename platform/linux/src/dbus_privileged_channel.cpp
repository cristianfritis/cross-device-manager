#include "devmgr/platform/linux/dbus_privileged_channel.hpp"

#include <chrono>
#include <string>

#include <sdbus-c++/sdbus-c++.h>

#include "devmgr/platform/linux/dbus_contract.hpp"

namespace devmgr::platform_linux {

DbusPrivilegedChannel::DbusPrivilegedChannel(Bus bus) : bus_(bus) {}

core::Result<void> DbusPrivilegedChannel::setDeviceEnabled(const core::Device& device,
                                                           bool enabled) {
    try {
        auto connection = bus_ == Bus::Session ? sdbus::createSessionBusConnection()
                                               : sdbus::createSystemBusConnection();
        auto proxy = sdbus::createProxy(std::move(connection), sdbus::ServiceName{kBusName},
                                        sdbus::ObjectPath{kObjectPath});
        proxy->callMethod("SetDeviceEnabled")
            .onInterface(sdbus::InterfaceName{kInterfaceName})
            .withArguments(device.sysfsPath, enabled)
            .withTimeout(std::chrono::minutes(2));
        return {};
    } catch (const sdbus::Error& e) {
        return tl::unexpected(coreErrorFor(std::string{e.getName()}, std::string{e.getMessage()}));
    }
}

// Module/driver verbs arrive in Task 9 (IPC v2) with the real D-Bus calls.
core::Result<void> DbusPrivilegedChannel::loadModule(const std::string&) {
    return core::makeError(core::Error::Code::Unsupported, "loadModule arrives in Task 9");
}
core::Result<void> DbusPrivilegedChannel::unloadModule(const std::string&) {
    return core::makeError(core::Error::Code::Unsupported, "unloadModule arrives in Task 9");
}
core::Result<void> DbusPrivilegedChannel::bindDriver(const core::Device&, const std::string&) {
    return core::makeError(core::Error::Code::Unsupported, "bindDriver arrives in Task 9");
}
core::Result<void> DbusPrivilegedChannel::unbindDriver(const core::Device&) {
    return core::makeError(core::Error::Code::Unsupported, "unbindDriver arrives in Task 9");
}
core::Result<std::vector<core::DisabledDeviceEntry>> DbusPrivilegedChannel::listDisabledDevices() {
    return core::makeError(core::Error::Code::Unsupported, "listDisabledDevices arrives in Task 9");
}

}  // namespace devmgr::platform_linux
