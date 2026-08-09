#pragma once

namespace devmgr::pal {

// Which PAL interfaces the running platform supplies a real implementation for.
// Plain bools, fixed at backend-set construction and constant for the process:
// a platform cannot grow a capability while running.
//
// This exists so presentation code can decide whether to OFFER something —
// compose a toolbar, populate a view, bind a shortcut — without calling a
// backend to find out. Attempting a verb in order to discover it is unavailable
// is the "attempt then apologise" pattern docs/DESIGN.md §5.5 forbids.
//
// It is NOT a substitute for handling failure: a capability reported implemented
// can still fail at runtime (an unreachable helper, a device that vanished), and
// callers keep their existing error paths.
struct PlatformCapabilities {
    bool deviceEnumeration = false;
    bool hotplug = false;
    bool deviceControl = false;
    bool driverManagement = false;
    bool privilegedChannel = false;
    bool updateProviders = false;
    bool criticalityProbing = false;
    bool systemInfo = false;
};

}  // namespace devmgr::pal
