#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "devmgr/pal/backend_set.hpp"
#include "devmgr/pal/capabilities.hpp"
#include "devmgr/pal/refusing_backends.hpp"

namespace {

using devmgr::core::Error;

// Every refusal must be Unsupported and must carry no message of its own — the
// shared wording table owns what a user reads (backend-availability spec).
template <class T>
void expectRefused(const devmgr::core::Result<T>& r) {
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Unsupported);
    EXPECT_TRUE(r.error().message.empty());
}

devmgr::core::Device sampleDevice() {
    devmgr::core::Device d;
    d.id.value = "dev0";
    d.nativeId = "/sys/devices/usb1/1-2";
    return d;
}

TEST(RefusingBackendsTest, EnumeratorRefuses) {
    expectRefused(devmgr::pal::refusingDeviceEnumerator().enumerate());
}

TEST(RefusingBackendsTest, HotplugMonitorRefusesAndStopIsInert) {
    auto& monitor = devmgr::pal::refusingHotplugMonitor();
    bool fired = false;
    expectRefused(monitor.start([&](const devmgr::pal::HotplugEvent&) { fired = true; }));
    monitor.stop();  // vacuously satisfies the shutdown contract
    monitor.stop();  // and is idempotent
    EXPECT_FALSE(fired);
}

TEST(RefusingBackendsTest, DeviceControllerRefusesEveryVerb) {
    auto& controller = devmgr::pal::refusingDeviceController();
    expectRefused(controller.setEnabled("/sys/devices/usb1/1-2", false, ""));
    expectRefused(controller.setEnabled("/sys/devices/usb1/1-2", true, "usbhid"));
    expectRefused(controller.bindDriver("/sys/devices/usb1/1-2", "usbhid"));
    expectRefused(controller.unbindDriver("/sys/devices/usb1/1-2"));
}

TEST(RefusingBackendsTest, DriverManagerRefusesEveryVerb) {
    auto& drivers = devmgr::pal::refusingDriverManager();
    expectRefused(drivers.driversFor(sampleDevice()));
    expectRefused(drivers.loadModule("usbhid"));
    expectRefused(drivers.unloadModule("usbhid"));
    expectRefused(drivers.listLoadedModules());
    expectRefused(drivers.moduleInfo("usbhid"));
    expectRefused(drivers.modprobeInfo("usbhid"));
    expectRefused(drivers.devicesUsingModule("usbhid"));
}

TEST(RefusingBackendsTest, SystemInfoRefuses) {
    expectRefused(devmgr::pal::refusingSystemInfo().query());
}

TEST(RefusingBackendsTest, CriticalityProberRefuses) {
    expectRefused(devmgr::pal::refusingCriticalityProber().probe());
}

TEST(RefusingBackendsTest, PrivilegedChannelRefusesEveryVerb) {
    auto& channel = devmgr::pal::refusingPrivilegedChannel();
    const auto device = sampleDevice();
    expectRefused(channel.setDeviceEnabled(device, false));
    expectRefused(channel.loadModule("usbhid"));
    expectRefused(channel.unloadModule("usbhid"));
    expectRefused(channel.bindDriver(device, "usbhid"));
    expectRefused(channel.unbindDriver(device));
    expectRefused(channel.listDisabledDevices());
    expectRefused(channel.snapshotList());
    expectRefused(channel.snapshotCreate("label"));
    expectRefused(channel.snapshotRestore("id"));
    expectRefused(channel.snapshotDelete("id"));
    expectRefused(channel.snapshotDiff("base", "target"));
}

// Exhaustiveness: one refusing type per interface BackendSet holds by
// reference, and every accessor hands back the same shared instance, so
// identity comparison is a valid "is this the refusing backend" test.
TEST(RefusingBackendsTest, OneSharedRefusingInstancePerInterface) {
    EXPECT_EQ(&devmgr::pal::refusingDeviceEnumerator(), &devmgr::pal::refusingDeviceEnumerator());
    EXPECT_EQ(&devmgr::pal::refusingHotplugMonitor(), &devmgr::pal::refusingHotplugMonitor());
    EXPECT_EQ(&devmgr::pal::refusingDeviceController(), &devmgr::pal::refusingDeviceController());
    EXPECT_EQ(&devmgr::pal::refusingDriverManager(), &devmgr::pal::refusingDriverManager());
    EXPECT_EQ(&devmgr::pal::refusingPrivilegedChannel(), &devmgr::pal::refusingPrivilegedChannel());
    EXPECT_EQ(&devmgr::pal::refusingSystemInfo(), &devmgr::pal::refusingSystemInfo());
    EXPECT_EQ(&devmgr::pal::refusingCriticalityProber(), &devmgr::pal::refusingCriticalityProber());

    // A BackendSet can be built entirely from refusing backends — the shape a
    // platform with no implementations at all would produce.
    const devmgr::pal::BackendSet none{.enumerator = devmgr::pal::refusingDeviceEnumerator(),
                                       .hotplug = devmgr::pal::refusingHotplugMonitor(),
                                       .controller = devmgr::pal::refusingDeviceController(),
                                       .drivers = devmgr::pal::refusingDriverManager(),
                                       .privileged = devmgr::pal::refusingPrivilegedChannel(),
                                       .systemInfo = devmgr::pal::refusingSystemInfo(),
                                       .criticality = devmgr::pal::refusingCriticalityProber(),
                                       .updateProviders = {}};
    // Absence of update providers is the empty list, not a refusing provider:
    // "none" is already expressible for a list.
    EXPECT_TRUE(none.updateProviders.empty());
}

TEST(RefusingBackendsTest, CapabilitiesDefaultToNothingImplemented) {
    const devmgr::pal::PlatformCapabilities caps;
    EXPECT_FALSE(caps.deviceEnumeration);
    EXPECT_FALSE(caps.hotplug);
    EXPECT_FALSE(caps.deviceControl);
    EXPECT_FALSE(caps.driverManagement);
    EXPECT_FALSE(caps.privilegedChannel);
    EXPECT_FALSE(caps.updateProviders);
    EXPECT_FALSE(caps.criticalityProbing);
    EXPECT_FALSE(caps.systemInfo);
}

}  // namespace
