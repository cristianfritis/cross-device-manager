#include <atomic>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/core/update_models.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/progress.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/fake_update_provider.hpp"

using namespace devmgr;

namespace {

constexpr int kProgressPercent = 42;

core::UpdateCandidate candidate(std::string id) {
    core::UpdateCandidate c;
    c.providerId = "fake";
    c.id = std::move(id);
    c.displayName = "device " + c.id;
    return c;
}

core::PendingAction pendingNeedsReboot(std::string deviceId) {
    core::PendingAction a;
    a.providerId = "fake";
    a.deviceId = std::move(deviceId);
    a.disposition = core::InstallDisposition::NeedsReboot;
    a.version = "2";
    return a;
}

// Collector mirroring the idiom in test_application_facade.cpp: subscribe on the
// test thread, providers publish from a TaskScheduler worker, read under lock.
struct Collector {
    std::mutex m;
    std::vector<core::TaskCompletedEvent> completed;
    std::vector<core::TaskProgressEvent> progress;

    std::optional<core::TaskCompletedEvent> completionFor(const std::string& taskId) {
        std::scoped_lock lock(m);
        for (const auto& e : completed)
            if (e.taskId == taskId) return e;
        return std::nullopt;
    }
};

class FacadeUpdatesTest : public ::testing::Test {
   protected:
    FacadeUpdatesTest() { fakeB_.id_ = "fakeB"; }

    void waitRefresh() { facade_.refreshUpdates().get(); }
    app::ApplicationFacade& facade() { return facade_; }

    runtime::EventBus bus_;
    runtime::TaskScheduler scheduler_{2};
    test::FakePal pal_;
    app::DeviceService svc_{bus_};
    tests::FakeUpdateProvider fakeA_;
    tests::FakeUpdateProvider fakeB_;
    app::ApplicationFacade facade_{
        pal_, scheduler_, bus_, svc_, nullptr, nullptr, nullptr, nullptr, {&fakeA_, &fakeB_}};
};

}  // namespace

TEST_F(FacadeUpdatesTest, PartialProviderFailureIsFirstClass) {  // review test 10
    fakeA_.enumerateResult_ = core::makeError(core::Error::Code::Io, "down");
    fakeB_.enumerateResult_ = std::vector<core::UpdateCandidate>{candidate("b1")};
    waitRefresh();
    const auto snap = facade().updatesSnapshot();
    ASSERT_EQ(snap.size(), 2U);
    EXPECT_TRUE(snap[0].refreshError.has_value());  // A failed
    EXPECT_EQ(snap[1].candidates.size(), 1U);       // B landed anyway
}

TEST_F(FacadeUpdatesTest, RefreshFailureKeepsLastGoodRows) {  // spec §8.1 deliberate retain
    fakeA_.enumerateResult_ = std::vector<core::UpdateCandidate>{candidate("a1")};
    waitRefresh();
    fakeA_.enumerateResult_ = core::makeError(core::Error::Code::Io, "flake");
    waitRefresh();
    const auto snap = facade().updatesSnapshot();
    EXPECT_EQ(snap[0].candidates.size(), 1U);       // retained
    EXPECT_TRUE(snap[0].refreshError.has_value());  // AND flagged
}

TEST_F(FacadeUpdatesTest, RebootBannerSurvivesCandidateDisappearance) {  // M1 / review test 8
    fakeA_.installResult_ =
        core::InstallOutcome{.disposition = core::InstallDisposition::NeedsReboot,
                             .needsReboot = true,
                             .observedVersion = "2",
                             .message = "reboot required"};
    facade().installUpdate("fake", "a1", {.remoteId = "r", .checksum = "c"}).get();
    EXPECT_TRUE(facade().rebootPendingEffective());  // session outcome → sticky

    fakeA_.enumerateResult_ = std::vector<core::UpdateCandidate>{};  // candidate GONE
    fakeA_.pendingResult_ = std::vector<core::PendingAction>{pendingNeedsReboot("a1")};
    waitRefresh();
    EXPECT_TRUE(facade().rebootPendingEffective());  // V4: ⊥ derived from candidates
}

