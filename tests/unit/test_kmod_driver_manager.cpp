#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "devmgr/platform/linux/kmod_driver_manager.hpp"

namespace fs = std::filesystem;
using devmgr::platform_linux::KmodDriverManager;

// libkmod's module lookup (kmod_module_new_from_lookup) reads
// modules.dep.bin/modules.alias.bin exclusively -- it has never had a
// text-index fallback. So the fixture uses three tiny fake .ko files
// (generated at build time by tests/fixtures/kmod/generate_fixtures.sh; see
// tests/CMakeLists.txt) laid out under a real /lib/modules/<ver> tree and
// processed by the real `depmod`, which produces the binary indexes libkmod
// expects. Only the modprobe.d config directory stays hand-written text --
// config parsing (options/blacklist) IS text-based in libkmod.
class KmodDriverManagerTest : public ::testing::Test {
   protected:
    fs::path root_;     // fake sysfs
    fs::path moddir_;   // fake /lib/modules/<ver>
    fs::path confdir_;  // fake modprobe.d

    void SetUp() override {
        const auto base =
            fs::temp_directory_path() /
            ("devmgr-kmod-" +
             std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(base);
        root_ = base / "sys";
        moddir_ = base / "lib/modules/1.0.0";
        confdir_ = base / "modprobe.d";
        fs::create_directories(root_);
        fs::create_directories(moddir_ / "kernel/drivers/net");
        fs::create_directories(moddir_ / "kernel/drivers/hid");
        fs::create_directories(confdir_);

        const fs::path fixtureDir = DEVMGR_KMOD_FIXTURE_DIR;
        fs::copy_file(fixtureDir / "dummy.ko", moddir_ / "kernel/drivers/net/dummy.ko");
        fs::copy_file(fixtureDir / "helper.ko", moddir_ / "kernel/drivers/net/helper.ko");
        fs::copy_file(fixtureDir / "usbhid.ko", moddir_ / "kernel/drivers/hid/usbhid.ko");
        const std::string depmodCmd = "depmod -b '" + base.string() + "' 1.0.0";
        ASSERT_EQ(std::system(depmodCmd.c_str()), 0)
            << "depmod failed (is the `kmod` package installed?): " << depmodCmd;

        std::ofstream(confdir_ / "test.conf") << "options dummy numdummies=2\n"
                                                 "blacklist evil\n"
                                                 "install shady /bin/false\n";
    }
    void TearDown() override { fs::remove_all(root_.parent_path()); }

    KmodDriverManager make() {
        KmodDriverManager::Options o;
        o.sysfsRoot = root_.string();
        o.moduleDir = moddir_.string();
        o.configPaths = std::vector<std::string>{confdir_.string()};
        return KmodDriverManager(std::move(o));
    }
};

TEST_F(KmodDriverManagerTest, ModaliasLookupYieldsCandidateWithDependencies) {
    auto mgr = make();
    devmgr::core::Device d;
    d.modalias = "usb:v046DpC52Bd1101";
    d.sysfsPath = (root_ / "devices/usb1/1-2").string();
    auto r = mgr.driversFor(d);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_FALSE(r->empty());
    EXPECT_EQ(r->front().name, "dummy");
    ASSERT_EQ(r->front().dependencies.size(), 1U);
    EXPECT_EQ(r->front().dependencies[0], "helper");
}

TEST_F(KmodDriverManagerTest, BoundDriverResolvedViaSysfsComesFirst) {
    // <dev>/driver -> <root>/bus/usb/drivers/usbhid; driver/module -> module dir name.
    const fs::path dev = root_ / "devices/usb1/1-2";
    const fs::path drv = root_ / "bus/usb/drivers/usbhid";
    const fs::path mod = root_ / "module/usbhid";
    fs::create_directories(dev);
    fs::create_directories(drv);
    fs::create_directories(mod);
    fs::create_directory_symlink(drv, dev / "driver");
    fs::create_directory_symlink(mod, drv / "module");
    auto mgr = make();
    devmgr::core::Device d;
    d.sysfsPath = dev.string();
    d.modalias = "usb:v046DpC52Bd1101";
    d.boundDriver = "usbhid";
    auto r = mgr.driversFor(d);
    ASSERT_TRUE(r.has_value());
    ASSERT_GE(r->size(), 2U);
    EXPECT_EQ(r->front().name, "usbhid");  // bound first
    EXPECT_EQ((*r)[1].name, "dummy");      // then candidates
}

TEST_F(KmodDriverManagerTest, BuiltinDriverDetectedWhenNoModuleLink) {
    const fs::path dev = root_ / "devices/platform/gpio-keys";
    const fs::path drv = root_ / "bus/platform/drivers/gpio_keys";
    fs::create_directories(dev);
    fs::create_directories(drv);  // NO module link => builtin
    fs::create_directory_symlink(drv, dev / "driver");
    auto mgr = make();
    devmgr::core::Device d;
    d.sysfsPath = dev.string();
    auto r = mgr.driversFor(d);
    ASSERT_TRUE(r.has_value());
    ASSERT_FALSE(r->empty());
    EXPECT_EQ(r->front().name, "gpio_keys");
    EXPECT_EQ(r->front().kind, devmgr::core::DriverKind::Builtin);
}

TEST_F(KmodDriverManagerTest, ModprobeInfoReadsOptionsAndBlacklistFromFixtureConf) {
    auto mgr = make();
    auto dummy = mgr.modprobeInfo("dummy");
    ASSERT_TRUE(dummy.has_value()) << dummy.error().message;
    ASSERT_TRUE(dummy->options.has_value());
    EXPECT_EQ(*dummy->options, "numdummies=2");
    EXPECT_FALSE(dummy->blacklisted);
    auto evil = mgr.modprobeInfo("evil");
    ASSERT_TRUE(evil.has_value());
    EXPECT_TRUE(evil->blacklisted);
}

TEST_F(KmodDriverManagerTest, DevicesUsingModuleWalksModuleDriversToDevices) {
    // /sys/module/usbhid/drivers/usb:usbhid -> driver dir; driver dir has a
    // device symlink "1-2:1.0" -> the device under /sys/devices.
    const fs::path dev = root_ / "devices/usb1/1-2/1-2:1.0";
    const fs::path drv = root_ / "bus/usb/drivers/usbhid";
    fs::create_directories(dev);
    fs::create_directories(drv);
    fs::create_directory_symlink(dev, drv / "1-2:1.0");
    std::ofstream(drv / "uevent") << "";  // non-symlink entries must be skipped
    fs::create_directories(root_ / "module/usbhid/drivers");
    fs::create_directory_symlink(drv, root_ / "module/usbhid/drivers/usb:usbhid");
    auto mgr = make();
    auto r = mgr.devicesUsingModule("usbhid");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->size(), 1U);
    EXPECT_EQ((*r)[0], fs::weakly_canonical(dev).string());
}

TEST_F(KmodDriverManagerTest, ModuleInfoUnknownNameIsNotFound) {
    auto mgr = make();
    auto r = mgr.moduleInfo("no_such_module_xyz");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, devmgr::core::Error::Code::NotFound);
}
