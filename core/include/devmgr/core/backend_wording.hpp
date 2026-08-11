#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "devmgr/core/result.hpp"

namespace devmgr::core {

// Shared wording for "a backend cannot serve this view" (backend-availability
// spec; docs/DESIGN.md §6, §6.1). Lives in core, not app/, because cli/ does
// not link app/ — a wording home inside the VM layer would leave the CLI's
// hint strings (cli/src/cli.cpp:37) as a third, untested copy. Established
// precedent: core/src/snapshot_presentation.cpp already emits user-facing
// sentences for the same reason.
//
// The rule this header enforces structurally: unavailabilityText() takes no
// Error and no string, so a D-Bus name, errno value, exception name, or
// filesystem path cannot reach a user-visible sentence at all (design D1). The
// raw text is a diagnostic — it belongs in the log and behind a disclosure,
// which app::BackendStatusVM owns.

// Snapshots is a backend identity even though no single service supplies it:
// the snapshot store lives behind the privileged channel, so a platform without
// one has no snapshots at all and the Snapshots view needs a sentence of its own
// rather than borrowing the devmgrd one.
enum class BackendId { Devmgrd, Fwupd, Dkms, Snapshots };

// Why a backend cannot serve, at the granularity that changes what the user
// would do about it. Derived from Error::Code by kindFor() — the single place
// an error code is interpreted for presentation.
//
// Unsupported is the one kind that is a fact of the BUILD and the MACHINE, not
// of the machine's configuration: the running platform has no implementation of
// that backend. Nothing the user installs, starts or retries changes it, which
// is why its sentences promise nothing and why noteRole() pins it to
// information permanently.
enum class UnavailabilityKind { Absent, Unreachable, NotPermitted, Unsupported };

// Enumerable for the total-mapping tests (and any future exhaustive render).
inline constexpr std::array<BackendId, 4> kAllBackends{BackendId::Devmgrd, BackendId::Fwupd,
                                                       BackendId::Dkms, BackendId::Snapshots};
inline constexpr std::array<UnavailabilityKind, 4> kAllUnavailabilityKinds{
    UnavailabilityKind::Absent, UnavailabilityKind::Unreachable, UnavailabilityKind::NotPermitted,
    UnavailabilityKind::Unsupported};

// NotFound -> absent, Io -> unreachable, Permission -> not permitted,
// Unsupported -> unsupported (backend-availability spec). Every other code
// falls back to Unreachable: in an availability context "something answered,
// but not usefully" is the honest reading, and it is the conservative one
// (it warns rather than staying quiet).
UnavailabilityKind kindFor(Error::Code code);

// The user-facing subject for a backend — never the process/service name alone.
// Used by the generic fallback sentence and available to surfaces that need to
// name the backend outside a full sentence.
std::string_view backendName(BackendId backend);

// The sentence a surface renders. Total: an unlisted (backend, kind) pair
// yields a calm generic sentence naming the backend, never an empty string and
// never a substituted diagnostic.
std::string unavailabilityText(BackendId backend, UnavailabilityKind kind);

// Update-provider id (core::UpdateProviderState::providerId) to backend
// identity. Lives beside the table so a new provider is a row here, never a
// branch in a view. nullopt for an unrecognized provider — a caller then has no
// sentence to offer and must stay silent rather than invent one.
std::optional<BackendId> backendForProvider(std::string_view providerId);

}  // namespace devmgr::core
