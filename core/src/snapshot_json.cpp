#include "devmgr/core/snapshot_json.hpp"

#include <nlohmann/json.hpp>

namespace devmgr::core {
using nlohmann::json;

namespace {

json metaToJson(const SnapshotMeta& m) {
    return json{{"id", m.id},
                {"parent", m.parent ? json(*m.parent) : json(nullptr)},
                {"createdAtUtc", m.createdAtUtc},
                {"trigger", to_string(m.trigger)},
                {"reason", {{"verb", m.reason.verb}, {"subject", m.reason.subject}}},
                {"corrupt", m.health != SnapshotHealth::Ok},
                {"health", to_string(m.health)},
                {"entryCount", m.entryCount},
                {"modprobeFileCount", m.modprobeFileCount}};
}

SnapshotMeta metaFromJson(const json& j) {  // throws json::exception on bad shape
    SnapshotMeta m;
    m.id = j.at("id");
    if (j.at("parent").is_string()) m.parent = j["parent"].get<std::string>();
    m.createdAtUtc = j.at("createdAtUtc");
    if (const auto t = snapshotTriggerFrom(j.at("trigger").get<std::string>())) m.trigger = *t;
    m.reason.verb = j.at("reason").at("verb");
    m.reason.subject = j.at("reason").at("subject");
    const std::string health = j.at("health");
    if (health == "corrupt") m.health = SnapshotHealth::Corrupt;
    if (health == "unsupported") m.health = SnapshotHealth::Unsupported;
    m.entryCount = j.at("entryCount");
    m.modprobeFileCount = j.at("modprobeFileCount");
    return m;
}

}  // namespace

std::string snapshotListToJson(const std::vector<SnapshotMeta>& metas) {
    json arr = json::array();
    for (const auto& m : metas) arr.push_back(metaToJson(m));
    return arr.dump();
}

Result<std::vector<SnapshotMeta>> snapshotListFromJson(const std::string& text) {
    const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_array())
        return makeError(Error::Code::Io, "malformed snapshot list JSON");
    std::vector<SnapshotMeta> out;
    try {
        for (const auto& j : doc) out.push_back(metaFromJson(j));
    } catch (const nlohmann::json::exception& e) {
        return makeError(Error::Code::Io, std::string("malformed snapshot list JSON: ") + e.what());
    }
    return out;
}

std::string restoreOutcomeToJson(const RestoreOutcome& outcome) {
    json items = json::array();
    for (const auto& i : outcome.items) {
        items.push_back({{"subject", i.subject},
                         {"action", i.action},
                         {"status", i.status},
                         {"detail", i.detail}});
    }
    return json{{"snapshotId", outcome.snapshotId},
                {"safetySnapshotId", outcome.safetySnapshotId},
                {"items", std::move(items)}}
        .dump();
}

std::string snapshotDiffToJson(const SnapshotDiff& diff) {
    json entries = json::array();
    for (const auto& e : diff.entries) {
        entries.push_back(
            {{"kind", e.kind}, {"key", e.key}, {"before", e.before}, {"after", e.after}});
    }
    return json{{"baseId", diff.baseId},
                {"targetId", diff.targetId},
                {"differences", !diff.identical()},
                {"entries", std::move(entries)}}
        .dump();
}

Result<SnapshotDiff> snapshotDiffFromJson(const std::string& text) {
    const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object())
        return makeError(Error::Code::Io, "malformed snapshot diff JSON");
    SnapshotDiff out;
    try {
        out.baseId = doc.at("baseId");
        out.targetId = doc.at("targetId");
        for (const auto& j : doc.at("entries")) {
            out.entries.push_back({.kind = j.at("kind"),
                                   .key = j.at("key"),
                                   .before = j.at("before"),
                                   .after = j.at("after")});
        }
    } catch (const nlohmann::json::exception& e) {
        return makeError(Error::Code::Io, std::string("malformed snapshot diff JSON: ") + e.what());
    }
    // `differences` is redundant with the entry count on the read side; a
    // document that disagrees with itself is malformed, not merely odd.
    if (doc.value("differences", !out.entries.empty()) == out.entries.empty())
        return makeError(Error::Code::Io, "snapshot diff JSON contradicts its differences marker");
    return out;
}

Result<RestoreOutcome> restoreOutcomeFromJson(const std::string& text) {
    const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object())
        return makeError(Error::Code::Io, "malformed restore outcome JSON");
    RestoreOutcome out;
    try {
        out.snapshotId = doc.at("snapshotId");
        out.safetySnapshotId = doc.at("safetySnapshotId");
        for (const auto& j : doc.at("items")) {
            out.items.push_back({.subject = j.at("subject"),
                                 .action = j.at("action"),
                                 .status = j.at("status"),
                                 .detail = j.at("detail")});
        }
    } catch (const nlohmann::json::exception& e) {
        return makeError(Error::Code::Io,
                         std::string("malformed restore outcome JSON: ") + e.what());
    }
    return out;
}

}  // namespace devmgr::core
