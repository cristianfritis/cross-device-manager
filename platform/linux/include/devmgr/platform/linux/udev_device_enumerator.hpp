#pragma once
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// Real libudev-backed enumerator. libudev is an implementation detail (this
// header pulls NO <libudev.h>), so consumers stay free of the native dep.
class UdevDeviceEnumerator final : public pal::IDeviceEnumerator {
   public:
    core::Result<std::vector<core::Device>> enumerate() override;
};

}  // namespace devmgr::platform_linux
