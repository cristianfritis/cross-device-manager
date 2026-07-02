#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "fakes/inline_ui_dispatcher.hpp"

using namespace std::chrono_literals;

namespace {

devmgr::core::Device usb(std::string id, std::string name) {
    devmgr::core::Device d;
    d.id = devmgr::core::DeviceId{std::move(id)};
    d.name = std::move(name);
    d.bus = devmgr::core::BusType::Usb;
    return d;
}

template <class Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return pred();
}

// Polls pred() for `duration`, returning false the instant it fails — the
// mirror image of waitFor() (which polls for a condition to become true).
template <class Pred>
bool holdsFor(Pred pred, std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!pred()) return false;
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

// Runs one "publish A, let its clear become due, publish distinct B, assert B
// survives inside its own ttl" iteration for
// StaleClearDoesNotBlankANewerMessage below. Extracted (mirroring holdsFor()
// above) to keep that test's TestBody under clang-tidy's
// cognitive-complexity threshold — the three ASSERT_* expansions inline
// pushed it over. ttl/pollWindow are both durations but distinct by name and
// every call site is local (below), not a public API where a caller could
// accidentally transpose them.
void expectStaleClearNeverBlanksNewerMessage(
    devmgr::runtime::EventBus& bus, devmgr::app::StatusLineVM& vm,
    std::chrono::milliseconds ttl,  // NOLINT(bugprone-easily-swappable-parameters)
    std::chrono::milliseconds pollWindow, int iteration) {
    const std::string suffix = std::to_string(iteration);
    const std::string messageA = "usb device added: A" + suffix;
    const std::string messageB = "usb device added: B" + suffix;

    bus.publish(devmgr::core::DeviceAddedEvent{usb("dev-a-" + suffix, "A" + suffix)});
    ASSERT_EQ(vm.text(), messageA);

    // Let clear-A become due — and, racing the worker thread's own wakeup,
    // very likely already dequeued (cancel() about to fail) — before
    // superseding it with a newer message.
    std::this_thread::sleep_for(ttl);

    bus.publish(devmgr::core::DeviceAddedEvent{usb("dev-b-" + suffix, "B" + suffix)});
    ASSERT_EQ(vm.text(), messageB);

    ASSERT_TRUE(holdsFor([&] { return vm.text() == messageB; }, pollWindow))
        << "iteration " << iteration << ": message blanked before its own ttl";
}

}  // namespace

TEST(StatusLineVM, StaysSilentUntilArmedThenShowsAndClears) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;
    devmgr::test::InlineUiDispatcher dispatcher;
    devmgr::app::StatusLineVM vm(bus, timer, dispatcher, 40ms);

    // Disarmed: the initial enumeration's events produce no message.
    bus.publish(devmgr::core::DeviceAddedEvent{usb("dev-1", "Widget")});
    EXPECT_TRUE(vm.text().empty());

    vm.arm();
    bus.publish(devmgr::core::DeviceAddedEvent{usb("dev-2", "Gadget")});
    EXPECT_EQ(vm.text(), "usb device added: Gadget");

    // Auto-clears after the ttl.
    EXPECT_TRUE(waitFor([&] { return vm.text().empty(); }, 1s));
}

TEST(StatusLineVM, LatestEventWinsAndRemoveHasAGenericMessage) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;
    devmgr::test::InlineUiDispatcher dispatcher;
    devmgr::app::StatusLineVM vm(bus, timer, dispatcher, 5s);  // long ttl; we assert the text
    vm.arm();

    bus.publish(devmgr::core::DeviceAddedEvent{usb("dev-2", "Gadget")});
    bus.publish(devmgr::core::DeviceChangedEvent{usb("dev-2", "Gadget v2")});
    EXPECT_EQ(vm.text(), "usb device changed: Gadget v2");

    bus.publish(devmgr::core::DeviceRemovedEvent{devmgr::core::DeviceId{"dev-2"}});
    EXPECT_EQ(vm.text(), "device removed");
}

// Finding 2 regression: setMessage() cancels the previous clear and schedules
// a new one on every call. DelayedScheduler::cancel() is best-effort (see
// delayed_scheduler.hpp) — if the timer thread had already dequeued the prior
// clear, cancel() silently no-ops and that stale callback still runs. Without
// the generation guard in onClearFired(), that stale callback would
// unconditionally clear text_ and zero clearHandle_, wiping out whatever
// newer message + newer pending timer superseded it — reachable during an
// ordinary rapid hotplug burst, not just at teardown. This test fires a fast
// burst of events (well within the ttl) and asserts the final message
// survives untouched for a window comfortably inside its own ttl, then still
// clears afterward — proving the *last* setMessage()'s timer, and only that
// one, governs.
TEST(StatusLineVM, RapidBurstNeverClearsMessageBeforeItsOwnTtl) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;
    devmgr::test::InlineUiDispatcher dispatcher;
    devmgr::app::StatusLineVM vm(bus, timer, dispatcher, 150ms);
    vm.arm();

    constexpr int kBurst = 15;
    for (int i = 0; i < kBurst; ++i) {
        bus.publish(devmgr::core::DeviceAddedEvent{
            usb("dev-" + std::to_string(i), "Item" + std::to_string(i))});
    }
    const std::string latest = "usb device added: Item" + std::to_string(kBurst - 1);
    ASSERT_EQ(vm.text(), latest);

    // Poll well inside the 150ms ttl: a stale, superseded clear from any
    // earlier burst iteration must never blank the latest message early.
    EXPECT_TRUE(holdsFor([&] { return vm.text() == latest; }, 80ms));

    // The final message's own timer still governs and eventually clears it.
    EXPECT_TRUE(waitFor([&] { return vm.text().empty(); }, 1s));
}

