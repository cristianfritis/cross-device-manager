#include <gtest/gtest.h>

#include <string>

#include "devmgr/pal/backend_set.hpp"
#include "devmgr/pal/capabilities.hpp"
#include "devmgr/pal/platform_backends.hpp"
#include "devmgr/pal/refusing_backends.hpp"
#include "fakes/backend_capability_invariant.hpp"

// The Windows backend set, against the REAL factory. Built only where that
// factory is (DEVMGR_PLATFORM=windows), so this is the Windows CI job's test.
// Its neutral halves — the property mapper and the hotplug lifecycle — are
// covered on every platform by their own suites.
namespace {

using devmgr::pal::BackendOptions;
using devmgr::pal::PlatformBackends;
using devmgr::test::capabilityMismatches;

TEST(WindowsPlatformBackendsTest, CreatesWithoutAnEventBus) {
    // Nothing Windows implements publishes diagnostics on the bus, so an entry
    // point that has none still gets a fully wired set.
    const auto backends = PlatformBackends::create(BackendOptions{});
    ASSERT_TRUE(backends) << backends.error().message;
    ASSERT_NE(backends->get(), nullptr);
}

TEST(WindowsPlatformBackendsTest, ReportsExactlyTheThreeImplementedCapabilities) {
    const auto backends = PlatformBackends::create(BackendOptions{});
    ASSERT_TRUE(backends);
    const auto caps = (*backends)->capabilities();
    EXPECT_TRUE(caps.deviceEnumeration);
    EXPECT_TRUE(caps.hotplug);
    EXPECT_TRUE(caps.systemInfo);
    EXPECT_FALSE(caps.deviceControl);
    EXPECT_FALSE(caps.driverManagement);
    EXPECT_FALSE(caps.privilegedChannel);
    EXPECT_FALSE(caps.updateProviders);
    // Stated, not inferred from every device looking ordinary. A Windows write
    // verb may not ship until this is true.
    EXPECT_FALSE(caps.criticalityProbing);
}

// The descriptor and the set agree, which is what makes "no mutation is
// reachable" structural: the four mutating interfaces ARE the shared refusing
// instances, so there is no other object a call could land on.
TEST(WindowsPlatformBackendsTest, DescriptorAndSetAgree) {
    const auto backends = PlatformBackends::create(BackendOptions{});
    ASSERT_TRUE(backends);
    EXPECT_TRUE(capabilityMismatches((*backends)->capabilities(), (*backends)->backends()).empty());
    EXPECT_TRUE((*backends)->backends().updateProviders.empty());
}

TEST(WindowsPlatformBackendsTest, EveryMutatingVerbRefusesWithoutTouchingTheSystem) {
    const auto backends = PlatformBackends::create(BackendOptions{});
    ASSERT_TRUE(backends);
    const auto& set = (*backends)->backends();
    const devmgr::core::Device device;

    const auto refused = [](const auto& result) {
        return !result && result.error().code == devmgr::core::Error::Code::Unsupported;
    };

    EXPECT_TRUE(refused(set.controller.setEnabled("id", true, "")));
    EXPECT_TRUE(refused(set.controller.setEnabled("id", false, "")));
    EXPECT_TRUE(refused(set.controller.bindDriver("id", "driver")));
    EXPECT_TRUE(refused(set.controller.unbindDriver("id")));

    EXPECT_TRUE(refused(set.drivers.loadModule("mod")));
    EXPECT_TRUE(refused(set.drivers.unloadModule("mod")));
    EXPECT_TRUE(refused(set.drivers.driversFor(device)));
    EXPECT_TRUE(refused(set.drivers.listLoadedModules()));

    EXPECT_TRUE(refused(set.privileged.setDeviceEnabled(device, false)));
    EXPECT_TRUE(refused(set.privileged.loadModule("mod")));
    EXPECT_TRUE(refused(set.privileged.unloadModule("mod")));
    EXPECT_TRUE(refused(set.privileged.bindDriver(device, "driver")));
    EXPECT_TRUE(refused(set.privileged.unbindDriver(device)));
    EXPECT_TRUE(refused(set.privileged.snapshotCreate("label")));
    EXPECT_TRUE(refused(set.privileged.snapshotRestore("id")));
    EXPECT_TRUE(refused(set.privileged.snapshotDelete("id")));
    EXPECT_TRUE(refused(set.privileged.snapshotList()));
    EXPECT_TRUE(refused(set.privileged.listDisabledDevices()));

    EXPECT_TRUE(refused(set.criticality.probe()));
}

// Secure Boot that cannot be read is unknown, never "off" — "off" is a claim
// that unsigned code loads.
TEST(WindowsPlatformBackendsTest, SystemInfoReportsWhatItCanVerify) {
    const auto backends = PlatformBackends::create(BackendOptions{});
    ASSERT_TRUE(backends);
    const auto info = (*backends)->backends().systemInfo.query();
    ASSERT_TRUE(info);
    EXPECT_FALSE(info->kernelVersion.empty());
    // Lockdown is a Linux concept; no Windows notion is substituted into it.
    EXPECT_EQ(info->lockdownMode, "none");
}

}  // namespace
