#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "devmgr/core/models.hpp"
#include "devmgr/core/snapshot_models.hpp"
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

// Runs the real `devmgr` recovery binary on the private session bus and returns
// its exit code plus captured stdout. Driving the shipped executable (not the
// channel directly) is the point: it proves argv parsing, exit codes, and the
// D-Bus round-trip end to end. Inherits DBUS_SESSION_BUS_ADDRESS from
// dbus-run-session, so it reaches the same daemon the fixture started.
struct CliRun {
    int code;
    std::string out;
};

CliRun runCli(const std::string& args) {
    const std::string cmd = std::string(DEVMGR_CLI_BIN) + " --bus session " + args + " 2>/dev/null";
    std::string out;
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (pipe == nullptr) return {-1, out};
    std::array<char, 512> buffer{};
    size_t n = 0;
    while ((n = ::fread(buffer.data(), 1, buffer.size(), pipe)) > 0) out.append(buffer.data(), n);
    const int status = ::pclose(pipe);
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, out};
}

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
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
        // Private modprobe.d: snapshot payloads must never capture (or restore
        // over) the host's real /etc/modprobe.d.
        fs::create_directories(root_ / "modprobe.d");
    }

    void startDaemon(const char* authority) {
        daemonPid_ = ::fork();
        ASSERT_NE(daemonPid_, -1);
        if (daemonPid_ == 0) {
            ::execl(DEVMGRD_BIN, "devmgrd", "--bus", "session", "--sysfs-root", root_.c_str(),
                    "--mounts-path", (root_ / "mounts").c_str(), "--state-dir",
                    (root_ / "state").c_str(), "--modprobe-dir", (root_ / "modprobe.d").c_str(),
                    "--authority", authority, static_cast<char*>(nullptr));
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

// ApiVersion 4: malformed arguments come back as InvalidArgs
// (org.devmgr.Error.InvalidArgs), which is what proves the new error name
// survives the real bus rather than collapsing to Io on the way home.
TEST_F(IpcRoundTripTest, InvalidModuleNameRefusedAsInvalidArgs) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.loadModule("../evil");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::InvalidArgs);
    EXPECT_EQ(r.error().message, "invalid module name");
}

TEST_F(IpcRoundTripTest, OversizedArgumentRefusedOverTheBus) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.loadModule(std::string(4U << 20U, 'a'));  // 4 MiB
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::InvalidArgs);
    // The daemon stayed responsive after refusing the oversized payload.
    EXPECT_TRUE(channel.snapshotList().has_value());
}

TEST_F(IpcRoundTripTest, SnapshotLifecycleRoundTripsThroughTheBus) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);

    // Disable → the pre-mutation auto snapshot (empty payload) appears.
    ASSERT_TRUE(channel.setDeviceEnabled(deviceAt(device_.string()), false).has_value());
    auto afterDisable = channel.snapshotList();
    ASSERT_TRUE(afterDisable.has_value()) << afterDisable.error().message;
    ASSERT_EQ(afterDisable->size(), 1U);
    const devmgr::core::SnapshotMeta autoMeta = (*afterDisable)[0];
    EXPECT_EQ(autoMeta.id.size(), 64U);
    EXPECT_FALSE(autoMeta.parent.has_value());
    EXPECT_EQ(autoMeta.trigger, devmgr::core::SnapshotTrigger::Auto);
    EXPECT_EQ(autoMeta.reason.verb, "SetDeviceEnabled");
    EXPECT_EQ(autoMeta.health, devmgr::core::SnapshotHealth::Ok);
    EXPECT_EQ(autoMeta.entryCount, 0U);

    // Manual snapshot captures the disabled state and chains onto the auto one.
    auto manualId = channel.snapshotCreate("with-disable");
    ASSERT_TRUE(manualId.has_value()) << manualId.error().message;
    auto listed = channel.snapshotList();
    ASSERT_TRUE(listed.has_value());
    ASSERT_EQ(listed->size(), 2U);  // newest first via the HEAD chain
    EXPECT_EQ((*listed)[0].id, *manualId);
    EXPECT_EQ((*listed)[0].trigger, devmgr::core::SnapshotTrigger::Manual);
    EXPECT_EQ((*listed)[0].reason.subject, "with-disable");
    EXPECT_EQ((*listed)[0].entryCount, 1U);
    ASSERT_TRUE((*listed)[0].parent.has_value());
    EXPECT_EQ(*(*listed)[0].parent, autoMeta.id);

    // Restore the pre-disable snapshot: device re-enabled, outcome itemized.
    auto outcome = channel.snapshotRestore(autoMeta.id);
    ASSERT_TRUE(outcome.has_value()) << outcome.error().message;
    EXPECT_EQ(outcome->snapshotId, autoMeta.id);
    // The safety snapshot dedupes onto the manual one: state is unchanged
    // between the manual capture and the restore.
    EXPECT_EQ(outcome->safetySnapshotId, *manualId);
    ASSERT_EQ(outcome->items.size(), 1U);
    EXPECT_EQ(outcome->items[0].action, "re-enable");
    EXPECT_EQ(outcome->items[0].status, "ok");
    EXPECT_EQ(readFile(device_ / "authorized"), "1");
    auto disabled = channel.listDisabledDevices();
    ASSERT_TRUE(disabled.has_value());
    EXPECT_TRUE(disabled->empty());

    // Delete the manual snapshot (HEAD): the auto one survives as new HEAD.
    ASSERT_TRUE(channel.snapshotDelete(*manualId).has_value());
    auto afterDelete = channel.snapshotList();
    ASSERT_TRUE(afterDelete.has_value());
    ASSERT_EQ(afterDelete->size(), 1U);
    EXPECT_EQ((*afterDelete)[0].id, autoMeta.id);
}

