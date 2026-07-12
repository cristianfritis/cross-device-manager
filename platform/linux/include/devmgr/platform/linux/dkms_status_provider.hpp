#pragma once

#include <string>
#include <vector>

#include "devmgr/core/result.hpp"
#include "devmgr/core/update_models.hpp"
#include "devmgr/pal/interfaces.hpp"
#include "devmgr/runtime/progress.hpp"

namespace devmgr::platform_linux {

// Read-only DKMS status reporter: walks <dkmsRoot>/<mod>/<ver>/<kernel>/<arch>/module/
// and cross-references <modulesRoot>/<kernel>/updates/dkms/ — no dkms binary
// invocation, no D-Bus (spec §6). Never actionable: capabilities() == Query,
// install() always returns Unsupported.
class DkmsStatusProvider : public pal::IUpdateProvider {
   public:
    // Injectable roots (spec §6): prod defaults; tests pass fixture dirs.
    explicit DkmsStatusProvider(std::string dkmsRoot = "/var/lib/dkms",
                                std::string modulesRoot = "/lib/modules");

    std::string providerId() const override;
    pal::UpdateProviderCaps capabilities() const override;
    core::ProviderAvailability availability() const override;
    core::Result<std::vector<core::UpdateCandidate>> enumerate() override;
    core::Result<std::vector<core::PendingAction>> pendingActions() override;
    core::Result<core::InstallOutcome> install(const std::string& candidateId,
                                               const core::ReleaseRef& release,
                                               runtime::ProgressReporter progress) override;

   private:
    std::string dkmsRoot_;
    std::string modulesRoot_;
};

}  // namespace devmgr::platform_linux
