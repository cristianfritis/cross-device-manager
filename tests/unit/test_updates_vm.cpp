#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/updates_vm.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/core/update_models.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/fake_update_provider.hpp"
#include "fakes/inline_ui_dispatcher.hpp"

using namespace devmgr;

namespace {

constexpr int kEarlierPercent = 10;
constexpr int kProgressPercent = 42;
constexpr int kUnrelatedPercent = 99;
constexpr int kStormPublishes = 5000;

// Queuing dispatcher with the T11 (FTXUI) / T12 (Qt) shape: post() returns
// before the closure runs. Mutex-guarded because EventBus handlers post from
// facade worker threads in the teardown-storm test.
class QueuingUiDispatcher final : public app::IUiDispatcher {
   public:
    void post(std::function<void()> fn) override {
        std::scoped_lock lock(m_);
        queued_.push_back(std::move(fn));
    }
    std::vector<std::function<void()>> drain() {
        std::scoped_lock lock(m_);
        return std::exchange(queued_, {});
    }

   private:
    std::mutex m_;
    std::vector<std::function<void()>> queued_;
};

// One fwupd-ish updatable candidate with a locally resolvable release.
core::UpdateCandidate webcam() {
    core::UpdateCandidate c;
    c.providerId = "fwupd";
    c.id = "a1";
    c.displayName = "Webcam";
    c.currentVersion = "1.2.2";
    c.candidateVersion = "1.2.4";
    c.facts = {.updatable = true, .supported = true, .needsRebootAfterUpdate = false};
    core::ReleaseInfo r;
    r.version = "1.2.4";
    r.summary = "Stability fixes";
    r.remoteId = "vendor";
    r.checksum = "abc123";
    r.localCab = true;
    c.releases.push_back(r);
    return c;
}

// One dkms status row: status-only, no releases, no candidate version.
core::UpdateCandidate dkmsRow() {
    core::UpdateCandidate c;
    c.providerId = "dkms";
    c.id = "dkms:nvidia/550.120";
    c.displayName = "nvidia";
    c.currentVersion = "550.120";
    c.facts = {.updatable = false, .supported = true, .needsRebootAfterUpdate = false};
    return c;
}

std::string joined(const std::vector<std::string>& lines) {
    std::string s;
    for (const auto& l : lines) s += l + "\n";
    return s;
}

}  // namespace

class UpdatesVmTest : public ::testing::Test {
   protected:
    UpdatesVmTest() {
        fwupd_.id_ = "fwupd";
        dkms_.id_ = "dkms";
        dkms_.caps_ = pal::UpdateProviderCaps::Query;
    }

    void refresh() { facade_.refreshUpdates().get(); }

    runtime::EventBus bus_;
    runtime::TaskScheduler scheduler_{2};
    test::FakePal pal_;
    app::DeviceService svc_{bus_};
    tests::FakeUpdateProvider fwupd_;
    tests::FakeUpdateProvider dkms_;
    test::InlineUiDispatcher dispatcher_;
    app::ApplicationFacade facade_{pal_,  scheduler_,       bus_, svc_, nullptr, nullptr, nullptr,
                                   &pal_, {&fwupd_, &dkms_}};
    // UpdatesVM holds references + Subscriptions: construct in place per test.
};

TEST_F(UpdatesVmTest, RowsAreByteFrozenFormat) {
    fwupd_.enumerateResult_ = std::vector<core::UpdateCandidate>{webcam()};
    dkms_.enumerateResult_ = std::vector<core::UpdateCandidate>{dkmsRow()};
    refresh();
    app::UpdatesVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    ASSERT_EQ(vm.rowsRef().size(), 2U);
    // These literals ARE the parity contract both UIs render (V3) — byte-frozen.
    EXPECT_EQ(vm.rowsRef()[0],
              "fwupd  Webcam                         1.2.2        -> 1.2.4        ");
    EXPECT_EQ(vm.rowsRef()[1],
              "dkms   nvidia                         550.120      -> -            ");
}

