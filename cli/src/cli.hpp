#pragma once
#include <iosfwd>
#include <string>
#include <vector>

#include "devmgr/pal/capabilities.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::cli {

// Process exit codes (snapshot-cli spec "Exit codes and output"). A recovery
// tool's caller scripts these, so they are a stable contract.
inline constexpr int kOk = 0;             // success
inline constexpr int kUsage = 1;          // usage error (bad command / arguments)
inline constexpr int kNotFound = 2;       // no such snapshot / ambiguous prefix
inline constexpr int kNotAuthorized = 3;  // polkit denied the mutating verb
inline constexpr int kUnreachable = 4;    // devmgrd is not answering on the bus
inline constexpr int kFailed = 5;         // the daemon reached but the operation failed

// Everything one invocation may reach. Both members are references into the
// platform's backend set, so this type names no platform and owns nothing.
//
// The two verb families need different halves of it and must not pay for each
// other: the snapshot verbs go through the privileged channel, and the
// inventory verbs read the enumerator directly with no helper and no daemon
// (design D6). `capabilities` decides which families this platform offers at
// all, read from the descriptor rather than by attempting a call.
struct Context {
    pal::IPrivilegedChannel& channel;
    pal::IDeviceEnumerator& enumerator;
    pal::PlatformCapabilities capabilities;
};

// Runs one CLI invocation. `args` are the tokens after argv[0] (i.e. starting
// at the command word); the caller (main) strips the global `--bus` flag first
// because it selects the channel endpoint. All I/O flows through the injected
// backends and the two streams, so the whole surface is unit-testable with
// fakes and no bus. Returns the process exit code (see the constants above).
int run(const Context& context, const std::vector<std::string>& args, std::ostream& out,
        std::ostream& err);

// Snapshot-only overload, for callers that have a channel and nothing else.
// Equivalent to a Context whose platform reports no device enumerator, so the
// inventory verbs are not offered — the same behaviour a platform without one
// gets, rather than a special case.
int run(pal::IPrivilegedChannel& channel, const std::vector<std::string>& args, std::ostream& out,
        std::ostream& err);

}  // namespace devmgr::cli
