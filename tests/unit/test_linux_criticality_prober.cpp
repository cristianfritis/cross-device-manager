#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "devmgr/platform/linux/linux_criticality_prober.hpp"

namespace fs = std::filesystem;
using devmgr::platform_linux::LinuxCriticalityProber;

namespace {
// Builds a miniature sysfs+/dev+mounts world in a temp dir. Layout mirrors
// real sysfs: class/{block,input} entries are symlinks into devices/.
class ProberTest : public ::testing::Test {
   protected:
    fs::path root_;

    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("devmgr-prober-" + std::to_string(::getpid()) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::create_directories(root_ / "class/block");
        fs::create_directories(root_ / "class/input");
        fs::create_directories(root_ / "dev");
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    // A block device living under `devicesSubdir`, linked from class/block
    // and from /dev. Returns the canonical device dir.
    fs::path addBlock(const std::string& name, const std::string& devicesSubdir) {
        const fs::path dir = root_ / "devices" / devicesSubdir / "block" / name;
        fs::create_directories(dir);
        fs::create_directory_symlink(dir, root_ / "class/block" / name);
        std::ofstream(root_ / "dev" / name) << "";  // realpath target for mounts source
        return fs::canonical(dir);
    }

    fs::path addInput(const std::string& name, const std::string& devicesSubdir,
                      const std::string& keyBits, const std::string& relBits) {
        const fs::path dir = root_ / "devices" / devicesSubdir / "input" / name;
        fs::create_directories(dir / "capabilities");
        std::ofstream(dir / "capabilities/key") << keyBits;
        std::ofstream(dir / "capabilities/rel") << relBits;
        fs::create_directory_symlink(dir, root_ / "class/input" / name);
        return fs::canonical(dir);
    }

    void writeMounts(const std::string& content) { std::ofstream(root_ / "mounts") << content; }

    LinuxCriticalityProber prober() {
        return LinuxCriticalityProber(root_.string(), (root_ / "mounts").string());
    }
};

// capabilities/key with KEY_Q..KEY_P (codes 16-25) set = 0x3ff0000; a real
// keyboard line has more words — the parser must index from the RIGHT.
constexpr const char* kKeyboardKey = "3ff0000";
constexpr const char* kNoKeys = "0";
}  // namespace

TEST_F(ProberTest, RootMountResolvesToBlockDevicePath) {
    const auto sda = addBlock("sda1", "pci0000:00/ata1/host0");
    writeMounts(root_.string() + "/dev/sda1 / ext4 rw 0 0\n");
    auto facts = prober().probe();
    ASSERT_TRUE(facts.has_value()) << facts.error().message;
    ASSERT_EQ(facts->rootBackingPaths.size(), 1u);
    EXPECT_EQ(facts->rootBackingPaths[0], sda.string());
    EXPECT_TRUE(facts->bootBackingPaths.empty());
}

TEST_F(ProberTest, BootMountIsCollectedSeparately) {
    addBlock("sda1", "pci0000:00/ata1/host0");
    const auto sda2 = addBlock("sda2", "pci0000:00/ata1/host0");
    writeMounts(root_.string() + "/dev/sda1 / ext4 rw 0 0\n" + root_.string() +
                "/dev/sda2 /boot ext4 rw 0 0\n");
    auto facts = prober().probe();
    ASSERT_TRUE(facts.has_value());
    ASSERT_EQ(facts->bootBackingPaths.size(), 1u);
    EXPECT_EQ(facts->bootBackingPaths[0], sda2.string());
}

TEST_F(ProberTest, DmDeviceExpandsThroughSlaves) {
    // dm-0 is virtual; its slaves/ entry names the physical sda2.
    const auto sda2 = addBlock("sda2", "pci0000:00/usb1/1-4/host7");
    const fs::path dm = root_ / "devices/virtual/block/dm-0";
    fs::create_directories(dm / "slaves");
    fs::create_directory_symlink(root_ / "class/block/sda2", dm / "slaves/sda2");
    fs::create_directory_symlink(dm, root_ / "class/block/dm-0");
    std::ofstream(root_ / "dev/dm-0") << "";
    writeMounts(root_.string() + "/dev/dm-0 / ext4 rw 0 0\n");

    auto facts = prober().probe();
    ASSERT_TRUE(facts.has_value());
    ASSERT_EQ(facts->rootBackingPaths.size(), 1u);
    EXPECT_EQ(facts->rootBackingPaths[0], sda2.string());
}

TEST_F(ProberTest, NonDevSourcesAreSkipped) {
    writeMounts("tmpfs / tmpfs rw 0 0\n");
    auto facts = prober().probe();
    ASSERT_TRUE(facts.has_value());
    EXPECT_TRUE(facts->rootBackingPaths.empty());
}

TEST_F(ProberTest, ClassifiesKeyboardAndPointer) {
    const auto kb = addInput("input5", "pci0000:00/usb1/1-3/1-3:1.0", kKeyboardKey, "0");
    const auto mouse = addInput("input6", "pci0000:00/usb1/1-4/1-4:1.0", kNoKeys, "3");
    writeMounts("");
    auto facts = prober().probe();
    ASSERT_TRUE(facts.has_value());
    ASSERT_EQ(facts->keyboardPaths.size(), 1u);
    EXPECT_EQ(facts->keyboardPaths[0], kb.string());
    ASSERT_EQ(facts->pointerPaths.size(), 1u);
    EXPECT_EQ(facts->pointerPaths[0], mouse.string());
}

TEST_F(ProberTest, MultiWordKeyBitmapIndexesFromTheRight) {
    // Real keyboards print many words: "... 3ff0000 0 0 0" style. Codes 16-25
    // live in the LAST (least significant) word.
    const auto kb = addInput("input7", "platform/i8042/serio0", "fffffffff 3ff0000", "0");
    writeMounts("");
    auto facts = prober().probe();
    ASSERT_TRUE(facts.has_value());
    ASSERT_EQ(facts->keyboardPaths.size(), 1u);
    EXPECT_EQ(facts->keyboardPaths[0], kb.string());
}

TEST_F(ProberTest, MissingMountsFileIsAnError) {
    auto facts = LinuxCriticalityProber(root_.string(), (root_ / "nope").string()).probe();
    EXPECT_FALSE(facts.has_value());
}
