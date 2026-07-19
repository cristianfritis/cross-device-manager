#include "devmgr/core/snapshot_diff.hpp"

#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace devmgr::core {
namespace {

// modprobe.d directives devmgr understands, reduced to one state string per
// module. Anything else in the file (comments, unknown keywords) is invisible
// here and surfaces as a modprobe-kind row instead.
using ModuleStates = std::map<std::string, std::string>;

std::vector<std::string> tokenize(const std::string& line) {
    const auto comment = line.find('#');
    std::istringstream in(comment == std::string::npos ? line : line.substr(0, comment));
    std::vector<std::string> tokens;
    for (std::string token; in >> token;) tokens.push_back(std::move(token));
    return tokens;
}

void appendState(std::string& state, const std::string& part) {
    if (!state.empty()) state += ", ";
    state += part;
}

std::string joinFrom(const std::vector<std::string>& tokens, std::size_t first) {
    std::string joined;
    for (std::size_t i = first; i < tokens.size(); ++i) {
        if (!joined.empty()) joined += " ";
        joined += tokens[i];
    }
    return joined;
}

void collectDirectives(const std::string& content, ModuleStates& states) {
    std::istringstream in(content);
    for (std::string line; std::getline(in, line);) {
        const auto tokens = tokenize(line);
        if (tokens.size() < 2) continue;
        const std::string& keyword = tokens[0];
        const std::string& module = tokens[1];
        if (keyword == "blacklist") {
            appendState(states[module], kDiffStateBlacklisted);
        } else if (keyword == "options" && tokens.size() > 2) {
            appendState(states[module], "options: " + joinFrom(tokens, 2));
        } else if (keyword == "install" && tokens.size() > 2) {
            appendState(states[module], "install: " + joinFrom(tokens, 2));
        }
    }
}

// Directives across every devmgr-owned file, so a blacklist moving between
// files reads as one unchanged module rather than two module changes.
ModuleStates moduleStates(const std::map<std::string, std::string>& files) {
    ModuleStates states;
    for (const auto& [name, content] : files) collectDirectives(content, states);
    return states;
}

ModuleStates fileModuleStates(const std::string& content) {
    ModuleStates states;
    collectDirectives(content, states);
    return states;
}

template <class Map>
std::set<std::string> unionKeys(const Map& lhs, const Map& rhs) {
    std::set<std::string> keys;
    for (const auto& [key, _] : lhs) keys.insert(key);
    for (const auto& [key, _] : rhs) keys.insert(key);
    return keys;
}

std::string stateOr(const ModuleStates& states, const std::string& key, const char* fallback) {
    const auto it = states.find(key);
    return it == states.end() ? std::string{fallback} : it->second;
}

// Device entries keyed by label. Presence in a payload means devmgr has the
// device disabled; absence means it is enabled as far as devmgr is concerned.
std::map<std::string, std::string> deviceStates(const std::vector<DisabledDeviceEntry>& entries) {
    std::map<std::string, std::string> states;
    for (const auto& entry : entries) states[deviceEntryLabel(entry)] = deviceEntryState(entry);
    return states;
}

void diffDevices(const SnapshotPayload& base, const SnapshotPayload& target,
                 std::vector<SnapshotDiffEntry>& out) {
    const auto before = deviceStates(base.entries);
    const auto after = deviceStates(target.entries);
    for (const auto& key : unionKeys(before, after)) {
        auto b = stateOr(before, key, kDiffStateEnabled);
        auto a = stateOr(after, key, kDiffStateEnabled);
        if (b == a) continue;
        out.push_back(
            {.kind = kDiffKindDevice, .key = key, .before = std::move(b), .after = std::move(a)});
    }
}

void diffModules(const SnapshotPayload& base, const SnapshotPayload& target,
                 std::vector<SnapshotDiffEntry>& out) {
    const auto before = moduleStates(base.modprobeFiles);
    const auto after = moduleStates(target.modprobeFiles);
    for (const auto& key : unionKeys(before, after)) {
        auto b = stateOr(before, key, kDiffStateAbsent);
        auto a = stateOr(after, key, kDiffStateAbsent);
        if (b == a) continue;
        out.push_back(
            {.kind = kDiffKindModule, .key = key, .before = std::move(b), .after = std::move(a)});
    }
}

void diffModprobeFiles(const SnapshotPayload& base, const SnapshotPayload& target,
                       std::vector<SnapshotDiffEntry>& out) {
    const auto& before = base.modprobeFiles;
    const auto& after = target.modprobeFiles;
    for (const auto& name : unionKeys(before, after)) {
        const auto b = before.find(name);
        const auto a = after.find(name);
        const bool inBase = b != before.end();
        const bool inTarget = a != after.end();
        if (inBase && inTarget) {
            // Directive-level changes are already module rows; only report the
            // file when its bytes moved without its directives moving.
            if (b->second == a->second) continue;
            if (fileModuleStates(b->second) != fileModuleStates(a->second)) continue;
            out.push_back({.kind = kDiffKindModprobe,
                           .key = name,
                           .before = kDiffStatePresent,
                           .after = kDiffStateEdited});
            continue;
        }
        out.push_back({.kind = kDiffKindModprobe,
                       .key = name,
                       .before = inBase ? kDiffStatePresent : kDiffStateAbsent,
                       .after = inTarget ? kDiffStatePresent : kDiffStateAbsent});
    }
}

}  // namespace

std::string deviceEntryLabel(const DisabledDeviceEntry& entry) {
    const auto& key = entry.key;
    if (key.position.empty() && key.vendorId.empty() && key.productId.empty())
        return entry.lastSysfsPath.empty() ? std::string{"unknown device"} : entry.lastSysfsPath;
    std::string label = key.bus.empty() ? std::string{"device"} : key.bus;
    if (!key.vendorId.empty() || !key.productId.empty())
        label += " " + key.vendorId + ":" + key.productId;
    if (!key.position.empty())
        label += " @" + key.position;
    else if (!entry.lastSysfsPath.empty())
        label += " @" + entry.lastSysfsPath;
    if (!key.serial.empty()) label += " #" + key.serial;
    return label;
}

std::string deviceEntryState(const DisabledDeviceEntry& entry) {
    return entry.mechanism.empty() ? std::string{"disabled"} : "disabled (" + entry.mechanism + ")";
}

SnapshotDiff diffPayloads(const SnapshotPayload& base, const SnapshotPayload& target) {
    SnapshotDiff diff;
    diffDevices(base, target, diff.entries);
    diffModules(base, target, diff.entries);
    diffModprobeFiles(base, target, diff.entries);
    return diff;
}

}  // namespace devmgr::core
