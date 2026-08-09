#pragma once
#include <string>
#include <vector>

#include "devmgr/pal/backend_set.hpp"
#include "devmgr/pal/capabilities.hpp"
#include "devmgr/pal/refusing_backends.hpp"

namespace devmgr::test {

// The platform-portability invariant, checkable against any platform's factory:
// a capability reported implemented must NOT be backed by the refusing
// instance, and a capability reported unimplemented must be. Returns one entry
// per interface that breaks it, empty when the descriptor and the set agree.
//
// Update providers are excluded on purpose: absence there is the empty list,
// not a refusing implementation, so the rule is `updateProviders == !empty()`
// and is checked separately below.
inline std::vector<std::string> capabilityMismatches(const pal::PlatformCapabilities& caps,
                                                     const pal::BackendSet& set) {
    std::vector<std::string> bad;
    const auto check = [&bad](const char* name, bool implemented, const void* actual,
                              const void* refusing) {
        const bool isRefusing = actual == refusing;
        if (implemented == isRefusing) bad.emplace_back(name);
    };
    check("deviceEnumeration", caps.deviceEnumeration, &set.enumerator,
          &pal::refusingDeviceEnumerator());
    check("hotplug", caps.hotplug, &set.hotplug, &pal::refusingHotplugMonitor());
    check("deviceControl", caps.deviceControl, &set.controller, &pal::refusingDeviceController());
    check("driverManagement", caps.driverManagement, &set.drivers, &pal::refusingDriverManager());
    check("privilegedChannel", caps.privilegedChannel, &set.privileged,
          &pal::refusingPrivilegedChannel());
    check("systemInfo", caps.systemInfo, &set.systemInfo, &pal::refusingSystemInfo());
    check("criticalityProbing", caps.criticalityProbing, &set.criticality,
          &pal::refusingCriticalityProber());
    if (caps.updateProviders == set.updateProviders.empty()) bad.emplace_back("updateProviders");
    return bad;
}

}  // namespace devmgr::test
