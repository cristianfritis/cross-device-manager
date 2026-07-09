#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "devmgr/core/models.hpp"
#include "devmgr/platform/linux/dbus_privileged_channel.hpp"

namespace fs = std::filesystem;
using devmgr::core::Error;
using devmgr::platform_linux::DbusPrivilegedChannel;

namespace {

devmgr::core::Device deviceAt(const std::string& path) {
    devmgr::core::Device d;
    d.id = devmgr::core::DeviceId{"ipc-test"};
    d.sysfsPath = path;
    d.name = "ipc test device";
    return d;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    std::string s;
    std::getline(in, s);
    return s;
}

// Spawns devmgrd on the private session bus (the whole binary runs under
// dbus-run-session — see CMake add_test) against a fake sysfs tree.
class IpcRoundTripTest : public ::testing::Test {
   protected:
    fs::path root_;
    fs::path device_;
    pid_t daemonPid_ = -1;

    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("devmgr-ipc-" + std::to_string(::getpid()) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        device_ = root_ / "devices/pci0000:00/usb1/1-4";
        fs::create_directories(device_);
        std::ofstream(device_ / "authorized") << "1";
        fs::create_directories(root_ / "bus/usb");
        fs::create_directory_symlink(root_ / "bus/usb", device_ / "subsystem");
        std::ofstream(device_ / "idVendor") << "0x1234";
        std::ofstream(device_ / "idProduct") << "0x5678";
        std::ofstream(device_ / "serial") << "IPCSER";
        std::ofstream(root_ / "mounts") << "";  // no storage facts by default
    }

    void startDaemon(const char* authority) {
        daemonPid_ = ::fork();
        ASSERT_NE(daemonPid_, -1);
        if (daemonPid_ == 0) {
            ::execl(DEVMGRD_BIN, "devmgrd", "--bus", "session", "--sysfs-root", root_.c_str(),
                    "--mounts-path", (root_ / "mounts").c_str(), "--state-dir",
                    (root_ / "state").c_str(), "--authority", authority,
                    static_cast<char*>(nullptr));
            ::_exit(127);
        }
        // Up when a bogus-path call stops failing with "helper unavailable".
        DbusPrivilegedChannel probe(DbusPrivilegedChannel::Bus::Session);
        for (int i = 0; i < 100; ++i) {
            auto r = probe.setDeviceEnabled(deviceAt("/nonexistent"), false);
            if (!r.has_value() && r.error().code == Error::Code::NotFound) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        FAIL() << "devmgrd did not come up on the session bus";
    }

    void stopDaemon() {
        if (daemonPid_ > 0) {
            ::kill(daemonPid_, SIGTERM);
            int status = 0;
            ::waitpid(daemonPid_, &status, 0);
            daemonPid_ = -1;
        }
    }

    void TearDown() override {
        stopDaemon();
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
};

}  // namespace

TEST_F(IpcRoundTripTest, DisableAndEnableFlipAuthorizedThroughTheBus) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto off = channel.setDeviceEnabled(deviceAt(device_.string()), false);
    ASSERT_TRUE(off.has_value()) << off.error().message;
    EXPECT_EQ(readFile(device_ / "authorized"), "0");
    auto on = channel.setDeviceEnabled(deviceAt(device_.string()), true);
    ASSERT_TRUE(on.has_value()) << on.error().message;
    EXPECT_EQ(readFile(device_ / "authorized"), "1");
}

TEST_F(IpcRoundTripTest, GuardRefusalArrivesAsConflictWithReason) {
    // Fake tree where the target USB device hosts the root filesystem's disk.
    const fs::path block = device_ / "1-4:1.0/host7/block/sdb";
    fs::create_directories(block);
    fs::create_directories(root_ / "class/block");
    fs::create_directory_symlink(block, root_ / "class/block/sdb");
    fs::create_directories(root_ / "dev");
    std::ofstream(root_ / "dev/sdb") << "";
    std::ofstream(root_ / "mounts") << (root_ / "dev/sdb").string() + " / ext4 rw 0 0\n";

    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.setDeviceEnabled(deviceAt(device_.string()), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Conflict);
    EXPECT_EQ(r.error().message, "backs the root filesystem");
    EXPECT_EQ(readFile(device_ / "authorized"), "1");  // untouched
}

TEST_F(IpcRoundTripTest, DeniedAuthorityArrivesAsPermission) {
    startDaemon("deny-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.setDeviceEnabled(deviceAt(device_.string()), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Permission);
}

TEST_F(IpcRoundTripTest, MissingDeviceArrivesAsNotFound) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.setDeviceEnabled(deviceAt((root_ / "devices/ghost").string()), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}

TEST_F(IpcRoundTripTest, DeviceWithoutAuthorizedAttrArrivesAsUnsupported) {
    const fs::path pci = root_ / "devices/pci0000:00/0000:00:02.0";
    fs::create_directories(pci);
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.setDeviceEnabled(deviceAt(pci.string()), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Unsupported);
}

TEST_F(IpcRoundTripTest, AbsentDaemonIsHelperUnavailableIo) {
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.setDeviceEnabled(deviceAt(device_.string()), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Io);
    EXPECT_EQ(r.error().message, "helper devmgrd is not available");
}

TEST_F(IpcRoundTripTest, DisabledDeviceAppearsInBulkListAndClearsOnEnable) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    ASSERT_TRUE(channel.setDeviceEnabled(deviceAt(device_.string()), false).has_value());
    auto list = channel.listDisabledDevices();
    ASSERT_TRUE(list.has_value()) << list.error().message;
    ASSERT_EQ(list->size(), 1U);
    EXPECT_EQ((*list)[0].mechanism, "authorized");
    EXPECT_EQ((*list)[0].key.serial, "IPCSER");
    ASSERT_TRUE(channel.setDeviceEnabled(deviceAt(device_.string()), true).has_value());
    auto after = channel.listDisabledDevices();
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->empty());
}

