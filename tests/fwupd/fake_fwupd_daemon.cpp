#include "fwupd/fake_fwupd_daemon.hpp"

#include <utility>

namespace devmgr::test {
namespace {
using devmgr::platform_linux::fwupd::kBusName;
using devmgr::platform_linux::fwupd::kInterface;
using devmgr::platform_linux::fwupd::kObjectPath;
}  // namespace

FakeFwupdDaemon::FakeFwupdDaemon() {
    connection_ = sdbus::createSessionBusConnection(sdbus::ServiceName{kBusName});
    object_ = sdbus::createObject(*connection_, sdbus::ObjectPath{kObjectPath});
    registerVTable();
    connection_->enterEventLoopAsync();
}

FakeFwupdDaemon::~FakeFwupdDaemon() {
    // Stop processing before tearing down the object/connection (mirrors the
    // provider's own teardown discipline in fwupd_update_provider.cpp).
    if (connection_) connection_->leaveEventLoop();
    object_.reset();
    connection_.reset();
}

void FakeFwupdDaemon::registerVTable() {
    // sd_bus_add_object_vtable rejects PARTIAL parameter naming: for a given
    // method, either every parameter (both input AND output sides) is named,
    // or none are — naming only one side throws InvalidArgs at registration
    // time (discovered empirically; ListDisabledDevices/SetDeviceEnabled in
    // manager_adaptor.cpp happen to only ever name one side because the other
    // side has zero parameters there, so this codebase had no prior example
    // of a method with non-zero parameters on BOTH sides).
    const sdbus::InterfaceName iface{kInterface};
    object_
        ->addVTable(
            sdbus::registerMethod("GetDevices").implementedAs([this] { return getDevices(); }),
            sdbus::registerMethod("GetUpgrades")
                .withInputParamNames("device_id")
                .withOutputParamNames("releases")
                .implementedAs(
                    [this](const std::string& deviceId) { return getUpgrades(deviceId); }),
            sdbus::registerMethod("GetRemotes").implementedAs([this] { return getRemotes(); }),
            sdbus::registerMethod("GetHistory").implementedAs([this] { return getHistory(); }),
            sdbus::registerMethod("GetResults")
                .withInputParamNames("device_id")
                .withOutputParamNames("results")
                .implementedAs(
                    [this](const std::string& deviceId) { return getResults(deviceId); }),
            sdbus::registerMethod("Install")
                .withInputParamNames("device_id", "handle", "options")
                .implementedAs([this](const std::string& deviceId, const sdbus::UnixFd& fd,
                                      const std::map<std::string, sdbus::Variant>& options) {
                    install(deviceId, fd, options);
                }),
            sdbus::registerSignal("DeviceAdded").withParameters<Dict>(),
            sdbus::registerSignal("DeviceRemoved").withParameters<Dict>(),
            sdbus::registerSignal("DeviceChanged").withParameters<Dict>(),
            sdbus::registerSignal("Changed"),
            sdbus::registerSignal("DeviceRequest").withParameters<Dict>(),
            sdbus::registerProperty("DaemonVersion").withGetter([this] { return daemonVersion_; }),
            sdbus::registerProperty("Status").withGetter([this] { return status_.load(); }),
            sdbus::registerProperty("Percentage").withGetter([this] { return percentage_.load(); }))
        .forInterface(iface);
}

std::vector<FakeFwupdDaemon::Dict> FakeFwupdDaemon::getDevices() const {
    std::scoped_lock lock(mutex_);
    return devices_;
}

std::vector<FakeFwupdDaemon::Dict> FakeFwupdDaemon::getRemotes() const {
    std::scoped_lock lock(mutex_);
    return remotes_;
}

std::vector<FakeFwupdDaemon::Dict> FakeFwupdDaemon::getHistory() const {
    std::scoped_lock lock(mutex_);
    return history_;
}

std::vector<FakeFwupdDaemon::Dict> FakeFwupdDaemon::getUpgrades(const std::string& deviceId) const {
    std::scoped_lock lock(mutex_);
    const auto it = upgrades_.find(deviceId);
    if (it == upgrades_.end()) return {};  // unset ⇒ empty, no throw
    const UpgradeScript& script = it->second;
    if (script.error)
        throw sdbus::Error(sdbus::Error::Name{script.error->first}, script.error->second);
    if (!script.releases) return {};
    if (script.releases->empty())
        throw sdbus::Error(sdbus::Error::Name{"org.freedesktop.fwupd.NothingToDo"},
                           "no upgrades available");
    return *script.releases;
}

FakeFwupdDaemon::Dict FakeFwupdDaemon::getResults(const std::string& deviceId) const {
    std::scoped_lock lock(mutex_);
    const auto it = results_.find(deviceId);
    if (it == results_.end())
        throw sdbus::Error(sdbus::Error::Name{"org.freedesktop.fwupd.NothingToDo"}, "no results");
    return it->second;
}

void FakeFwupdDaemon::install(const std::string& deviceId, const sdbus::UnixFd& fd,
                              const std::map<std::string, sdbus::Variant>& options) {
    InstallHook hook;
    {
        std::scoped_lock lock(mutex_);
        hook = installHook_;
    }
    if (hook) hook(deviceId, fd.get(), options);
}

void FakeFwupdDaemon::setDevices(std::vector<Dict> devices) {
    std::scoped_lock lock(mutex_);
    devices_ = std::move(devices);
}

void FakeFwupdDaemon::setUpgrades(const std::string& deviceId, std::vector<Dict> releases) {
    std::scoped_lock lock(mutex_);
    upgrades_[deviceId].releases = std::move(releases);
}

void FakeFwupdDaemon::setUpgradesError(const std::string& deviceId, const std::string& errName,
                                       const std::string& errMsg) {
    std::scoped_lock lock(mutex_);
    upgrades_[deviceId].error = {errName, errMsg};
}

void FakeFwupdDaemon::setRemotes(std::vector<Dict> remotes) {
    std::scoped_lock lock(mutex_);
    remotes_ = std::move(remotes);
}

void FakeFwupdDaemon::setHistory(std::vector<Dict> entries) {
    std::scoped_lock lock(mutex_);
    history_ = std::move(entries);
}

void FakeFwupdDaemon::setResults(const std::string& deviceId, Dict results) {
    std::scoped_lock lock(mutex_);
    results_[deviceId] = std::move(results);
}

void FakeFwupdDaemon::scriptInstall(InstallHook hook) {
    std::scoped_lock lock(mutex_);
    installHook_ = std::move(hook);
}

void FakeFwupdDaemon::emitDeviceAdded(const Dict& device) {
    object_->emitSignal("DeviceAdded")
        .onInterface(sdbus::InterfaceName{kInterface})
        .withArguments(device);
}

void FakeFwupdDaemon::emitProgress(std::uint32_t status, std::uint32_t percentage) {
    status_.store(status);
    percentage_.store(percentage);
    object_->emitPropertiesChangedSignal(
        sdbus::InterfaceName{kInterface},
        {sdbus::PropertyName{"Status"}, sdbus::PropertyName{"Percentage"}});
}

void FakeFwupdDaemon::emitDeviceRequest(const Dict& request) {
    object_->emitSignal("DeviceRequest")
        .onInterface(sdbus::InterfaceName{kInterface})
        .withArguments(request);
}

void FakeFwupdDaemon::dropName() {
    connection_->releaseName(sdbus::ServiceName{kBusName});
}

void FakeFwupdDaemon::reacquireName() {
    connection_->requestName(sdbus::ServiceName{kBusName});
}

}  // namespace devmgr::test
