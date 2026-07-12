#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sdbus-c++/sdbus-c++.h>

#include "devmgr/core/events.hpp"
#include "devmgr/core/result.hpp"
#include "devmgr/core/update_models.hpp"
#include "devmgr/platform/linux/fwupd_contract.hpp"
#include "devmgr/platform/linux/fwupd_update_provider.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "fwupd/fake_fwupd_daemon.hpp"

namespace fw = devmgr::platform_linux::fwupd;
namespace core = devmgr::core;
using Dict = fw::Dict;
using devmgr::platform_linux::FwupdUpdateProvider;
using devmgr::test::FakeFwupdDaemon;

namespace {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — a{sv} dict builder, wire-key order
Dict makeDevice(const std::string& id, const std::string& version, bool updatable) {
    Dict d;
    d["DeviceId"] = sdbus::Variant{id};
    d["Name"] = sdbus::Variant{std::string{"Device " + id}};
    d["Vendor"] = sdbus::Variant{std::string{"ACME"}};
    d["Version"] = sdbus::Variant{version};
    const std::uint64_t flags = updatable ? fw::kDeviceFlagUpdatable : 0ULL;
    d["Flags"] = sdbus::Variant{flags};
    return d;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — a{sv} dict builder, wire-key order
Dict makeRelease(const std::string& version, const std::string& remoteId,
                 const std::string& checksum, const std::vector<std::string>& locations) {
    Dict r;
    r["Version"] = sdbus::Variant{version};
    r["RemoteId"] = sdbus::Variant{remoteId};
    r["Checksum"] = sdbus::Variant{checksum};
    r["Locations"] = sdbus::Variant{locations};
    return r;
}

// FwupdRemoteKind (fwupd-remote.h): DIRECTORY=3 — only directory-kind remotes
// unlock relative-location cab resolution (cab_resolver.cpp, spec §5.3).
constexpr std::uint32_t kRemoteKindDirectory = 3;

Dict makeRemote(const std::string& id, std::uint32_t kind, const std::string& filenameCache) {
    Dict r;
    r["RemoteId"] = sdbus::Variant{id};
    r["Type"] = sdbus::Variant{kind};
    r["FilenameCache"] = sdbus::Variant{filenameCache};
    return r;
}

Dict makeHistoryEntry(const std::string& deviceId, std::uint32_t updateState,
                      const std::string& version) {
    Dict h;
    h["DeviceId"] = sdbus::Variant{deviceId};
    h["Name"] = sdbus::Variant{std::string{"Device " + deviceId}};
    h["UpdateState"] = sdbus::Variant{updateState};
    h["Version"] = sdbus::Variant{version};
    return h;
}

const core::UpdateCandidate* findCandidate(const std::vector<core::UpdateCandidate>& v,
                                           const std::string& id) {
    const auto it =
        std::ranges::find_if(v, [&](const core::UpdateCandidate& c) { return c.id == id; });
    return it == v.end() ? nullptr : &*it;
}

// The "upgrades" detail value (the provider's query-failure note), if present.
std::optional<std::string> upgradesDetail(const core::UpdateCandidate& c) {
    for (const auto& [key, value] : c.details)
        if (key == "upgrades") return value;
    return std::nullopt;
}

// Row-shape probe as an AssertionResult: keeps each TestBody's cognitive
// complexity under the tidy threshold (gtest macros are costly there) while
// preserving useful failure messages.
::testing::AssertionResult emptyRow(const core::UpdateCandidate* c, bool expectQueryFailure) {
    if (c == nullptr) return ::testing::AssertionFailure() << "device row missing";
    if (!c->releases.empty())
        return ::testing::AssertionFailure() << c->releases.size() << " release(s), expected none";
    const auto detail = upgradesDetail(*c);
    if (expectQueryFailure && !detail.value_or("").starts_with("query failed"))
        return ::testing::AssertionFailure()
               << "expected a 'query failed' upgrades detail, got: " << detail.value_or("(none)");
    if (!expectQueryFailure && detail.has_value())
        return ::testing::AssertionFailure() << "unexpected upgrades detail: " << *detail;
    return ::testing::AssertionSuccess();
}

// Subscribes to `Event`, runs `trigger` (expected to cause the fake daemon to
// emit a D-Bus signal the provider forwards onto `bus`), then waits up to 2s
// on a condition_variable — never sleep-polls. Returns nullopt on timeout.
template <class Event>
std::optional<Event> waitForEvent(devmgr::runtime::EventBus& bus,
                                  const std::function<void()>& trigger) {
    std::mutex m;
    std::condition_variable cv;
    std::optional<Event> received;
    auto sub = bus.subscribe<Event>([&](const Event& e) {
        std::scoped_lock lock(m);
        received = e;
        cv.notify_all();
    });
    trigger();
    std::unique_lock<std::mutex> lock(m);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return received.has_value(); });
    return received;
}

