#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "devmgr/platform/linux/dkms_status_provider.hpp"

namespace fs = std::filesystem;
using devmgr::platform_linux::DkmsStatusProvider;

class DkmsStatusProviderTest : public ::testing::Test {
   protected:
    void SetUp() override {
        root_ = fs::path(::testing::TempDir()) /
                ("dkms_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        dkms_ = root_ / "var-lib-dkms";
        mods_ = root_ / "lib-modules";
        fs::create_directories(dkms_);
        fs::create_directories(mods_);
    }
    void TearDown() override { fs::remove_all(root_); }
    // <dkmsRoot>/<mod>/<ver>/<kernel>/<arch>/module/<file>
    void addBuilt(const std::string& mod, const std::string& ver, const std::string& kernel,
                  const std::string& file) {
        const auto d = dkms_ / mod / ver / kernel / "x86_64" / "module";
        fs::create_directories(d);
        std::ofstream(d / file) << "elf";
        fs::create_directories(mods_ / kernel);  // kernel present unless test says otherwise
    }
    void addInstalled(const std::string& kernel, const std::string& file) {
        const auto d = mods_ / kernel / "updates" / "dkms";
        fs::create_directories(d);
        std::ofstream(d / file) << "elf";
    }
    DkmsStatusProvider provider() { return DkmsStatusProvider(dkms_.string(), mods_.string()); }
    fs::path root_, dkms_, mods_;
};

TEST_F(DkmsStatusProviderTest, UnavailableWithoutRoot) {
    DkmsStatusProvider p((root_ / "nope").string(), mods_.string());
    EXPECT_FALSE(p.availability().available);
}
TEST_F(DkmsStatusProviderTest, BuiltAndInstalledStates) {
    addBuilt("nvidia", "565.1", "6.8.0-49-generic", "nvidia.ko");
    addInstalled("6.8.0-49-generic", "nvidia.ko");
    auto r = provider().enumerate();
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->size(), 1U);
    EXPECT_EQ((*r)[0].id, "dkms:nvidia/565.1");
    EXPECT_FALSE((*r)[0].facts.updatable);  // status-only, never actionable (V1)
    ASSERT_FALSE((*r)[0].details.empty());
    EXPECT_NE((*r)[0].details[0].second.find("installed"), std::string::npos);
}
TEST_F(DkmsStatusProviderTest, BuiltNotInstalled) {
    addBuilt("hello", "1.0", "6.8.0-49-generic", "hello.ko");
    auto r = provider().enumerate();
    ASSERT_TRUE(r.has_value());
    EXPECT_NE((*r)[0].details[0].second.find("built"), std::string::npos);
    EXPECT_EQ((*r)[0].details[0].second.find("installed"), std::string::npos);
}
TEST_F(DkmsStatusProviderTest, CompressedExtensionsRecognized) {
    addBuilt("z1", "1", "k1", "z1.ko.xz");
    addBuilt("z2", "1", "k1", "z2.ko.gz");
    addBuilt("z3", "1", "k1", "z3.ko.zst");
    addInstalled("k1", "z1.ko.xz");
    auto r = provider().enumerate();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 3U);  // all three seen as built
}
TEST_F(DkmsStatusProviderTest, InstalledMatchByBasenameNotPackageName) {
    // dkms.conf BUILT_MODULE_NAME may differ from package name — match the
    // actual build-output basename (spec §6).
    addBuilt("pkgname", "2.0", "k1", "realmod.ko");
    addInstalled("k1", "realmod.ko");
    auto r = provider().enumerate();
    EXPECT_NE((*r)[0].details[0].second.find("installed"), std::string::npos);
}
TEST_F(DkmsStatusProviderTest, FailedBuildResidueIsUnknown) {
    fs::create_directories(dkms_ / "broken" / "1.0" / "k1" / "x86_64");  // no module/ output
    fs::create_directories(mods_ / "k1");
    auto r = provider().enumerate();
    ASSERT_EQ(r->size(), 1U);
    EXPECT_NE((*r)[0].details[0].second.find("unknown"), std::string::npos);
}
TEST_F(DkmsStatusProviderTest, AddedOnlyState) {
    fs::create_directories(dkms_ / "fresh" / "3.0" / "source");  // registration only
    auto r = provider().enumerate();
    ASSERT_EQ(r->size(), 1U);
    EXPECT_NE((*r)[0].details[0].second.find("added"), std::string::npos);
}
TEST_F(DkmsStatusProviderTest, KernelAbsentState) {
    addBuilt("old", "1.0", "5.15.0-gone", "old.ko");
    fs::remove_all(mods_ / "5.15.0-gone");
    auto r = provider().enumerate();
    EXPECT_NE((*r)[0].details[0].second.find("kernel absent"), std::string::npos);
}
TEST_F(DkmsStatusProviderTest, SymlinkOutsideRootNotFollowed) {
    addBuilt("evil", "1.0", "k1", "evil.ko");
    fs::create_directory_symlink("/etc", dkms_ / "evil" / "1.0" / "k1" / "x86_64" / "link");
    EXPECT_TRUE(provider().enumerate().has_value());  // no crash, no wandering walk
}
TEST_F(DkmsStatusProviderTest, SourceSymlinkSkipped) {
    fs::create_directories(dkms_ / "m" / "1" / "k1" / "x86_64" / "module");
    std::ofstream(dkms_ / "m" / "1" / "k1" / "x86_64" / "module" / "m.ko") << "e";
    fs::create_directories(mods_ / "k1");
    fs::create_directory_symlink(root_, dkms_ / "m" / "1" / "source");  // standard dkms layout
    auto r = provider().enumerate();
    ASSERT_EQ(r->size(), 1U);  // "source" is not a kernel dir
}
