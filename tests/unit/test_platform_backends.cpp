#include <gtest/gtest.h>

#include <vector>

#include "devmgr/pal/backend_set.hpp"
#include "devmgr/pal/capabilities.hpp"
#include "devmgr/pal/platform_backends.hpp"
#include "devmgr/pal/refusing_backends.hpp"
#include "fakes/backend_capability_invariant.hpp"
#include "fakes/fake_pal.hpp"

namespace {

using devmgr::pal::BackendSet;
using devmgr::pal::PlatformCapabilities;
using devmgr::test::capabilityMismatches;

// A stand-in for a platform factory: whatever a real one wires, the descriptor
// and the set must agree. FakePal implements enumeration, control, driver
// management and system info, so a "half-implemented" platform is expressible
// without any platform code.
struct SyntheticBackends {
    devmgr::test::FakePal pal;
    BackendSet set{.enumerator = pal,
                   .hotplug = devmgr::pal::refusingHotplugMonitor(),
                   .controller = pal,
                   .drivers = pal,
                   .privileged = devmgr::pal::refusingPrivilegedChannel(),
                   .systemInfo = pal,
                   .criticality = devmgr::pal::refusingCriticalityProber(),
                   .updateProviders = {}};
    PlatformCapabilities caps{.deviceEnumeration = true,
                              .hotplug = false,
                              .deviceControl = true,
                              .driverManagement = true,
                              .privilegedChannel = false,
                              .updateProviders = false,
                              .criticalityProbing = false,
                              .systemInfo = true};
};

TEST(PlatformBackendsInvariantTest, DescriptorAndSetAgree) {
    SyntheticBackends platform;
    EXPECT_TRUE(capabilityMismatches(platform.caps, platform.set).empty());
}

TEST(PlatformBackendsInvariantTest, ImplementedButRefusingIsCaught) {
    SyntheticBackends platform;
    platform.caps.hotplug = true;  // claims a backend it did not wire
    const auto bad = capabilityMismatches(platform.caps, platform.set);
    ASSERT_EQ(bad.size(), 1U);
    EXPECT_EQ(bad.front(), "hotplug");
}

TEST(PlatformBackendsInvariantTest, RealButReportedUnimplementedIsCaught) {
    SyntheticBackends platform;
    platform.caps.deviceEnumeration = false;  // hides a backend it did wire
    const auto bad = capabilityMismatches(platform.caps, platform.set);
    ASSERT_EQ(bad.size(), 1U);
    EXPECT_EQ(bad.front(), "deviceEnumeration");
}

TEST(PlatformBackendsInvariantTest, UpdateProvidersFollowTheListNotARefusingType) {
    SyntheticBackends platform;
    EXPECT_TRUE(capabilityMismatches(platform.caps, platform.set).empty());

    platform.caps.updateProviders = true;  // claims providers with an empty list
    EXPECT_EQ(capabilityMismatches(platform.caps, platform.set),
              std::vector<std::string>{"updateProviders"});
}

// The wholly refusing platform — the shape DEVMGR_PLATFORM=none would produce —
// is internally consistent with an all-false descriptor.
TEST(PlatformBackendsInvariantTest, NoPlatformIsConsistent) {
    const BackendSet none{.enumerator = devmgr::pal::refusingDeviceEnumerator(),
                          .hotplug = devmgr::pal::refusingHotplugMonitor(),
                          .controller = devmgr::pal::refusingDeviceController(),
                          .drivers = devmgr::pal::refusingDriverManager(),
                          .privileged = devmgr::pal::refusingPrivilegedChannel(),
                          .systemInfo = devmgr::pal::refusingSystemInfo(),
                          .criticality = devmgr::pal::refusingCriticalityProber(),
                          .updateProviders = {}};
    EXPECT_TRUE(capabilityMismatches(PlatformCapabilities{}, none).empty());
}

// BackendOptions defaults to the production endpoint, so an entry point that
// passes nothing never reaches an isolated test endpoint by accident.
TEST(PlatformBackendsInvariantTest, BackendOptionsDefaultToTheProductionEndpoint) {
    const devmgr::pal::BackendOptions options;
    EXPECT_EQ(options.privilegedEndpoint, devmgr::pal::BackendOptions::PrivilegedEndpoint::Default);
}

}  // namespace
