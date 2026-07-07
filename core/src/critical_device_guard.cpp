#include "devmgr/services/critical_device_guard.hpp"

#include <algorithm>
#include <vector>

namespace devmgr::services {
namespace {

// True iff `path` equals `prefix` or lies inside it, honoring the '/'
// boundary ("/sys/a/1-1" is NOT under "/sys/a/1-10").
bool isUnder(const std::string& path, const std::string& prefix) {
    if (prefix.empty() || path.size() < prefix.size()) return false;
    if (!path.starts_with(prefix)) return false;
    return path.size() == prefix.size() || path[prefix.size()] == '/';
}

bool anyUnder(const std::vector<std::string>& paths, const std::string& target) {
    return std::ranges::any_of(paths, [&](const std::string& p) { return isUnder(p, target); });
}

// True iff disabling `target` would remove EVERY entry in `paths` (and there
// is at least one to remove) — the "sole remaining keyboard/pointer" rule.
bool wouldRemoveAll(const std::vector<std::string>& paths, const std::string& target) {
    if (paths.empty()) return false;
    return std::ranges::all_of(paths, [&](const std::string& p) { return isUnder(p, target); });
}

}  // namespace

GuardVerdict evaluateDisable(const pal::CriticalityFacts& facts,
                             const std::string& targetSysfsPath) {
    if (anyUnder(facts.rootBackingPaths, targetSysfsPath))
        return {.allowed = false, .reason = "backs the root filesystem"};
    if (anyUnder(facts.bootBackingPaths, targetSysfsPath))
        return {.allowed = false, .reason = "backs the boot filesystem"};
    if (wouldRemoveAll(facts.keyboardPaths, targetSysfsPath))
        return {.allowed = false, .reason = "would disable the only keyboard"};
    if (wouldRemoveAll(facts.pointerPaths, targetSysfsPath))
        return {.allowed = false, .reason = "would disable the only pointer"};
    return {};
}

GuardVerdict evaluateModuleUnload(const pal::CriticalityFacts& facts,
                                  const ModuleUnloadFacts& module) {
    if (!module.holders.empty()) {
        std::string names;
        for (const auto& h : module.holders) {
            if (!names.empty()) names += ", ";
            names += h;
        }
        return {.allowed = false, .reason = "in use by " + names};
    }
    if (module.refCount > 0)
        return {.allowed = false,
                .reason = "in use (refcount " + std::to_string(module.refCount) + ")"};
    for (const auto& path : module.affectedDevicePaths) {
        const auto verdict = evaluateDisable(facts, path);
        if (!verdict.allowed)
            return {.allowed = false,
                    .reason = "module backs a critical device: " + verdict.reason};
    }
    return {};
}

}  // namespace devmgr::services
