#pragma once

// The user-facing unavailability sentences, in ONE place every surface's tests
// can see.
//
// Why a shared fixture header rather than a literal per test file: the GUI test
// binary links Qt, the TUI render binary links only the presentation layer
// (devmgr_tui_render deliberately links neither app nor core), so neither could
// otherwise assert against core's wording table. Each surface's test asserts its
// rendered text against these constants, and
// tests/unit/test_backend_parity.cpp asserts these constants against
// core::unavailabilityText(). Changing a sentence in the table therefore fails
// one test in one place instead of silently letting a surface drift.

namespace devmgr::tests {

// core::unavailabilityText(BackendId::Devmgrd, UnavailabilityKind::Unreachable)
constexpr const char* kDevmgrdUnreachableSentence =
    "Device service unavailable — showing read-only system state.";
// core::unavailabilityText(BackendId::Fwupd, UnavailabilityKind::Unreachable)
constexpr const char* kFwupdUnreachableSentence =
    "Firmware updates unavailable — the fwupd service is not responding.";
// core::unavailabilityText(BackendId::Dkms, UnavailabilityKind::Absent)
constexpr const char* kDkmsAbsentSentence =
    "DKMS status unavailable — DKMS is not installed on this system.";

}  // namespace devmgr::tests
