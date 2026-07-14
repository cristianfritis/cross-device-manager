#include <unistd.h>  // ::pread — T8 fd-passing proof reads the cab back

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
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

// ---- T8 install-path helpers ----

// FWUPD_STATUS_DEVICE_WRITE (fwupd-enums.h) — the status the fake reports
// while "flashing"; fwupd::statusName(5) == "device-write".
constexpr std::uint32_t kStatusDeviceWrite = 5;

constexpr const char* kCabBytes = "FAKE-CAB-BYTES: not a real archive\n";

// Percentage waypoints the fake reports mid-install, and their expected
// rendering in the collected log. Deliberately non-monotonic (50→30): the
// provider must forward each verbatim, never reorder or filter. Opaque values;
// only their appearance in the log matters.
constexpr std::array<std::uint32_t, 4> kProgressWaypoints{10, 50, 30, 100};
constexpr const char* kProgressJoined =
    "10:device-write|50:device-write|30:device-write|100:device-write";
// A percentage that never appears in kProgressWaypoints, so its absence from a
// log proves a stray (idle/late) progress signal was dropped by the V5 gate.
constexpr std::uint32_t kStrayPercent = 7;
// Storm test: emits to observe before destroying the provider, then more after.
constexpr int kStormWarmup = 20;
constexpr int kStormPostDtor = 50;

constexpr std::size_t kReadBufBytes = 4096;

// Reads the received fd's content WITHOUT moving the shared file offset
// (the fd is a dup of the provider's — fd-passing proof for M2).
std::string readAll(int fd) {
    std::string out(kReadBufBytes, '\0');
    const ssize_t n = ::pread(fd, out.data(), out.size(), 0);
    out.resize(n > 0 ? static_cast<std::size_t>(n) : 0);
    return out;
}

Dict resultsDict(std::uint32_t updateState) {
    Dict d;
    d["UpdateState"] = sdbus::Variant{updateState};
    return d;
}

// Thread-safe progress collector: the reporter fires on the provider's D-Bus
// event-loop thread while the test thread owns install()'s call stack.
struct ProgressLog {
    std::mutex m;
    std::vector<devmgr::runtime::ProgressUpdate> updates;

    devmgr::runtime::ProgressReporter reporter() {
        return [this](const devmgr::runtime::ProgressUpdate& u) {
            std::scoped_lock lock(m);
            updates.push_back(u);
        };
    }

    // "10:device-write|50:device-write|..." — one string comparison per test
    // keeps TestBody cognitive complexity down and failures readable.
    std::string joined() {
        std::scoped_lock lock(m);
        std::string out;
        for (const auto& u : updates) {
            if (!out.empty()) out += "|";
            out += std::to_string(u.percent) + ":" + u.stage;
        }
        return out;
    }
};

devmgr::runtime::ProgressReporter noProgress() {
    return [](const devmgr::runtime::ProgressUpdate&) {};
}

// Cross-thread coordination for install-hook tests: a scripted install hook
// runs on the fake's D-Bus dispatch thread while the assertions run on the
// test thread. Flag lets one thread wait for the other without sleep-polling;
// every wait is bounded, so a logic error surfaces as a 2s timeout instead of
// a hung suite.
struct Flag {
    std::mutex m;
    std::condition_variable cv;
    bool raised = false;

    void raise() {
        {
            std::scoped_lock lock(m);
            raised = true;
        }
        cv.notify_all();
    }

    bool wait(std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        std::unique_lock<std::mutex> lock(m);
        return cv.wait_for(lock, timeout, [&] { return raised; });
    }
};

// install() failed with exactly `code`. Collapsing the has_value + code checks
// into one AssertionResult keeps each TestBody's cognitive complexity under the
// tidy threshold (gtest macros are costly there) while preserving readable
// failures.
::testing::AssertionResult failedWith(const core::Result<core::InstallOutcome>& r,
                                      core::Error::Code code) {
    if (r.has_value()) return ::testing::AssertionFailure() << "expected a failure, got success";
    if (r.error().code != code)
        return ::testing::AssertionFailure() << "wrong error code, message: " << r.error().message;
    return ::testing::AssertionSuccess();
}

