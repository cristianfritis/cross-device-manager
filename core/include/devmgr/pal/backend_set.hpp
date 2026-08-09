#pragma once
#include <vector>

#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::pal {

// Every platform-dependent implementation the application consumes, in one
// place. The members are REFERENCES, never pointers: a platform that supplies
// no real implementation for an interface is given the refusing implementation
// for it (pal/refusing_backends.hpp), so every reference here is always valid to
// call and no consumer needs a null check or a platform branch.
//
// The update providers are a list rather than a reference because "none" is
// already expressible: a platform with no providers supplies an empty list, and
// the Updates surface reads that directly. Individual providers additionally
// report their own runtime ProviderAvailability, which is a different question
// from whether the platform has any at all.
//
// Non-owning. PlatformBackends owns the implementations and outlives every
// consumer of the set it hands out.
struct BackendSet {
    IDeviceEnumerator& enumerator;
    IHotplugMonitor& hotplug;
    IDeviceController& controller;
    IDriverManager& drivers;
    IPrivilegedChannel& privileged;
    ISystemInfo& systemInfo;
    ICriticalityProber& criticality;
    std::vector<IUpdateProvider*> updateProviders;
};

}  // namespace devmgr::pal