TEST_F(UpdatesVmTest, PlaceholderRowWhenEmptyNeverActionable) {  // V1 + Phase 5 rule
    refresh();  // both providers return zero candidates
    app::UpdatesVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    ASSERT_EQ(vm.rowsRef().size(), 1U);
    EXPECT_EQ(vm.rowsRef()[0], "(no updates available)");
    EXPECT_FALSE(vm.selectedInstall().has_value());
}

TEST_F(UpdatesVmTest, RemoteOnlyReleaseNotInstallableWithGuidance) {  // review test 4
    auto c = webcam();
    c.releases[0].localCab = false;
    c.releases[0].locations = {"https://fwupd.org/downloads/webcam.cab"};
    fwupd_.enumerateResult_ = std::vector<core::UpdateCandidate>{c};
    refresh();
    app::UpdatesVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    EXPECT_FALSE(vm.selectedInstall().has_value());
    EXPECT_NE(vm.rowsRef()[0].find("external download"), std::string::npos);
    const auto detail = joined(vm.detailLines());
    EXPECT_NE(detail.find("external download required — run `fwupdmgr update`"), std::string::npos);
}

TEST_F(UpdatesVmTest, SelectedInstallCarriesReleaseRefAndConfirmText) {
    auto c = webcam();
    c.facts.needsRebootAfterUpdate = true;
    fwupd_.enumerateResult_ = std::vector<core::UpdateCandidate>{c};
    refresh();
    app::UpdatesVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    const auto args = vm.selectedInstall();
    ASSERT_TRUE(args.has_value());
    EXPECT_EQ(args->providerId, "fwupd");
    EXPECT_EQ(args->candidateId, "a1");
    EXPECT_EQ(args->release, (core::ReleaseRef{.remoteId = "vendor", .checksum = "abc123"}));
    EXPECT_NE(args->confirmText.find("1.2.2 → 1.2.4"), std::string::npos);
    EXPECT_NE(args->confirmText.find("reboot"), std::string::npos);
}

TEST_F(UpdatesVmTest, BannerShowsAvailabilityRebootAndSecureBoot) {
    fwupd_.availability_ = {
        .available = false,
        .version = {},
        .error = core::Error{.code = core::Error::Code::Io, .message = "daemon down"},
        .notices = {}};
    // Seed the durable pending state via a session install outcome (M1): an
    // unavailable provider is never queried for pendingActions() — no evidence
    // — so the sticky entry survives the refresh below.
    fwupd_.installResult_ =
        core::InstallOutcome{.disposition = core::InstallDisposition::NeedsReboot,
                             .needsReboot = true,
                             .observedVersion = "2",
                             .message = "reboot required"};
    facade_.installUpdate("fwupd", "a1", {.remoteId = "vendor", .checksum = "abc123"}).get();
    refresh();
    app::UpdatesVM vm(facade_, bus_, dispatcher_);
    const auto b = vm.banner();
    EXPECT_NE(b.find("daemon down"), std::string::npos);
    EXPECT_NE(b.find("reboot required"), std::string::npos);
    EXPECT_NE(b.find("Secure Boot"), std::string::npos);
}

TEST_F(UpdatesVmTest, BannerIncludesAvailabilityNotices) {  // spec §8.3
    fwupd_.availability_ = {.available = true,
                            .version = "2.0.20",
                            .error = {},
                            .notices = {"lvfs metadata 42 days old", "1 failed update in history"}};
    refresh();
    app::UpdatesVM vm(facade_, bus_, dispatcher_);
    const auto b = vm.banner();
    const auto p1 = b.find("lvfs metadata 42 days old");
    const auto p2 = b.find("1 failed update in history");
    EXPECT_NE(p1, std::string::npos);
    EXPECT_NE(p2, std::string::npos);
    EXPECT_LT(p1, p2);  // notices preserve provider order within the segment
}

TEST_F(UpdatesVmTest, RefreshedEventCoalescesToOneRebuild) {  // ModulesVM discipline
    refresh();
    QueuingUiDispatcher queuing;
    app::UpdatesVM vm(facade_, bus_, queuing);
    int rebuilds = 0;
    vm.setRebuildHooks({}, [&] { ++rebuilds; });
    bus_.publish(core::UpdatesRefreshedEvent{});
    bus_.publish(core::UpdatesRefreshedEvent{});
    bus_.publish(core::UpdatesRefreshedEvent{});
    for (auto& fn : queuing.drain()) fn();
    EXPECT_EQ(rebuilds, 1);
}

