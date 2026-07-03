#include <algorithm>

#include <gtest/gtest.h>
#include <umockdev.h>

#include "devmgr/platform/linux/udev_device_enumerator.hpp"

namespace {

class UdevEnumeratorTest : public ::testing::Test {
   protected:
    UMockdevTestbed* bed_ = nullptr;
    void SetUp() override {
        bed_ = umockdev_testbed_new();
        ASSERT_NE(bed_, nullptr);
    }
    void TearDown() override {
        if (bed_ != nullptr) g_object_unref(bed_);
    }
};

TEST_F(UdevEnumeratorTest, MapsUsbDeviceFieldsAndIsDeterministic) {
    gchar* sys =
        umockdev_testbed_add_device(bed_, "usb", "1-1", nullptr, "idVendor", "1d6b", "idProduct",
                                    "0002", nullptr, "ID_VENDOR_ID", "1d6b", "ID_MODEL_ID", "0002",
                                    "SUBSYSTEM", "usb", "MODALIAS", "usb:v1D6Bp0002", nullptr);
    ASSERT_NE(sys, nullptr);
    g_free(sys);

    devmgr::platform_linux::UdevDeviceEnumerator enumr;
    auto res = enumr.enumerate();
    ASSERT_TRUE(res.has_value()) << res.error().message;

    const auto& devs = *res;
    auto it = std::find_if(devs.begin(), devs.end(), [](const auto& d) {
        return d.vendorId == "1d6b" && d.productId == "0002";
    });
    ASSERT_NE(it, devs.end());
    EXPECT_EQ(it->bus, devmgr::core::BusType::Usb);
    EXPECT_EQ(it->status, devmgr::core::DeviceStatus::Active);
    EXPECT_NE(it->sysfsPath.find("/devices/"), std::string::npos);
    EXPECT_EQ(it->modalias.rfind("usb:v1D6Bp0002", 0), 0u);

    // Determinism: a second enumeration yields the identical DeviceId.
    auto res2 = enumr.enumerate();
    ASSERT_TRUE(res2.has_value());
    auto it2 =
        std::find_if(res2->begin(), res2->end(), [&](const auto& d) { return d.id == it->id; });
    EXPECT_NE(it2, res2->end());
}

TEST_F(UdevEnumeratorTest, DeauthorizedUsbDeviceMapsToDisabled) {
    gchar* sys = umockdev_testbed_add_device(bed_, "usb", "1-9", nullptr, "authorized", "0",
                                             "idVendor", "abcd", "idProduct", "ef01", nullptr,
                                             "SUBSYSTEM", "usb", nullptr);
    ASSERT_NE(sys, nullptr);
    g_free(sys);

    devmgr::platform_linux::UdevDeviceEnumerator enumr;
    auto res = enumr.enumerate();
    ASSERT_TRUE(res.has_value()) << res.error().message;
    auto it = std::find_if(res->begin(), res->end(), [](const auto& d) {
        return d.vendorId == "abcd" && d.productId == "ef01";
    });
    ASSERT_NE(it, res->end());
    EXPECT_EQ(it->status, devmgr::core::DeviceStatus::Disabled);
}

}  // namespace
