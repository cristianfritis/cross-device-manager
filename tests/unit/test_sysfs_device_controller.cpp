#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "devmgr/platform/linux/sysfs_device_controller.hpp"

namespace fs = std::filesystem;
using devmgr::core::Error;
using devmgr::platform_linux::SysfsDeviceController;

namespace {
std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    std::string s;
    std::getline(in, s);
    return s;
}

class SysfsControllerTest : public ::testing::Test {
   protected:
    fs::path root_;
    fs::path device_;
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("devmgr-sysfs-" + std::to_string(::getpid()) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        device_ = root_ / "devices/pci0000:00/usb1/1-4";
        fs::create_directories(device_);
        std::ofstream(device_ / "authorized") << "1";
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
};
}  // namespace

TEST_F(SysfsControllerTest, DisableWritesZeroAndEnableWritesOne) {
    SysfsDeviceController controller(root_.string());
    ASSERT_TRUE(controller.setEnabled(device_.string(), false).has_value());
    EXPECT_EQ(readFile(device_ / "authorized"), "0");
    ASSERT_TRUE(controller.setEnabled(device_.string(), true).has_value());
    EXPECT_EQ(readFile(device_ / "authorized"), "1");
}

TEST_F(SysfsControllerTest, MissingDeviceIsNotFound) {
    SysfsDeviceController controller(root_.string());
    auto r = controller.setEnabled((root_ / "devices/ghost").string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}

TEST_F(SysfsControllerTest, PathOutsideRootIsNotFound) {
    SysfsDeviceController controller((root_ / "devices").string());
    // ".." escape attempts must be rejected after canonicalization.
    auto r = controller.setEnabled((root_ / "devices/../..").string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}

TEST_F(SysfsControllerTest, NoAuthorizedAttrIsUnsupported) {
    const fs::path pci = root_ / "devices/pci0000:00/0000:00:02.0";
    fs::create_directories(pci);
    SysfsDeviceController controller(root_.string());
    auto r = controller.setEnabled(pci.string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Unsupported);
}

TEST_F(SysfsControllerTest, UnwritableAttrIsIo) {
    if (::geteuid() == 0) GTEST_SKIP() << "root ignores file permissions";
    fs::permissions(device_ / "authorized", fs::perms::owner_read);
    SysfsDeviceController controller(root_.string());
    auto r = controller.setEnabled(device_.string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Io);
}
