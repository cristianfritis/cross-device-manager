#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "devmgr/platform/linux/cab_resolver.hpp"

namespace fs = std::filesystem;
using namespace devmgr::platform_linux;
using devmgr::core::Error;

class CabResolverTest : public ::testing::Test {
   protected:
    void SetUp() override {
        root_ = fs::path(::testing::TempDir()) /
                ("cab_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(root_ / "cabs");
    }
    void TearDown() override { fs::remove_all(root_); }
    std::string writeCab(const std::string& rel, std::size_t bytes = 16) {
        const fs::path p = root_ / "cabs" / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p) << std::string(bytes, 'x');
        return p.string();
    }
    RemoteRef dirRemote() { return {"r1", "directory", (root_ / "cabs").string()}; }
    RemoteRef dlRemote() { return {"lvfs", "download", (root_ / "meta.xml.zst").string()}; }
    fs::path root_;
};

TEST_F(CabResolverTest, AbsolutePathResolves) {
    const auto abs = writeCab("a.cab");
    auto r = resolveAndOpenCab({abs}, {dirRemote()}, "r1", 16);
    ASSERT_TRUE(r.has_value());
    EXPECT_GE(r->fd.get(), 0);
    EXPECT_EQ(r->sizeBytes, 16U);
}
TEST_F(CabResolverTest, FileUriResolves) {
    const auto abs = writeCab("b.cab");
    ASSERT_TRUE(resolveAndOpenCab({"file://" + abs}, {}, "", 16).has_value());
}
TEST_F(CabResolverTest, RelativeResolvesOnlyAgainstDirectoryRemote) {
    writeCab("c.cab");
    EXPECT_TRUE(resolveAndOpenCab({"c.cab"}, {dirRemote()}, "r1", 16).has_value());
    const auto r = resolveAndOpenCab({"c.cab"}, {dlRemote()}, "lvfs", 16);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Unsupported);
}
TEST_F(CabResolverTest, HttpsIsUnsupported) {
    const auto r = resolveAndOpenCab({"https://lvfs.example/x.cab"}, {dirRemote()}, "r1", 16);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Unsupported);
}
TEST_F(CabResolverTest, EmptyAndUnknownSchemeUnsupported) {
    EXPECT_FALSE(resolveAndOpenCab({""}, {dirRemote()}, "r1", 16).has_value());
    EXPECT_FALSE(resolveAndOpenCab({"ftp://x/y.cab"}, {dirRemote()}, "r1", 16).has_value());
    EXPECT_FALSE(resolveAndOpenCab({}, {dirRemote()}, "r1", 16).has_value());
}
TEST_F(CabResolverTest, TraversalEscapeRejected) {
    writeCab("d.cab");
    std::ofstream(root_ / "outside.cab") << "xxxxxxxxxxxxxxxx";
    const auto r = resolveAndOpenCab({"../outside.cab"}, {dirRemote()}, "r1", 16);
    ASSERT_FALSE(r.has_value());  // escapes the directory-remote root
}
TEST_F(CabResolverTest, SymlinkRejected) {
    const auto real = writeCab("real.cab");
    fs::create_symlink(real, root_ / "cabs" / "link.cab");
    EXPECT_FALSE(resolveAndOpenCab({"link.cab"}, {dirRemote()}, "r1", 16).has_value());
}
TEST_F(CabResolverTest, NonRegularFileRejected) {
    fs::create_directory(root_ / "cabs" / "adir.cab");
    EXPECT_FALSE(resolveAndOpenCab({"adir.cab"}, {dirRemote()}, "r1", 16).has_value());
}
TEST_F(CabResolverTest, OversizeRejectedAndZeroSizeRejected) {
    writeCab("big.cab", 64);
    // expectedSize 16 → cap = 24 (×1.5) → 64 rejected
    EXPECT_FALSE(resolveAndOpenCab({"big.cab"}, {dirRemote()}, "r1", 16).has_value());
    writeCab("empty.cab", 0);
    EXPECT_FALSE(resolveAndOpenCab({"empty.cab"}, {dirRemote()}, "r1", 16).has_value());
    // expectedSize 0 (metadata absent) → hard cap only → 64 accepted
    EXPECT_TRUE(resolveAndOpenCab({"big.cab"}, {dirRemote()}, "r1", 0).has_value());
}
TEST_F(CabResolverTest, FirstResolvableLocationWins) {
    writeCab("e.cab");
    EXPECT_TRUE(
        resolveAndOpenCab({"https://x/y.cab", "e.cab"}, {dirRemote()}, "r1", 16).has_value());
}
TEST_F(CabResolverTest, IsLocallyResolvableMirrorsRules) {
    EXPECT_TRUE(isLocallyResolvable({"x.cab"}, {dirRemote()}, "r1"));
    EXPECT_FALSE(isLocallyResolvable({"x.cab"}, {dlRemote()}, "lvfs"));
    EXPECT_FALSE(isLocallyResolvable({"https://x/y.cab"}, {dlRemote()}, "lvfs"));
    EXPECT_TRUE(isLocallyResolvable({"file:///tmp/x.cab"}, {}, ""));
    EXPECT_FALSE(isLocallyResolvable({}, {}, ""));
}
