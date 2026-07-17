#include "devmgr/daemon/state_store.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "devmgr/daemon/atomic_file.hpp"
#include "devmgr/daemon/entry_json.hpp"
#include "devmgr/services/device_key.hpp"

namespace devmgr::daemon {
namespace fs = std::filesystem;
using nlohmann::json;

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
        quarantineFile(fs::path(dir_), "state.json");
        return {};
    }
    std::vector<core::DisabledDeviceEntry> parsed;
    try {
        for (const auto& j : doc["entries"]) parsed.push_back(entryFromJson(j));
    } catch (const nlohmann::json::exception&) {
        // Structurally-valid JSON but wrong entry shape (wrong types, missing
        // keys, etc). Same graceful path as top-level corruption: never
        // silently destroy evidence, and never leave entries_ half-filled.
        quarantineFile(fs::path(dir_), "state.json");
        return {};
    }
    entries_ = std::move(parsed);
    return {};
}

core::Result<void> StateStore::save() {
    json doc{{"version", 1}, {"entries", json::array()}};
    for (const auto& e : entries_) doc["entries"].push_back(entryToJson(e));
    return atomicWriteFile(fs::path(dir_), "state.json", doc.dump(2));
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

core::Result<void> StateStore::replaceAll(std::vector<core::DisabledDeviceEntry> entries) {
    const std::scoped_lock<std::mutex> lock(mutex_);
    entries_ = std::move(entries);
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