// As failedWith, and additionally the fake's Install hook never ran (the
// refusal happened before any D-Bus Install).
::testing::AssertionResult refusedWithoutDbus(const core::Result<core::InstallOutcome>& r,
                                              core::Error::Code code,
                                              const std::atomic<bool>& hookRan) {
    const auto failed = failedWith(r, code);
    if (!failed) return failed;
    if (hookRan.load()) return ::testing::AssertionFailure() << "daemon Install ran before refusal";
    return ::testing::AssertionSuccess();
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
    std::filesystem::path cabDir_;  // per-test directory-remote root (T8)

    void SetUp() override {
        // Cab fixture (T8): a per-test directory-remote root with one real
        // cab file the resolver can open — unique per test name so parallel
        // or re-run suites never collide.
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        cabDir_ = std::filesystem::path(::testing::TempDir()) /
                  (std::string{"fwupd-cab-"} + info->name());
        std::filesystem::create_directories(cabDir_);
        std::ofstream(cabDir_ / "fixture.cab") << kCabBytes;

        daemon_ = std::make_unique<FakeFwupdDaemon>();
        provider_ = std::make_unique<FwupdUpdateProvider>(
            bus_, FwupdUpdateProvider::Config{.useSessionBus = true});
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(cabDir_, ec);  // best-effort cleanup
    }

    // Standard installable device: devA @1.2.3 (updatable) with one release
    // 1.2.4 whose location resolves inside the directory remote. Returns the
    // ReleaseRef install() must be called with.
    core::ReleaseRef scriptInstallableDevice(const std::vector<std::string>& locations = {
                                                 "fixture.cab"}) {
        daemon_->setDevices({makeDevice("devA", "1.2.3", true)});
        daemon_->setRemotes({makeRemote("vendor-dir", kRemoteKindDirectory, cabDir_.string())});
        daemon_->setUpgrades("devA", {makeRelease("1.2.4", "vendor-dir", "cs-1", locations)});
        return core::ReleaseRef{.remoteId = "vendor-dir", .checksum = "cs-1"};
    }

    // Scripts the fake so a subsequent install("devA") succeeds: it reports
    // each waypoint as progress, then "applies" newVersion and records `state`.
    // When cabOut is given, the received cab fd's bytes are copied there under
    // *cabMu first (M2 fd-passing proof). Keeps hook complexity out of TestBody.
    void scriptSuccessfulInstall(const std::string& newVersion, std::uint32_t state,
                                 std::vector<std::uint32_t> waypoints, std::mutex* cabMu = nullptr,
                                 std::string* cabOut = nullptr) {
        daemon_->scriptInstall(
            [this, newVersion, state, waypoints = std::move(waypoints), cabMu, cabOut](
                const std::string&, int cabFd, const std::map<std::string, sdbus::Variant>&) {
                if (cabOut != nullptr) {
                    std::scoped_lock lock(*cabMu);
                    *cabOut = readAll(cabFd);
                }
                for (const auto pct : waypoints) daemon_->emitProgress(kStatusDeviceWrite, pct);
                daemon_->setDevices({makeDevice("devA", newVersion, true)});
                daemon_->setResults("devA", resultsDict(state));
            });
    }

    // Scripts the fake's Install to fail with a D-Bus error (name→message).
    void scriptInstallThrows(const std::string& errName, const std::string& msg) {
        daemon_->scriptInstall(
            [errName, msg](auto&&...) { throw sdbus::Error(sdbus::Error::Name{errName}, msg); });
    }

    // Scripts an Install that raises `entered` then parks the fake's dispatch
    // thread on `release` — lets a test observe a mid-flight install.
    void scriptBlockingInstall(Flag& entered, Flag& release) {
        daemon_->scriptInstall([&entered, &release](auto&&...) {
            entered.raise();
            release.wait();
        });
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

// ---- T8 install lifecycle (M2 fd + M3 §5.5 + V5 progress) ----

TEST_F(F, InstallHappyPathWithProgress) {
    const auto ref = scriptInstallableDevice();
    ProgressLog log;
    std::mutex hookMu;
    std::string receivedCab;
    scriptSuccessfulInstall("1.2.4", fw::kUpdateStateSuccess,
                            {kProgressWaypoints.begin(), kProgressWaypoints.end()}, &hookMu,
                            &receivedCab);

    const auto r = provider_->install("devA", ref, log.reporter());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->disposition, core::InstallDisposition::Completed);
    EXPECT_EQ(r->observedVersion.value_or("(absent)"), "1.2.4");
    {
        std::scoped_lock lock(hookMu);
        EXPECT_EQ(receivedCab, kCabBytes);  // M2 fd-passing proof
    }
    // Every signal is sent before the Install reply, so a single-threaded
    // provider event loop has forwarded them all by the time install() returns.
    EXPECT_EQ(log.joined(), kProgressJoined);
}

TEST_F(F, PreflightRefusesVanishedRelease) {
    const auto ref = scriptInstallableDevice();  // release checksum cs-1
    ASSERT_TRUE(provider_->enumerate().has_value());
    std::atomic<bool> hookRan{false};
    daemon_->scriptInstall([&](auto&&...) { hookRan.store(true); });
    // The release the UI saw is gone: same device, different checksum.
    daemon_->setUpgrades("devA",
                         {makeRelease("1.2.4", "vendor-dir", "cs-CHANGED", {"fixture.cab"})});

    const auto r = provider_->install("devA", ref, noProgress());
    EXPECT_TRUE(refusedWithoutDbus(r, core::Error::Code::Conflict, hookRan));
}

TEST_F(F, PreflightRefusesNonUpdatableDevice) {
    daemon_->setDevices({makeDevice("devA", "1.2.3", false)});  // not updatable
    daemon_->setRemotes({makeRemote("vendor-dir", kRemoteKindDirectory, cabDir_.string())});
    daemon_->setUpgrades("devA", {makeRelease("1.2.4", "vendor-dir", "cs-1", {"fixture.cab"})});
    std::atomic<bool> hookRan{false};
    daemon_->scriptInstall([&](auto&&...) { hookRan.store(true); });

    const auto r = provider_->install(
        "devA", core::ReleaseRef{.remoteId = "vendor-dir", .checksum = "cs-1"}, noProgress());
    EXPECT_TRUE(refusedWithoutDbus(r, core::Error::Code::Conflict, hookRan));
}

TEST_F(F, RemoteOnlyReleaseRefusedBeforeDbus) {
    // A release whose only location is an https URL — never locally resolvable.
    const auto ref = scriptInstallableDevice({"https://example.com/firmware.cab"});
    std::atomic<bool> hookRan{false};
    daemon_->scriptInstall([&](auto&&...) { hookRan.store(true); });

    const auto r = provider_->install("devA", ref, noProgress());
    EXPECT_TRUE(refusedWithoutDbus(r, core::Error::Code::Unsupported, hookRan));
    ASSERT_FALSE(r.has_value());
    EXPECT_NE(r.error().message.find("fwupdmgr update"), std::string::npos) << r.error().message;
}

TEST_F(F, TraversalCabRefusedBeforeDbus) {
    const auto ref = scriptInstallableDevice({"../evil.cab"});  // escapes the remote dir
    std::atomic<bool> hookRan{false};
    daemon_->scriptInstall([&](auto&&...) { hookRan.store(true); });

    const auto r = provider_->install("devA", ref, noProgress());
    EXPECT_TRUE(refusedWithoutDbus(r, core::Error::Code::Unsupported, hookRan));
}

TEST_F(F, OfflineUpdateReportsScheduled) {
    const auto ref = scriptInstallableDevice();
    // Version stays 1.2.3; the daemon records a PENDING (offline) result.
    daemon_->setResults("devA", resultsDict(fw::kUpdateStatePending));

    const auto r = provider_->install("devA", ref, noProgress());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->disposition, core::InstallDisposition::Scheduled);
    EXPECT_FALSE(r->observedVersion.has_value()) << "no version applied yet for an offline update";
}

