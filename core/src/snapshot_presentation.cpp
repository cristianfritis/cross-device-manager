#include "devmgr/core/snapshot_presentation.hpp"

#include <array>
#include <cstdio>

namespace devmgr::core {
namespace {

// Widest kind is "modprobe" (8); one trailing space separates it from the key.
constexpr int kKindColumnWidth = 9;

// Item statuses as SnapshotService reports them. "ok" is the only converged
// one; everything else needs to reach the user with its reason.
constexpr const char* kStatusOk = "ok";
constexpr const char* kStatusGuardRefused = "guard-refused";
constexpr const char* kStatusFailed = "failed";
constexpr const char* kStatusDeviceAbsent = "device-absent";

}  // namespace

std::string diffEntryLine(const SnapshotDiffEntry& entry) {
    static constexpr std::size_t kLineBufferSize = 256;
    std::array<char, kLineBufferSize> line{};
    // Fixed-column printf shape, the same byte-frozen idiom SnapshotsVM uses
    // for list rows.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    std::snprintf(line.data(), line.size(), "%-*s%s: %s -> %s", kKindColumnWidth,
                  entry.kind.c_str(), entry.key.c_str(), entry.before.c_str(), entry.after.c_str());
    return {line.data()};
}

std::vector<std::string> diffLines(const SnapshotDiff& diff) {
    if (diff.identical()) return {"No differences."};
    std::vector<std::string> out;
    out.reserve(diff.entries.size());
    for (const auto& entry : diff.entries) out.push_back(diffEntryLine(entry));
    return out;
}

std::string chainMarkers(const SnapshotChainRow& row) {
    std::vector<std::string> marks;
    if (row.chainStart) marks.emplace_back("chain start");
    if (row.head) marks.emplace_back("HEAD");
    if (row.lastGood) marks.emplace_back("last good");
    if (marks.empty()) return "";
    std::string out = "  [";
    for (std::size_t i = 0; i < marks.size(); ++i) {
        if (i > 0) out += ", ";
        out += marks[i];
    }
    return out + "]";
}

std::vector<std::string> restorePreviewChangeLines(const std::optional<SnapshotDiff>& diff) {
    if (!diff) return {"What will change is unavailable — the diff could not be computed."};
    if (diff->identical())
        return {"This snapshot already matches the current state; nothing would change."};
    std::vector<std::string> out;
    out.emplace_back("Differences (snapshot -> current state):");
    for (const auto& line : diffLines(*diff)) out.push_back("  " + line);
    out.emplace_back("Restoring re-applies the snapshot side of every row above.");
    return out;
}

std::string restorePreviewConvergenceNote() {
    return "Convergence may be partial: the critical-device guard can refuse individual items. "
           "The restore still completes and reports every item.";
}

std::string restoreSummary(const RestoreOutcome& outcome) {
    std::size_t ok = 0;
    std::size_t refused = 0;
    std::size_t failed = 0;
    std::size_t absent = 0;
    for (const auto& item : outcome.items) {
        if (item.status == kStatusOk)
            ++ok;
        else if (item.status == kStatusGuardRefused)
            ++refused;
        else if (item.status == kStatusFailed)
            ++failed;
        else if (item.status == kStatusDeviceAbsent)
            ++absent;
    }
    std::string text = "Restored " + snapshotShortId(outcome.snapshotId) + ": ";
    if (outcome.items.empty()) {
        text += "no changes needed";
    } else {
        text += std::to_string(ok) + " ok";
        if (refused > 0) text += ", " + std::to_string(refused) + " guard-refused";
        if (failed > 0) text += ", " + std::to_string(failed) + " failed";
        if (absent > 0) text += ", " + std::to_string(absent) + " device-absent";
    }
    text += "; safety snapshot " + snapshotShortId(outcome.safetySnapshotId);
    return text;
}

std::string restoreRecoveryCommand(const RestoreOutcome& outcome) {
    return "devmgr snapshot restore " + snapshotShortId(outcome.safetySnapshotId);
}

std::vector<std::string> restoreGuidanceLines(const RestoreOutcome& outcome) {
    std::vector<std::string> unconverged;
    for (const auto& item : outcome.items) {
        if (item.status == kStatusOk) continue;
        // Status first so the column scans; then what it happened to, what was
        // being attempted, and why it did not converge.
        std::string line = "  " + item.status + "  " + item.subject + " (" + item.action + ")";
        if (!item.detail.empty()) line += ": " + item.detail;
        unconverged.push_back(std::move(line));
    }
    if (unconverged.empty()) return {};

    std::vector<std::string> out;
    out.push_back("Restore of " + snapshotShortId(outcome.snapshotId) + " left " +
                  std::to_string(unconverged.size()) +
                  (unconverged.size() == 1 ? " item unconverged:" : " items unconverged:"));
    out.insert(out.end(), unconverged.begin(), unconverged.end());
    out.push_back("The state from before this restore is kept as safety snapshot " +
                  snapshotShortId(outcome.safetySnapshotId) + ".");
    out.push_back("To go back, run: " + restoreRecoveryCommand(outcome));
    return out;
}

}  // namespace devmgr::core
