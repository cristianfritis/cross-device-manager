#pragma once
#include <string>
#include <vector>

#include "devmgr/pal/criticality.hpp"

namespace devmgr::core {

// How much a component is load-bearing, as shown to the user. This is a
// PRESENTATION tier, never an authorization: the daemon's guard decides what is
// actually permitted (services::evaluateDisable / evaluateModuleUnload), and it
// runs again, authoritatively, at the moment of the request.
enum class Criticality { Ordinary, Important, Essential };

// The user-facing word. Paired with the marker glyph everywhere it is shown, so
// the tier survives mono/plain terminals where colour is unavailable
// (DESIGN.md §10: colour is never the sole signal). Ordinary renders nothing.
const char* displayCriticality(Criticality level);

// Devices are classified from the SAME facts and the SAME pure policy the guard
// itself applies, so a row can never be marked Ordinary and then be refused (or
// marked Essential and sail through). Essential ⇔ the guard would refuse to
// disable this device right now.
//
// `facts` is probed ONCE per rebuild and reused across every row: probing is
// filesystem work and belongs nowhere near a per-row path (DESIGN.md §8).
Criticality classifyDevice(const pal::CriticalityFacts& facts, const std::string& sysfsPath);

// Modules mix live facts with a curated list, because refcounts alone are
// silent about the dangerous case: a display or storage driver at refcount 0
// still makes the machine unusable when unloaded, and that is exactly the
// unload a user is most likely to attempt. Precedence:
//   Essential  — on the curated list below (unload may make the system unusable)
//   Important  — held by another module, in use (refcount > 0), or a security
//                enforcement module
//   Ordinary   — everything else; no marker
Criticality classifyModule(const std::string& name, long refCount,
                           const std::vector<std::string>& holders);

}  // namespace devmgr::core
