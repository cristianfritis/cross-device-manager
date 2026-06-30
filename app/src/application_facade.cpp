#include "devmgr/app/application_facade.hpp"

#include <utility>

#include "devmgr/core/events.hpp"

namespace devmgr::app {

std::future<void> ApplicationFacade::refresh() {
    return scheduler_.submit([this] {
        auto result = enumerator_.enumerate();
        if (result) {
            service_.applyEnumeration(std::move(*result));
        } else {
            bus_.publish(
                core::ErrorEvent{.source = "enumerate", .message = result.error().message});
        }
    });
}

}  // namespace devmgr::app
