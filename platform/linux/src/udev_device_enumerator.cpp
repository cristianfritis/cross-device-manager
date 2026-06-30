#include "devmgr/platform/linux/udev_device_enumerator.hpp"

#include <libudev.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/platform/linux/udev_field_mapping.hpp"

namespace devmgr::platform_linux {
namespace {

struct UdevCtxDeleter {
    void operator()(udev* p) const noexcept { udev_unref(p); }
};
struct UdevEnumDeleter {
    void operator()(udev_enumerate* p) const noexcept { udev_enumerate_unref(p); }
};
struct UdevDevDeleter {
    void operator()(udev_device* p) const noexcept { udev_device_unref(p); }
};
using UdevCtx = std::unique_ptr<udev, UdevCtxDeleter>;
using UdevEnum = std::unique_ptr<udev_enumerate, UdevEnumDeleter>;
using UdevDev = std::unique_ptr<udev_device, UdevDevDeleter>;
// NOTE: udev_device_get_parent() returns a BORROWED pointer (freed with the
// child). Never wrap it in UdevDev and never unref it.

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

}  // namespace

core::Result<std::vector<core::Device>> UdevDeviceEnumerator::enumerate() {
    UdevCtx udev{udev_new()};
    if (!udev) return core::makeError(core::Error::Code::Io, "udev_new() failed");

    UdevEnum en{udev_enumerate_new(udev.get())};
    if (!en) return core::makeError(core::Error::Code::Io, "udev_enumerate_new() failed");

    for (const char* sub : {"pci", "usb", "platform", "virtio"}) {
        udev_enumerate_add_match_subsystem(en.get(), sub);
    }
    if (udev_enumerate_scan_devices(en.get()) < 0) {
        return core::makeError(core::Error::Code::Io, "udev_enumerate_scan_devices failed");
    }

    std::vector<core::Device> out;
    udev_list_entry* entry = nullptr;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(en.get())) {
        const char* syspath = udev_list_entry_get_name(entry);  // entry NAME == syspath
        if (syspath == nullptr) continue;

        UdevDev dev{udev_device_new_from_syspath(udev.get(), syspath)};
        if (!dev) {  // FAULT ISOLATION: one bad device never aborts the scan
            core::Device bad;
            bad.id = core::DeviceId{std::string("dev-err-") + syspath};
            bad.sysfsPath = syspath;
            bad.status = core::DeviceStatus::Error;
            bad.errorNote = "udev_device_new_from_syspath failed";
            out.push_back(std::move(bad));
            continue;
        }
        out.push_back(mapDevice(dev.get()));
    }
    return out;
}

}  // namespace devmgr::platform_linux
