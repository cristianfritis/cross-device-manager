#include "devmgr/app/modules_vm.hpp"

#include <algorithm>
#include <array>
#include <cctype>  // CONTROLLER AMENDMENT (D-5): tolower
#include <chrono>  // FIX ROUND 1 (i-1): wait_for coalescing check
#include <cstdint>
#include <cstdio>
#include <utility>

#include "devmgr/core/events.hpp"

namespace devmgr::app {
namespace {

// CONTROLLER AMENDMENT (D-9): shared helper instead of the two
// std::transform(..., ::tolower) call sites the plan specified — identical
// behavior for ASCII, and it removes real UB from passing a (possibly
// negative) plain `char` to ::tolower.
std::string toLowerCopy(std::string s) {
    std::ranges::transform(s, s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Comma-joined module holders, truncated to the fixed column width.
std::string joinHolders(const std::vector<std::string>& holders) {
    std::string joined;
    for (const auto& h : holders) {
        if (!joined.empty()) joined += ",";
        joined += h;
    }
    static constexpr std::size_t kHoldersMaxLen = 24;
    static constexpr std::size_t kHoldersTruncatedLen = 21;
    if (joined.size() > kHoldersMaxLen) joined = joined.substr(0, kHoldersTruncatedLen) + "…";
    return joined;
}

// Fixed-column module row (shared byte-for-byte with T11/T12; the emitted
// format — NOT the brief prose — is authoritative per CONTROLLER AMENDMENT D-6).
std::string formatModuleRow(const core::LoadedModule& m, const std::string& holders,
                            const char* signatureCell) {
    static constexpr std::size_t kRowBufferSize = 128;
    static constexpr std::uint64_t kBytesPerKb = 1024;
    std::array<char, kRowBufferSize> row{};
    // snprintf is the only practical way to reproduce the fixed printf-style
    // column alignment shared byte-for-byte with T11/T12.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    std::snprintf(row.data(), row.size(), "%-28s %8lluK %4ld  %-24s %s", m.name.c_str(),
                  static_cast<unsigned long long>(m.sizeBytes / kBytesPerKb), m.refCount,
                  holders.c_str(), signatureCell);
    return {row.data()};
}

// "yes (signer)" | "yes" | "NO" for a resolved driver.
std::string signatureLabel(const core::Driver& d) {
    if (!d.isSigned) return "NO";
    if (const auto& signer = d.signer) return "yes (" + *signer + ")";
    return "yes";
}

// Signature-cell text for the row column: "?" when the module could not be
// classified, otherwise signatureLabel().
std::string signatureCellFor(const core::Result<core::Driver>& info) {
    return info ? signatureLabel(*info) : std::string("?");
}

// Maps the signature-column cell text to a colouring state (design decision
// 1a). "NO" → Unsigned, a "yes…" cell → Signed; "?" (unclassifiable) and "…"
// (async pending) both → Undetermined. Kept next to signatureCellFor() so the
// two agree on what the cell strings mean.
ModuleSignature classifySignatureCell(const std::string& cell) {
    if (cell == "NO") return ModuleSignature::Unsigned;
    if (cell.starts_with("yes")) return ModuleSignature::Signed;
    return ModuleSignature::Undetermined;
}

// Restores the highlighted module by name after a rebuild, then clamps.
int restoreSelection(const std::vector<std::optional<std::string>>& rowNames,
                     const std::optional<std::string>& keep, int current, int rowCount) {
    if (keep) {
        if (auto it = std::ranges::find(rowNames, keep); it != rowNames.end())
            current = static_cast<int>(std::distance(rowNames.begin(), it));
    }
    return std::clamp(current, 0, rowCount - 1);
}

void appendModuleInfo(std::vector<std::string>& out, const core::Result<core::Driver>& info) {
    if (!info) return;
    if (!info->version.empty()) out.push_back("Version: " + info->version);
    if (!info->path.empty()) out.push_back("Path:    " + info->path);
    out.push_back("Signed:  " + signatureLabel(*info));
    if (info->dependencies.empty()) return;
    std::string deps;
    for (const auto& d : info->dependencies) {
        if (!deps.empty()) deps += ", ";
        deps += d;
    }
    out.push_back("Depends: " + deps);
}

void appendModprobeInfo(std::vector<std::string>& out, const core::Result<core::ModprobeInfo>& mp) {
    if (!mp) return;
    if (const auto& options = mp->options) out.push_back("Options: " + *options);
    if (mp->blacklisted) out.emplace_back("modprobe.d: blacklisted");
}

}  // namespace

ModulesVM::ModulesVM(ApplicationFacade& facade, runtime::EventBus& bus,
                     runtime::TaskScheduler& scheduler, IUiDispatcher& dispatcher)
    : facade_(facade), bus_(bus), scheduler_(scheduler), dispatcher_(dispatcher) {
    subModules_ = bus_.subscribe<core::ModulesChangedEvent>(
        [this](const core::ModulesChangedEvent&) { onModulesChanged(); });
}

ModulesVM::~ModulesVM() {
    // FIX ROUND 1 (i-2): clear the alive token BEFORE waiting. Any closure
    // already posted to a queuing dispatcher (FTXUI/Qt in T11/T12) checks
    // this and no-ops instead of touching a dead VM. Safe without a mutex:
    // the destructor and IUiDispatcher::post()'s execution both happen on the
    // UI thread and therefore serialize.
    alive_->store(false);
    // The worker lambda submitted to scheduler_ still captures `this`
    // directly (for facade_/dispatcher_/scheduler_) and is NOT alive_-token
    // guarded, so this wait must remain: it is the only thing that keeps
    // that worker's `this` access memory-safe.
    if (sigFill_.valid()) sigFill_.wait();
}

void ModulesVM::onModulesChanged() {
    if (rebuildQueued_.exchange(true)) return;  // coalesce bursts
    auto alive = alive_;
    dispatcher_.post([this, alive] {
        if (!alive->load()) return;  // FIX ROUND 1 (i-2): VM died before this ran
        rebuildQueued_.store(false);
        rebuild();
    });
}

void ModulesVM::setFilter(std::string filter) {
    filter_ = toLowerCopy(std::move(filter));
    rebuild();
}

void ModulesVM::setRebuildHooks(std::function<void()> before, std::function<void()> after) {
    beforeRebuild_ = std::move(before);
    afterRebuild_ = std::move(after);
}

void ModulesVM::rebuild() {
    if (beforeRebuild_) beforeRebuild_();
    const auto keep = selectedModule();
    auto loaded = facade_.listModules();
    snapshot_ = loaded ? std::move(*loaded) : std::vector<core::LoadedModule>{};
    std::ranges::sort(snapshot_, {}, &core::LoadedModule::name);
    rows_.clear();
    rowNames_.clear();
    for (const auto& m : snapshot_) {
        const std::string haystack = toLowerCopy(m.name);
        if (!filter_.empty() && haystack.find(filter_) == std::string::npos) continue;
        const auto sig = signatureCell_.find(m.name);
        rows_.push_back(formatModuleRow(m, joinHolders(m.holders),
                                        sig != signatureCell_.end() ? sig->second.c_str() : "…"));
        rowNames_.emplace_back(m.name);
    }
    if (rows_.empty()) {
        rows_.emplace_back(filter_.empty() ? "(no modules)" : "(no matches)");
        rowNames_.emplace_back(std::nullopt);
    }
    selected_ = restoreSelection(rowNames_, keep, selected_, static_cast<int>(rows_.size()));
    if (afterRebuild_) afterRebuild_();
}

std::optional<std::string> ModulesVM::selectedModule() const {
    if (selected_ < 0 || std::cmp_greater_equal(selected_, rowNames_.size())) return std::nullopt;
    return rowNames_[static_cast<std::size_t>(selected_)];
}

std::optional<ModuleSignature> ModulesVM::signedForRow(int row) const {
    if (row < 0 || std::cmp_greater_equal(row, rowNames_.size())) return std::nullopt;
    const auto& name = rowNames_[static_cast<std::size_t>(row)];
    if (!name) return std::nullopt;  // placeholder row carries no module
    const auto it = signatureCell_.find(*name);
    return classifySignatureCell(it != signatureCell_.end() ? it->second : std::string("…"));
}

std::shared_future<void> ModulesVM::fillSignatures() {
    // FIX ROUND 1 (i-1 sub-issue), coalescing option (a): if a fill is
    // already in flight (valid and not yet ready), return that handle
    // instead of starting a second overlapping worker. This keeps the
    // invariant that sigFill_ always names the SOLE outstanding worker, so
    // the destructor's sigFill_.wait() always waits for it — the worker
    // lambda captures `this` directly and is not alive_-token guarded (see
    // dtor), so that invariant is what keeps it memory-safe. It also avoids
    // redundant duplicate facade_.moduleDetail() calls for names already
    // being classified by the in-flight worker.
    if (sigFill_.valid() && sigFill_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return sigFill_;

    // Snapshot the UNCACHED names on the caller (UI) thread (spec §7.1 perf:
    // re-entering the view never re-reads .ko files already classified); the
    // worker only touches the facade + a local map, then posts the merge back.
    std::vector<std::string> names;
    for (const auto& m : snapshot_)
        if (!signatureCell_.contains(m.name)) names.push_back(m.name);
    if (names.empty()) {
        // FIX ROUND 1 (i-1): produce an already-ready future rather than
        // returning a possibly-default-constructed (invalid) sigFill_. This
        // is exactly the class's own designed happy path (spec §7.1):
        // re-entering the view when every signature is already cached.
        std::promise<void> ready;
        ready.set_value();
        sigFill_ = ready.get_future().share();
        return sigFill_;
    }
    auto alive = alive_;
    sigFill_ = scheduler_
                   .submit([this, names = std::move(names), alive] {
                       std::map<std::string, std::string> cells;
                       for (const auto& name : names)
                           cells[name] = signatureCellFor(facade_.moduleDetail(name));
                       dispatcher_.post([this, alive, cells = std::move(cells)]() mutable {
                           if (!alive->load()) return;  // FIX ROUND 1 (i-2): VM already dead
                           for (auto& [k, v] : cells) signatureCell_[k] = std::move(v);
                           rebuild();
                       });
                   })
                   .share();
    return sigFill_;
}

std::string ModulesVM::banner() const {
    const auto info = facade_.systemInfo();
    if (!info) return "Secure Boot: unknown";
    std::string b = std::string("Secure Boot: ") + (info->secureBoot ? "ON" : "off") +
                    " · Lockdown: " + info->lockdownMode;
    if (info->secureBoot || info->lockdownMode != "none")
        b += " — unsigned modules will be rejected";
    return b;
}

std::vector<std::string> ModulesVM::detailLines() const {
    const auto name = selectedModule();
    if (!name) return {"(no module selected)"};
    std::vector<std::string> out;
    out.push_back("Module:  " + *name);
    appendModuleInfo(out, facade_.moduleDetail(*name));
    appendModprobeInfo(out, facade_.modprobeDetail(*name));
    return out;
}

}  // namespace devmgr::app
