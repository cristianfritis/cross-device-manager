#pragma once
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>  // std::pair

#include "devmgr/app/modules_vm.hpp"        // app::ModuleSignature
#include "devmgr/app/status_line_vm.hpp"    // app::StatusSeverity
#include "devmgr/app/updates_vm.hpp"        // app::UpdateRowState
#include "devmgr/core/criticality.hpp"      // core::Criticality
#include "devmgr/core/models.hpp"           // core::DeviceStatus
#include "devmgr/core/snapshot_models.hpp"  // core::SnapshotHealth
#include "tui/src/semantics.hpp"

// Per-row state → semantic role/glyph mapping (docs/DESIGN.md §4.1 TUI table).
//
// The role→colour mapping deliberately lives in the TUI (per-surface
// presentation) while the *state* comes from the ViewModel accessors (design
// decision 1a). It sits here rather than inside the shell closure so the
// documented per-view colour table is directly unit-testable: these are pure
// switches over the app/core enums with no toolkit dependency, so the tests
// that link app+core can assert the mapping while the render tests assert what
// each role paints. A nullopt state means the row is structural (group header,
// placeholder, out-of-range row): it maps to no colour and no glyph and stays
// plain. Nullopt never means "normal" — a checked-and-normal row takes
// Role::Nominal, so the two cases cannot be confused.

