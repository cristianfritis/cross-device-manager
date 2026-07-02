#include "udev_device_mapper.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "devmgr/platform/linux/udev_field_mapping.hpp"

namespace devmgr::platform_linux {
namespace {

std::string s(const char* p) {
    return p != nullptr ? std::string(p) : std::string();
}
std::string_view sv(const char* p) {
    return p != nullptr ? std::string_view(p) : std::string_view();
}
std::optional<std::string> opt(const char* p) {
    return p != nullptr ? std::optional<std::string>(p) : std::nullopt;
}
const char* prop(udev_device* d, const char* k) {
    return udev_device_get_property_value(d, k);
}
const char* attr(udev_device* d, const char* k) {
    return udev_device_get_sysattr_value(d, k);
}

// Lazy counterpart of firstNonEmpty (udev_field_mapping.hpp): getters run left
// to right and stop at the first non-empty hit. attr() getters read sysfs
// files, so later fallbacks must not be evaluated when an earlier udev
// property already answers — the eager initializer_list form reads them all.
template <typename... Getters>
std::string firstNonEmptyLazy(const Getters&... getters) {
    const char* hit = nullptr;
    auto tryOne = [&hit](const auto& getter) {
        hit = getter();
        return hit != nullptr && *hit != '\0';
    };
    if ((tryOne(getters) || ...)) return {hit};
    return {};
}

std::string vendorFor(udev_device* d) {
    return firstNonEmptyLazy([&] { return prop(d, "ID_VENDOR_ID"); },
                             [&] { return attr(d, "idVendor"); },
                             [&] { return attr(d, "vendor"); });
}
std::string productFor(udev_device* d) {
    return firstNonEmptyLazy([&] { return prop(d, "ID_MODEL_ID"); },
                             [&] { return attr(d, "idProduct"); },
                             [&] { return attr(d, "device"); });
}
std::string serialFor(udev_device* d) {
    return firstNonEmptyLazy([&] { return prop(d, "ID_SERIAL_SHORT"); },
                             [&] { return prop(d, "ID_SERIAL"); });
}

std::string idFor(udev_device* d) {
    return stableId(sv(udev_device_get_subsystem(d)), sv(udev_device_get_syspath(d)), vendorFor(d),
                    productFor(d), serialFor(d));
}

}  // namespace

core::Device mapDevice(udev_device* d) {
    // Fetch each identity field exactly once — stableId and the Device fields
    // below share them instead of re-running the (sysfs-reading) chains.
    const char* subsystem = udev_device_get_subsystem(d);
    const char* syspath = udev_device_get_syspath(d);
    const std::string vendor = vendorFor(d);
    const std::string product = productFor(d);
    std::string serial = serialFor(d);

    core::Device dev;
    dev.id = core::DeviceId{stableId(sv(subsystem), sv(syspath), vendor, product, serial)};
    dev.bus = busFor(sv(subsystem));
    dev.sysfsPath = s(syspath);
    dev.name = firstNonEmptyLazy(
        [&] { return prop(d, "ID_MODEL_FROM_DATABASE"); }, [&] { return prop(d, "ID_MODEL"); },
        [&] { return attr(d, "product"); }, [&] { return udev_device_get_sysname(d); });
    dev.modalias =
        firstNonEmptyLazy([&] { return prop(d, "MODALIAS"); }, [&] { return attr(d, "modalias"); });
    dev.vendorId = strip0x(vendor);
    dev.productId = strip0x(product);
    dev.serial = std::move(serial);
    dev.boundDriver = opt(udev_device_get_driver(d));
    dev.status = core::DeviceStatus::Active;

    if (udev_device* parent = udev_device_get_parent(d)) {  // BORROWED — no unref
        dev.parent = core::DeviceId{idFor(parent)};
    }

    udev_list_entry* p = nullptr;
    udev_list_entry_foreach(p, udev_device_get_properties_list_entry(d)) {
        dev.properties.insert_or_assign(s(udev_list_entry_get_name(p)),
                                        s(udev_list_entry_get_value(p)));
    }
    return dev;
}

}  // namespace devmgr::platform_linux
