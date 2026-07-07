#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "devmgr/daemon/authority.hpp"
#include "devmgr/daemon/request_processor.hpp"

namespace fs = std::filesystem;
using devmgr::core::Error;
using namespace devmgr;

namespace {

class RecordingController final : public pal::IDeviceController {
   public:
    core::Result<std::optional<std::string>> setEnabled(
        const std::string& sysfsPath, bool enabled,
        const std::string& /*rebindDriverHint*/) override {
        calls.push_back({sysfsPath, enabled});
        if (!next) return tl::unexpected(next.error());
        return std::optional<std::string>{};
    }
    core::Result<void> bindDriver(const std::string&, const std::string&) override { return {}; }
    core::Result<void> unbindDriver(const std::string&) override { return {}; }
    struct Call {
        std::string sysfsPath;
        bool enabled;
    };
    std::vector<Call> calls;
    core::Result<void> next = {};
};

class StubProber final : public pal::ICriticalityProber {
   public:
    core::Result<pal::CriticalityFacts> probe() override {
        ++probes;
        return next;
    }
    int probes = 0;
    core::Result<pal::CriticalityFacts> next = pal::CriticalityFacts{};
};

class StubAuthority final : public daemon::IAuthority {
   public:
    core::Result<bool> checkAuthorized(const daemon::CallerId& caller,
                                       const std::string& actionId) override {
        ++checks;
        lastAction = actionId;
        lastCaller = caller;
        return next;
    }
    int checks = 0;
    std::string lastAction;
    daemon::CallerId lastCaller;
    core::Result<bool> next = true;
};

// The processor canonicalizes and containment-checks paths itself, so the
// tests need a real directory to point at.
class RequestProcessorTest : public ::testing::Test {
   protected:
    fs::path root_;
    fs::path device_;
    RecordingController controller_;
    StubProber prober_;
    StubAuthority authority_;

    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("devmgr-reqproc-" + std::to_string(::getpid()) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        device_ = root_ / "devices/pci0000:00/usb1/1-4";
        fs::create_directories(device_);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    daemon::RequestProcessor processor() {
        return daemon::RequestProcessor(controller_, prober_, authority_, root_.string());
    }
};

}  // namespace

TEST_F(RequestProcessorTest, HappyPathDisablesViaControllerWithCanonicalPath) {
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.42", device_.string(), false);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(controller_.calls.size(), 1u);
    EXPECT_EQ(controller_.calls[0].sysfsPath, fs::weakly_canonical(device_).string());
    EXPECT_FALSE(controller_.calls[0].enabled);
    EXPECT_EQ(authority_.checks, 1);
    EXPECT_EQ(authority_.lastAction, daemon::kActionSetDeviceEnabled);
    EXPECT_EQ(authority_.lastCaller, ":1.42");
}

TEST_F(RequestProcessorTest, GuardRefusalShortCircuitsBeforeAuthorityAndController) {
    pal::CriticalityFacts f;
    f.rootBackingPaths = {fs::weakly_canonical(device_).string() + "/host0/block/sdb"};
    prober_.next = f;
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", device_.string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Conflict);
    EXPECT_EQ(r.error().message, "backs the root filesystem");
    EXPECT_EQ(authority_.checks, 0);  // no password prompt for a doomed request
    EXPECT_TRUE(controller_.calls.empty());
}

TEST_F(RequestProcessorTest, EnableSkipsGuardButStillAuthorizes) {
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", device_.string(), true);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(prober_.probes, 0);  // re-enabling can't hurt — guard not consulted
    EXPECT_EQ(authority_.checks, 1);
}

TEST_F(RequestProcessorTest, DeniedAuthorityIsPermissionAndBlocksController) {
    authority_.next = false;
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", device_.string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Permission);
    EXPECT_EQ(r.error().message, "authorization denied");
    EXPECT_TRUE(controller_.calls.empty());
}

TEST_F(RequestProcessorTest, AuthorityErrorPropagates) {
    authority_.next = core::makeError(Error::Code::Io, "polkit unavailable");
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", device_.string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Io);
    EXPECT_TRUE(controller_.calls.empty());
}

TEST_F(RequestProcessorTest, PathOutsideRootIsNotFoundBeforeEverything) {
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", "/etc/passwd", false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
    EXPECT_EQ(prober_.probes, 0);
    EXPECT_EQ(authority_.checks, 0);
    EXPECT_TRUE(controller_.calls.empty());
}

TEST_F(RequestProcessorTest, MissingDeviceDirIsNotFound) {
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", (root_ / "devices/ghost").string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}

TEST_F(RequestProcessorTest, ProberErrorPropagatesOnDisable) {
    prober_.next = core::makeError(Error::Code::Io, "mounts unreadable");
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", device_.string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Io);
    EXPECT_EQ(authority_.checks, 0);
}