TEST_F(IpcRoundTripTest, DesiredStateSurvivesDaemonRestartAndSweepReapplies) {
    startDaemon("allow-all");
    {
        DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
        ASSERT_TRUE(channel.setDeviceEnabled(deviceAt(device_.string()), false).has_value());
    }
    stopDaemon();
    std::ofstream(device_ / "authorized") << "1";  // "replug": kernel re-enables
    startDaemon("allow-all");                      // sweep must re-disable
    for (int i = 0; i < 100 && readFile(device_ / "authorized") != "0"; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(readFile(device_ / "authorized"), "0");
}

TEST_F(IpcRoundTripTest, SurgicalUnbindGoesThroughTheBusAndSkipsTheStore) {
    // PCI device bound to a driver in the fake tree (Task 3 layout).
    const fs::path pci = root_ / "devices/pci0000:00/0000:00:03.0";
    const fs::path drv = root_ / "bus/pci/drivers/virtio-pci";
    fs::create_directories(pci);
    fs::create_directories(drv);
    std::ofstream(drv / "unbind") << "";
    std::ofstream(drv / "bind") << "";
    std::ofstream(root_ / "bus/pci/drivers_probe") << "";
    fs::create_directory_symlink(drv, pci / "driver");
    fs::create_directory_symlink(root_ / "bus/pci", pci / "subsystem");
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    ASSERT_TRUE(channel.unbindDriver(deviceAt(pci.string())).has_value());
    EXPECT_EQ(readFile(drv / "unbind"), "0000:00:03.0");
    auto list = channel.listDisabledDevices();
    ASSERT_TRUE(list.has_value());
    EXPECT_TRUE(list->empty());  // surgical: never persisted
    ASSERT_TRUE(channel.bindDriver(deviceAt(pci.string()), "virtio-pci").has_value());
    EXPECT_EQ(readFile(drv / "bind"), "0000:00:03.0");
}

TEST_F(IpcRoundTripTest, InvalidModuleNameRefusedAsNotFound) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.loadModule("../evil");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
    EXPECT_EQ(r.error().message, "invalid module name");
}
