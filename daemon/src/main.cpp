#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <sdbus-c++/sdbus-c++.h>
#include <spdlog/spdlog.h>

#include "daemon/src/manager_adaptor.hpp"
#include "daemon/src/polkit_authority.hpp"
#include "devmgr/daemon/authority.hpp"
#include "devmgr/daemon/request_processor.hpp"
#include "devmgr/platform/linux/dbus_contract.hpp"
#include "devmgr/platform/linux/linux_criticality_prober.hpp"
#include "devmgr/platform/linux/sysfs_device_controller.hpp"
#include "devmgr/runtime/logging.hpp"

namespace {

struct Options {
    std::string bus = "system";
    std::string sysfsRoot = "/sys";
    std::string mountsPath = "/proc/self/mounts";
    std::string authority = "polkit";
    bool valid = true;
};

Options parse(std::span<char*> args) {
    Options opts;
    // Flag → member table keeps the parser below the cognitive-complexity gate.
    const std::map<std::string_view, std::string*> flags = {{"--bus", &opts.bus},
                                                            {"--sysfs-root", &opts.sysfsRoot},
                                                            {"--mounts-path", &opts.mountsPath},
                                                            {"--authority", &opts.authority}};
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

}  // namespace

int main(int argc, char** argv) {
    using namespace devmgr;
    const Options opts = parse(std::span<char*>(argv, static_cast<std::size_t>(argc)));
    if (!opts.valid) {
        std::cerr << "usage: devmgrd [--bus system|session] [--sysfs-root PATH] "
                     "[--mounts-path PATH] [--authority polkit|allow-all|deny-all]\n";
        return 2;
    }
    devmgr::runtime::init();  // spdlog global setup — the repo's logging entry point
    if (opts.bus != "system" || opts.sysfsRoot != "/sys" ||
        opts.mountsPath != "/proc/self/mounts" || opts.authority != "polkit") {
        spdlog::warn(
            "running with NON-DEFAULT seams (test/dev mode): bus={} sysfs={} mounts={} "
            "authority={} — never use these in production",
            opts.bus, opts.sysfsRoot, opts.mountsPath, opts.authority);
    }
    try {
        auto connection =
            opts.bus == "session"
                ? sdbus::createSessionBusConnection(sdbus::ServiceName{platform_linux::kBusName})
                : sdbus::createSystemBusConnection(sdbus::ServiceName{platform_linux::kBusName});
        platform_linux::SysfsDeviceController controller(opts.sysfsRoot);
        platform_linux::LinuxCriticalityProber prober(opts.sysfsRoot, opts.mountsPath);
        auto authority = makeAuthority(opts.authority);
        daemon::RequestProcessor processor(controller, prober, *authority, opts.sysfsRoot);
        daemon::ManagerAdaptor adaptor(*connection, processor);
        spdlog::info("devmgrd serving {} on the {} bus", platform_linux::kBusName, opts.bus);
        connection->enterEventLoop();
    } catch (const std::exception& e) {
        spdlog::error("devmgrd failed: {}", e.what());
        return 1;
    }
    return 0;
}
