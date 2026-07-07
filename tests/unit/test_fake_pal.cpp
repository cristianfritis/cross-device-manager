#include <gtest/gtest.h>
#include "devmgr/pal/interfaces.hpp"
#include "fakes/fake_pal.hpp"

using devmgr::core::Device;
using devmgr::core::DeviceId;
using devmgr::core::DeviceStatus;
using devmgr::pal::IDeviceController;
using devmgr::pal::IDeviceEnumerator;
using devmgr::test::FakePal;

TEST(FakePal, EnumeratesSeededDevices) {
    FakePal pal;
    pal.seedDevice(Device{DeviceId{"usb:1-2"}});
    IDeviceEnumerator& enumerator = pal;
    auto result = enumerator.enumerate();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].id.value, "usb:1-2");
}

TEST(FakePal, SetEnabledUpdatesState) {
    FakePal pal;
    Device d{DeviceId{"usb:1-2"}};
    d.sysfsPath = "/sys/devices/usb1/1-2";
    pal.seedDevice(d);
    IDeviceController& controller = pal;
    ASSERT_TRUE(controller.setEnabled("/sys/devices/usb1/1-2", false, "").has_value());
    EXPECT_FALSE(pal.enabled("/sys/devices/usb1/1-2"));
}

TEST(FakePal, SetEnabledOnUnknownDeviceReturnsNotFound) {
    FakePal pal;
    IDeviceController& controller = pal;
    auto result = controller.setEnabled("/sys/devices/ghost", false, "");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, devmgr::core::Error::Code::NotFound);
}
