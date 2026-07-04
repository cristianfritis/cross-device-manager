#pragma once
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// IPrivilegedChannel over the D-Bus system bus → devmgrd. sdbus-c++ LEAF
// FILE #2 (the .cpp; this header is sdbus-free). Each call opens a fresh
// short-lived connection+proxy (sdbus-c++ v2 sync calls block their
// connection; mutations are rare). Blocking with a 120 s timeout — the
// interactive-polkit budget — so callers must run it on a worker thread.
// Bus::Session exists for the private-bus integration tests only.
class DbusPrivilegedChannel final : public pal::IPrivilegedChannel {
   public:
    enum class Bus { System, Session };
    explicit DbusPrivilegedChannel(Bus bus = Bus::System);
    core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) override;

   private:
    Bus bus_;
};

}  // namespace devmgr::platform_linux
