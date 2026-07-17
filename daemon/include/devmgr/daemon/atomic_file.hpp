#pragma once
#include <filesystem>
#include <string>

#include "devmgr/core/result.hpp"

namespace devmgr::daemon {

// Shared file-discipline helpers for the daemon's persistent stores
// (state.json, snapshots). Extracted from StateStore so JsonSnapshotStore
// reuses the exact same crash-safety idioms instead of re-implementing them.

// Writes `content` to <dir>/<fileName> with the tmp+fsync+rename idiom
// (write <fileName>.tmp, flush, fsync file, rename, fsync directory).
// Creates `dir` if missing. A crash at any point leaves either the old file
// or the new file, never a torn one.
core::Result<void> atomicWriteFile(const std::filesystem::path& dir, const std::string& fileName,
                                   const std::string& content);

// Moves <dir>/<fileName> aside to "<fileName>.bad-<timestamp>[.n]" so corrupt
// evidence is never silently overwritten or destroyed. Every call gets a
// fresh, distinct name (numeric suffix on same-second collision). Returns the
// quarantine file name (not the full path); best-effort — rename errors are
// swallowed, matching the original StateStore behavior.
std::string quarantineFile(const std::filesystem::path& dir, const std::string& fileName);

}  // namespace devmgr::daemon
