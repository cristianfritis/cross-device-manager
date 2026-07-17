#include "devmgr/daemon/atomic_file.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <ctime>
#include <fstream>

namespace devmgr::daemon {
namespace fs = std::filesystem;

namespace {
// Opens `path` with `flags`, fsyncs it, and closes it, so callers can make a
// prior write (file contents or a rename) durable across a crash/power loss
// (spec §5.2: "atomic tmp+fsync+rename"). Never leaves the fd open.
core::Result<void> syncFd(const std::string& path, int flags) {
    const int fd = ::open(path.c_str(), flags);  // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (fd < 0) return core::makeError(core::Error::Code::Io, "open for fsync failed: " + path);
    if (::fsync(fd) != 0) {
        ::close(fd);
        return core::makeError(core::Error::Code::Io, "fsync failed: " + path);
    }
    if (::close(fd) != 0) return core::makeError(core::Error::Code::Io, "close failed: " + path);
    return {};
}
}  // namespace

// fileName is always a short store-owned name and content a JSON document;
// call sites read naturally, so a swap isn't a realistic mistake.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
core::Result<void> atomicWriteFile(const fs::path& dir, const std::string& fileName,
                                   const std::string& content) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = dir / fileName;
    const fs::path tmp = dir / (fileName + ".tmp");
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return core::makeError(core::Error::Code::Io, "cannot write " + tmp.string());
        out << content;
        out.flush();
        if (!out) {
            fs::remove(tmp, ec);
            return core::makeError(core::Error::Code::Io, "write failed: " + tmp.string());
        }
    }  // ofstream closed here — safe to reopen and fsync its contents.
    if (auto r = syncFd(tmp.string(), O_WRONLY | O_CLOEXEC); !r) {
        fs::remove(tmp, ec);
        return r;
    }
    fs::rename(tmp, file, ec);  // atomic on POSIX
    if (ec) {
        const auto msg = ec.message();
        fs::remove(tmp, ec);
        return core::makeError(core::Error::Code::Io, "rename failed: " + msg);
    }
    // fsync the directory entry too, so the rename itself survives a crash.
    if (auto r = syncFd(dir.string(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); !r) return r;
    return {};
}

std::string quarantineFile(const fs::path& dir, const std::string& fileName) {
    std::error_code ec;
    const std::string base = fileName + ".bad-" + std::to_string(std::time(nullptr));
    std::string badName = base;
    for (int n = 1; fs::exists(dir / badName, ec); ++n) {
        badName = base;
        badName += '.';
        badName += std::to_string(n);
    }
    fs::rename(dir / fileName, dir / badName, ec);
    return badName;
}

}  // namespace devmgr::daemon
