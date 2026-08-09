#pragma once
#include <memory>

#include "devmgr/core/result.hpp"
#include "devmgr/pal/backend_set.hpp"
#include "devmgr/pal/capabilities.hpp"

namespace devmgr::runtime {
class EventBus;
}

namespace devmgr::pal {

// Options an entry point passes down to backend construction. Deliberately
// small and platform-neutral: a platform that has no notion of an option
// ignores it rather than failing.
struct BackendOptions {
    // The application's event bus, for backends that publish diagnostics while
    // running rather than only returning a Result. A platform whose backends
    // all need one fails create() when it is absent, rather than wiring a
    // half-working set.
    runtime::EventBus* eventBus = nullptr;

    // Which endpoint the privileged channel connects to, where a platform has
    // more than one. `Default` is the production endpoint; `Private` is the
    // isolated endpoint the integration rig stands up, so a test run cannot
    // talk to the machine's real helper. Platforms with a single endpoint, or
    // with no privileged channel at all, ignore this.
    enum class PrivilegedEndpoint { Default, Private };
    PrivilegedEndpoint privilegedEndpoint = PrivilegedEndpoint::Default;
};

// The one place a platform is chosen. `create()` is DECLARED here, in core, and
// DEFINED exactly once per platform target, inside that platform's own backend
// directory; exactly one definition is linked into any binary, so selection
// happens at link time and this header names no platform type. Adding a platform
// means adding a directory and a definition — no frontend changes.
//
// The returned object OWNS every backend and must outlive every consumer of
// backends(). Entry points hold it for the process lifetime, in main()'s scope,
// so the existing construction/teardown ordering is preserved rather than
// re-derived — the Phase 2 hotplug use-after-free fix depends on that ordering.
//
// backends() is always fully populated: interfaces the platform does not
// implement carry the refusing implementations (pal/refusing_backends.hpp), and
// capabilities() states which ones those are. The two always agree.
class PlatformBackends {
   public:
    // Fails only when a backend the platform DOES implement cannot be brought
    // up. A platform that simply lacks an interface still succeeds — absence is
    // reported through capabilities(), never as a construction failure.
    static core::Result<std::unique_ptr<PlatformBackends>> create(const BackendOptions& options);

    PlatformBackends() = default;
    PlatformBackends(const PlatformBackends&) = delete;
    PlatformBackends& operator=(const PlatformBackends&) = delete;
    PlatformBackends(PlatformBackends&&) = delete;
    PlatformBackends& operator=(PlatformBackends&&) = delete;
    virtual ~PlatformBackends() = default;

    virtual const BackendSet& backends() const = 0;
    virtual PlatformCapabilities capabilities() const = 0;
};

}  // namespace devmgr::pal
