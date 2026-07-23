#include "devmgr/app/device_list_vm.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <utility>

#include "devmgr/core/device_presentation.hpp"
#include "devmgr/core/events.hpp"

namespace devmgr::app {
namespace {

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The canonical name joins the haystack alongside the raw one: a user who reads
// "AMD USB controller" in the list must be able to type it into the filter and
// match, and a user who knows the kernel's own name must keep matching too.
std::string haystackFor(const core::Device& d) {
    return toLower(core::displayDeviceName(d) + " " + d.name + " " + d.vendorId + ":" +
                   d.productId + " " + core::to_string(d.bus));
}

}  // namespace

DeviceListVM::DeviceListVM(ApplicationFacade& facade, runtime::EventBus& bus,
                           IUiDispatcher& dispatcher)
    : facade_(facade), dispatcher_(dispatcher) {
    subAdded_ = bus.subscribe<core::DeviceAddedEvent>([this](const auto&) { onModelChanged(); });
    subRemoved_ =
        bus.subscribe<core::DeviceRemovedEvent>([this](const auto&) { onModelChanged(); });
    subChanged_ =
        bus.subscribe<core::DeviceChangedEvent>([this](const auto&) { onModelChanged(); });
}

void DeviceListVM::onModelChanged() {
    // Handler may run on a TaskScheduler worker or the hotplug timer thread.
    // Invalidate the snapshot first, then marshal the rebuild to the UI thread
    // — at most one queued rebuild per burst of deltas: the flag is cleared
    // before rebuild() runs, so a delta arriving mid-rebuild posts a fresh one.
    snapshotValid_.store(false);
    if (!rebuildQueued_.exchange(true)) {
        dispatcher_.post([this] {
            rebuildQueued_.store(false);
            rebuild();
        });
    }
}

void DeviceListVM::setFilter(std::string filter) {
    filter_ = std::move(filter);
    rebuild();  // called on the UI thread (Input.on_change); reuses the cached snapshot
}

void DeviceListVM::refreshSnapshot() {
    snapshot_ = facade_.devices();
    // Probed here and nowhere else: this runs once per model change, whereas
    // rebuild() also runs on every filter keystroke.
    facts_ = facade_.criticalityFacts();
    haystacks_.clear();
    haystacks_.reserve(snapshot_.size());
    for (const auto& d : snapshot_) haystacks_.push_back(haystackFor(d));
}

// The row vectors are parallel by contract — statusForRow/nameForRow/
// criticalityForRow all index them with the same row number. Every append and
// every reset goes through these two helpers so a new per-row vector cannot be
// half-wired and silently misalign the others.
void DeviceListVM::clearRows() {
    rows_.clear();
    rowIds_.clear();
    rowStatus_.clear();
    rowName_.clear();
    rowCriticality_.clear();
}

void DeviceListVM::pushNonDeviceRow(std::string text) {
    rows_.push_back(std::move(text));
    rowIds_.emplace_back(std::nullopt);
    rowStatus_.emplace_back(std::nullopt);
    rowName_.emplace_back(std::nullopt);
    rowCriticality_.emplace_back(std::nullopt);
}

void DeviceListVM::appendRows(core::BusType bus, const std::vector<const core::Device*>& group) {
    if (group.empty()) return;
    // Order by the label the user actually reads. Sorting on Device::name would
    // file every uncatalogued device under its kernel address, scattering rows
    // that read "AMD USB controller" across the group.
    std::vector<std::pair<std::string, const core::Device*>> labelled;
    labelled.reserve(group.size());
    for (const core::Device* d : group) labelled.emplace_back(core::displayDeviceName(*d), d);
    std::ranges::sort(labelled, [](const auto& a, const auto& b) { return a.first < b.first; });

    pushNonDeviceRow(std::string("── ") + core::displayBus(bus) + " ──");
    for (const auto& [label, d] : labelled) {
        // Names repeat (a machine has six identically named bridges), so the
        // identity carries the ids AND the position that tell the rows apart.
        const std::string identity = core::displayDeviceIdentity(*d);
        std::string row = "  " + label;
        if (!identity.empty()) {
            row += "  (";
            row += identity;
            row += ")";
        }
        rows_.push_back(std::move(row));
        rowIds_.emplace_back(d->id);
        rowStatus_.emplace_back(d->status);
        rowName_.emplace_back(label);
        // Pure policy over the facts probed with the snapshot — no filesystem
        // work here. Without facts nothing is marked, matching how the advisory
        // guard degrades to "allowed".
        rowCriticality_.emplace_back(facts_ ? core::classifyDevice(*facts_, d->sysfsPath)
                                            : core::Criticality::Ordinary);
    }
}

void DeviceListVM::rebuild() {
    if (beforeRebuild_) beforeRebuild_();
    static constexpr std::array<core::BusType, 5> kOrder = {
        core::BusType::Pci, core::BusType::Usb, core::BusType::Platform, core::BusType::Virtio,
        core::BusType::Other};

    const std::optional<core::DeviceId> keep = selectedDeviceId();
    const std::string needle = toLower(filter_);

    // Claim-then-fetch: exchange before copying so an invalidation racing this
    // fetch is observed by that event's own posted rebuild (see onModelChanged).
    if (!snapshotValid_.exchange(true)) refreshSnapshot();

    // Group matches by bus in one pass, by pointer — no Device copies per rebuild.
    std::vector<std::vector<const core::Device*>> groups(kOrder.size());
    for (std::size_t i = 0; i < snapshot_.size(); ++i) {
        if (!needle.empty() && haystacks_[i].find(needle) == std::string::npos) continue;
        const auto slot = static_cast<std::size_t>(
            std::distance(kOrder.begin(), std::ranges::find(kOrder, snapshot_[i].bus)));
        if (slot < groups.size()) groups[slot].push_back(&snapshot_[i]);
    }

    clearRows();
    for (std::size_t g = 0; g < kOrder.size(); ++g) appendRows(kOrder.at(g), groups[g]);
    if (rows_.empty()) pushNonDeviceRow("(no devices)");
    restoreSelection(keep);
    if (afterRebuild_) afterRebuild_();
}

void DeviceListVM::restoreSelection(const std::optional<core::DeviceId>& keep) {
    if (keep) {  // keep the highlighted device stable across mutations
        if (auto it = std::ranges::find(rowIds_, keep); it != rowIds_.end())
            selected_ = static_cast<int>(std::distance(rowIds_.begin(), it));
    }
    // device vanished -> clamp to nearest valid row
    selected_ = std::max(0, std::min(static_cast<int>(rows_.size()) - 1, selected_));
}

std::optional<core::DeviceId> DeviceListVM::selectedDeviceId() const {
    if (selected_ < 0 || std::cmp_greater_equal(selected_, rowIds_.size())) return std::nullopt;
    return rowIds_[selected_];
}

bool DeviceListVM::isHeader(int row) const {
    if (row < 0 || std::cmp_greater_equal(row, rowIds_.size())) return false;
    return !rowIds_[static_cast<std::size_t>(row)].has_value();
}

std::optional<core::DeviceStatus> DeviceListVM::statusForRow(int row) const {
    if (row < 0 || std::cmp_greater_equal(row, rowStatus_.size())) return std::nullopt;
    return rowStatus_[static_cast<std::size_t>(row)];
}

std::optional<std::string> DeviceListVM::nameForRow(int row) const {
    if (row < 0 || std::cmp_greater_equal(row, rowName_.size())) return std::nullopt;
    return rowName_[static_cast<std::size_t>(row)];
}

std::optional<core::Criticality> DeviceListVM::criticalityForRow(int row) const {
    if (row < 0 || std::cmp_greater_equal(row, rowCriticality_.size())) return std::nullopt;
    return rowCriticality_[static_cast<std::size_t>(row)];
}

}  // namespace devmgr::app
