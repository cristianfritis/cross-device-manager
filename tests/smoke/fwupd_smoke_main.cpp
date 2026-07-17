// Phase 6 VM/host smoke binary: drives OUR stack end-to-end (spec §11.3 —
// the smoke must exercise FwupdUpdateProvider / DkmsStatusProvider, never
// shell out to `fwupdmgr`/`dkms`). No gtest; plain main().
//
// Usage: devmgr_fwupd_smoke [--install] [--device <substr>] [--expect-version <v>]
//                            [--dkms <module>]
// Exit 0 = assertions held. Prints PHASE6-SMOKE lines for the shell script to grep.
#include <cstdio>
#include <string>

#include "devmgr/core/update_models.hpp"
#include "devmgr/platform/linux/dkms_status_provider.hpp"
#include "devmgr/platform/linux/fwupd_update_provider.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/progress.hpp"

int main(int argc, char** argv) {
    bool install = false;
    std::string device, expectVersion, dkmsModule;
    for (int i = 1; i < argc; ++i) {  // plain loop, no deps
        const std::string a = argv[i];
        if (a == "--install")
            install = true;
        else if (a == "--device" && i + 1 < argc)
            device = argv[++i];
        else if (a == "--expect-version" && i + 1 < argc)
            expectVersion = argv[++i];
        else if (a == "--dkms" && i + 1 < argc)
            dkmsModule = argv[++i];
    }

    if (!dkmsModule.empty()) {
        devmgr::platform_linux::DkmsStatusProvider dkms;
        const auto r = dkms.enumerate();
        if (!r) {
            std::printf("PHASE6-SMOKE dkms-enumerate-failed\n");
            return 4;
        }
        for (const auto& c : *r) {
            if (c.id.rfind("dkms:" + dkmsModule + "/", 0) != 0) continue;
            for (const auto& [k, v] : c.details) {
                if (v.find("installed") == std::string::npos) continue;
                std::printf("PHASE6-SMOKE dkms %s %s: %s\n", c.id.c_str(), k.c_str(), v.c_str());
                std::printf("PHASE6-SMOKE OK\n");
                return 0;
            }
        }
        std::printf("PHASE6-SMOKE dkms module '%s' not installed\n", dkmsModule.c_str());
        return 4;
    }

    // Deferred until here (not declared at top of main): the --dkms branch
    // above always returns before touching fwupd, so an EventBus constructed
    // unconditionally would sit unused on that path.
    devmgr::runtime::EventBus bus;
    devmgr::platform_linux::FwupdUpdateProvider fwupd(bus);  // system bus (default Config)
    const auto avail = fwupd.availability();
    if (!avail.available) {
        std::printf("PHASE6-SMOKE fwupd-unavailable: %s\n",
                    avail.error ? avail.error->message.c_str() : "?");
        return 2;
    }
    const auto cands = fwupd.enumerate();
    if (!cands) {
        std::printf("PHASE6-SMOKE enumerate-failed\n");
        return 2;
    }
    for (const auto& c : *cands) {
        const bool matchesName = c.displayName.find(device) != std::string::npos;
        const bool matchesId = !device.empty() && c.id.find(device) != std::string::npos;
        if ((!matchesName && !matchesId) || c.releases.empty()) continue;
        const auto& rel = c.releases.front();
        std::printf("PHASE6-SMOKE found %s %s -> %s localcab=%d\n", c.displayName.c_str(),
                    c.currentVersion.c_str(), rel.version.c_str(), rel.localCab ? 1 : 0);
        if (!install) {
            std::printf("PHASE6-SMOKE OK\n");
            return 0;
        }
        const auto out = fwupd.install(c.id, rel.ref(), [](const auto& u) {
            std::printf("PHASE6-SMOKE progress %d%% %s\n", u.percent, u.stage.c_str());
        });
        if (!out) {
            std::printf("PHASE6-SMOKE install-failed: %s\n", out.error().message.c_str());
            return 3;
        }
        const auto ver = out->observedVersion.value_or("(scheduled)");
        std::printf("PHASE6-SMOKE installed disposition=%d version=%s\n",
                    static_cast<int>(out->disposition), ver.c_str());
        if (!expectVersion.empty() && out->observedVersion != expectVersion) return 3;
        std::printf("PHASE6-SMOKE OK\n");
        return 0;
    }
    std::printf("PHASE6-SMOKE no matching device '%s'\n", device.c_str());
    return 2;
}
