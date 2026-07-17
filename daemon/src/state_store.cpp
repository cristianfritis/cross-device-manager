#include "devmgr/daemon/state_store.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "devmgr/services/device_key.hpp"

namespace devmgr::daemon {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
json toJson(const core::DisabledDeviceEntry& e) {
    return json{{"bus", e.key.bus},
                {"vendor_id", e.key.vendorId},
                {"product_id", e.key.productId},
                {"serial", e.key.serial},
                {"position", e.key.position},
                {"mechanism", e.mechanism},
                {"last_driver", e.lastDriver},
                {"last_sysfs_path", e.lastSysfsPath},
                {"disabled_at_utc", e.disabledAtUtc},
                {"guard_suspended", e.guardSuspended}};
}

core::DisabledDeviceEntry fromJson(const json& j) {
    core::DisabledDeviceEntry e;
    e.key = core::DeviceKey{.bus = j.at("bus"),
                            .vendorId = j.at("vendor_id"),
                            .productId = j.at("product_id"),
                            .serial = j.at("serial"),
                            .position = j.at("position")};
    e.mechanism = j.at("mechanism");
    e.lastDriver = j.at("last_driver");
    e.lastSysfsPath = j.at("last_sysfs_path");
    e.disabledAtUtc = j.at("disabled_at_utc");
    e.guardSuspended = j.at("guard_suspended");
    return e;
}

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

// Moves `file` aside so a corrupt/malformed state.json never gets silently
// overwritten or destroyed (spec §5.2). Every call gets a fresh, distinct
// name: timestamped, with a numeric suffix appended if a same-second
// quarantine already exists — so a second corruption never clobbers the
// first piece of evidence (Phase 5 review T4 m-4). dir is state_store's own
// directory (dir_), file is always dir/"state.json"; both call sites pass
// them in this fixed order, so a swap isn't a realistic mistake.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void quarantine(const fs::path& dir, const fs::path& file) {
    std::error_code ec;
    const auto stamp = std::to_string(std::time(nullptr));
    fs::path bad = dir / ("state.json.bad-" + stamp);
    for (int n = 1; fs::exists(bad, ec); ++n)
        bad = dir / ("state.json.bad-" + stamp + "." + std::to_string(n));
    fs::rename(file, bad, ec);
}
}  // namespace

StateStore::StateStore(std::string dirPath) : dir_(std::move(dirPath)) {}

core::Result<void> StateStore::load() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    entries_.clear();
    const fs::path file = fs::path(dir_) / "state.json";
    std::error_code ec;
    if (!fs::exists(file, ec)) return {};
    std::ifstream in(file);
    json doc = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("entries") ||
        !doc["entries"].is_array()) {
        // Never silently destroy evidence (spec §5.2); null/non-array "entries"
        // must quarantine too — iterating it as empty would silently drop every
        // persisted disable (Phase 5 review T4 m-5).
        quarantine(fs::path(dir_), file);
        return {};
    }
    std::vector<core::DisabledDeviceEntry> parsed;
    try {
        for (const auto& j : doc["entries"]) parsed.push_back(fromJson(j));
    } catch (const nlohmann::json::exception&) {
        // Structurally-valid JSON but wrong entry shape (wrong types, missing
        // keys, etc). Same graceful path as top-level corruption: never
        // silently destroy evidence, and never leave entries_ half-filled.
        quarantine(fs::path(dir_), file);
        return {};
    }
    entries_ = std::move(parsed);
    return {};
}

core::Result<void> StateStore::save() {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    json doc{{"version", 1}, {"entries", json::array()}};
    for (const auto& e : entries_) doc["entries"].push_back(toJson(e));
    const fs::path file = fs::path(dir_) / "state.json";
    const fs::path tmp = fs::path(dir_) / "state.json.tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return core::makeError(core::Error::Code::Io, "cannot write " + tmp.string());
        out << doc.dump(2);
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
    if (auto r = syncFd(dir_, O_RDONLY | O_DIRECTORY | O_CLOEXEC); !r) return r;
    return {};
}

core::Result<void> StateStore::upsert(const core::DisabledDeviceEntry& entry) {
    const std::scoped_lock<std::mutex> lock(mutex_);
    for (auto& e : entries_) {
        if (e.key == entry.key) {
            e = entry;
            return save();
        }
    }
    entries_.push_back(entry);
    return save();
}

core::Result<void> StateStore::remove(const core::DeviceKey& key) {
    const std::scoped_lock<std::mutex> lock(mutex_);
    std::erase_if(entries_, [&](const auto& e) { return e.key == key; });
    return save();
}

core::Result<void> StateStore::setGuardSuspended(const core::DeviceKey& key, bool suspended) {
    const std::scoped_lock<std::mutex> lock(mutex_);
    for (auto& e : entries_) {
        if (e.key == key) {
            e.guardSuspended = suspended;
            return save();
        }
    }
    return core::makeError(core::Error::Code::NotFound, "no entry for key");
}

core::Result<void> StateStore::setLastSysfsPath(const core::DeviceKey& key,
                                                const std::string& path) {
    const std::scoped_lock<std::mutex> lock(mutex_);
    for (auto& e : entries_) {
        if (e.key == key) {
            e.lastSysfsPath = path;
            return save();
        }
    }
    return core::makeError(core::Error::Code::NotFound, "no entry for key");
}

std::vector<core::DisabledDeviceEntry> StateStore::entries() const {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return entries_;
}

std::optional<core::DisabledDeviceEntry> StateStore::findFor(const core::Device& device) const {
    const std::scoped_lock<std::mutex> lock(mutex_);
    for (const auto& e : entries_) {
        if (services::matchesDevice(e.key, device) || e.lastSysfsPath == device.sysfsPath) return e;
    }
    return std::nullopt;
}

}  // namespace devmgr::daemon
