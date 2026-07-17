#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <unistd.h>  // ::close for the UniqueFd destructor

#include "devmgr/core/result.hpp"

namespace devmgr::platform_linux {

// A fwupd remote as the client sees it: id, kind ("directory" | "download" | …),
// and FilenameCache — the cab DIRECTORY for directory-kind remotes, the metadata
// FILE for download-kind remotes (verified on live fwupd). See spec §5.3.
struct RemoteRef {
    std::string id;
    std::string kind;
    std::string filenameCache;
};

// Move-only RAII wrapper around an owned file descriptor: closes on destruction,
// release() hands ownership to sdbus-c++ when it serializes an `h` (fd) argument.
class UniqueFd {
   public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    ~UniqueFd() {
        if (fd_ >= 0) ::close(fd_);
    }
    [[nodiscard]] int get() const { return fd_; }
    int release() {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

   private:
    int fd_ = -1;
};

struct CabFile {
    UniqueFd fd;
    std::uint64_t sizeBytes = 0;
};

// No I/O — string/kind checks only. Called at enumerate() to set release.localCab.
bool isLocallyResolvable(const std::vector<std::string>& locations,
                         const std::vector<RemoteRef>& remotes, const std::string& remoteId);

// Full M2 contract (spec §5.3): resolve a location to a local path, then open it
// ONCE with O_NOFOLLOW and validate (regular file, size bounds). Install-time only.
core::Result<CabFile> resolveAndOpenCab(const std::vector<std::string>& locations,
                                        const std::vector<RemoteRef>& remotes,
                                        const std::string& remoteId,
                                        std::uint64_t expectedSizeBytes);

}  // namespace devmgr::platform_linux
