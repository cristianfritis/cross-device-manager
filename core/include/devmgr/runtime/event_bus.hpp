#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
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

class EventBus {
   public:
    template <class Event>
    [[nodiscard]] Subscription subscribe(std::function<void(const Event&)> handler) {
        const auto type = std::type_index(typeid(Event));
        auto stored = std::make_shared<std::function<void(const Event&)>>(std::move(handler));
        std::lock_guard<std::mutex> lock(mutex_);
        const auto id = nextId_++;
        handlers_[type].push_back(Entry{id, std::move(stored)});
        return Subscription(this, type, id);
    }

    template <class Event>
    void publish(const Event& event) {
        std::vector<std::shared_ptr<void>> targets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = handlers_.find(std::type_index(typeid(Event)));
            if (it == handlers_.end()) return;
            targets.reserve(it->second.size());
            for (const auto& entry : it->second) targets.push_back(entry.fn);
        }
        // Invoke outside the lock so handlers may (un)subscribe without deadlock.
        for (const auto& fn : targets) {
            (*std::static_pointer_cast<std::function<void(const Event&)>>(fn))(event);
        }
    }

   private:
    friend class Subscription;
    struct Entry {
        std::uint64_t id;
        std::shared_ptr<void> fn;
    };

    void unsubscribe(std::type_index type, std::uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = handlers_.find(type);
        if (it == handlers_.end()) return;
        auto& vec = it->second;
        vec.erase(
            std::remove_if(vec.begin(), vec.end(), [id](const Entry& e) { return e.id == id; }),
            vec.end());
    }

    std::mutex mutex_;
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::type_index, std::vector<Entry>> handlers_;
};

inline void Subscription::reset() {
    if (bus_ == nullptr) return;
    bus_->unsubscribe(type_, id_);
    bus_ = nullptr;
}

}  // namespace devmgr::runtime