// Finding 2 adversarial regression test. RapidBurstNeverClearsMessageBefore-
// ItsOwnTtl (above) is a valid behavioral test but never actually exercises
// the generation_ guard: its burst runs in microseconds, so every
// intermediate setMessage()'s cancel(clearHandle_) call finds its prior
// clear still queued (cancel() returns true) and onClearFired()'s
// `generation != generation_` branch is never taken. That test would pass
// identically against code without the guard.
//
// This test forces the actual race the guard defends against: a clear
// callback dequeued by DelayedScheduler's worker thread — so a *subsequent*
// cancel() on it returns false, per delayed_scheduler.hpp's contract — but
// not yet executed, racing a newer setMessage() for a distinct message.
// Without the guard, that stale callback unconditionally blanks text_ the
// instant it runs, almost immediately and long before the newer message's
// own ttl elapses. With the guard, it recognizes its captured generation is
// superseded and no-ops.
//
// delayed_scheduler.cpp's run() erases every due entry from its queue/index
// (so cancel() starts reporting false for it) while holding the scheduler's
// own mutex, then releases that mutex and only afterwards invokes the
// collected callbacks — so the "dequeued but not yet run" window is
// typically microseconds wide and cannot be forced open deterministically
// from a test thread. So, mirroring the Finding 1 stress test's
// probabilistic style (which the reviewer explicitly accepts): each
// iteration publishes message A, sleeps ~one ttl (so clear-A's due time
// passes and — win or lose the race with the worker thread's own wakeup — is
// very likely already dequeued), then immediately publishes a distinct
// message B and polls that B survives a window comfortably inside its own
// ttl. Some fraction of kIterations iterations land B's setMessage() call
// inside the dequeued-but-not-yet-run window; a single early blank on any
// iteration fails the test. Empirically (see task report): with the
// generation_ guard temporarily removed, this test reliably failed within a
// few iterations; restored, 30 consecutive full runs all passed.
TEST(StatusLineVM, StaleClearDoesNotBlankANewerMessage) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;  // shared: one busy worker races every iteration
    devmgr::test::InlineUiDispatcher dispatcher;

    constexpr auto kTtl = 30ms;
    devmgr::app::StatusLineVM vm(bus, timer, dispatcher, kTtl);
    vm.arm();

    constexpr int kIterations = 120;
    // Comfortably inside kTtl: message B's own clear fires ~kTtl after B is
    // published, so this leaves a wide margin against an early-blank false
    // negative while still being long enough to reliably observe one.
    constexpr auto kPollWindow = 12ms;

    for (int i = 0; i < kIterations; ++i) {
        expectStaleClearNeverBlanksNewerMessage(bus, vm, kTtl, kPollWindow, i);
    }
}

// Finding 1 regression/stress test: ~StatusLineVM() must not return while a
// clear callback dequeued-but-not-yet-run by DelayedScheduler's worker thread
// is still outstanding, since that callback locks mutex_ and touches
// text_/clearHandle_ which would otherwise start being destroyed concurrently
// (a genuine use-after-destruction race). A true UAF race is inherently
// non-deterministic to force open on demand, so this test instead stresses
// the window heavily: many construct/arm/publish/destroy cycles against a
// single shared, busy DelayedScheduler with a ~0ms ttl, so the clear is very
// likely already due — and often already dequeued by the worker thread —
// the instant the destructor runs.
//
// This is not just a "doesn't crash" smoke test: an earlier draft of this fix
// claimed in-flight status from *inside* onClearFired() (mirroring
// HotplugService::flush()'s shape literally), and this exact test hung
// reproducibly under it within the first couple of iterations — there is a
// real window, between the worker thread dequeuing an entry (so cancel()
// already reports failure) and that callback actually acquiring mutex_ to
// claim itself, during which ~StatusLineVM() could observe zero outstanding
// work and return early, i.e. exactly the UAF this test exists to catch. The
// fix (see the class doc comment in status_line_vm.hpp) claims outstanding
// status at schedule() time instead, closing that window. So: a hang or
// crash here is a real regression, not test flakiness.
TEST(StatusLineVM, RepeatedShortTtlDestructionDoesNotRaceOrHang) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;  // shared across iterations: one busy worker thread
    devmgr::test::InlineUiDispatcher dispatcher;

    constexpr int kIterations = 200;
    for (int i = 0; i < kIterations; ++i) {
        devmgr::app::StatusLineVM vm(bus, timer, dispatcher, 0ms);
        vm.arm();
        bus.publish(devmgr::core::DeviceAddedEvent{usb("dev-" + std::to_string(i), "Widget")});
        // No synchronization delay here on purpose: destroying immediately
        // maximizes the chance the ~0ms clear is already due (or already
        // dequeued by the timer thread) right as the destructor's cancel()
        // races it.
    }
    SUCCEED();
}