TEST_F(UpdatesVmTest, ChangedEventCoalescesToOneRefresh) {  // D6 custody + wiring
    QueuingUiDispatcher queuing;
    {
        app::UpdatesVM vm(facade_, bus_, queuing);
        bus_.publish(core::UpdatesChangedEvent{});
        bus_.publish(core::UpdatesChangedEvent{});
        auto closures = queuing.drain();
        ASSERT_EQ(closures.size(), 1U);  // coalesced to ONE queued refresh
        for (auto& fn : closures) fn();  // launches facade_.refreshUpdates()
    }  // dtor waits lastRefresh_ (future custody) → refresh completed here
    EXPECT_EQ(fwupd_.enumerateCalls_.load(), 1);  // provider refreshed exactly once
}

TEST_F(UpdatesVmTest, RequestBannerDurableUntilDismiss) {  // spec §9 / review item 7
    app::UpdatesVM vm(facade_, bus_, dispatcher_);
    EXPECT_TRUE(vm.requestBanner().empty());
    bus_.publish(core::UpdateRequestEvent{.providerId = "fwupd",
                                          .deviceId = "a1",
                                          .kind = "post",
                                          .message = "unplug and replug the device"});
    EXPECT_NE(vm.requestBanner().find("unplug and replug"), std::string::npos);
    // Progress, completion, and refresh must NOT overwrite it (durable, spec §9;
    // request clearing is EXPLICIT-DISMISS ONLY — sanctioned narrowing, T13 ledger).
    bus_.publish(core::TaskProgressEvent{
        .taskId = "install-update:a1", .percent = kEarlierPercent, .stage = "device-write"});
    bus_.publish(
        core::TaskCompletedEvent{.taskId = "install-update:a1", .ok = true, .message = "done"});
    bus_.publish(core::UpdatesRefreshedEvent{});
    EXPECT_NE(vm.requestBanner().find("unplug and replug"), std::string::npos);
    vm.dismissRequest();
    EXPECT_TRUE(vm.requestBanner().empty());
}

TEST_F(UpdatesVmTest, InstallProgressTextFollowsTaskProgress) {
    app::UpdatesVM vm(facade_, bus_, dispatcher_);
    EXPECT_TRUE(vm.installProgressText().empty());
    bus_.publish(core::TaskProgressEvent{
        .taskId = "install-update:a1", .percent = kProgressPercent, .stage = "device-write"});
    EXPECT_NE(vm.installProgressText().find("42%"), std::string::npos);
    EXPECT_NE(vm.installProgressText().find("device-write"), std::string::npos);
    // Unrelated taskId → ignored.
    bus_.publish(core::TaskProgressEvent{
        .taskId = "set-enabled:x", .percent = kUnrelatedPercent, .stage = "no"});
    EXPECT_NE(vm.installProgressText().find("42%"), std::string::npos);
    // Terminal completion for the install clears the text.
    bus_.publish(
        core::TaskCompletedEvent{.taskId = "install-update:a1", .ok = true, .message = "ok"});
    EXPECT_TRUE(vm.installProgressText().empty());
}

