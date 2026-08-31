// Standalone org.freedesktop.fwupd double for the design-verification harness
// (design-verification spec, cua-design-sandbox task 3.3).
//
// Why this exists rather than a script: the harness needs an fwupd-PRESENT
// posture, and the only honest way to serve one is to answer the exact contract
// FwupdUpdateProvider speaks. FakeFwupdDaemon already does — it is the double
// the devmgr_fwupd suite tests the real provider against — so reusing it keeps
// one implementation of that contract instead of two that can drift apart.
// This file only claims it on the SYSTEM bus and seeds a fixed inventory.
//
// The inventory is deliberately small, fixed, and machine-independent: the
// point of the posture is that "fwupd is present and has updates" renders the
// same way on every machine.
#include <chrono>
#include <csignal>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <sdbus-c++/sdbus-c++.h>

#include "devmgr/platform/linux/fwupd_contract.hpp"
#include "fwupd/fake_fwupd_daemon.hpp"

namespace {

namespace fw = devmgr::platform_linux::fwupd;
using Dict = fw::Dict;

volatile std::sig_atomic_t g_stop =
    0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void onSignal(int /*sig*/) {
    g_stop = 1;
}

Dict device(const std::string& id, const std::string& name, const std::string& vendor,
            const std::string& version, std::uint64_t flags) {
    Dict d;
    d["DeviceId"] = sdbus::Variant{id};
    d["Name"] = sdbus::Variant{name};
    d["Vendor"] = sdbus::Variant{vendor};
    d["Version"] = sdbus::Variant{version};
    d["Flags"] = sdbus::Variant{flags};
    return d;
}

Dict release(const std::string& version, const std::string& remoteId, const std::string& checksum) {
    Dict r;
    r["Version"] = sdbus::Variant{version};
    r["RemoteId"] = sdbus::Variant{remoteId};
    r["Checksum"] = sdbus::Variant{checksum};
    r["Flags"] = sdbus::Variant{fw::kReleaseFlagIsUpgrade};
    return r;
}

}  // namespace

int main() {
    devmgr::test::FakeFwupdDaemon daemon{devmgr::test::FakeFwupdDaemon::Bus::System};

    // One updatable device and one that is merely supported, so the Updates tab
    // has both an actionable row and a non-actionable one.
    const std::uint64_t updatable = fw::kDeviceFlagUpdatable | fw::kDeviceFlagSupported;
    daemon.setDevices({
        device("fixture-system-firmware", "Fixture System Firmware", "Fixture Vendor", "1.0.0",
               updatable),
        device("fixture-dock", "Fixture Dock", "Fixture Vendor", "3.2.1", fw::kDeviceFlagSupported),
    });
    daemon.setUpgrades("fixture-system-firmware",
                       {release("1.1.0", "fixture-remote", "cs-fixture")});
    daemon.setUpgrades("fixture-dock", {});  // explicitly empty => NothingToDo, not "unset"

    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);
    while (g_stop == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return 0;
}
