#pragma once
#include <string>
#include <vector>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::test {

// Records channel calls; `next` scripts the outcome.
class FakePrivilegedChannel final : public pal::IPrivilegedChannel {
   public:
    core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) override {
        calls.push_back({device.sysfsPath, enabled});
        return next;
    }
    struct Call {
        std::string sysfsPath;
        bool enabled;
    };
    std::vector<Call> calls;
    core::Result<void> next = {};
};

}  // namespace devmgr::test