TEST_F(UpdatesVmTest, PercentOnlyFrameRetainsLastNamedStage) {  // design "Risks" carry-over
    app::UpdatesVM vm(facade_, bus_, dispatcher_);
    // fwupd sends Percentage and Status in separate PropertiesChanged frames;
    // a percent-only frame decodes as stage "unknown" and must NOT replace the
    // last named stage in the progress text.
    bus_.publish(core::TaskProgressEvent{
        .taskId = "install-update:a1", .percent = kEarlierPercent, .stage = "device-write"});
    bus_.publish(core::TaskProgressEvent{
        .taskId = "install-update:a1", .percent = kProgressPercent, .stage = "unknown"});
    EXPECT_NE(vm.installProgressText().find("42%"), std::string::npos);
    EXPECT_NE(vm.installProgressText().find("device-write"), std::string::npos);
    EXPECT_EQ(vm.installProgressText().find("unknown"), std::string::npos);
    // A newly named stage takes over again.
    bus_.publish(core::TaskProgressEvent{
        .taskId = "install-update:a1", .percent = kProgressPercent, .stage = "device-verify"});
    EXPECT_NE(vm.installProgressText().find("device-verify"), std::string::npos);
    // Completion clears the retained stage: the next install must not inherit
    // it, so its leading percent-only frame shows "unknown" honestly.
    bus_.publish(
        core::TaskCompletedEvent{.taskId = "install-update:a1", .ok = true, .message = "ok"});
    bus_.publish(core::TaskProgressEvent{
        .taskId = "install-update:b2", .percent = kEarlierPercent, .stage = "unknown"});
    EXPECT_NE(vm.installProgressText().find("unknown"), std::string::npos);
}

TEST_F(UpdatesVmTest, StateForRowAvailableUpToDateAndOutOfRange) {
    fwupd_.enumerateResult_ = std::vector<core::UpdateCandidate>{webcam()};  // has candidateVersion
    dkms_.enumerateResult_ = std::vector<core::UpdateCandidate>{dkmsRow()};  // no candidateVersion
    refresh();
    app::UpdatesVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    ASSERT_EQ(vm.rowsRef().size(), 2U);  // row 0 = fwupd Webcam, row 1 = dkms nvidia
    EXPECT_EQ(vm.stateForRow(0), app::UpdateRowState::Available);
    EXPECT_EQ(vm.stateForRow(1), app::UpdateRowState::UpToDate);
    EXPECT_FALSE(vm.stateForRow(-1).has_value());
    EXPECT_FALSE(vm.stateForRow(99).has_value());
}

TEST_F(UpdatesVmTest, StateForRowNulloptOnPlaceholder) {
    refresh();  // both providers empty → single placeholder row
    app::UpdatesVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    ASSERT_EQ(vm.rowsRef().size(), 1U);
    EXPECT_EQ(vm.rowsRef()[0], "(no updates available)");
    EXPECT_FALSE(vm.stateForRow(0).has_value());
}

TEST_F(UpdatesVmTest, StateForRowErrorWhenProviderUnavailableWithRetainedRows) {
    fwupd_.enumerateResult_ = std::vector<core::UpdateCandidate>{webcam()};
    refresh();  // caches Webcam as this provider's last-good candidate
    // Provider goes unavailable: buildProviderState retains the last-good rows
    // (§8.1) but the availability error makes them suspect → Error.
    fwupd_.availability_ = {
        .available = false,
        .version = {},
        .error = core::Error{.code = core::Error::Code::Io, .message = "daemon down"},
        .notices = {}};
    refresh();
    app::UpdatesVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    ASSERT_GE(vm.rowsRef().size(), 1U);
    EXPECT_NE(vm.rowsRef()[0].find("Webcam"), std::string::npos);  // retained row
    EXPECT_EQ(vm.stateForRow(0), app::UpdateRowState::Error);
}

TEST_F(UpdatesVmTest, TeardownStormNoPostAfterDrain) {  // review test 14 (VM level)
    refresh();
    QueuingUiDispatcher queuing;
    std::atomic<int> rebuilds{0};
    std::vector<std::function<void()>> orphans;
    {
        app::UpdatesVM vm(facade_, bus_, queuing);
        vm.setRebuildHooks({}, [&] { ++rebuilds; });
        // Hammer from another thread while the VM is alive; join BEFORE the VM
        // dies so every post lands in the queue and outlives the VM.
        std::thread hammer([&] {
            for (int i = 0; i < kStormPublishes; ++i) bus_.publish(core::UpdatesRefreshedEvent{});
        });
        hammer.join();
        orphans = queuing.drain();
    }  // VM destroyed here; queued closures orphaned
    ASSERT_EQ(orphans.size(), 1U);  // the storm coalesced to ONE queued rebuild
    for (auto& fn : orphans) fn();  // must not crash; must be a no-op (alive token)
    EXPECT_EQ(rebuilds.load(), 0);  // never drained while alive → never rebuilt
}
