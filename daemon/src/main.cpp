#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

#include <sdbus-c++/sdbus-c++.h>
#include <spdlog/spdlog.h>

#include "daemon/src/manager_adaptor.hpp"
#include "daemon/src/polkit_authority.hpp"
#include "devmgr/core/version.hpp"
#include "devmgr/daemon/authority.hpp"
#include "devmgr/daemon/enforcement_service.hpp"
#include "devmgr/daemon/request_processor.hpp"
#include "devmgr/daemon/snapshot_service.hpp"
#include "devmgr/daemon/snapshot_store.hpp"
#include "devmgr/daemon/state_store.hpp"
#include "devmgr/platform/linux/dbus_contract.hpp"
#include "devmgr/platform/linux/kmod_driver_manager.hpp"
#include "devmgr/platform/linux/linux_criticality_prober.hpp"
#include "devmgr/platform/linux/sysfs_device_controller.hpp"
#include "devmgr/platform/linux/udev_device_enumerator.hpp"
#include "devmgr/platform/linux/udev_hotplug_monitor.hpp"
#include "devmgr/runtime/logging.hpp"

namespace {

struct Options {
    std::string bus = "system";
    std::string sysfsRoot = "/sys";
    std::string mountsPath = "/proc/self/mounts";
    std::string authority = "polkit";
    std::string stateDir = "/var/lib/devmgrd";
    std::string modprobeDir = "/etc/modprobe.d";
    bool valid = true;
};

Options parse(std::span<char*> args) {
    Options opts;
    // Flag → member table keeps the parser below the cognitive-complexity gate.
    const std::map<std::string_view, std::string*> flags = {{"--bus", &opts.bus},
                                                            {"--sysfs-root", &opts.sysfsRoot},
                                                            {"--mounts-path", &opts.mountsPath},
                                                            {"--authority", &opts.authority},
                                                            {"--state-dir", &opts.stateDir},
                                                            {"--modprobe-dir", &opts.modprobeDir}};
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto flag = flags.find(args[i]);
        if (flag == flags.end() || i + 1 == args.size()) {
            opts.valid = false;
            break;
        }
        *flag->second = args[++i];
    }
    if (opts.bus != "system" && opts.bus != "session") opts.valid = false;
    if (opts.authority != "polkit" && opts.authority != "allow-all" && opts.authority != "deny-all")
        opts.valid = false;
    return opts;
}

std::unique_ptr<devmgr::daemon::IAuthority> makeAuthority(const std::string& kind) {
    if (kind == "allow-all") return std::make_unique<devmgr::daemon::AllowAllAuthority>();
    if (kind == "deny-all") return std::make_unique<devmgr::daemon::DenyAllAuthority>();
    return std::make_unique<devmgr::daemon::PolkitAuthority>();
}

void warnNonDefaultSeams(const Options& opts) {
    if (opts.bus == "system" && opts.sysfsRoot == "/sys" &&
        opts.mountsPath == "/proc/self/mounts" && opts.authority == "polkit" &&
        opts.stateDir == "/var/lib/devmgrd" && opts.modprobeDir == "/etc/modprobe.d")
        return;
    spdlog::warn(
        "running with NON-DEFAULT seams (test/dev mode): bus={} sysfs={} mounts={} "
        "authority={} state-dir={} modprobe-dir={} — never use these in production",
        opts.bus, opts.sysfsRoot, opts.mountsPath, opts.authority, opts.stateDir, opts.modprobeDir);
}

// --version must exit before any bus/logging setup (release-versioning spec).
bool handleVersionFlag(std::span<char*> args) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (std::string_view(args[i]) == "--version") {
            std::cout << devmgr::core::versionLine("devmgrd") << "\n";
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace devmgr;
    const std::span<char*> rawArgs(argv, static_cast<std::size_t>(argc));
    if (handleVersionFlag(rawArgs)) return 0;
    const Options opts = parse(rawArgs);
    if (!opts.valid) {
        std::cerr << "usage: devmgrd [--bus system|session] [--sysfs-root PATH] "
                     "[--mounts-path PATH] [--authority polkit|allow-all|deny-all] "
                     "[--state-dir PATH] [--modprobe-dir PATH]\n";
        return 2;
    }
    devmgr::runtime::init();  // spdlog global setup — the repo's logging entry point
    warnNonDefaultSeams(opts);
    try {
        auto connection =
            opts.bus == "session"
                ? sdbus::createSessionBusConnection(sdbus::ServiceName{platform_linux::kBusName})
                : sdbus::createSystemBusConnection(sdbus::ServiceName{platform_linux::kBusName});
        platform_linux::SysfsDeviceController controller(opts.sysfsRoot);
        platform_linux::LinuxCriticalityProber prober(opts.sysfsRoot, opts.mountsPath);
        platform_linux::KmodDriverManager drivers({.sysfsRoot = opts.sysfsRoot});
        platform_linux::UdevDeviceEnumerator enumerator;
        daemon::StateStore store(opts.stateDir);
        if (auto loaded = store.load(); !loaded)
            spdlog::warn("state store load failed: {}", loaded.error().message);
        std::mutex applyMutex;
        auto authority = makeAuthority(opts.authority);
        daemon::JsonSnapshotStore snapshotStore(opts.stateDir + "/snapshots");
        daemon::SnapshotService snapshots(snapshotStore, store, enumerator, controller, prober,
                                          opts.modprobeDir);
        daemon::RequestProcessor processor(controller, prober, *authority, drivers, enumerator,
                                           store, snapshots, applyMutex, opts.sysfsRoot);
        daemon::EnforcementService enforcement(enumerator, controller, prober, store, applyMutex);
        enforcement.sweep();  // startup re-apply (spec §5.3)
        platform_linux::UdevHotplugMonitor monitor;
        if (auto started = monitor.start(
                [&enforcement](const pal::HotplugEvent& e) { enforcement.onHotplug(e); });
            !started)
            spdlog::warn("hotplug watch unavailable ({}): enforcement is sweep-only",
                         started.error().message);
        daemon::ManagerAdaptor adaptor(*connection, processor);
        spdlog::info("devmgrd serving {} (api {}) on the {} bus", platform_linux::kBusName,
                     platform_linux::kApiVersion, opts.bus);
        connection->enterEventLoop();
        monitor.stop();
    } catch (const std::exception& e) {
        spdlog::error("devmgrd failed: {}", e.what());
        return 1;
    }
    return 0;
}
