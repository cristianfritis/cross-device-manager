#include "devmgr/platform/linux/udev_device_enumerator.hpp"

#include <libudev.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/platform/linux/udev_field_mapping.hpp"
#include "udev_device_mapper.hpp"

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

}  // namespace

core::Result<std::vector<core::Device>> UdevDeviceEnumerator::enumerate() {
    UdevCtx udev{udev_new()};
    if (!udev) return core::makeError(core::Error::Code::Io, "udev_new() failed");

    UdevEnum en{udev_enumerate_new(udev.get())};
    if (!en) return core::makeError(core::Error::Code::Io, "udev_enumerate_new() failed");

    for (std::string_view sub : kSubsystems) {
        udev_enumerate_add_match_subsystem(en.get(), std::string(sub).c_str());
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