TEST_F(IpcRoundTripTest, SnapshotMutationsDeniedButListStaysUnprivileged) {
    startDaemon("deny-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto created = channel.snapshotCreate("nope");
    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error().code, Error::Code::Permission);
    auto restored = channel.snapshotRestore("deadbeef");
    ASSERT_FALSE(restored.has_value());
    EXPECT_EQ(restored.error().code, Error::Code::Permission);
    auto deleted = channel.snapshotDelete("deadbeef");
    ASSERT_FALSE(deleted.has_value());
    EXPECT_EQ(deleted.error().code, Error::Code::Permission);
    auto listed = channel.snapshotList();  // metadata only: no authorization needed
    ASSERT_TRUE(listed.has_value()) << listed.error().message;
    EXPECT_TRUE(listed->empty());
}

// The two failures are deliberately different codes: a well-formed id that
// names nothing is NotFound; a malformed id is InvalidArgs.
TEST_F(IpcRoundTripTest, UnknownIdIsNotFoundWhileMalformedIdIsInvalidArgs) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto restored = channel.snapshotRestore("deadbeef");  // valid hex, no such snapshot
    ASSERT_FALSE(restored.has_value());
    EXPECT_EQ(restored.error().code, Error::Code::NotFound);
    auto listed = channel.snapshotList();  // spec: a refused restore takes no snapshot
    ASSERT_TRUE(listed.has_value());
    EXPECT_TRUE(listed->empty());
    auto deleted = channel.snapshotDelete("deadbeef");
    ASSERT_FALSE(deleted.has_value());
    EXPECT_EQ(deleted.error().code, Error::Code::NotFound);
    auto traversal = channel.snapshotRestore("../evil");  // path-traversal guard
    ASSERT_FALSE(traversal.has_value());
    EXPECT_EQ(traversal.error().code, Error::Code::InvalidArgs);
    EXPECT_EQ(traversal.error().message, "invalid snapshot id");
}

TEST_F(IpcRoundTripTest, RestorePartialConvergenceReportsGuardRefusal) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    // Disable while harmless, capture that state manually, then re-enable.
    ASSERT_TRUE(channel.setDeviceEnabled(deviceAt(device_.string()), false).has_value());
    auto snapId = channel.snapshotCreate("disabled-era");
    ASSERT_TRUE(snapId.has_value()) << snapId.error().message;
    ASSERT_TRUE(channel.setDeviceEnabled(deviceAt(device_.string()), true).has_value());

    // The device now hosts the root filesystem's disk: the restore's re-apply
    // must be guard-refused — as an outcome ITEM, with the verb succeeding.
    const fs::path block = device_ / "1-4:1.0/host7/block/sdb";
    fs::create_directories(block);
    fs::create_directories(root_ / "class/block");
    fs::create_directory_symlink(block, root_ / "class/block/sdb");
    fs::create_directories(root_ / "dev");
    std::ofstream(root_ / "dev/sdb") << "";
    std::ofstream(root_ / "mounts") << (root_ / "dev/sdb").string() + " / ext4 rw 0 0\n";

    auto outcome = channel.snapshotRestore(*snapId);
    ASSERT_TRUE(outcome.has_value()) << outcome.error().message;
    EXPECT_EQ(outcome->snapshotId, *snapId);
    EXPECT_FALSE(outcome->safetySnapshotId.empty());
    ASSERT_EQ(outcome->items.size(), 1U);
    EXPECT_EQ(outcome->items[0].action, "re-apply-disable");
    EXPECT_EQ(outcome->items[0].status, "guard-refused");
    EXPECT_EQ(outcome->items[0].detail, "backs the root filesystem");
    EXPECT_EQ(readFile(device_ / "authorized"), "1");  // refusal is never bypassed
}

// --- Recovery CLI end-to-end (snapshot-cli spec) ------------------------------

TEST_F(IpcRoundTripTest, CliExitsFourWhenDaemonUnreachable) {
    // No daemon on the private bus: every verb must exit 4 naming the bus error.
    EXPECT_EQ(runCli("snapshot list").code, 4);
    EXPECT_EQ(runCli("snapshot create").code, 4);
    EXPECT_EQ(runCli("snapshot restore deadbeef").code, 4);
}

TEST_F(IpcRoundTripTest, CliSnapshotVerbsRoundTripThroughTheBus) {
    startDaemon("allow-all");

    // create prints the new full id and exits 0.
    auto created = runCli("snapshot create --label cli-made");
    ASSERT_EQ(created.code, 0) << created.out;
    const std::string id = trimmed(created.out);
    ASSERT_EQ(id.size(), 64U);

    // list (human) shows the short id; --json carries the full id.
    auto human = runCli("snapshot list");
    EXPECT_EQ(human.code, 0);
    EXPECT_NE(human.out.find(id.substr(0, 12)), std::string::npos);
    auto asJson = runCli("snapshot list --json");
    EXPECT_EQ(asJson.code, 0);
    EXPECT_NE(asJson.out.find(id), std::string::npos);

    // restore accepts a unique prefix and reports the outcome; exit 0.
    auto restored = runCli("snapshot restore " + id.substr(0, 12));
    EXPECT_EQ(restored.code, 0) << restored.out;
    EXPECT_NE(restored.out.find("restored " + id.substr(0, 12)), std::string::npos);

    // delete accepts a unique prefix; the store is empty afterwards.
    auto deleted = runCli("snapshot delete " + id.substr(0, 12));
    EXPECT_EQ(deleted.code, 0) << deleted.out;
    auto empty = runCli("snapshot list");
    EXPECT_EQ(empty.code, 0);
    EXPECT_NE(empty.out.find("(no snapshots)"), std::string::npos);
}
