#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "devmgr/core/update_models.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::tests {

// Scripted IUpdateProvider: tests assign the public members. install() runs
// onInstall_ (emit progress mid-call, block on a latch, flip state...) before
// returning installResult_.
class FakeUpdateProvider : public pal::IUpdateProvider {
   public:
    std::string id_ = "fake";
    pal::UpdateProviderCaps caps_ =
        pal::UpdateProviderCaps::Query | pal::UpdateProviderCaps::Install;
    core::ProviderAvailability availability_{
        .available = true, .version = "1.0", .error = {}, .notices = {}};
    core::Result<std::vector<core::UpdateCandidate>> enumerateResult_ =
        std::vector<core::UpdateCandidate>{};
    core::Result<std::vector<core::PendingAction>> pendingResult_ =
        std::vector<core::PendingAction>{};
    core::Result<core::InstallOutcome> installResult_ = core::InstallOutcome{};
    std::function<void(runtime::ProgressReporter&)> onInstall_;
    std::atomic<int> enumerateCalls_{0};
    std::atomic<int> pendingCalls_{0};
    std::atomic<int> installCalls_{0};
    std::string lastInstallCandidate_;
    core::ReleaseRef lastInstallRelease_;

    std::string providerId() const override { return id_; }
    pal::UpdateProviderCaps capabilities() const override { return caps_; }
    core::ProviderAvailability availability() const override { return availability_; }
    core::Result<std::vector<core::UpdateCandidate>> enumerate() override {
        ++enumerateCalls_;
        return enumerateResult_;
    }
    core::Result<std::vector<core::PendingAction>> pendingActions() override {
        ++pendingCalls_;
        return pendingResult_;
    }
    core::Result<core::InstallOutcome> install(const std::string& candidateId,
                                               const core::ReleaseRef& release,
                                               runtime::ProgressReporter progress) override {
        ++installCalls_;
        lastInstallCandidate_ = candidateId;
        lastInstallRelease_ = release;
        if (onInstall_) onInstall_(progress);
        return installResult_;
    }
};

}  // namespace devmgr::tests
