#pragma once
#include <iosfwd>
#include <string>
#include <vector>

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

// Runs one CLI invocation against an already-connected privileged channel.
// `args` are the tokens after argv[0] (i.e. starting at the `snapshot` command
// word); the caller (main) strips the global `--bus` flag first because it
// selects the channel. All I/O flows through the injected channel and the two
// streams, so the whole surface is unit-testable with a fake channel and no
// bus. Returns the process exit code (see ExitCode).
int run(pal::IPrivilegedChannel& channel, const std::vector<std::string>& args, std::ostream& out,
        std::ostream& err);

}  // namespace devmgr::cli
