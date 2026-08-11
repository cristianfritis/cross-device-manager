#pragma once
#include <array>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/app/status_line_vm.hpp"  // StatusSeverity
#include "devmgr/core/backend_wording.hpp"
#include "devmgr/core/result.hpp"
#include "devmgr/pal/capabilities.hpp"

namespace devmgr::app {

// Which PAL capability supplies each backend identity. A platform that does not
// implement the capability has no such backend at all, which is what
// UnavailabilityKind::Unsupported means.
//
//   devmgrd   privilegedChannel  every mutation and the module/device control
//                                paths reach the helper through it
//   fwupd     updateProviders    firmware candidates come from a provider
//   dkms      updateProviders    the DKMS status reporter is a provider too
//   snapshots privilegedChannel  the snapshot store lives behind the helper
//
// Free-standing and total so a test can enumerate it, and so the mapping is
// stated once rather than re-derived at each call site.
bool capabilitySupplies(const pal::PlatformCapabilities& capabilities, core::BackendId backend);

// What a view renders IN PLACE OF ITS CONTENT when every source feeding it is
// unimplemented on the running platform (backend-availability: "A view with no
// implemented source states why rather than showing nothing").
//
// nullopt whenever the view has at least one implemented source — including
// when that source is merely unreachable, which is a different fact with a
// different sentence and a view that still renders what it can.
//
// `everySourceUnimplemented` is the caller's own reading of the capability
// descriptor, because which capabilities feed a view is the view's knowledge,
// not this type's. What is shared is the consequence: one sentence, from the
// core table, at information severity, and no empty-result or loading string
// beside it.
std::optional<std::string> unsupportedViewText(core::BackendId backend,
                                               bool everySourceUnimplemented);

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
//   Info         if kind is Unsupported                   (platform, not config)
//   Warning      if kind is Unreachable or NotPermitted   (present, not serving)
//   Warning      if the note explains a verb the user attempted
//   Info         otherwise                                (Absent)
//
// Unsupported is tested FIRST, so it is information under every input including
// the attempted-verb one. A platform that does not implement a backend offers no
// verb that depends on it, so there is no attempt to escalate for; pinning it
// here means a mis-gated caller degrades to a calm sentence instead of raising a
// warning about something the user can never change.
//
// Danger is not in the range — not because a reviewer would catch it, but
// because it is not a branch. Danger stays reserved for an operation that ran
// and failed (docs/DESIGN.md §5.5: a steady-state configuration is not an
// error). An optional service that was never installed therefore never carries
// a standing warning.
StatusSeverity noteRole(core::UnavailabilityKind kind, bool blocksAttemptedVerb);

// The lines a diagnostics affordance reveals: one per degraded backend, naming
// the backend and then its raw text verbatim. Shared by both surfaces so the
// GUI's "Details" region and the TUI's Diagnostics region reveal byte-identical
// bytes — parity covers the demoted detail, not only the sentence.
//
// Never empty for a note whose backend reported no detail: the region says so
// rather than rendering a blank line under a heading.
std::vector<std::string> diagnosticLines(const std::vector<BackendNote>& notes);

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

    // Seeds the backends the running platform has no implementation of, from
    // the capability descriptor alone — no backend is called and no error is
    // needed to discover it (design D1; backend-availability: the distinction
    // "SHALL be drawn from the platform capability descriptor, not from a
    // failed call").
    //
    // Called once, at facade construction. A backend marked here is unsupported
    // for the life of the process: a platform cannot grow a capability while
    // running, so observe() cannot clear it and does not log for it. That also
    // means a mis-gated caller that does attempt the verb gets the same calm
    // unsupported sentence rather than a second, contradictory note.
    void applyCapabilities(const pal::PlatformCapabilities& capabilities);

    // Current availability of one backend. An empty `error` means healthy and
    // clears any existing note; the next degradation logs again.
    //
    // Ignored for a backend applyCapabilities() marked unsupported — including
    // an empty error, which would otherwise report a nonexistent backend as
    // healthy.
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
    // Set once by applyCapabilities() and never cleared — the platform fact,
    // held apart from entries_ so an observation cannot overwrite it.
    std::array<bool, kSlots> unimplemented_{};
    // Last kind logged per backend — the transition key. Cleared when the
    // backend recovers so a later outage is logged again.
    std::array<std::optional<core::UnavailabilityKind>, kSlots> logged_;
};

}  // namespace devmgr::app
