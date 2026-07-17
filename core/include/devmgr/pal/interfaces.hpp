#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/core/models.hpp"
#include "devmgr/core/result.hpp"
#include "devmgr/core/snapshot_models.hpp"
#include "devmgr/core/update_models.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/runtime/progress.hpp"

namespace devmgr::pal {

class IDeviceEnumerator {
   public:
    virtual ~IDeviceEnumerator() = default;
    virtual core::Result<std::vector<core::Device>> enumerate() = 0;
};

class IHotplugMonitor {
   public:
    using Callback = std::function<void(const HotplugEvent&)>;
    virtual ~IHotplugMonitor() = default;
    virtual core::Result<void> start(Callback callback) = 0;
    // Implementations must block until the reader thread is joined and must
    // guarantee no further callbacks fire after stop() returns —
    // HotplugService::stop()'s correctness depends on this.
    virtual void stop() = 0;
};

class IDeviceController {
   public:
    virtual ~IDeviceController() = default;
    // Identity is the device's canonical sysfs path. Phase 5 mechanisms:
    //  - `authorized` attr present (USB): write 0/1. Returns nullopt.
    //  - otherwise: disable = unbind current driver (returns its name, ""
    //    if none was bound — the value signals mechanism "unbind");
    //    enable = write rebindDriverHint to driver_override (when non-empty
    //    and the attr exists), then bus drivers_probe; override cleared even
    //    on failure. Returns nullopt on enables and authorized-mechanism ops.
    virtual core::Result<std::optional<std::string>> setEnabled(
        const std::string& sysfsPath, bool enabled, const std::string& rebindDriverHint) = 0;
    // Surgical verbs (never persisted by callers).
    virtual core::Result<void> bindDriver(const std::string& sysfsPath,
                                          const std::string& driverName) = 0;
    virtual core::Result<void> unbindDriver(const std::string& sysfsPath) = 0;
};

class IDriverManager {
   public:
    virtual ~IDriverManager() = default;
    // Takes the full Device: modalias lookup + driver-symlink resolution need
    // modalias and sysfsPath (spec §4.2 refinement; same rationale as
    // IPrivilegedChannel taking Device). Returns the modalias candidate list
    // ordered per the pinned contract (kmod_driver_manager.hpp): the FIRST
    // element is the currently-bound (or builtin) driver when one exists —
    // both frontends' bind-prefill depends on that ordering.
    virtual core::Result<std::vector<core::Driver>> driversFor(const core::Device& device) = 0;
    virtual core::Result<void> loadModule(const std::string& name) = 0;
    virtual core::Result<void> unloadModule(const std::string& name) = 0;
    virtual core::Result<std::vector<core::LoadedModule>> listLoadedModules() = 0;
    virtual core::Result<core::Driver> moduleInfo(const std::string& name) = 0;
    virtual core::Result<core::ModprobeInfo> modprobeInfo(const std::string& name) = 0;
    // Canonical sysfs device paths bound via any of the module's drivers.
    virtual core::Result<std::vector<std::string>> devicesUsingModule(const std::string& name) = 0;
};

enum class UpdateProviderCaps : unsigned { Query = 1U << 0U, Install = 1U << 1U };
constexpr UpdateProviderCaps operator|(UpdateProviderCaps a, UpdateProviderCaps b) {
    // Flag-enum combination: the result is deliberately a value with no
    // single matching enumerator (e.g. Query|Install). clang-analyzer's
    // enum-range check doesn't model bitmask enums and flags every OR'd
    // result as "out of range" — false positive, not a real bug.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    return static_cast<UpdateProviderCaps>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr bool hasCap(UpdateProviderCaps caps, UpdateProviderCaps bit) {
    return (static_cast<unsigned>(caps) & static_cast<unsigned>(bit)) != 0U;
}

class IUpdateProvider {
   public:
    virtual ~IUpdateProvider() = default;
    virtual std::string providerId() const = 0;
    virtual UpdateProviderCaps capabilities() const = 0;
    virtual core::ProviderAvailability availability() const = 0;
    virtual core::Result<std::vector<core::UpdateCandidate>> enumerate() = 0;
    // Durable pending/reboot records (fwupd: GetHistory/GetResults; dkms: {}).
    virtual core::Result<std::vector<core::PendingAction>> pendingActions() = 0;
    // Blocking (minutes: polkit prompt + flash) — TaskScheduler worker only,
    // never a UI thread. progress runs on provider threads; percent -1 =
    // indeterminate. Implementations must be exception-free (spec V2) and
    // reject non-installable targets (spec V1) even though the UI pre-gates.
    virtual core::Result<core::InstallOutcome> install(const std::string& candidateId,
                                                       const core::ReleaseRef& release,
                                                       runtime::ProgressReporter progress) = 0;
};

class ISystemInfo {
   public:
    struct Info {
        std::string osVersion;
        std::string kernelVersion;
        bool secureBoot = false;
        bool rebootPending = false;
        std::string lockdownMode = "none";
    };
    virtual ~ISystemInfo() = default;
    virtual core::Result<Info> query() = 0;
};

class IPrivilegedChannel {
   public:
    virtual ~IPrivilegedChannel() = default;
    // Takes the full Device (the channel needs sysfsPath on the wire and name
    // for messages). Blocking: interactive polkit auth may take ~minutes —
    // call from a TaskScheduler worker, never a UI thread.
    virtual core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) = 0;
    virtual core::Result<void> loadModule(const std::string& name) = 0;
    virtual core::Result<void> unloadModule(const std::string& name) = 0;
    virtual core::Result<void> bindDriver(const core::Device& device,
                                          const std::string& driverName) = 0;
    virtual core::Result<void> unbindDriver(const core::Device& device) = 0;
    virtual core::Result<std::vector<core::DisabledDeviceEntry>> listDisabledDevices() = 0;
    // ApiVersion 3 snapshot verbs (snapshot-ipc spec). The mutating three are
    // polkit-gated (interactive auth: blocking, worker-thread only, like the
    // verbs above); list is unprivileged metadata. Guard refusals during
    // restore are ITEMS in the outcome, never channel-level errors.
    virtual core::Result<std::vector<core::SnapshotMeta>> snapshotList() = 0;
    virtual core::Result<std::string> snapshotCreate(const std::string& label) = 0;
    virtual core::Result<core::RestoreOutcome> snapshotRestore(const std::string& id) = 0;
    virtual core::Result<void> snapshotDelete(const std::string& id) = 0;
};

}  // namespace devmgr::pal