// Fixture: fake daemon first (claims the name), then the real provider
// pointed at the private session bus (dbus-run-session; see CMakeLists.txt).
// Declaration order matters for teardown: members are destroyed in reverse
// order, so provider_ (the client) is destroyed before daemon_ (the server)
// before bus_ — never the other way around.
class F : public ::testing::Test {
   public:  // all-public members: cppcoreguidelines-non-private-member-variables
    devmgr::runtime::EventBus bus_;
    std::unique_ptr<FakeFwupdDaemon> daemon_;
    std::unique_ptr<FwupdUpdateProvider> provider_;

    void SetUp() override {
        daemon_ = std::make_unique<FakeFwupdDaemon>();
        provider_ = std::make_unique<FwupdUpdateProvider>(
            bus_, FwupdUpdateProvider::Config{.useSessionBus = true});
    }
};

}  // namespace

TEST_F(F, AvailabilityReportsVersion) {
    const auto a = provider_->availability();
    EXPECT_TRUE(a.available);
    EXPECT_EQ(a.version.value_or("(absent)"), "2.0.20-fake");
}

TEST_F(F, EnumerateMergesDevicesUpgradesRemotes) {
    daemon_->setDevices({makeDevice("devA", "1.0", true), makeDevice("devB", "2.0", false)});
    daemon_->setRemotes({makeRemote("vendor-dir", kRemoteKindDirectory, "/opt/vendor/cabs")});
    // https:// location → never locally resolvable; relative location tied to
    // a directory-kind remote → resolvable (cab_resolver.cpp string pre-filter).
    auto relHttps = makeRelease("1.0.1", "lvfs", "cs-https", {"https://example.com/y.cab"});
    auto relDir = makeRelease("1.0.2", "vendor-dir", "cs-dir", {"relative.cab"});
    daemon_->setUpgrades("devA", {relHttps, relDir});

    const auto result = provider_->enumerate();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->size(), 2U);

    const auto* devA = findCandidate(*result, "devA");
    ASSERT_NE(devA, nullptr);
    ASSERT_EQ(devA->releases.size(), 2U);
    EXPECT_FALSE(devA->releases[0].localCab);  // https
    EXPECT_TRUE(devA->releases[1].localCab);   // relative + directory remote
    // candidateVersion = releases.front().version (fake's order preserved — NO version sort).
    EXPECT_EQ(devA->candidateVersion.value_or("(absent)"), devA->releases.front().version);
}

TEST_F(F, NothingToDoUpgradesMeansEmptyNotError) {
    daemon_->setDevices({makeDevice("devA", "1.0", true)});
    daemon_->setUpgrades("devA", {});  // explicitly empty ⇒ NothingToDo, not an error

    const auto result = provider_->enumerate();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(emptyRow(findCandidate(*result, "devA"), /*expectQueryFailure=*/false));
}

TEST_F(F, PartialUpgradesFailureKeepsRow) {
    daemon_->setDevices({makeDevice("devA", "1.0", true), makeDevice("devB", "2.0", true)});
    daemon_->setUpgradesError("devA", "org.freedesktop.fwupd.Internal", "boom");
    daemon_->setUpgrades("devB", {makeRelease("2.1", "lvfs", "cs-b", {"https://x/b.cab"})});

    const auto result = provider_->enumerate();
    ASSERT_TRUE(result.has_value()) << result.error().message;  // overall enumerate() is ok
    EXPECT_TRUE(emptyRow(findCandidate(*result, "devA"), /*expectQueryFailure=*/true));
    const auto* devB = findCandidate(*result, "devB");
    EXPECT_TRUE(devB != nullptr && !devB->releases.empty());  // B unaffected by A's failure
}