TEST_F(F, AuthFailedMapsToPermission) {
    const auto ref = scriptInstallableDevice();
    scriptInstallThrows("org.freedesktop.fwupd.AuthFailed", "cancelled");

    const auto r = provider_->install("devA", ref, noProgress());
    EXPECT_TRUE(failedWith(r, core::Error::Code::Permission));
}

TEST_F(F, SecondInstallWhileActiveIsBusy) {
    const auto ref = scriptInstallableDevice();
    daemon_->setResults("devA", resultsDict(fw::kUpdateStateSuccess));
    Flag entered;
    Flag release;
    scriptBlockingInstall(entered, release);

    auto first = std::async(std::launch::async,
                            [&] { return provider_->install("devA", ref, noProgress()); });
    ASSERT_TRUE(entered.wait()) << "first install never reached the daemon";
    const auto second = provider_->install("devA", ref, noProgress());
    release.raise();
    const auto firstResult = first.get();
    EXPECT_TRUE(failedWith(second, core::Error::Code::Busy));
    EXPECT_TRUE(firstResult.has_value()) << firstResult.error().message;
}

TEST_F(F, ProgressIgnoredWhileIdle) {
    // A progress signal with no install of ours in flight: the sink is null,
    // so the provider drops it (V5). Barrier on a DeviceAdded to prove it was
    // processed-and-dropped, not merely in-flight.
    const auto flushed = waitForEvent<core::UpdatesChangedEvent>(bus_, [&] {
        daemon_->emitProgress(kStatusDeviceWrite, kStrayPercent);
        daemon_->emitDeviceAdded(makeDevice("devFlush", "1.0", false));
    });
    ASSERT_TRUE(flushed.has_value());

    // A normal install afterwards still reports clean progress — no stray value.
    const auto ref = scriptInstallableDevice();
    ProgressLog log;
    scriptSuccessfulInstall("1.2.4", fw::kUpdateStateSuccess,
                            {kProgressWaypoints.begin(), kProgressWaypoints.end()});
    const auto r = provider_->install("devA", ref, log.reporter());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(log.joined(), kProgressJoined);  // no stray kStrayPercent
}

