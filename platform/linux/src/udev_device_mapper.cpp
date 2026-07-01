#include "udev_device_mapper.hpp"

#include <memory>
#include <optional>
#include <string>

#include "devmgr/platform/linux/udev_field_mapping.hpp"

namespace devmgr::platform_linux {
namespace {

std::string s(const char* p) {
    return p != nullptr ? std::string(p) : std::string();
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

std::string idFor(udev_device* d) {
    return stableId(
        s(udev_device_get_subsystem(d)), s(udev_device_get_syspath(d)),
        firstNonEmpty({prop(d, "ID_VENDOR_ID"), attr(d, "idVendor"), attr(d, "vendor")}),
        firstNonEmpty({prop(d, "ID_MODEL_ID"), attr(d, "idProduct"), attr(d, "device")}),
        firstNonEmpty({prop(d, "ID_SERIAL_SHORT"), prop(d, "ID_SERIAL")}));
}

}  // namespace

core::Device mapDevice(udev_device* d) {
    core::Device dev;
    dev.id = core::DeviceId{idFor(d)};
    dev.bus = busFor(s(udev_device_get_subsystem(d)));
    dev.sysfsPath = s(udev_device_get_syspath(d));
    dev.name = firstNonEmpty({prop(d, "ID_MODEL_FROM_DATABASE"), prop(d, "ID_MODEL"),
                              attr(d, "product"), udev_device_get_sysname(d)});
    dev.modalias = firstNonEmpty({prop(d, "MODALIAS"), attr(d, "modalias")});
    dev.vendorId =
        strip0x(firstNonEmpty({prop(d, "ID_VENDOR_ID"), attr(d, "idVendor"), attr(d, "vendor")}));
    dev.productId =
        strip0x(firstNonEmpty({prop(d, "ID_MODEL_ID"), attr(d, "idProduct"), attr(d, "device")}));
    dev.serial = firstNonEmpty({prop(d, "ID_SERIAL_SHORT"), prop(d, "ID_SERIAL")});
    dev.boundDriver = opt(udev_device_get_driver(d));
    dev.status = core::DeviceStatus::Active;

    if (udev_device* parent = udev_device_get_parent(d)) {  // BORROWED — no unref
        dev.parent = core::DeviceId{idFor(parent)};
    }

    udev_list_entry* p = nullptr;
    udev_list_entry_foreach(p, udev_device_get_properties_list_entry(d)) {
        dev.properties[s(udev_list_entry_get_name(p))] = s(udev_list_entry_get_value(p));
    }
    return dev;
}

}  // namespace devmgr::platform_linux
