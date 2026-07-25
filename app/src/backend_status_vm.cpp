#include "devmgr/app/backend_status_vm.hpp"

#include "devmgr/runtime/logging.hpp"

namespace devmgr::app {
namespace {

// entries_/logged_ are indexed by the enum value directly, so kAllBackends must
// stay the dense 0..N-1 listing of BackendId. Adding a backend without adding it
// here fails to compile rather than silently losing its slot.
static_assert(core::kAllBackends.size() == 3);
static_assert(core::kAllBackends[0] == core::BackendId::Devmgrd);
static_assert(core::kAllBackends[1] == core::BackendId::Fwupd);
static_assert(core::kAllBackends[2] == core::BackendId::Dkms);

std::size_t slotOf(core::BackendId backend) {
    return static_cast<std::size_t>(backend);
}

// Machine name for the log — deliberately not core::backendName(), which is the
// user-facing subject. A log reader wants the unit/service to look at.
const char* logName(core::BackendId backend) {
    switch (backend) {
        case core::BackendId::Devmgrd:
            return "devmgrd";
        case core::BackendId::Fwupd:
            return "fwupd";
        case core::BackendId::Dkms:
            return "dkms";
    }
    return "backend";
}

const char* kindLabel(core::UnavailabilityKind kind) {
    switch (kind) {
        case core::UnavailabilityKind::Absent:
            return "absent";
        case core::UnavailabilityKind::Unreachable:
            return "unreachable";
        case core::UnavailabilityKind::NotPermitted:
            return "not permitted";
        case core::UnavailabilityKind::Unsupported:
            return "unsupported";
    }
    return "unknown";
}

BackendNote makeNote(core::BackendId backend, core::UnavailabilityKind kind,
                     const std::string& diagnostic, bool blocksAttemptedVerb) {
    BackendNote note;
    note.backend = backend;
    note.kind = kind;
    note.text = core::unavailabilityText(backend, kind);
    note.diagnostic = diagnostic;
    note.role = noteRole(kind, blocksAttemptedVerb);
    return note;
}

}  // namespace

StatusSeverity noteRole(core::UnavailabilityKind kind, bool blocksAttemptedVerb) {
    if (blocksAttemptedVerb) return StatusSeverity::Warning;
    switch (kind) {
        case core::UnavailabilityKind::Unreachable:
        case core::UnavailabilityKind::NotPermitted:
            return StatusSeverity::Warning;
        case core::UnavailabilityKind::Absent:
        case core::UnavailabilityKind::Unsupported:
            break;
    }
    return StatusSeverity::Info;
}

std::vector<std::string> diagnosticLines(const std::vector<BackendNote>& notes) {
    std::vector<std::string> out;
    out.reserve(notes.size());
    for (const auto& note : notes) {
        std::string line = std::string(core::backendName(note.backend)) + ": ";
        line += note.diagnostic.empty() ? "no detail reported" : note.diagnostic;
        out.push_back(std::move(line));
    }
    return out;
}

void BackendStatusVM::observe(core::BackendId backend, const std::optional<core::Error>& error) {
    const std::size_t slot = slotOf(backend);
    const std::lock_guard lock(mutex_);
    if (!error) {
        entries_.at(slot).reset();
        logged_.at(slot).reset();  // a later outage is a fresh transition, so it logs again
        return;
    }
    const core::UnavailabilityKind kind = core::kindFor(error->code);
    const bool transition = !logged_.at(slot) || *logged_.at(slot) != kind;
    entries_.at(slot) = Entry{.kind = kind, .diagnostic = error->message};
    if (!transition) return;  // same state, another poll — nothing new to record
    logged_.at(slot) = kind;
    // Logged under the lock: transitions are rare, and serialising them keeps a
    // concurrent recovery from interleaving a stale line after this one.
    spdlog::warn("{} unavailable ({}): {}", logName(backend), kindLabel(kind), error->message);
}

std::vector<BackendNote> BackendStatusVM::notes() const {
    std::vector<BackendNote> out;
    const std::lock_guard lock(mutex_);
    for (const auto backend : core::kAllBackends) {
        const auto& entry = entries_.at(slotOf(backend));
        if (!entry) continue;
        out.push_back(makeNote(backend, entry->kind, entry->diagnostic,
                               /*blocksAttemptedVerb=*/false));
    }
    return out;
}

std::optional<BackendNote> BackendStatusVM::noteFor(core::BackendId backend,
                                                    bool blocksAttemptedVerb) const {
    const std::size_t slot = slotOf(backend);
    const std::lock_guard lock(mutex_);
    const auto& entry = entries_.at(slot);
    if (!entry) return std::nullopt;
    return makeNote(backend, entry->kind, entry->diagnostic, blocksAttemptedVerb);
}

}  // namespace devmgr::app
