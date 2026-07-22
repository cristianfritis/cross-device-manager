#include "devmgr/app/status_line_vm.hpp"

#include <cctype>
#include <utility>

#include "devmgr/core/events.hpp"

namespace devmgr::app {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Sentence-case status prose (DESIGN.md §6) lowercases the shared displayBus()
// token so the bus name flows mid-sentence ("usb device added: ...") while
// still deriving from the one casing source, not a second ad-hoc conversion.
std::string describe(const core::Device& d, const char* verb) {
    return lower(core::displayBus(d.bus)) + " device " + verb + ": " + d.name;
}

}  // namespace

StatusLineVM::StatusLineVM(runtime::EventBus& bus, runtime::DelayedScheduler& timer,
                           IUiDispatcher& dispatcher, std::chrono::milliseconds ttl)
    : timer_(timer), dispatcher_(dispatcher), ttl_(ttl) {
    subAdded_ = bus.subscribe<core::DeviceAddedEvent>([this](const core::DeviceAddedEvent& e) {
        if (armed_.load()) setMessage(describe(e.device, "added"), StatusSeverity::Info);
    });
    subChanged_ =
        bus.subscribe<core::DeviceChangedEvent>([this](const core::DeviceChangedEvent& e) {
            if (armed_.load()) setMessage(describe(e.device, "changed"), StatusSeverity::Info);
        });
    subRemoved_ = bus.subscribe<core::DeviceRemovedEvent>([this](const core::DeviceRemovedEvent&) {
        // Removed carries only a DeviceId (the device is gone) — generic text.
        if (armed_.load()) setMessage("device removed", StatusSeverity::Info);
    });
    subTaskCompleted_ =
        bus.subscribe<core::TaskCompletedEvent>([this](const core::TaskCompletedEvent& e) {
            // Mutation results (Phase 4) share the transient line — success and
            // failure alike; the message already says which, and its ok flag
            // gives the colour valence (a guard refusal publishes ok=false).
            if (armed_.load())
                setMessage(e.message, e.ok ? StatusSeverity::Success : StatusSeverity::Danger);
        });
}

StatusLineVM::~StatusLineVM() {
    // Unsubscribe first, before touching the barrier below — mirrors
    // HotplugService::stop() calling monitor_.stop() first to join the
    // reader thread before draining in-flight work. This narrows the race
    // but does not close it: per EventBus::publish() (event_bus.hpp),
    // publish() snapshots its handler shared_ptrs under its own lock,
    // releases that lock, then invokes them outside it, while
    // Subscription::reset() (invoked below) only erases the entry from
    // EventBus's handler map under that same lock — it does not, and
    // cannot, wait for an invocation that was already snapshotted before
    // reset() ran. So a bus.publish() call already past its snapshot point
    // when this destructor runs can still invoke setMessage() on a `this`
    // that is mid-destruction or already destroyed, later. Unsubscribing
    // first only guarantees no *new* snapshot taken after this point will
    // reach setMessage(); see the class doc comment in status_line_vm.hpp
    // for why this residual gap isn't eliminated by this class alone.
    subAdded_.reset();
    subRemoved_.reset();
    subChanged_.reset();
    subTaskCompleted_.reset();

    // Cancel the pending clear so its callback can't run against a half-dead
    // VM. cancel() returning true guarantees it will never run, so we can
    // resolve (decrement) it right here; false means the timer thread had
    // already dequeued it — see the class doc comment for why the barrier
    // below, not this cancel() call, is what actually has to resolve that case.
    std::unique_lock<std::mutex> lock(mutex_);
    if (clearHandle_ != 0 && timer_.cancel(clearHandle_)) --outstandingClears_;
    // Bump generation_ so that if the cancel() above returned false and
    // onClearFired() is still outstanding, it recognizes itself as superseded
    // once it does run and skips mutating text_/clearHandle_ (belt-and-
    // suspenders: harmless either way since we're about to destroy, but keeps
    // the invariant "only the current generation mutates state" uniform).
    ++generation_;
    // Block until every outstanding clear — any schedule() not yet resolved
    // above by a true cancel() — has actually finished running. Otherwise
    // this destructor could return, and text_/mutex_/armed_ start being
    // destroyed, while such a callback is still mid-flight about to lock
    // mutex_ on this object.
    clearDone_.wait(lock, [this] { return outstandingClears_ == 0; });
}

std::string StatusLineVM::text() const {
    std::scoped_lock lock(mutex_);
    return text_;
}

StatusSeverity StatusLineVM::severity() const {
    std::scoped_lock lock(mutex_);
    // Empty line ⇒ steady state, regardless of the last message's valence.
    return text_.empty() ? StatusSeverity::Ok : severity_;
}

void StatusLineVM::setMessage(std::string message, StatusSeverity severity) {
    {
        std::scoped_lock lock(mutex_);
        text_ = std::move(message);
        severity_ = severity;
        // Resolve the previous clear immediately if cancel() guarantees it
        // will never run; otherwise leave it counted in outstandingClears_ —
        // it will resolve itself from inside onClearFired() once it actually
        // runs (see there for why: this is what closes the gap between "the
        // timer thread dequeued it" and "the timer thread's callback has
        // actually acquired mutex_ to do anything observable").
        if (clearHandle_ != 0 && timer_.cancel(clearHandle_)) --outstandingClears_;
        const std::uint64_t myGeneration = ++generation_;
        ++outstandingClears_;  // this new schedule is outstanding until resolved
        clearHandle_ = timer_.schedule(ttl_, [this, myGeneration] { onClearFired(myGeneration); });
    }
    dispatcher_.post([] {});  // wake the UI to re-render the new message
}

void StatusLineVM::onClearFired(std::uint64_t generation) {
    bool cleared = false;
    {
        std::scoped_lock lock(mutex_);
        // `generation` not matching generation_ means a newer setMessage() has
        // already superseded this callback — either because our own cancel()
        // (in setMessage() or the destructor) raced a dequeue already in
        // flight, or, during ordinary operation, because a rapid-fire
        // setMessage() scheduled its own clear before this stale one got a
        // chance to run. Either way this clear must no-op: touching
        // text_/clearHandle_ now would wipe out a newer message and its own
        // still-pending clear timer.
        if (generation == generation_) {
            text_.clear();
            severity_ = StatusSeverity::Ok;  // back to steady state with the empty line
            clearHandle_ = 0;
            cleared = true;
        }
        // This schedule is now resolved regardless of whether it was stale —
        // it has definitely finished running. This is the only place a
        // false-cancel()'d schedule ever gets resolved, which is exactly what
        // makes ~StatusLineVM()'s clearDone_.wait() above safe to block on.
        --outstandingClears_;
    }
    if (cleared) dispatcher_.post([] {});  // wake the UI to re-render the cleared line
    clearDone_.notify_all();
}

}  // namespace devmgr::app
