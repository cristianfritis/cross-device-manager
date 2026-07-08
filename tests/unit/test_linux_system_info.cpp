#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "devmgr/platform/linux/linux_system_info.hpp"

namespace fs = std::filesystem;
namespace pl = devmgr::platform_linux;

class LinuxSystemInfoTest : public ::testing::Test {
   protected:
    fs::path dir_;
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("devmgr-sysinfo-" + std::string(::testing::UnitTest::GetInstance()
                                                    ->current_test_info()
                                                    ->name()));
        fs::create_directories(dir_ / "efivars");
        fs::create_directories(dir_ / "security");
    }
    void TearDown() override { fs::remove_all(dir_); }
};

TEST_F(LinuxSystemInfoTest, SecureBootReadsByteFourOfTheEfiVariable) {
    // 4-byte attribute header + value byte 0x01 = enabled.
    std::ofstream(dir_ / "efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c",
                  std::ios::binary)
        .write("\x06\x00\x00\x00\x01", 5);
    EXPECT_TRUE(pl::readSecureBoot((dir_ / "efivars").string()));
}

TEST_F(LinuxSystemInfoTest, NoEfivarsMeansBiosBootSecureBootOff) {
    EXPECT_FALSE(pl::readSecureBoot((dir_ / "no-such-dir").string()));
}

TEST_F(LinuxSystemInfoTest, LockdownParsesTheBracketedToken) {
    std::ofstream(dir_ / "security/lockdown") << "none [integrity] confidentiality\n";
    EXPECT_EQ(pl::readLockdownMode((dir_ / "security").string()), "integrity");
}

TEST_F(LinuxSystemInfoTest, MissingLockdownFileMeansNone) {
    EXPECT_EQ(pl::readLockdownMode((dir_ / "security").string()), "none");
}

TEST_F(LinuxSystemInfoTest, PrettyNameParsedFromOsRelease) {
    std::ofstream(dir_ / "os-release") << "NAME=Gentoo\nPRETTY_NAME=\"Gentoo Linux\"\n";
    EXPECT_EQ(pl::readPrettyName((dir_ / "os-release").string()), "Gentoo Linux");
}

TEST_F(LinuxSystemInfoTest, QueryFillsKernelVersionAndDefaults) {
    pl::LinuxSystemInfo::Paths p;
    p.osRelease = (dir_ / "missing").string();
    p.efivarsDir = (dir_ / "efivars").string();
    p.securityDir = (dir_ / "security").string();
    pl::LinuxSystemInfo info(p);
    auto r = info.query();
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r->kernelVersion.empty());  // uname(2) always answers
    EXPECT_FALSE(r->rebootPending);          // honest Phase 6 stub
    EXPECT_EQ(r->lockdownMode, "none");
}
