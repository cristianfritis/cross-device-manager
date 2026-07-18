#include "devmgr/daemon/snapshot_store.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "devmgr/core/sha256.hpp"
#include "devmgr/daemon/atomic_file.hpp"
#include "devmgr/daemon/entry_json.hpp"

namespace devmgr::daemon {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

json payloadToJson(const core::SnapshotPayload& payload) {
    json entries = json::array();
    for (const auto& e : payload.entries) entries.push_back(entryToJson(e));
    return json{{"entries", std::move(entries)}, {"modprobe_files", payload.modprobeFiles}};
}

core::SnapshotPayload payloadFromJson(const json& j) {  // throws json::exception on bad shape
    core::SnapshotPayload payload;
    for (const auto& e : j.at("entries")) payload.entries.push_back(entryFromJson(e));
    payload.modprobeFiles = j.at("modprobe_files").get<std::map<std::string, std::string>>();
    return payload;
}

// Canonical payload serialization the id hashes over: compact dump of the
// payload object. nlohmann::json objects iterate in sorted key order, so the
// same payload always serializes to the same bytes.
std::string canonicalPayload(const core::SnapshotPayload& payload) {
    return payloadToJson(payload).dump();
}

std::string snapshotFileName(const std::string& id) {
    return id + ".json";
}

std::optional<std::string> stringField(const json& doc, const char* key) {
    if (doc.is_object() && doc.contains(key) && doc[key].is_string())
        return doc[key].get<std::string>();
    return std::nullopt;
}

// Best-effort meta from a document that failed integrity or format checks:
// pull whatever fields parse, fall back to defaults. Never throws.
core::SnapshotMeta bestEffortMeta(const json& doc, const std::string& fallbackId) {
    core::SnapshotMeta meta;
    meta.id = stringField(doc, "id").value_or(fallbackId);
    meta.parent = stringField(doc, "parent");
    if (doc.is_object() && doc.contains("createdAtUtc") && doc["createdAtUtc"].is_number_integer())
        meta.createdAtUtc = doc["createdAtUtc"];
    if (const auto t = core::snapshotTriggerFrom(stringField(doc, "trigger").value_or("")))
        meta.trigger = *t;
    const json reason = doc.is_object() && doc.contains("reason") ? doc["reason"] : json();
    meta.reason.verb = stringField(reason, "verb").value_or("");
    meta.reason.subject = stringField(reason, "subject").value_or("");
    return meta;
}

json parseFile(const fs::path& file) {  // discarded json on unreadable/invalid
    std::ifstream in(file);
    return json::parse(in, nullptr, /*allow_exceptions=*/false);
}

// "abc123.json" -> "abc123"; "abc123.json.bad-17..." -> "abc123".
std::string idFromFileName(const std::string& name) {
    return name.substr(0, name.find(".json"));
}

// Newest first: the HEAD->parent chain is authoritative for reachable
// snapshots; anything off-chain (orphans, corrupt, unsupported) follows,
// ordered by timestamp. A visited set guards against malformed cycles.
std::vector<core::SnapshotMeta> orderNewestFirst(std::vector<core::SnapshotMeta> metas,
                                                 std::optional<std::string> headId) {
    std::vector<core::SnapshotMeta> ordered;
    std::unordered_map<std::string, std::size_t> byId;
    for (std::size_t i = 0; i < metas.size(); ++i) byId.emplace(metas[i].id, i);
    std::unordered_set<std::string> taken;
    std::optional<std::string> cursor = std::move(headId);
    while (cursor && taken.insert(*cursor).second) {
        const auto it = byId.find(*cursor);
        if (it == byId.end()) break;  // dangling parent (pruned) — tolerated
        ordered.push_back(metas[it->second]);
        cursor = metas[it->second].parent;
    }
    std::vector<core::SnapshotMeta> rest;
    for (auto& m : metas)
        if (!taken.contains(m.id)) rest.push_back(std::move(m));
    std::ranges::stable_sort(
        rest, [](const auto& a, const auto& b) { return a.createdAtUtc > b.createdAtUtc; });
    ordered.insert(ordered.end(), std::make_move_iterator(rest.begin()),
                   std::make_move_iterator(rest.end()));
    return ordered;
}

// Quarantines <dir>/<name> and returns the Io error naming the evidence file.
// name is always snapshotFileName(id) at every call site — not swappable in practice.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
tl::unexpected<core::Error> quarantineAndFail(const fs::path& dir, const std::string& id,
                                              const std::string& name, const std::string& what) {
    const auto bad = quarantineFile(dir, name);
    return core::makeError(core::Error::Code::Io,
                           "snapshot " + id + " " + what + ", quarantined as " + bad);
}

// <id>.json is absent: either it was quarantined earlier (corrupt story) or
// the id is simply unknown.
// name is always snapshotFileName(id) at every call site — not swappable in practice.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
tl::unexpected<core::Error> missingSnapshotError(const fs::path& dir, const std::string& id,
                                                 const std::string& name) {
    std::error_code ec;
    const std::string badPrefix = name + ".bad-";
    std::string quarantined;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        auto other = entry.path().filename().string();
        if (other.starts_with(badPrefix)) {
            quarantined = std::move(other);
            break;
        }
    }
    if (!quarantined.empty())
        return core::makeError(core::Error::Code::Io,
                               "snapshot " + id + " is corrupt, quarantined as " + quarantined);
    return core::makeError(core::Error::Code::NotFound, "no snapshot " + id);
}

}  // namespace