namespace devmgr::tui {

constexpr std::optional<Role> roleForDeviceStatus(std::optional<core::DeviceStatus> status) {
    if (!status) return std::nullopt;
    switch (*status) {
        case core::DeviceStatus::Active:
            return Role::Nominal;
        case core::DeviceStatus::Disabled:
            // Muted, not danger: a device the user turned off is not a fault, and
            // Error below already owns danger.
            return Role::Muted;
        case core::DeviceStatus::Transitioning:
            return Role::Warning;
        case core::DeviceStatus::Error:
            return Role::Danger;
        case core::DeviceStatus::Unknown:
            return Role::Muted;
    }
    return std::nullopt;
}

constexpr std::optional<render::Glyph> glyphForDeviceStatus(
    std::optional<core::DeviceStatus> status) {
    if (!status) return std::nullopt;
    switch (*status) {
        case core::DeviceStatus::Active:
            return render::Glyph::Enabled;  // +
        case core::DeviceStatus::Disabled:
            return render::Glyph::Disabled;  // -
        case core::DeviceStatus::Transitioning:
            return render::Glyph::Unavailable;  // ? (in-flight/indeterminate)
        case core::DeviceStatus::Error:
            return render::Glyph::Unsigned;  // ! (fault)
        case core::DeviceStatus::Unknown:
            return render::Glyph::Unavailable;  // ?
    }
    return std::nullopt;
}

constexpr std::optional<Role> roleForSignature(std::optional<app::ModuleSignature> sig) {
    if (!sig) return std::nullopt;
    switch (*sig) {
        case app::ModuleSignature::Signed:
            return Role::Nominal;
        case app::ModuleSignature::Unsigned:
            return Role::Danger;
        case app::ModuleSignature::Undetermined:
            return Role::Muted;
    }
    return std::nullopt;
}

constexpr std::optional<Role> roleForUpdateState(std::optional<app::UpdateRowState> state) {
    if (!state) return std::nullopt;
    switch (*state) {
        case app::UpdateRowState::Available:
            return Role::Info;
        case app::UpdateRowState::UpToDate:
            return Role::Nominal;  // settled, no action
        case app::UpdateRowState::Error:
            return Role::Danger;
    }
    return std::nullopt;
}

// Health wins over the HEAD/last-good accent: a corrupt HEAD must read danger,
// not accent. Healthy HEAD/last-good rows take the accent (the history-view text
// markers carry the same fact without colour, §10).
constexpr std::optional<Role> roleForSnapshotRow(std::optional<core::SnapshotHealth> health,
                                                 bool isHead, bool isLastGood) {
    if (!health) return std::nullopt;
    switch (*health) {
        case core::SnapshotHealth::Corrupt:
            return Role::Danger;
        case core::SnapshotHealth::Unsupported:
            return Role::Warning;
        case core::SnapshotHealth::Ok:
            break;
    }
    if (isHead || isLastGood) return Role::Accent;
    return Role::Nominal;  // healthy, unmarked: the same resting state as the other views
}

constexpr std::optional<Role> roleForSeverity(app::StatusSeverity severity) {
    switch (severity) {
        case app::StatusSeverity::Ok:
            return std::nullopt;
        case app::StatusSeverity::Success:
            return Role::Success;
        case app::StatusSeverity::Warning:
            return Role::Warning;
        case app::StatusSeverity::Danger:
            return Role::Danger;
        case app::StatusSeverity::Info:
            return Role::Info;
    }
    return std::nullopt;
}

// Criticality badge (R4). Essential and Important both take the WARNING role and
// never danger: a marker that mislabels risk must not read as a destructive
// outcome (design Decision 7). Ordinary earns no badge at all, so an unremarkable
// list stays unmarked. The two levels carry different glyphs ("#" and "~") so
// they are told apart with no colour, and the detail pane names the level in
// words.
constexpr std::optional<render::Badge> badgeForCriticality(std::optional<core::Criticality> level) {
    if (!level) return std::nullopt;
    switch (*level) {
        case core::Criticality::Essential:
            return render::Badge{.glyph = render::Glyph::Essential, .role = Role::Warning};
        case core::Criticality::Important:
            return render::Badge{.glyph = render::Glyph::Important, .role = Role::Warning};
        case core::Criticality::Ordinary:
            return std::nullopt;
    }
    return std::nullopt;
}

// How loud a severity is. Only the Warning/Danger end is load-bearing — a
// refusal must never be quieter than whatever it shares the row with. Success
// outranks Info deliberately: Success reports what the user just did, Info is an
// ambient hotplug notice, and if both land on one row the user's own action is
// the one they are waiting on.
constexpr int severityRank(app::StatusSeverity s) {
    switch (s) {
        case app::StatusSeverity::Ok:
            return 0;
        case app::StatusSeverity::Info:
            return 1;
        case app::StatusSeverity::Success:
            return 2;
        case app::StatusSeverity::Warning:
            return 3;
        case app::StatusSeverity::Danger:
            return 4;
    }
    return 0;
}

constexpr app::StatusSeverity maxSeverity(app::StatusSeverity a, app::StatusSeverity b) {
    return severityRank(a) >= severityRank(b) ? a : b;
}

// One status row and its valence, decided together.
struct StatusRow {
    std::string text;
    std::optional<Role> role;  // nullopt = neutral (steady state or a prompt)
};

// Composes the segments a view wants on its single status row (docs/DESIGN.md
// §3.2 — one row, stable screen edge). Segments arrive in display order; empty
// ones are dropped, and the row takes the MAXIMUM severity of the segments
// actually shown.
//
// This exists because the Updates row has two sources: an in-flight install's
// progress text and the shared status message. The previous precedence showed
// the progress and dropped the message entirely — so a guard refusal published
// during an install ("not installable from here…", severity Danger) was never
// seen, because `UpdatesVM::onCompleted` only clears the progress text for
// install task ids and a refusal is not one. Composing instead of choosing keeps
// the refusal on screen, and taking the max keeps it red rather than letting the
// neutral progress segment quiet it (K2).
inline StatusRow composeStatus(
    std::initializer_list<std::pair<std::string, app::StatusSeverity>> segments) {
    StatusRow row;
    auto severity = app::StatusSeverity::Ok;
    for (const auto& [text, s] : segments) {
        if (text.empty()) continue;  // an absent source claims none of the row
        if (!row.text.empty()) row.text += " · ";
        row.text += text;
        severity = maxSeverity(severity, s);
    }
    row.role = roleForSeverity(severity);
    return row;
}

}  // namespace devmgr::tui
