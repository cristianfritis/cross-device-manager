// K1 (task 9.1): the documented per-view colour table, asserted directly.
//
// design.md's "Per-view coloring" table is the contract these switches
// implement. Pass 1 wired them inside the `runTuiApp` closure where nothing
// could reach them, so the only evidence that a disabled device reads danger or
// an available update reads information was a screenshot — and the manual pass
// found states that were never exercised at all. They now live in
// tui/src/state_roles.hpp: pure, toolkit-free, and covered here for every enum
// value. What each role *paints* is asserted separately in the TUI render tests
// (devmgr_tui_tests), which is where FTXUI lives.
#include "tui/src/state_roles.hpp"

#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace devmgr::tui {
namespace {

// --------------------------------------------------------------------------
// Devices — every status, including the four non-green ones the manual pass
// could not confirm because the dev box had no disabled/error device.
// --------------------------------------------------------------------------

TEST(StateRoles, DeviceStatusMapsToTheDocumentedRole) {
    // Active is the resting state (nominal), not an outcome (success). Disabled is
    // deliberate, so it is muted and Error keeps danger to itself.
    EXPECT_EQ(roleForDeviceStatus(core::DeviceStatus::Active), Role::Nominal);
    EXPECT_EQ(roleForDeviceStatus(core::DeviceStatus::Disabled), Role::Muted);
    EXPECT_EQ(roleForDeviceStatus(core::DeviceStatus::Transitioning), Role::Warning);
    EXPECT_EQ(roleForDeviceStatus(core::DeviceStatus::Error), Role::Danger);
    EXPECT_EQ(roleForDeviceStatus(core::DeviceStatus::Unknown), Role::Muted);
}

// Danger means a fault and nothing else: an errored device is the only row in an
// ordinary Devices list that can be red.
TEST(StateRoles, DangerIsReservedForFaults) {
    for (const auto status : {core::DeviceStatus::Active, core::DeviceStatus::Disabled,
                              core::DeviceStatus::Transitioning, core::DeviceStatus::Unknown}) {
        EXPECT_NE(roleForDeviceStatus(status), Role::Danger) << core::to_string(status);
    }
    EXPECT_EQ(roleForDeviceStatus(core::DeviceStatus::Error), Role::Danger);
}

// Disabled and Unknown deliberately share the muted role (design D5). Sharing a
// role must not cost the distinction, so the glyph and the state word carry it in
// every colour mode.
TEST(StateRoles, StatesSharingMutedStayDistinct) {
    EXPECT_EQ(roleForDeviceStatus(core::DeviceStatus::Disabled),
              roleForDeviceStatus(core::DeviceStatus::Unknown));
    EXPECT_NE(glyphForDeviceStatus(core::DeviceStatus::Disabled),
              glyphForDeviceStatus(core::DeviceStatus::Unknown));
    EXPECT_STRNE(core::to_string(core::DeviceStatus::Disabled),
                 core::to_string(core::DeviceStatus::Unknown));
}

// Every status also carries a glyph, so the state survives with no colour (§10).
TEST(StateRoles, EveryDeviceStatusPairsItsRoleWithAGlyph) {
    for (const auto status : {core::DeviceStatus::Active, core::DeviceStatus::Disabled,
                              core::DeviceStatus::Transitioning, core::DeviceStatus::Error,
                              core::DeviceStatus::Unknown}) {
        EXPECT_TRUE(roleForDeviceStatus(status).has_value()) << static_cast<int>(status);
        EXPECT_TRUE(glyphForDeviceStatus(status).has_value()) << static_cast<int>(status);
    }
    // Disabled and Error stay distinguishable without colour: disabled is a
    // deliberate state ("-"), an error is a fault ("!").
    EXPECT_NE(glyphForDeviceStatus(core::DeviceStatus::Disabled),
              glyphForDeviceStatus(core::DeviceStatus::Error));
}

TEST(StateRoles, NonDeviceRowsCarryNeitherRoleNorGlyph) {
    EXPECT_FALSE(roleForDeviceStatus(std::nullopt).has_value());
    EXPECT_FALSE(glyphForDeviceStatus(std::nullopt).has_value());
    EXPECT_FALSE(roleForSignature(std::nullopt).has_value());
    EXPECT_FALSE(roleForUpdateState(std::nullopt).has_value());
    EXPECT_FALSE(
        roleForSnapshotRow(std::nullopt, /*isHead=*/true, /*isLastGood=*/true).has_value());
}

// --------------------------------------------------------------------------
// Modules, Updates, Snapshots.
// --------------------------------------------------------------------------

TEST(StateRoles, ModuleSignatureMapsToTheDocumentedRole) {
    EXPECT_EQ(roleForSignature(app::ModuleSignature::Signed), Role::Nominal);
    EXPECT_EQ(roleForSignature(app::ModuleSignature::Unsigned), Role::Danger);
    EXPECT_EQ(roleForSignature(app::ModuleSignature::Undetermined), Role::Muted);
}

TEST(StateRoles, UpdateStateMapsToTheDocumentedRole) {
    EXPECT_EQ(roleForUpdateState(app::UpdateRowState::Available), Role::Info);
    EXPECT_EQ(roleForUpdateState(app::UpdateRowState::UpToDate), Role::Nominal);
    EXPECT_EQ(roleForUpdateState(app::UpdateRowState::Error), Role::Danger);
}

TEST(StateRoles, SnapshotHealthMapsToTheDocumentedRole) {
    // A healthy, unmarked snapshot takes the same resting role as the other three
    // views. Nullopt is now reserved for structural rows only, so "healthy" and
    // "not a row" can no longer be confused.
    EXPECT_EQ(roleForSnapshotRow(core::SnapshotHealth::Ok, false, false), Role::Nominal);
    EXPECT_EQ(roleForSnapshotRow(core::SnapshotHealth::Corrupt, false, false), Role::Danger);
    EXPECT_EQ(roleForSnapshotRow(core::SnapshotHealth::Unsupported, false, false), Role::Warning);
    EXPECT_EQ(roleForSnapshotRow(core::SnapshotHealth::Ok, /*isHead=*/true, false), Role::Accent);
    EXPECT_EQ(roleForSnapshotRow(core::SnapshotHealth::Ok, false, /*isLastGood=*/true),
              Role::Accent);
}

// Health outranks the HEAD/last-good accent: a corrupt HEAD must read as danger,
// not as the marker colour, or the list would advertise a broken snapshot as the
// one to restore to.
TEST(StateRoles, HealthOutranksTheHeadAccent) {
    EXPECT_EQ(roleForSnapshotRow(core::SnapshotHealth::Corrupt, /*isHead=*/true, true),
              Role::Danger);
    EXPECT_EQ(roleForSnapshotRow(core::SnapshotHealth::Unsupported, /*isHead=*/true, true),
              Role::Warning);
}

// Success is a completed operation, never a condition. No row state in any of
// the four collection views may claim it: with nothing in flight and nothing
// recently finished, the loud green must be absent from the screen entirely, and
// the only way to reach it is a status-line severity.
TEST(StateRoles, NoRowStateEmitsSuccess) {
    for (const auto status : {core::DeviceStatus::Active, core::DeviceStatus::Disabled,
                              core::DeviceStatus::Transitioning, core::DeviceStatus::Error,
                              core::DeviceStatus::Unknown}) {
        EXPECT_NE(roleForDeviceStatus(status), Role::Success) << core::to_string(status);
    }
    for (const auto sig : {app::ModuleSignature::Signed, app::ModuleSignature::Unsigned,
                           app::ModuleSignature::Undetermined}) {
        EXPECT_NE(roleForSignature(sig), Role::Success) << static_cast<int>(sig);
    }
    for (const auto state : {app::UpdateRowState::Available, app::UpdateRowState::UpToDate,
                             app::UpdateRowState::Error}) {
        EXPECT_NE(roleForUpdateState(state), Role::Success) << static_cast<int>(state);
    }
    for (const auto health : {core::SnapshotHealth::Ok, core::SnapshotHealth::Corrupt,
                              core::SnapshotHealth::Unsupported}) {
        for (const bool isHead : {false, true}) {
            for (const bool isLastGood : {false, true}) {
                EXPECT_NE(roleForSnapshotRow(health, isHead, isLastGood), Role::Success)
                    << static_cast<int>(health);
            }
        }
    }
    // Still reachable where it belongs: an operation the user just performed.
    EXPECT_EQ(roleForSeverity(app::StatusSeverity::Success), Role::Success);
}

// --------------------------------------------------------------------------
// Status line and the Modules security banner.
// --------------------------------------------------------------------------

TEST(StateRoles, StatusSeverityMapsToTheDocumentedRole) {
    EXPECT_FALSE(roleForSeverity(app::StatusSeverity::Ok).has_value());  // steady state, no colour
    EXPECT_EQ(roleForSeverity(app::StatusSeverity::Success), Role::Success);
    EXPECT_EQ(roleForSeverity(app::StatusSeverity::Warning), Role::Warning);
    EXPECT_EQ(roleForSeverity(app::StatusSeverity::Danger), Role::Danger);
    EXPECT_EQ(roleForSeverity(app::StatusSeverity::Info), Role::Info);
}

// The Modules banner's valence used to be recovered here by searching the
// rendered string for "will be rejected". It now arrives from the ViewModel
// with the text (app::ModulesVM::bannerLine), so there is no string-matching
// role function left to test: the mapping is roleForSeverity above, and the
// state→severity decision is pinned in tests/unit/test_modules_vm.cpp
// (BannerCarriesItsOwnSeverity / BannerSeverityTracksStateNotWording).

// --------------------------------------------------------------------------
// K2 (task 9.2): composite status rows take the maximum severity, so a refusal
// is never quieted — or dropped — by whatever else wants the row.
// --------------------------------------------------------------------------

TEST(StatusComposition, MaxSeverityEscalatesTowardsDanger) {
    using app::StatusSeverity;
    EXPECT_EQ(maxSeverity(StatusSeverity::Ok, StatusSeverity::Danger), StatusSeverity::Danger);
    EXPECT_EQ(maxSeverity(StatusSeverity::Danger, StatusSeverity::Ok), StatusSeverity::Danger);
    EXPECT_EQ(maxSeverity(StatusSeverity::Warning, StatusSeverity::Danger), StatusSeverity::Danger);
    EXPECT_EQ(maxSeverity(StatusSeverity::Success, StatusSeverity::Warning),
              StatusSeverity::Warning);
    // The user's own outcome outranks an ambient hotplug notice.
    EXPECT_EQ(maxSeverity(StatusSeverity::Info, StatusSeverity::Success), StatusSeverity::Success);
    EXPECT_EQ(maxSeverity(StatusSeverity::Ok, StatusSeverity::Ok), StatusSeverity::Ok);
}

TEST(StatusComposition, EmptyRowIsNeutral) {
    const StatusRow row = composeStatus({{"", app::StatusSeverity::Danger}});
    EXPECT_EQ(row.text, "");
    EXPECT_FALSE(row.role.has_value());  // an absent source claims none of the row
}

TEST(StatusComposition, SingleSegmentKeepsItsOwnValence) {
    const StatusRow row = composeStatus({{"Enabled eth0", app::StatusSeverity::Success}});
    EXPECT_EQ(row.text, "Enabled eth0");
    EXPECT_EQ(row.role, Role::Success);
}

// The Updates row: an install's progress text is neutral, but a guard refusal
// raised during that install must keep BOTH its words and its danger colour.
// Before this, the progress text shadowed the message entirely and the refusal
// was never displayed.
TEST(StatusComposition, ProgressAndRefusalShareTheRowAtTheHigherSeverity) {
    const StatusRow row =
        composeStatus({{"installing Dock fw — 40%", app::StatusSeverity::Ok},
                       {"not installable from here (status-only)", app::StatusSeverity::Danger}});
    EXPECT_EQ(row.text, "installing Dock fw — 40% · not installable from here (status-only)");
    EXPECT_EQ(row.role, Role::Danger);
}

TEST(StatusComposition, AbsentSegmentsAreDroppedWithoutASeparator) {
    EXPECT_EQ(composeStatus(
                  {{"", app::StatusSeverity::Ok}, {"device removed", app::StatusSeverity::Info}})
                  .text,
              "device removed");
    EXPECT_EQ(
        composeStatus({{"installing…", app::StatusSeverity::Ok}, {"", app::StatusSeverity::Ok}})
            .text,
        "installing…");
}

}  // namespace
}  // namespace devmgr::tui