JsonSnapshotStore::JsonSnapshotStore(std::string dirPath) : dir_(std::move(dirPath)) {}

core::Result<std::optional<std::string>> JsonSnapshotStore::headLocked() const {
    const fs::path file = fs::path(dir_) / "HEAD";
    std::error_code ec;
    if (!fs::exists(file, ec)) return std::optional<std::string>{};
    std::ifstream in(file);
    std::string id;
    in >> id;  // single token; trims whitespace/newline
    if (id.empty()) return std::optional<std::string>{};
    return std::optional<std::string>{std::move(id)};
}

core::Result<std::optional<std::string>> JsonSnapshotStore::head() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return headLocked();
}

core::Result<std::string> JsonSnapshotStore::put(const core::SnapshotPayload& payload,
                                                 core::SnapshotTrigger trigger,
                                                 const core::SnapshotReason& reason,
                                                 std::int64_t createdAtUtc) {
    const std::scoped_lock<std::mutex> lock(mutex_);
    std::string id = core::sha256Hex(canonicalPayload(payload));
    auto headId = headLocked();
    if (!headId) return tl::unexpected(headId.error());
    // Local ref: one deref through the expected, so the optional-access check
    // can track the check-then-use (house pattern, Phase 7 review).
    const std::optional<std::string>& headOpt = *headId;
    // Hash-dedupe: unchanged state is free — no new file, existing id reported.
    if (headOpt && *headOpt == id) return id;

    std::error_code ec;
    const bool exists = fs::exists(fs::path(dir_) / snapshotFileName(id), ec);
    if (!exists) {
        // Content-addressed files are immutable once written: if this payload
        // was seen before (state returned to an earlier content), the old
        // file keeps its original parent/metadata and only HEAD moves.
        json doc{{"formatVersion", core::kSnapshotFormatVersion},
                 {"id", id},
                 {"parent", headOpt ? json(*headOpt) : json(nullptr)},
                 {"createdAtUtc", createdAtUtc},
                 {"trigger", core::to_string(trigger)},
                 {"reason", {{"verb", reason.verb}, {"subject", reason.subject}}},
                 {"payload", payloadToJson(payload)}};
        if (auto w = atomicWriteFile(dir_, snapshotFileName(id), doc.dump(2)); !w)
            return tl::unexpected(w.error());
    }
    if (headOpt != id) {
        if (auto w = atomicWriteFile(dir_, "HEAD", id + "\n"); !w) return tl::unexpected(w.error());
    }
    if (!exists) pruneLocked();  // only a new file can push the auto count past the cap
    return id;
}

void JsonSnapshotStore::pruneLocked() {
    auto headId = headLocked();
    auto ordered = orderNewestFirst(scanLocked(), headId ? *headId : std::optional<std::string>{});
    std::size_t autosSeen = 0;
    for (const auto& m : ordered) {
        if (m.health != core::SnapshotHealth::Ok || m.trigger != core::SnapshotTrigger::Auto)
            continue;  // manual snapshots exempt; corrupt/unsupported never auto-deleted
        if (++autosSeen <= kKeepAutoSnapshots) continue;
        // Deleting the parent of the oldest survivor leaves a dangling parent
        // id; list() tolerates that by design.
        std::error_code ec;
        fs::remove(fs::path(dir_) / snapshotFileName(m.id), ec);
    }
}