TEST_F(FacadeUpdatesTest, PendingClearsOnlyOnPositiveEvidence) {  // M1 clear rule
    // Seed the sticky NeedsReboot state via a session install, matching the
    // end state of RebootBannerSurvivesCandidateDisappearance.
    fakeA_.installResult_ =
        core::InstallOutcome{.disposition = core::InstallDisposition::NeedsReboot,
                             .needsReboot = true,
                             .observedVersion = "2",
                             .message = "reboot required"};
    facade().installUpdate("fake", "a1", {.remoteId = "r", .checksum = "c"}).get();
    ASSERT_TRUE(facade().rebootPendingEffective());

    fakeA_.pendingResult_ = core::makeError(core::Error::Code::Io, "down");
    waitRefresh();
    EXPECT_TRUE(facade().rebootPendingEffective());  // query failed = NO evidence → retained

    fakeA_.pendingResult_ = std::vector<core::PendingAction>{};  // provider: nothing pending
    waitRefresh();
    EXPECT_FALSE(facade().rebootPendingEffective());  // positive evidence → cleared
}

TEST_F(FacadeUpdatesTest, InstallsSerializedSecondIsBusy) {  // spec §5.4 in-process
    Collector col;
    auto sub = bus_.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
        std::scoped_lock lock(col.m);
        col.completed.push_back(e);
    });

    std::promise<void> started;
    auto startedFut = started.get_future();
    std::promise<void> gate;
    auto gateFut = gate.get_future();
    fakeA_.onInstall_ = [&](runtime::ProgressReporter&) {
        started.set_value();
        gateFut.wait();
    };

    auto first = facade().installUpdate("fake", "a1", {.remoteId = "r", .checksum = "c"});
    startedFut.wait();  // install() entered → slot claimed, provider blocked
    auto second = facade().installUpdate("fake", "a2", {.remoteId = "r", .checksum = "c"});
    second.get();  // completes immediately

    EXPECT_EQ(fakeA_.installCalls_.load(), 1);  // provider never saw the second
    const auto busy = col.completionFor("install-update:a2");
    ASSERT_TRUE(busy.has_value());
    EXPECT_FALSE(busy->ok);
    EXPECT_NE(busy->message.find("in progress"), std::string::npos);

    gate.set_value();
    first.get();
}

TEST_F(FacadeUpdatesTest, InstallPublishesProgressAndCompletion) {
    Collector col;
    auto subP = bus_.subscribe<core::TaskProgressEvent>([&](const core::TaskProgressEvent& e) {
        std::scoped_lock lock(col.m);
        col.progress.push_back(e);
    });
    auto subC = bus_.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
        std::scoped_lock lock(col.m);
        col.completed.push_back(e);
    });

    fakeA_.onInstall_ = [](runtime::ProgressReporter& p) {
        p(runtime::ProgressUpdate{.percent = kProgressPercent, .stage = "device-write"});
    };
    facade().installUpdate("fake", "a1", {.remoteId = "r", .checksum = "c"}).get();

    std::scoped_lock lock(col.m);
    ASSERT_EQ(col.progress.size(), 1U);
    EXPECT_EQ(col.progress[0].taskId, "install-update:a1");
    EXPECT_EQ(col.progress[0].percent, kProgressPercent);
    EXPECT_EQ(col.progress[0].stage, "device-write");
    ASSERT_EQ(col.completed.size(), 1U);
    EXPECT_EQ(col.completed[0].taskId, "install-update:a1");
    EXPECT_TRUE(col.completed[0].ok);
}

TEST_F(FacadeUpdatesTest, CapsGateRefusesStatusOnlyProvider) {  // V1 defense in depth
    Collector col;
    auto sub = bus_.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
        std::scoped_lock lock(col.m);
        col.completed.push_back(e);
    });

    fakeB_.caps_ = pal::UpdateProviderCaps::Query;
    facade().installUpdate(fakeB_.id_, "x", {.remoteId = "r", .checksum = "c"}).get();

    EXPECT_EQ(fakeB_.installCalls_.load(), 0);
    const auto done = col.completionFor("install-update:x");
    ASSERT_TRUE(done.has_value());
    EXPECT_FALSE(done->ok);
    EXPECT_NE(done->message.find("status-only"), std::string::npos);
}
