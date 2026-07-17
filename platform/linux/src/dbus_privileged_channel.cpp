#include "devmgr/platform/linux/dbus_privileged_channel.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <sdbus-c++/sdbus-c++.h>

#include "devmgr/platform/linux/dbus_contract.hpp"

namespace {
constexpr int kListDisabledDevicesTimeoutSeconds = 5;

std::unique_ptr<sdbus::IProxy> makeProxy(devmgr::platform_linux::DbusPrivilegedChannel::Bus bus) {
    auto connection = bus == devmgr::platform_linux::DbusPrivilegedChannel::Bus::Session
                          ? sdbus::createSessionBusConnection()
                          : sdbus::createSystemBusConnection();
    return sdbus::createProxy(std::move(connection),
                              sdbus::ServiceName{devmgr::platform_linux::kBusName},
                              sdbus::ObjectPath{devmgr::platform_linux::kObjectPath});
}
}  // namespace

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

core::Result<void> DbusPrivilegedChannel::ensureApi2() {
    {
        const std::scoped_lock lock(cacheMutex_);
        if (cachedApi_) {
            if (*cachedApi_ >= 2) return {};
            return core::makeError(core::Error::Code::Unsupported,
                                   "devmgrd too old (API " + std::to_string(*cachedApi_) +
                                       " < 2) — restart the daemon");
        }
    }
    try {
        auto proxy = makeProxy(bus_);
        const sdbus::Variant v =
            proxy->getProperty("ApiVersion").onInterface(sdbus::InterfaceName{kInterfaceName});
        const auto api = v.get<std::uint32_t>();
        const std::scoped_lock lock(cacheMutex_);
        cachedApi_ = api;
        if (api >= 2) return {};
        return core::makeError(
            core::Error::Code::Unsupported,
            "devmgrd too old (API " + std::to_string(api) + " < 2) — restart the daemon");
    } catch (const sdbus::Error& e) {
        return tl::unexpected(coreErrorFor(std::string{e.getName()}, std::string{e.getMessage()}));
    }
}

core::Result<void> DbusPrivilegedChannel::loadModule(const std::string& name) {
    if (auto api = ensureApi2(); !api) return api;
    try {
        auto proxy = makeProxy(bus_);
        proxy->callMethod("LoadModule")
            .onInterface(sdbus::InterfaceName{kInterfaceName})
            .withArguments(name)
            .withTimeout(std::chrono::minutes(2));
        return {};
    } catch (const sdbus::Error& e) {
        return tl::unexpected(coreErrorFor(std::string{e.getName()}, std::string{e.getMessage()}));
    }
}

core::Result<void> DbusPrivilegedChannel::unloadModule(const std::string& name) {
    if (auto api = ensureApi2(); !api) return api;
    try {
        auto proxy = makeProxy(bus_);
        proxy->callMethod("UnloadModule")
            .onInterface(sdbus::InterfaceName{kInterfaceName})
            .withArguments(name)
            .withTimeout(std::chrono::minutes(2));
        return {};
    } catch (const sdbus::Error& e) {
        return tl::unexpected(coreErrorFor(std::string{e.getName()}, std::string{e.getMessage()}));
    }
}

core::Result<void> DbusPrivilegedChannel::bindDriver(const core::Device& device,
                                                     const std::string& driverName) {
    if (auto api = ensureApi2(); !api) return api;
    try {
        auto proxy = makeProxy(bus_);
        proxy->callMethod("BindDriver")
            .onInterface(sdbus::InterfaceName{kInterfaceName})
            .withArguments(device.sysfsPath, driverName)
            .withTimeout(std::chrono::minutes(2));
        return {};
    } catch (const sdbus::Error& e) {
        return tl::unexpected(coreErrorFor(std::string{e.getName()}, std::string{e.getMessage()}));
    }
}

core::Result<void> DbusPrivilegedChannel::unbindDriver(const core::Device& device) {
    if (auto api = ensureApi2(); !api) return api;
    try {
        auto proxy = makeProxy(bus_);
        proxy->callMethod("UnbindDriver")
            .onInterface(sdbus::InterfaceName{kInterfaceName})
            .withArguments(device.sysfsPath)
            .withTimeout(std::chrono::minutes(2));
        return {};
    } catch (const sdbus::Error& e) {
        return tl::unexpected(coreErrorFor(std::string{e.getName()}, std::string{e.getMessage()}));
    }
}

core::Result<std::vector<core::DisabledDeviceEntry>> DbusPrivilegedChannel::listDisabledDevices() {
    if (auto api = ensureApi2(); !api) return tl::unexpected(api.error());
    try {
        auto proxy = makeProxy(bus_);
        std::vector<std::map<std::string, sdbus::Variant>> raw;
        proxy->callMethod("ListDisabledDevices")
            .onInterface(sdbus::InterfaceName{kInterfaceName})
            .withTimeout(std::chrono::seconds(kListDisabledDevicesTimeoutSeconds))
            .storeResultsTo(raw);
        std::vector<core::DisabledDeviceEntry> out;
        for (const auto& m : raw) {
            core::DisabledDeviceEntry e;
            e.key.bus = m.at("bus").get<std::string>();
            e.key.vendorId = m.at("vendor_id").get<std::string>();
            e.key.productId = m.at("product_id").get<std::string>();
            e.key.serial = m.at("serial").get<std::string>();
            e.key.position = m.at("position").get<std::string>();
            e.mechanism = m.at("mechanism").get<std::string>();
            e.lastDriver = m.at("last_driver").get<std::string>();
            e.lastSysfsPath = m.at("last_sysfs_path").get<std::string>();
            e.disabledAtUtc = m.at("disabled_at_utc").get<std::int64_t>();
            e.guardSuspended = m.at("guard_suspended").get<bool>();
            out.push_back(std::move(e));
        }
        return out;
    } catch (const sdbus::Error& e) {
        return tl::unexpected(coreErrorFor(std::string{e.getName()}, std::string{e.getMessage()}));
    } catch (const std::exception& e) {
        // Defense vs a future daemon omitting keys: m.at() throws
        // std::out_of_range, which must not escape the never-throws Result
        // contract nor kill the facade refresh worker (Phase 5 review T9 m-1).
        return tl::unexpected(
            core::makeError(core::Error::Code::Io,
                            std::string{"malformed ListDisabledDevices reply: "} + e.what()));
    }
}

}  // namespace devmgr::platform_linux
