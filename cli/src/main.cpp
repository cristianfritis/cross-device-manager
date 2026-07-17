#include <cstddef>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "cli/src/cli.hpp"
#include "devmgr/platform/linux/dbus_privileged_channel.hpp"

// devmgr: the minimal recovery CLI (snapshot-cli spec). A thin D-Bus client
// over DbusPrivilegedChannel with zero UI dependencies, so it works on a bare
// console when the TUI/GUI are unusable. All verb logic lives in cli::run();
// main only picks the bus, builds the channel, and forwards the arguments.
int main(int argc, char** argv) {
    using devmgr::platform_linux::DbusPrivilegedChannel;

    // std::span over argv (daemon/src/main.cpp idiom) so argument handling
    // stays bounds-checked — no raw pointer arithmetic. Drop argv[0].
    const std::span<char*> raw(argv, static_cast<std::size_t>(argc));
    std::vector<std::string> tokens;
    for (std::size_t i = 1; i < raw.size(); ++i) tokens.emplace_back(raw[i]);
    auto bus = DbusPrivilegedChannel::Bus::System;

    // Optional leading `--bus system|session` selects the connection (the
    // remaining tokens start at the `snapshot` command word). Session is for
    // the private-bus integration tests; production uses the system bus.
    std::size_t start = 0;
    if (!tokens.empty() && tokens.front() == "--bus") {
        if (tokens.size() < 2) {
            std::cerr << "devmgr: --bus needs a value (system|session)\n";
            return devmgr::cli::kUsage;
        }
        const std::string& value = tokens[1];
        if (value == "system") {
            bus = DbusPrivilegedChannel::Bus::System;
        } else if (value == "session") {
            bus = DbusPrivilegedChannel::Bus::Session;
        } else {
            std::cerr << "devmgr: unknown bus '" << value << "' (want system|session)\n";
            return devmgr::cli::kUsage;
        }
        start = 2;
    }

    const std::vector<std::string> args(tokens.begin() + static_cast<std::ptrdiff_t>(start),
                                        tokens.end());
    try {
        DbusPrivilegedChannel channel(bus);
        return devmgr::cli::run(channel, args, std::cout, std::cerr);
    } catch (const std::exception& e) {
        // Belt-and-suspenders: every channel verb already catches sdbus errors
        // and returns a Result, so nothing should escape — but a recovery tool
        // must never crash with a stack trace (spec "no stack traces").
        std::cerr << "devmgr: " << e.what() << "\n";
        return devmgr::cli::kFailed;
    }
}
