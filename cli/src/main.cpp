#include <cstddef>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "cli/src/cli.hpp"
#include "devmgr/core/version.hpp"
#include "devmgr/pal/platform_backends.hpp"
#include "devmgr/runtime/event_bus.hpp"

// devmgr: the minimal recovery CLI (snapshot-cli spec). Zero UI dependencies, so
// it works on a bare console when the TUI/GUI are unusable. All verb logic lives
// in cli::run(); main only parses the global flags, asks the platform for its
// backend set, and forwards the arguments. It names no platform type.
namespace {

struct GlobalFlags {
    devmgr::pal::BackendOptions options;
    std::size_t consumed = 0;  // tokens the flag parse ate
    int exitCode = -1;         // >= 0 means stop and exit with this code
};

// Optional leading `--bus system|session` selects the privileged endpoint (the
// remaining tokens start at the command word). Session is the private endpoint
// the integration tests stand up; production uses the system bus. The flag
// spelling is a published CLI contract and stays as it is.
GlobalFlags parseGlobalFlags(const std::vector<std::string>& tokens, std::ostream& err) {
    using devmgr::pal::BackendOptions;
    GlobalFlags flags;
    if (tokens.empty() || tokens.front() != "--bus") return flags;
    if (tokens.size() < 2) {
        err << "devmgr: --bus needs a value (system|session)\n";
        flags.exitCode = devmgr::cli::kUsage;
        return flags;
    }
    const std::string& value = tokens[1];
    if (value == "system") {
        flags.options.privilegedEndpoint = BackendOptions::PrivilegedEndpoint::Default;
    } else if (value == "session") {
        flags.options.privilegedEndpoint = BackendOptions::PrivilegedEndpoint::Private;
    } else {
        err << "devmgr: unknown bus '" << value << "' (want system|session)\n";
        flags.exitCode = devmgr::cli::kUsage;
        return flags;
    }
    flags.consumed = 2;
    return flags;
}

}  // namespace

int main(int argc, char** argv) {
    // std::span over argv (daemon/src/main.cpp idiom) so argument handling
    // stays bounds-checked — no raw pointer arithmetic. Drop argv[0].
    const std::span<char*> raw(argv, static_cast<std::size_t>(argc));
    std::vector<std::string> tokens;
    for (std::size_t i = 1; i < raw.size(); ++i) tokens.emplace_back(raw[i]);
    // --version must exit before any backend construction
    // (release-versioning spec).
    if (!tokens.empty() && tokens.front() == "--version") {
        std::cout << devmgr::core::versionLine("devmgr") << "\n";
        return 0;
    }
    auto flags = parseGlobalFlags(tokens, std::cerr);
    if (flags.exitCode >= 0) return flags.exitCode;
    auto options = flags.options;

    const std::vector<std::string> args(
        tokens.begin() + static_cast<std::ptrdiff_t>(flags.consumed), tokens.end());
    try {
        // Owned for the whole run: the backends hold a reference to it.
        devmgr::runtime::EventBus bus;
        options.eventBus = &bus;
        auto backends = devmgr::pal::PlatformBackends::create(options);
        if (!backends) {
            std::cerr << "devmgr: cannot start: " << backends.error().message << "\n";
            return devmgr::cli::kFailed;
        }
        return devmgr::cli::run((*backends)->backends().privileged, args, std::cout, std::cerr);
    } catch (const std::exception& e) {
        // Belt-and-suspenders: every channel verb already catches sdbus errors
        // and returns a Result, so nothing should escape — but a recovery tool
        // must never crash with a stack trace (spec "no stack traces").
        std::cerr << "devmgr: " << e.what() << "\n";
        return devmgr::cli::kFailed;
    }
}
