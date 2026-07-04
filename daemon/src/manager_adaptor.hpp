#pragma once
#include <memory>

#include <sdbus-c++/sdbus-c++.h>

#include "devmgr/daemon/request_processor.hpp"

namespace devmgr::daemon {

// sdbus-c++ LEAF FILE #1 (with main.cpp/polkit_authority.cpp): translates
// org.devmgr.Manager1 D-Bus calls into RequestProcessor calls and Result
// errors into named D-Bus errors. No logic of its own.
class ManagerAdaptor {
   public:
    ManagerAdaptor(sdbus::IConnection& connection, RequestProcessor& processor);

   private:
    RequestProcessor& processor_;
    std::unique_ptr<sdbus::IObject> object_;
};

}  // namespace devmgr::daemon
