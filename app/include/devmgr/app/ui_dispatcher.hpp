#pragma once
#include <functional>

namespace devmgr::app {

// Marshals a callback onto the frontend's UI thread. The concrete impl lives in
// each frontend (FTXUI / Qt); the app layer depends only on this interface.
class IUiDispatcher {
   public:
    IUiDispatcher() = default;
    virtual ~IUiDispatcher() = default;
    virtual void post(std::function<void()> fn) = 0;

    // Non-copyable, non-movable: dispatchers are owned by a frontend and used
    // only via this interface (pure-interface idiom; satisfies Core Guidelines C.21).
    IUiDispatcher(const IUiDispatcher&) = delete;
    IUiDispatcher& operator=(const IUiDispatcher&) = delete;
    IUiDispatcher(IUiDispatcher&&) = delete;
    IUiDispatcher& operator=(IUiDispatcher&&) = delete;
};

}  // namespace devmgr::app
