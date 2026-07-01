#pragma once
#include <functional>
#include <utility>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::test {

// In-process IHotplugMonitor double. emit() synchronously invokes the callback
// on the caller's thread — deterministic for unit tests.
class FakeHotplugMonitor final : public pal::IHotplugMonitor {
   public:
    core::Result<void> start(Callback callback) override {
        callback_ = std::move(callback);
        started_ = true;
        return {};
    }
    void stop() override { started_ = false; }

    bool started() const { return started_; }
    void emit(const pal::HotplugEvent& event) {
        if (callback_) callback_(event);
    }

   private:
    Callback callback_;
    bool started_ = false;
};

}  // namespace devmgr::test
