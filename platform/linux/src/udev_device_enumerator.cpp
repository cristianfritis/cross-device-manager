#include "devmgr/platform/linux/udev_device_enumerator.hpp"

namespace devmgr::platform_linux {

core::Result<std::vector<core::Device>> UdevDeviceEnumerator::enumerate() {
    return core::makeError(core::Error::Code::Unsupported, "not implemented yet");
}

}  // namespace devmgr::platform_linux
