#include "devmgr/app/device_list_vm.hpp"

#include <algorithm>
#include <array>
#include <cctype>
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

bool matchesFilter(const core::Device& d, const std::string& needleLower) {
    if (needleLower.empty()) return true;
    std::string hay =
        toLower(d.name + " " + d.vendorId + ":" + d.productId + " " + core::to_string(d.bus));
    return hay.find(needleLower) != std::string::npos;
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
    // Handler may run on a TaskScheduler worker — marshal the rebuild to the UI thread.
    dispatcher_.post([this] { rebuild(); });
}

void DeviceListVM::setFilter(std::string filter) {
    filter_ = std::move(filter);
    rebuild();  // called on the UI thread (Input.on_change)
}

void DeviceListVM::rebuild() {
    static constexpr std::array<core::BusType, 5> kOrder = {
        core::BusType::Pci, core::BusType::Usb, core::BusType::Platform, core::BusType::Virtio,
        core::BusType::Other};

    const std::string needle = toLower(filter_);
    auto devices = facade_.devices();

    rows_.clear();
    rowIds_.clear();
    for (core::BusType bus : kOrder) {
        std::vector<core::Device> group;
        for (auto& d : devices) {
            if (d.bus == bus && matchesFilter(d, needle)) group.push_back(d);
        }
        if (group.empty()) continue;
        std::ranges::sort(
            group, [](const core::Device& a, const core::Device& b) { return a.name < b.name; });
        rows_.push_back(std::string("── ") + toUpper(core::to_string(bus)) + " ──");
        rowIds_.emplace_back(std::nullopt);  // header
        for (const auto& d : group) {
            rows_.push_back("  " + d.name + "  (" + d.vendorId + ":" + d.productId + ")");
            rowIds_.emplace_back(d.id);
        }
    }

    if (rows_.empty()) {
        rows_.emplace_back("(no devices)");
        rowIds_.emplace_back(std::nullopt);
    }
    if (selected_ < 0) selected_ = 0;
    if (std::cmp_greater_equal(selected_, rows_.size()))
        selected_ = static_cast<int>(rows_.size()) - 1;
}

std::optional<core::DeviceId> DeviceListVM::selectedDeviceId() const {
    if (selected_ < 0 || std::cmp_greater_equal(selected_, rowIds_.size())) return std::nullopt;
    return rowIds_[selected_];
}

}  // namespace devmgr::app
