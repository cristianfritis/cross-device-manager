#include <gtest/gtest.h>

#include "devmgr/platform/linux/fwupd_update_provider.hpp"
#include "devmgr/runtime/event_bus.hpp"

using devmgr::platform_linux::FwupdUpdateProvider;

TEST(FwupdProviderNoDaemon, DegradesWithoutThrowing) {
    // Unit env has no session bus (no dbus-run-session wrapper) — ctor must
    // absorb the failure (V2), availability must explain it, dtor must be safe.
    ::unsetenv("DBUS_SESSION_BUS_ADDRESS");
    devmgr::runtime::EventBus bus;
    FwupdUpdateProvider p(bus, {.useSessionBus = true});
    const auto a = p.availability();
    EXPECT_FALSE(a.available);
    ASSERT_TRUE(a.error.has_value());
    auto e = p.enumerate();
    EXPECT_FALSE(e.has_value());
}