TEST_F(F, MalformedVariantOverRealBus) {
    Dict device = makeDevice("devA", "1.0", true);
    device["Flags"] = sdbus::Variant{std::string{"not-a-number"}};  // wrong wire type
    daemon_->setDevices({device});

    const auto result = provider_->enumerate();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->size(), 1U);
    EXPECT_EQ((*result)[0].id, "devA");          // row survives
    EXPECT_FALSE((*result)[0].facts.updatable);  // malformed Flags ⇒ defaulted, not fatal
}

TEST_F(F, DuplicateReleasesDeduped) {
    daemon_->setDevices({makeDevice("devA", "1.0", true)});
    const auto release = makeRelease("2.0", "lvfs", "cs-dup", {"https://x/y.cab"});
    daemon_->setUpgrades("devA", {release, release});  // same (remoteId, checksum) twice

    const auto result = provider_->enumerate();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    const auto* devA = findCandidate(*result, "devA");
    ASSERT_NE(devA, nullptr);
    EXPECT_EQ(devA->releases.size(), 1U);
}

TEST_F(F, PendingActionsFromHistory) {
    daemon_->setHistory({makeHistoryEntry("devA", fw::kUpdateStateNeedsReboot, "1.2.3")});

    const auto result = provider_->pendingActions();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->size(), 1U);
    EXPECT_EQ((*result)[0].deviceId, "devA");
    EXPECT_EQ((*result)[0].disposition, core::InstallDisposition::NeedsReboot);
}

TEST_F(F, FailedHistoryBecomesAvailabilityNotice) {
    daemon_->setHistory({makeHistoryEntry("devA", fw::kUpdateStateFailed, "1.0")});

    const auto a = provider_->availability();
    EXPECT_TRUE(a.available);
    ASSERT_EQ(a.notices.size(), 1U);
    EXPECT_EQ(a.notices[0], "1 previous update(s) failed — see fwupdmgr history");
}

TEST_F(F, DaemonRestartRecovers) {
    EXPECT_TRUE(provider_->availability().available);

    daemon_->dropName();
    EXPECT_FALSE(provider_->availability().available);

    daemon_->reacquireName();
    EXPECT_TRUE(provider_->availability().available);
    daemon_->setDevices({makeDevice("devA", "1.0", false)});
    const auto e = provider_->enumerate();
    EXPECT_TRUE(e.has_value()) << e.error().message;
}

TEST_F(F, DeviceAddedSignalPublishesUpdatesChanged) {
    const auto ev = waitForEvent<core::UpdatesChangedEvent>(
        bus_, [&] { daemon_->emitDeviceAdded(makeDevice("devX", "1.0", false)); });
    EXPECT_TRUE(ev.has_value()) << "UpdatesChangedEvent never arrived within the timeout";
}

TEST_F(F, DeviceRequestPublishesUpdateRequestEvent) {
    Dict request;
    request["DeviceId"] = sdbus::Variant{std::string{"devX"}};
    request["RequestKind"] = sdbus::Variant{std::uint32_t{2}};  // IMMEDIATE
    request["UpdateMessage"] = sdbus::Variant{std::string{"Unplug and replug the device"}};

    const auto ev =
        waitForEvent<core::UpdateRequestEvent>(bus_, [&] { daemon_->emitDeviceRequest(request); });
    ASSERT_TRUE(ev.has_value()) << "UpdateRequestEvent never arrived within the timeout";
    // One joined comparison (provider|device|kind|message) keeps the TestBody
    // under the cognitive-complexity tidy gate; a mismatch shows both strings.
    const auto req = ev.value_or(core::UpdateRequestEvent{});
    EXPECT_EQ(req.providerId + "|" + req.deviceId + "|" + req.kind + "|" + req.message,
              "fwupd|devX|immediate|Unplug and replug the device");
}
