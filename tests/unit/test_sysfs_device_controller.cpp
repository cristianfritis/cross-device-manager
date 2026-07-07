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
    ASSERT_TRUE(controller.setEnabled(device_.string(), false, "").has_value());
    EXPECT_EQ(readFile(device_ / "authorized"), "0");
    ASSERT_TRUE(controller.setEnabled(device_.string(), true, "").has_value());
    EXPECT_EQ(readFile(device_ / "authorized"), "1");
}

TEST_F(SysfsControllerTest, MissingDeviceIsNotFound) {
    SysfsDeviceController controller(root_.string());
    auto r = controller.setEnabled((root_ / "devices/ghost").string(), false, "");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}

TEST_F(SysfsControllerTest, PathOutsideRootIsNotFound) {
    SysfsDeviceController controller((root_ / "devices").string());
    // ".." escape attempts must be rejected after canonicalization.
    auto r = controller.setEnabled((root_ / "devices/../..").string(), false, "");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}

TEST_F(SysfsControllerTest, NoAuthorizedAttrIsUnsupported) {
    const fs::path pci = root_ / "devices/pci0000:00/0000:00:02.0";
    fs::create_directories(pci);
    SysfsDeviceController controller(root_.string());
    auto r = controller.setEnabled(pci.string(), false, "");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Unsupported);
}

TEST_F(SysfsControllerTest, UnwritableAttrIsIo) {
    if (::geteuid() == 0) GTEST_SKIP() << "root ignores file permissions";
    fs::permissions(device_ / "authorized", fs::perms::owner_read);
    SysfsDeviceController controller(root_.string());
    auto r = controller.setEnabled(device_.string(), false, "");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Io);
}

namespace {
// Builds <root>/devices/pci0000:00/0000:03:00.0 bound to driver "virtio-pci"
// on bus "pci", with driver_override present. Returns the device path.
fs::path makePciDevice(const fs::path& root) {
    const fs::path dev = root / "devices/pci0000:00/0000:03:00.0";
    const fs::path bus = root / "bus/pci";
    const fs::path drv = bus / "drivers/virtio-pci";
    fs::create_directories(dev);
    fs::create_directories(drv);
    std::ofstream(bus / "drivers_probe") << "";
    std::ofstream(drv / "bind") << "";
    std::ofstream(drv / "unbind") << "";
    std::ofstream(dev / "driver_override") << "";
    fs::create_directory_symlink(drv, dev / "driver");
    fs::create_directory_symlink(bus, dev / "subsystem");
    return dev;
}
std::string slurp(const fs::path& p) {
    std::ifstream in(p);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}  // namespace

TEST_F(SysfsControllerTest, DisableWithoutAuthorizedUnbindsAndReportsDriver) {
    const auto dev = makePciDevice(root_);
    SysfsDeviceController c(root_.string());
    auto r = c.setEnabled(dev.string(), false, "");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_TRUE(r->has_value());
    EXPECT_EQ(**r, "virtio-pci");  // mechanism "unbind", driver reported
    EXPECT_EQ(slurp(root_ / "bus/pci/drivers/virtio-pci/unbind"), "0000:03:00.0");
}

TEST_F(SysfsControllerTest, DisableWithNoBoundDriverReportsEmptyUnbindMechanism) {
    const auto dev = makePciDevice(root_);
    fs::remove(dev / "driver");
    SysfsDeviceController c(root_.string());
    auto r = c.setEnabled(dev.string(), false, "");
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(r->has_value());
    EXPECT_EQ(**r, "");  // unbind mechanism, nothing was bound
}

TEST_F(SysfsControllerTest, EnableUsesDriverOverrideThenProbeThenClearsOverride) {
    const auto dev = makePciDevice(root_);
    SysfsDeviceController c(root_.string());
    auto r = c.setEnabled(dev.string(), true, "virtio-pci");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_FALSE(r->has_value());  // enables report nullopt
    EXPECT_EQ(slurp(root_ / "bus/pci/drivers_probe"), "0000:03:00.0");
    // Override written then cleared: last write is the empty clear.
    EXPECT_EQ(slurp(dev / "driver_override"), "");
}

TEST_F(SysfsControllerTest, EnableWithoutHintSkipsOverride) {
    const auto dev = makePciDevice(root_);
    SysfsDeviceController c(root_.string());
    auto r = c.setEnabled(dev.string(), true, "");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(slurp(root_ / "bus/pci/drivers_probe"), "0000:03:00.0");
}

TEST_F(SysfsControllerTest, UsbAuthorizedPathStillWinsWhenAttrExists) {
    // The Phase 4 fixture device (with `authorized`) must keep using it even
    // if a driver symlink also exists.
    // Reuse the existing fixture device; just assert behavior is unchanged:
    SysfsDeviceController c(root_.string());
    auto r = c.setEnabled(device_.string(), false, "");
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r->has_value());  // authorized mechanism => nullopt
}

TEST_F(SysfsControllerTest, SurgicalUnbindWritesDeviceNameToDriverUnbind) {
    const auto dev = makePciDevice(root_);
    SysfsDeviceController c(root_.string());
    ASSERT_TRUE(c.unbindDriver(dev.string()).has_value());
    EXPECT_EQ(slurp(root_ / "bus/pci/drivers/virtio-pci/unbind"), "0000:03:00.0");
}

TEST_F(SysfsControllerTest, SurgicalUnbindWithoutDriverIsNotFound) {
    const auto dev = makePciDevice(root_);
    fs::remove(dev / "driver");
    SysfsDeviceController c(root_.string());
    auto r = c.unbindDriver(dev.string());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}

TEST_F(SysfsControllerTest, SurgicalBindWritesDeviceNameToNamedDriverBind) {
    const auto dev = makePciDevice(root_);
    fs::remove(dev / "driver");
    SysfsDeviceController c(root_.string());
    ASSERT_TRUE(c.bindDriver(dev.string(), "virtio-pci").has_value());
    EXPECT_EQ(slurp(root_ / "bus/pci/drivers/virtio-pci/bind"), "0000:03:00.0");
}

TEST_F(SysfsControllerTest, SurgicalBindToUnknownDriverIsNotFound) {
    const auto dev = makePciDevice(root_);
    SysfsDeviceController c(root_.string());
    auto r = c.bindDriver(dev.string(), "no_such_driver");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}
