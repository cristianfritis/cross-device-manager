#pragma once

#include <memory>
#include <string>
#include <vector>

#include "devmgr/core/result.hpp"
#include "devmgr/core/update_models.hpp"
#include "devmgr/pal/interfaces.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/progress.hpp"

namespace devmgr::platform_linux {

// Frontend-direct fwupd client (spec §5, Phase 6): one system-bus connection,
// signals forwarded to the app EventBus, method calls issued from whatever
// thread calls enumerate()/pendingActions() (facade worker threads only —
// same discipline as DbusPrivilegedChannel). Quarantines sdbus-c++ behind a
// pimpl: this header stays toolkit/D-Bus free so composition roots (gui/tui)
// can hold a `pal::IUpdateProvider*` without pulling in sdbus-c++.
//
// V2: every public method is exception-free. Connection failure at
// construction is absorbed into a degraded state — availability() reports it
// via `error`, enumerate()/pendingActions() return Io — rather than throwing
// out of the constructor.
class FwupdUpdateProvider : public pal::IUpdateProvider {
   public:
    struct Config {
        // No default member initializer here (deliberately): resolving a DMI
        // for a class nested inside FwupdUpdateProvider requires
        // FwupdUpdateProvider's own complete-class context, but the ctor
        // below needs Config{} as a default *argument* — itself part of that
        // same complete-class context. GCC 15 and Clang 21 both reject the
        // resulting cycle. `Config cfg = {}` still value-initializes
        // useSessionBus to false, so the default behaves identically.
        bool useSessionBus;  // tests: fake daemon on dbus-run-session bus
    };

    explicit FwupdUpdateProvider(runtime::EventBus& bus, Config cfg = {});
    ~FwupdUpdateProvider() override;

    FwupdUpdateProvider(const FwupdUpdateProvider&) = delete;
    FwupdUpdateProvider& operator=(const FwupdUpdateProvider&) = delete;
    FwupdUpdateProvider(FwupdUpdateProvider&&) = delete;
    FwupdUpdateProvider& operator=(FwupdUpdateProvider&&) = delete;

    std::string providerId() const override;
    pal::UpdateProviderCaps capabilities() const override;
    core::ProviderAvailability availability() const override;
    core::Result<std::vector<core::UpdateCandidate>> enumerate() override;
    core::Result<std::vector<core::PendingAction>> pendingActions() override;
    // Returns Unsupported in this task — T8 wires the real install lifecycle
    // (spec §5.5) on top of the connection/signals built here.
    core::Result<core::InstallOutcome> install(const std::string& candidateId,
                                               const core::ReleaseRef& release,
                                               runtime::ProgressReporter progress) override;

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace devmgr::platform_linux
