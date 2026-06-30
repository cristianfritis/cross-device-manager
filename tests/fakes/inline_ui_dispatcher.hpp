#pragma once
#include <functional>

#include "devmgr/app/ui_dispatcher.hpp"

namespace devmgr::test {

// Runs the posted closure immediately on the calling thread — deterministic
// for unit tests (no real UI thread to marshal onto).
class InlineUiDispatcher final : public app::IUiDispatcher {
   public:
    void post(std::function<void()> fn) override { fn(); }
};

}  // namespace devmgr::test