core::Result<core::Snapshot> JsonSnapshotStore::readLocked(const std::string& id) {
    const std::string name = snapshotFileName(id);
    const fs::path file = fs::path(dir_) / name;
    std::error_code ec;
    if (!fs::exists(file, ec)) return missingSnapshotError(fs::path(dir_), id, name);
    const json doc = parseFile(file);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("formatVersion") ||
        !doc["formatVersion"].is_number_integer())
        return quarantineAndFail(fs::path(dir_), id, name, "is corrupt");
    if (doc["formatVersion"].get<int>() > core::kSnapshotFormatVersion)
        return core::makeError(core::Error::Code::Unsupported,
                               "snapshot " + id + " has a newer formatVersion; not touching it");
    core::Snapshot snap;
    try {
        snap.payload = payloadFromJson(doc.at("payload"));
        snap.meta = bestEffortMeta(doc, id);
    } catch (const nlohmann::json::exception&) {
        return quarantineAndFail(fs::path(dir_), id, name, "is corrupt");
    }
    if (core::sha256Hex(canonicalPayload(snap.payload)) != id || snap.meta.id != id)
        return quarantineAndFail(fs::path(dir_), id, name, "failed its integrity check");
    snap.meta.health = core::SnapshotHealth::Ok;
    snap.meta.entryCount = snap.payload.entries.size();
    snap.meta.modprobeFileCount = snap.payload.modprobeFiles.size();
    return snap;
}

core::Result<core::Snapshot> JsonSnapshotStore::read(const std::string& id) {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return readLocked(id);
}

std::vector<core::SnapshotMeta> JsonSnapshotStore::scanLocked() {
    std::vector<core::SnapshotMeta> metas;
    std::error_code ec;
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(dir_, ec))
        names.push_back(entry.path().filename().string());
    for (const auto& name : names) {
        if (name == "HEAD" || name.ends_with(".tmp")) continue;
        if (name.find(".bad-") != std::string::npos) {
            auto meta = bestEffortMeta(parseFile(fs::path(dir_) / name), idFromFileName(name));
            meta.health = core::SnapshotHealth::Corrupt;
            metas.push_back(std::move(meta));
            continue;
        }
        if (!name.ends_with(".json")) continue;
        metas.push_back(scanOneLocked(idFromFileName(name)));
    }
    return metas;
}

core::SnapshotMeta JsonSnapshotStore::scanOneLocked(const std::string& id) {
    auto snap = readLocked(id);  // verifies hash; quarantines on mismatch
    if (snap) return snap->meta;
    if (snap.error().code == core::Error::Code::Unsupported) {
        auto meta = bestEffortMeta(parseFile(fs::path(dir_) / snapshotFileName(id)), id);
        meta.health = core::SnapshotHealth::Unsupported;
        return meta;
    }
    // readLocked already quarantined the file; surface it as corrupt.
    auto meta = bestEffortMeta(json(), id);
    meta.health = core::SnapshotHealth::Corrupt;
    return meta;
}

core::Result<std::vector<core::SnapshotMeta>> JsonSnapshotStore::list() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    auto metas = scanLocked();
    auto headId = headLocked();
    return orderNewestFirst(std::move(metas), headId ? *headId : std::optional<std::string>{});
}

core::Result<void> JsonSnapshotStore::remove(const std::string& id) {
    const std::scoped_lock<std::mutex> lock(mutex_);
    auto snap = readLocked(id);  // NotFound / corrupt-quarantine / Unsupported all refuse here
    if (!snap) return tl::unexpected(snap.error());
    auto headId = headLocked();
    if (!headId) return tl::unexpected(headId.error());
    const std::optional<std::string>& headOpt = *headId;  // one deref (see put())
    if (headOpt && *headOpt == id) {
        // Move HEAD to the parent BEFORE deleting: a crash in between leaves
        // an orphaned-but-listed file, never a HEAD naming a missing one.
        const std::string next = snap->meta.parent.value_or("");
        if (auto w = atomicWriteFile(dir_, "HEAD", next.empty() ? "" : next + "\n"); !w) return w;
    }
    std::error_code ec;
    fs::remove(fs::path(dir_) / snapshotFileName(id), ec);
    if (ec) return core::makeError(core::Error::Code::Io, "delete failed: " + ec.message());
    return {};
}

}  // namespace devmgr::daemon
