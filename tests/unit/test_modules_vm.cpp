#include <gtest/gtest.h>

#include <chrono>  // FIX ROUND 1 (i-1): wait_for in the ready-future test
#include <functional>
#include <vector>

#include "devmgr/app/device_service.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "devmgr/app/ui_dispatcher.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/inline_ui_dispatcher.hpp"

using devmgr::app::ApplicationFacade;
using devmgr::app::ModulesVM;

namespace {

// FIX ROUND 1 (i-2 test): a dispatcher that queues closures instead of
// running them inline, so posted work outlives the VM that posted it — the
// shape T11 (FTXUI) and T12 (Qt) actually have, unlike InlineUiDispatcher.
class DeferringUiDispatcher final : public devmgr::app::IUiDispatcher {
   public:
    void post(std::function<void()> fn) override { queued.push_back(std::move(fn)); }
    std::vector<std::function<void()>> queued;
};

}  // namespace

class ModulesVMTest : public ::testing::Test {
   protected:
    devmgr::runtime::EventBus bus_;
    devmgr::runtime::TaskScheduler scheduler_;
    devmgr::test::FakePal pal_;
    devmgr::app::DeviceService service_{bus_};
    devmgr::test::InlineUiDispatcher dispatcher_;
    ApplicationFacade facade_{pal_, scheduler_, bus_, service_, nullptr, nullptr, &pal_, &pal_};
    // ModulesVM holds references + a Subscription: construct in place per test
    // (it is neither copyable nor movable).

    void seed(const std::string& name, long refs) {
        devmgr::core::LoadedModule m;
        m.name = name;
        m.sizeBytes = 4096;
        m.refCount = refs;
        pal_.seedLoadedModule(m);
    }
};

TEST_F(ModulesVMTest, RebuildListsModulesWithPlaceholderSignature) {
    seed("dummy", 0);
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    ASSERT_EQ(v.rowsRef().size(), 1U);
    EXPECT_NE(v.rowsRef()[0].find("dummy"), std::string::npos);
    EXPECT_NE(v.rowsRef()[0].find("…"), std::string::npos);  // async fill pending
    EXPECT_EQ(v.selectedModule(), "dummy");
}

TEST_F(ModulesVMTest, FilterNarrowsRows) {
    seed("dummy", 0);
    seed("usbhid", 2);
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    v.setFilter("usb");
    ASSERT_EQ(v.rowsRef().size(), 1U);
    EXPECT_NE(v.rowsRef()[0].find("usbhid"), std::string::npos);
}

TEST_F(ModulesVMTest, SignatureFillReplacesPlaceholder) {
    seed("dummy", 0);
    devmgr::core::Driver info;
    info.name = "dummy";
    info.isSigned = true;
    info.signer = "Build key";
    pal_.seedDriver("/anywhere", info);  // moduleInfo() finds by name
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    v.fillSignatures().wait();
    v.rebuild();  // the ModulesChangedEvent normally triggers this via dispatcher
    EXPECT_NE(v.rowsRef()[0].find("yes (Build key)"), std::string::npos);
}

TEST_F(ModulesVMTest, BannerReportsSecureBootAndLockdown) {
    pal_.info.secureBoot = true;
    pal_.info.lockdownMode = "integrity";
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    EXPECT_EQ(v.banner(),
              "Secure Boot: ON · Lockdown: integrity — unsigned modules will be rejected");
    pal_.info.secureBoot = false;
    pal_.info.lockdownMode = "none";
    EXPECT_EQ(v.banner(), "Secure Boot: off · Lockdown: none");
}

TEST_F(ModulesVMTest, DetailLinesIncludeModprobeInfo) {
    seed("dummy", 0);
    devmgr::core::Driver info;
    info.name = "dummy";
    info.version = "1.0";
    info.path = "/lib/modules/x/dummy.ko";
    pal_.seedDriver("/anywhere", info);
    pal_.modprobeResult =
        devmgr::core::ModprobeInfo{.options = "numdummies=2", .blacklisted = true};
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    const auto lines = v.detailLines();
    const std::string all = [&] {
        std::string s;
        for (const auto& l : lines) s += l + "\n";
        return s;
    }();
    EXPECT_NE(all.find("numdummies=2"), std::string::npos);
    EXPECT_NE(all.find("blacklisted"), std::string::npos);
}

// FIX ROUND 1 (i-1): fillSignatures() must never return an invalid
// shared_future. Before the fix, `if (names.empty()) return sigFill_;`
// returned the default-constructed sigFill_ verbatim whenever there was
// nothing left to fill AND no fill had ever run yet — a caller doing
// .wait()/.get() on that invalid handle is UB. With zero modules loaded,
// "names" is vacuously empty on the very first-ever call, which is the only
// way to observe sigFill_ still at its default (never-assigned) state
// through the public API — so this is the exact repro, not merely a
// plausible one. ASSERT_TRUE(valid()) runs before any wait so a pre-fix
// failure is a clean assertion failure, not a std::future_error crash.
TEST_F(ModulesVMTest, FillSignaturesWithNothingToFillReturnsReadyFuture) {
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();  // zero modules: snapshot_ empty, sigFill_ never assigned

    auto firstEver = v.fillSignatures();
    ASSERT_TRUE(firstEver.valid());
    EXPECT_EQ(firstEver.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    firstEver.wait();  // must not hang / must not be UB

    // Also cover the brief's original scenario (spec §7.1 perf case):
    // re-entering the view after a real fill has already cached everything.
    seed("dummy", 0);
    ModulesVM v2(facade_, bus_, scheduler_, dispatcher_);
    v2.rebuild();
    v2.fillSignatures().wait();
    v2.rebuild();  // now every name is cached

    auto again = v2.fillSignatures();
    ASSERT_TRUE(again.valid());
    EXPECT_EQ(again.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    again.wait();  // must not hang / must not be UB
}

// FIX ROUND 1 (i-2): the destructor's "no publish into a dead VM" guarantee
// held only for an inline dispatcher (post() runs synchronously). With a real
// queuing dispatcher (T11 FTXUI / T12 Qt), post() returns before the UI
// thread drains the queue, so a closure posted just before destruction could
// still run after the VM is gone. Reproduces that ordering with a
// test-local deferring dispatcher and asserts running the orphaned closure
// afterward is a safe no-op (a clean use-after-free repro under ASan before
// the alive-token fix).
TEST_F(ModulesVMTest, PostedClosureAfterDestructionIsDropped) {
    seed("dummy", 0);
    DeferringUiDispatcher deferring;
    {
        ModulesVM v(facade_, bus_, scheduler_, deferring);
        v.rebuild();
        v.fillSignatures().wait();  // worker completes and posts the merge closure
    }  // VM destroyed here; posted closure(s) still queued
    for (auto& fn : deferring.queued) fn();  // must not crash; must be a no-op
}
