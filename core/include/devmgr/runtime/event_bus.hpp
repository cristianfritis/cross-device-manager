#pragma once
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace devmgr::runtime {

class EventBus;

// Move-only RAII token; unsubscribes its handler on destruction.
class Subscription {
   public:
    Subscription() = default;
    Subscription(EventBus* bus, std::type_index type, std::uint64_t id)
        : bus_(bus), type_(type), id_(id) {}
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept { moveFrom(other); }
    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            reset();
            moveFrom(other);
        }
        return *this;
    }
    ~Subscription() { reset(); }

    void reset();

   private:
    void moveFrom(Subscription& other) {
        bus_ = other.bus_;
        type_ = other.type_;
        id_ = other.id_;
        other.bus_ = nullptr;
    }

    EventBus* bus_ = nullptr;
    std::type_index type_ = std::type_index(typeid(void));
    std::uint64_t id_ = 0;
};

// Thread-safe pub/sub with an unsubscribe barrier: once unsubscribe() (or the
// Subscription dtor) returns, the handler is not running and will never run
// again, so captured state may be destroyed immediately. The one exception is
// unsubscribing from inside the handler's own callback — the publishing thread
// cannot wait for itself, so the current invocation finishes; no further ones
// start.
//
// Hazard (inherent to barrier designs): unsubscribing while holding a lock
// that the handler's callback also takes can deadlock. Don't hold locks
// across unsubscribe.
class EventBus {
   public:
    template <class Event>
    [[nodiscard]] Subscription subscribe(std::function<void(const Event&)> handler) {
        const auto type = std::type_index(typeid(Event));
        auto entry = std::make_shared<Entry>();
        entry->fn = std::make_shared<std::function<void(const Event&)>>(std::move(handler));
        std::scoped_lock lock(mutex_);
        const auto id = nextId_++;
        entry->id = id;
        handlers_[type].push_back(std::move(entry));
        return {this, type, id};
    }

    template <class Event>
    void publish(const Event& event) {
        std::vector<std::shared_ptr<Entry>> targets;
        {
            std::scoped_lock lock(mutex_);
            const auto it = handlers_.find(std::type_index(typeid(Event)));
            if (it == handlers_.end()) return;
            targets = it->second;
        }
        // Invoke outside the lock so handlers may (un)subscribe without deadlock.
        for (const auto& entry : targets) {
            if (!beginInvoke(*entry)) continue;  // unsubscribed since the snapshot
            const InvokeGuard guard{*this, *entry};
            (*std::static_pointer_cast<std::function<void(const Event&)>>(entry->fn))(event);
        }
    }

   private:
    friend class Subscription;

    struct Entry {
        std::uint64_t id = 0;
        std::shared_ptr<void> fn;
        std::size_t inFlight = 0;              // invocations currently executing
        std::vector<std::thread::id> running;  // one element per in-flight invocation
        bool removed = false;                  // unsubscribed; no new invocations may start
    };

    // The no-callback-after-unsubscribe guarantee depends on this recheck:
    // a handler unsubscribed after a publish snapshot was taken must not fire.
    bool beginInvoke(Entry& entry) {
        std::scoped_lock lock(mutex_);
        if (entry.removed) return false;
        ++entry.inFlight;
        entry.running.push_back(std::this_thread::get_id());
        return true;
    }

    void endInvoke(Entry& entry) {
        bool drainedRemoved = false;
        {
            std::scoped_lock lock(mutex_);
            --entry.inFlight;
            auto& run = entry.running;
            run.erase(std::ranges::find(run, std::this_thread::get_id()));
            drainedRemoved = entry.removed && entry.inFlight == 0;
        }
        if (drainedRemoved) drained_.notify_all();
    }

    // Balances beginInvoke even when the handler throws — without it, an
    // exception would leave inFlight stuck and unsubscribe would wait forever.
    struct InvokeGuard {
        EventBus& bus;
        Entry& entry;
        InvokeGuard(EventBus& busIn, Entry& entryIn) : bus(busIn), entry(entryIn) {}
        InvokeGuard(const InvokeGuard&) = delete;
        InvokeGuard& operator=(const InvokeGuard&) = delete;
        InvokeGuard(InvokeGuard&&) = delete;
        InvokeGuard& operator=(InvokeGuard&&) = delete;
        ~InvokeGuard() { bus.endInvoke(entry); }
    };

    void unsubscribe(std::type_index type, std::uint64_t id) {
        std::unique_lock lock(mutex_);
        const auto it = handlers_.find(type);
        if (it == handlers_.end()) return;
        auto& vec = it->second;
        const auto pos = std::ranges::find_if(vec, [id](const auto& e) { return e->id == id; });
        if (pos == vec.end()) return;
        const std::shared_ptr<Entry> entry = std::move(*pos);
        entry->removed = true;
        vec.erase(pos);
        if (vec.empty()) handlers_.erase(it);
        // Re-entrant unsubscribe from inside this handler's own callback:
        // waiting would self-deadlock. removed=true already stops any future
        // invocation; the in-flight one is this frame's caller (deferred
        // removal — the Entry is released when publish drops its snapshot).
        const auto self = std::this_thread::get_id();
        if (std::ranges::find(entry->running, self) != entry->running.end()) return;
        // Barrier: wait until every in-flight invocation of this handler has
        // returned before letting the subscriber tear down captured state.
        drained_.wait(lock, [&entry] { return entry->inFlight == 0; });
    }

    std::mutex mutex_;
    std::condition_variable drained_;
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::type_index, std::vector<std::shared_ptr<Entry>>> handlers_;
};

inline void Subscription::reset() {
    if (bus_ == nullptr) return;
    bus_->unsubscribe(type_, id_);
    bus_ = nullptr;
}

}  // namespace devmgr::runtime
