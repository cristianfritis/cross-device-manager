#pragma once
#include <optional>
#include <string>
#include <vector>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/core/models.hpp"

namespace devmgr::app {

// Stateless view over the facade: turns a selected DeviceId into labeled lines.
class DeviceDetailVM {
   public:
    explicit DeviceDetailVM(ApplicationFacade& facade) : facade_(facade) {}
    std::vector<std::string> lines(const std::optional<core::DeviceId>& id) const;

   private:
    ApplicationFacade& facade_;
};

}  // namespace devmgr::app
