#include "devmgr/pal/refusing_backends.hpp"

namespace devmgr::pal {
namespace {

// One refusal, spelled once. The message is empty on purpose: the shared
// wording table owns every sentence a user reads for an unavailable backend.
tl::unexpected<core::Error> refuse() {
    return core::makeError(core::Error::Code::Unsupported, "");
}

}  // namespace

core::Result<std::vector<core::Device>> RefusingDeviceEnumerator::enumerate() {
    return refuse();
}

core::Result<void> RefusingHotplugMonitor::start(Callback /*callback*/) {
    return refuse();
}

void RefusingHotplugMonitor::stop() {}

core::Result<std::optional<std::string>> RefusingDeviceController::setEnabled(
    const std::string& /*nativeId*/, bool /*enabled*/, const std::string& /*rebindDriverHint*/) {
    return refuse();
}

core::Result<void> RefusingDeviceController::bindDriver(const std::string& /*nativeId*/,
                                                        const std::string& /*driverName*/) {
    return refuse();
}

core::Result<void> RefusingDeviceController::unbindDriver(const std::string& /*nativeId*/) {
    return refuse();
}

core::Result<std::vector<core::Driver>> RefusingDriverManager::driversFor(
    const core::Device& /*device*/) {
    return refuse();
}

core::Result<void> RefusingDriverManager::loadModule(const std::string& /*name*/) {
    return refuse();
}

core::Result<void> RefusingDriverManager::unloadModule(const std::string& /*name*/) {
    return refuse();
}

core::Result<std::vector<core::LoadedModule>> RefusingDriverManager::listLoadedModules() {
    return refuse();
}

core::Result<core::Driver> RefusingDriverManager::moduleInfo(const std::string& /*name*/) {
    return refuse();
}

core::Result<core::ModprobeInfo> RefusingDriverManager::modprobeInfo(const std::string& /*name*/) {
    return refuse();
}

core::Result<std::vector<std::string>> RefusingDriverManager::devicesUsingModule(
    const std::string& /*name*/) {
    return refuse();
}

core::Result<ISystemInfo::Info> RefusingSystemInfo::query() {
    return refuse();
}

core::Result<CriticalityFacts> RefusingCriticalityProber::probe() {
    return refuse();
}

core::Result<void> RefusingPrivilegedChannel::setDeviceEnabled(const core::Device& /*device*/,
                                                               bool /*enabled*/) {
    return refuse();
}

core::Result<void> RefusingPrivilegedChannel::loadModule(const std::string& /*name*/) {
    return refuse();
}

core::Result<void> RefusingPrivilegedChannel::unloadModule(const std::string& /*name*/) {
    return refuse();
}

core::Result<void> RefusingPrivilegedChannel::bindDriver(const core::Device& /*device*/,
                                                         const std::string& /*driverName*/) {
    return refuse();
}

core::Result<void> RefusingPrivilegedChannel::unbindDriver(const core::Device& /*device*/) {
    return refuse();
}

core::Result<std::vector<core::DisabledDeviceEntry>>
RefusingPrivilegedChannel::listDisabledDevices() {
    return refuse();
}

core::Result<std::vector<core::SnapshotMeta>> RefusingPrivilegedChannel::snapshotList() {
    return refuse();
}

core::Result<std::string> RefusingPrivilegedChannel::snapshotCreate(const std::string& /*label*/) {
    return refuse();
}

core::Result<core::RestoreOutcome> RefusingPrivilegedChannel::snapshotRestore(
    const std::string& /*id*/) {
    return refuse();
}

core::Result<void> RefusingPrivilegedChannel::snapshotDelete(const std::string& /*id*/) {
    return refuse();
}

core::Result<core::SnapshotDiff> RefusingPrivilegedChannel::snapshotDiff(
    const std::string& /*baseId*/, const std::string& /*targetId*/) {
    return refuse();
}

// Function-local statics: immortal, thread-safe on first use, and free of
// static-initialisation-order hazards. Stateless, so one instance serves every
// platform and every caller.
IDeviceEnumerator& refusingDeviceEnumerator() {
    static RefusingDeviceEnumerator instance;
    return instance;
}

IHotplugMonitor& refusingHotplugMonitor() {
    static RefusingHotplugMonitor instance;
    return instance;
}

IDeviceController& refusingDeviceController() {
    static RefusingDeviceController instance;
    return instance;
}

IDriverManager& refusingDriverManager() {
    static RefusingDriverManager instance;
    return instance;
}

IPrivilegedChannel& refusingPrivilegedChannel() {
    static RefusingPrivilegedChannel instance;
    return instance;
}

ISystemInfo& refusingSystemInfo() {
    static RefusingSystemInfo instance;
    return instance;
}

ICriticalityProber& refusingCriticalityProber() {
    static RefusingCriticalityProber instance;
    return instance;
}

}  // namespace devmgr::pal
