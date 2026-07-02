#include "devmgr/app/device_list_vm.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <utility>

#include "devmgr/core/events.hpp"

namespace devmgr::app {
namespace {

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string toUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string haystackFor(const core::Device& d) {
    return toLower(d.name + " " + d.vendorId + ":" + d.productId + " " + core::to_string(d.bus));
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
    haystacks_.clear();
    haystacks_.reserve(snapshot_.size());
    for (const auto& d : snapshot_) haystacks_.push_back(haystackFor(d));
}

void DeviceListVM::appendRows(core::BusType bus, std::vector<const core::Device*>& group) {
    if (group.empty()) return;
    std::ranges::sort(
        group, [](const core::Device* a, const core::Device* b) { return a->name < b->name; });
    rows_.push_back(std::string("── ") + toUpper(core::to_string(bus)) + " ──");
    rowIds_.emplace_back(std::nullopt);  // header
    for (const core::Device* d : group) {
        rows_.push_back("  " + d->name + "  (" + d->vendorId + ":" + d->productId + ")");
        rowIds_.emplace_back(d->id);
    }
}

void DeviceListVM::rebuild() {
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

    rows_.clear();
    rowIds_.clear();
    for (std::size_t g = 0; g < kOrder.size(); ++g) appendRows(kOrder.at(g), groups[g]);

    if (rows_.empty()) {
        rows_.emplace_back("(no devices)");
        rowIds_.emplace_back(std::nullopt);
    }
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

}  // namespace devmgr::app