TEST_F(F, LateProgressAfterCompletionDropped) {
    const auto ref = scriptInstallableDevice();
    ProgressLog log;
    scriptSuccessfulInstall("1.2.4", fw::kUpdateStateSuccess,
                            {kProgressWaypoints.begin(), kProgressWaypoints.end()});
    const auto r = provider_->install("devA", ref, log.reporter());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    const std::string afterInstall = log.joined();

    // A progress signal arriving after install() returned: the sink was
    // cleared on exit, so nothing is collected. Barrier on DeviceAdded again.
    const auto flushed = waitForEvent<core::UpdatesChangedEvent>(bus_, [&] {
        daemon_->emitProgress(kStatusDeviceWrite, kStrayPercent);
        daemon_->emitDeviceAdded(makeDevice("devFlush", "1.0", false));
    });
    ASSERT_TRUE(flushed.has_value());
    EXPECT_EQ(log.joined(), afterInstall) << "progress collected after install returned";
}

TEST_F(F, DaemonErrorMidInstallReturnsIoThenRecovers) {
    // A daemon that aborts an install surfaces as Io (unmapped fwupd error),
    // and the provider still recovers across a name drop/reacquire afterward.
    // NOTE: the brief's literal "hook: dropName + block" is not realizable — a
    // synchronous releaseName() from the fake's own dispatch thread deadlocks
    // its reply, and install()'s timeout floor is 600s, so a true block is
    // untestable. This exercises the same intent deterministically: the
    // transaction fails Io, then a full name drop/reacquire recovers. It does
    // NOT model an atomic vanish while the Install call is still in flight.
    const auto ref = scriptInstallableDevice();
    scriptInstallThrows("org.freedesktop.fwupd.Internal", "daemon shutting down");
    const auto r = provider_->install("devA", ref, noProgress());
    EXPECT_TRUE(failedWith(r, core::Error::Code::Io));

    daemon_->dropName();
    daemon_->reacquireName();
    daemon_->setHistory({makeHistoryEntry("devA", fw::kUpdateStateNeedsReboot, "1.2.3")});
    EXPECT_TRUE(provider_->pendingActions().has_value());
    EXPECT_TRUE(provider_->enumerate().has_value());
}

TEST_F(F, TeardownDuringSignalStorm) {
    std::atomic<int> published{0};
    auto sub =
        bus_.subscribe<core::UpdatesChangedEvent>([&](const auto&) { published.fetch_add(1); });
    std::atomic<bool> stop{false};
    std::atomic<int> emitted{0};
    std::thread storm([&] {
        while (!stop.load()) {
            daemon_->emitDeviceAdded(makeDevice("devS", "1.0", false));
            emitted.fetch_add(1);
        }
    });

    while (emitted.load() < kStormWarmup) std::this_thread::yield();  // ensure real overlap
    provider_.reset();  // destruct mid-storm, no crash
    const int afterDtor = published.load();

    const int mark = emitted.load();  // keep hammering past the dtor
    while (emitted.load() < mark + kStormPostDtor) std::this_thread::yield();
    stop.store(true);
    storm.join();
    EXPECT_EQ(published.load(), afterDtor) << "provider published after destruction";
}
