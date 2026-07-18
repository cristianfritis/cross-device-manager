#include "devmgr/platform/linux/cab_resolver.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace devmgr::platform_linux {
namespace fs = std::filesystem;
namespace {

constexpr std::uint64_t kHardCapBytes = 512ULL * 1024 * 1024;  // spec §5.3
constexpr std::string_view kFileScheme = "file://";

const RemoteRef* findRemote(const std::vector<RemoteRef>& remotes, const std::string& id) {
    for (const auto& r : remotes)
        if (r.id == id) return &r;
    return nullptr;
}

// "" ⇒ not resolvable as a local path (never treat empty/unknown scheme as fs path).
std::string candidatePath(const std::string& loc, const std::vector<RemoteRef>& remotes,
                          const std::string& remoteId) {
    if (loc.empty()) return {};
    if (loc.starts_with(kFileScheme)) return loc.substr(kFileScheme.size());
    if (loc.find("://") != std::string::npos) return {};  // https, ftp, anything remote
    if (loc.front() == '/') return loc;
    const auto* remote = findRemote(remotes, remoteId);
    if (remote == nullptr || remote->kind != "directory") return {};  // download-remote
    // FilenameCache IS the cab directory for directory-kind remotes (verified on
    // live fwupd — for download remotes it is the metadata FILE).
    return (fs::path(remote->filenameCache) / loc).string();
}

bool escapesRoot(const fs::path& candidate, const fs::path& root) {
    std::error_code ec;
    const auto canon = fs::weakly_canonical(candidate, ec);
    if (ec) return true;
    const auto canonRoot = fs::weakly_canonical(root, ec);
    if (ec) return true;
    const auto rel = canon.lexically_relative(canonRoot);
    return rel.empty() || rel.native().starts_with("..");
}

// Relative locations must stay inside their directory-remote root; absolute and
// file:// paths are allowed as-given (subject to the open-time checks). Returns an
// error message iff a relative location escapes, std::nullopt otherwise.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — raw loc vs resolved path
std::optional<std::string> relativeEscapeError(const std::string& loc, const std::string& path,
                                               const std::vector<RemoteRef>& remotes,
                                               const std::string& remoteId) {
    if (loc.front() == '/' || loc.starts_with(kFileScheme)) return std::nullopt;
    const auto* remote = findRemote(remotes, remoteId);
    if (remote == nullptr || escapesRoot(path, remote->filenameCache))
        return "cab location escapes remote directory: " + loc;
    return std::nullopt;
}

// Open ONCE — O_NOFOLLOW rejects symlinks; validate via fstat on the open fd
// (deleted-after-open is safe: the fd pins the inode).
core::Result<CabFile> openAndValidate(const std::string& path, std::uint64_t expectedSizeBytes) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) — ::open is a variadic POSIX call
    UniqueFd fd{::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
    if (fd.get() < 0)
        return core::makeError(core::Error::Code::NotFound, "cannot open firmware file: " + path);
    struct stat st = {};
    if (::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode))
        return core::makeError(core::Error::Code::Unsupported,
                               "firmware location is not a regular file: " + path);
    const auto size = static_cast<std::uint64_t>(st.st_size);
    const std::uint64_t cap =
        expectedSizeBytes > 0
            ? std::min<std::uint64_t>(expectedSizeBytes + expectedSizeBytes / 2, kHardCapBytes)
            : kHardCapBytes;
    if (size == 0 || size > cap)
        return core::makeError(core::Error::Code::Unsupported,
                               "firmware file size out of bounds: " + path);
    return CabFile{.fd = std::move(fd), .sizeBytes = size};
}

}  // namespace

bool isLocallyResolvable(const std::vector<std::string>& locations,
                         const std::vector<RemoteRef>& remotes, const std::string& remoteId) {
    for (const auto& loc : locations)
        if (!candidatePath(loc, remotes, remoteId).empty()) return true;
    return false;
}

core::Result<CabFile> resolveAndOpenCab(const std::vector<std::string>& locations,
                                        const std::vector<RemoteRef>& remotes,
                                        const std::string& remoteId,
                                        std::uint64_t expectedSizeBytes) {
    for (const auto& loc : locations) {
        const auto path = candidatePath(loc, remotes, remoteId);
        if (path.empty()) continue;
        if (const auto escape = relativeEscapeError(loc, path, remotes, remoteId))
            return core::makeError(core::Error::Code::Unsupported, *escape);
        return openAndValidate(path, expectedSizeBytes);
    }
    return core::makeError(core::Error::Code::Unsupported,
                           "no locally-resolvable firmware location (run `fwupdmgr update`)");
}

}  // namespace devmgr::platform_linux
