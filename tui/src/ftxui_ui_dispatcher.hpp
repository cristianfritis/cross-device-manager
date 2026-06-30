#pragma once
#include <functional>
#include <mutex>
#include <queue>

#include <ftxui/component/screen_interactive.hpp>

#include "devmgr/app/ui_dispatcher.hpp"

namespace devmgr::tui {

// IUiDispatcher over FTXUI: post() enqueues + wakes the loop with Event::Custom;
// drain() runs the queued closures on the UI thread. (FTXUI 6.1.9 also offers
// ScreenInteractive::Post(Closure); the queue+PostEvent variant is the locked design.)
class FtxuiUiDispatcher final : public app::IUiDispatcher {
   public:
    explicit FtxuiUiDispatcher(ftxui::ScreenInteractive& screen) : screen_(screen) {}

    void post(std::function<void()> fn) override {
        {
            std::scoped_lock lock(mutex_);
            queue_.push(std::move(fn));
        }
        screen_.PostEvent(ftxui::Event::Custom);
    }

    void drain() {
        std::queue<std::function<void()>> local;
        {
            std::scoped_lock lock(mutex_);
            std::swap(local, queue_);
        }
        while (!local.empty()) {
            local.front()();
            local.pop();
        }
    }

   private:
    ftxui::ScreenInteractive& screen_;
    std::mutex mutex_;
    std::queue<std::function<void()>> queue_;
};

}  // namespace devmgr::tui
