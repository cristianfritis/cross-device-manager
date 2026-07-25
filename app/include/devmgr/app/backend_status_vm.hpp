#pragma once
#include <array>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/app/status_line_vm.hpp"  // StatusSeverity
#include "devmgr/core/backend_wording.hpp"
#include "devmgr/core/result.hpp"

namespace devmgr::app {

// One degraded backend, as both surfaces read it (backend-availability spec;
// design D2). This type is the single accessor for "a backend cannot serve this
// view": the cross-surface parity test asserts that what the GUI renders and
// what the TUI renders both equal notes()[i].text, so wording agreement is
// checkable rather than a convention.
//
// text and diagnostic are deliberately two fields with no combined accessor:
// with nothing to reach for, a surface cannot concatenate the raw detail into
// the sentence by accident — which is exactly the shipped defect this change
// exists to remove (app/src/updates_vm.cpp availabilityCell()).
struct BackendNote {
    core::BackendId backend = core::BackendId::Devmgrd;
    core::UnavailabilityKind kind = core::UnavailabilityKind::Unreachable;
    std::string text;        // core wording table — the only thing a surface renders by default
    std::string diagnostic;  // raw backend message — log and disclosure only, never primary
    StatusSeverity role = StatusSeverity::Info;
};

// The role mapping, total and free-standing so a test can enumerate every
// (kind, blocksAttemptedVerb) pair (design D3):
//
//   Warning      if kind is Unreachable or NotPermitted   (present, not serving)
//   Warning      if the note explains a verb the user attempted
//   Info         otherwise                                (Absent, Unsupported)
//
// Danger is not in the range — not because a reviewer would catch it, but
// because it is not a branch. Danger stays reserved for an operation that ran
// and failed (docs/DESIGN.md §5.5: a steady-state configuration is not an
// error). An optional service that was never installed therefore never carries
// a standing warning.
StatusSeverity noteRole(core::UnavailabilityKind kind, bool blocksAttemptedVerb);

// Toolkit-agnostic availability status for every backend a surface reads.
//
// observe() is safe to call once per poll: the note is replaced, but the raw
// diagnostic is written to the log only when a backend's (backend, kind) pair
// changes. Availability is polled, so logging per observation would produce one
// warn line per poll forever on a machine that simply lacks fwupd.
//
// Availability observations arrive from facade workers while the UI thread
// reads notes(), so all state is mutex-guarded (StatusLineVM idiom).
class BackendStatusVM {
   public:
    BackendStatusVM() = default;
    ~BackendStatusVM() = default;
    // Holds a mutex: neither copyable nor movable (UpdatesVM idiom).
    BackendStatusVM(const BackendStatusVM&) = delete;
    BackendStatusVM& operator=(const BackendStatusVM&) = delete;
    BackendStatusVM(BackendStatusVM&&) = delete;
    BackendStatusVM& operator=(BackendStatusVM&&) = delete;

    // Current availability of one backend. An empty `error` means healthy and
    // clears any existing note; the next degradation logs again.
    void observe(core::BackendId backend, const std::optional<core::Error>& error);

    // Degraded backends only, in core::kAllBackends order. Empty when every
    // observed backend is healthy — a surface renders no region at all then.
    //
    // This is the PERSISTENT channel: every note's role here is a pure function
    // of its kind. It does not change on focus, on selection, or because a verb
    // was attempted — a standing note must not pulse.
    std::vector<BackendNote> notes() const;

    // The note for one backend, or nullopt when it is healthy or unobserved.
    // Pass blocksAttemptedVerb when the note is explaining a verb the user
    // attempted, which escalates an otherwise calm note to Warning — the
    // "blocked verb reuses the shared sentence" rule, so a disabled control
    // never gets a separately authored reason.
    //
    // That escalation is the TRANSIENT channel only: the disabled-action reason
    // and the status line (docs/DESIGN.md §5.3). It is computed per call and
    // stored nowhere, so it cannot bleed back into notes().
    std::optional<BackendNote> noteFor(core::BackendId backend,
                                       bool blocksAttemptedVerb = false) const;

   private:
    struct Entry {
        core::UnavailabilityKind kind = core::UnavailabilityKind::Unreachable;
        std::string diagnostic;
    };
    // Indexed by static_cast<std::size_t>(BackendId); kAllBackends is asserted
    // to be that same dense 0..N-1 order in the .cpp.
    static constexpr std::size_t kSlots = core::kAllBackends.size();

    mutable std::mutex mutex_;
    std::array<std::optional<Entry>, kSlots> entries_;
    // Last kind logged per backend — the transition key. Cleared when the
    // backend recovers so a later outage is logged again.
    std::array<std::optional<core::UnavailabilityKind>, kSlots> logged_;
};

}  // namespace devmgr::app
