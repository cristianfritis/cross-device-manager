// Cross-surface parity for backend unavailability (backend-availability spec:
// "Both surfaces show the same sentence for the same backend").
//
// The claim under test is that there is exactly ONE source of these words and
// both frontends read it: the GUI puts notes()[i].text in its banner and the TUI
// hands the same string to its view, so "the two surfaces agree" is checkable
// rather than aspirational.
//
// The chain has three links, and this file pins the one the other two hang off:
//
//   core::unavailabilityText()  ==  tests/fixtures/backend_sentences.hpp   (here)
//   GUI rendered banner         contains that constant  (gui/tests/test_main_window.cpp)
//   TUI rendered banner         contains that constant  (tui/tests/test_updates_view_render.cpp)
//
// The two surface binaries cannot link core and app respectively, which is why
// the shared constant exists at all; with this file in place, editing a sentence
// in the table without editing the fixture fails here first.
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "devmgr/app/backend_status_vm.hpp"
#include "devmgr/core/backend_wording.hpp"
#include "devmgr/core/result.hpp"
#include "fixtures/backend_sentences.hpp"

using namespace devmgr;
using devmgr::core::BackendId;
using devmgr::core::Error;
using devmgr::core::UnavailabilityKind;

namespace {

struct Case {
    BackendId backend;
    Error::Code code;
    UnavailabilityKind kind;
    const char* sentence;
};

// Every backend the change covers (task 8.2), each with the error code its real
// producer emits: devmgrd's ServiceUnknown mapping, fwupd's transport failure,
// and the dkms provider's missing root.
const std::vector<Case>& cases() {
    static const std::vector<Case> kCases{
        {BackendId::Devmgrd, Error::Code::Io, UnavailabilityKind::Unreachable,
         tests::kDevmgrdUnreachableSentence},
        {BackendId::Fwupd, Error::Code::Io, UnavailabilityKind::Unreachable,
         tests::kFwupdUnreachableSentence},
        {BackendId::Dkms, Error::Code::NotFound, UnavailabilityKind::Absent,
         tests::kDkmsAbsentSentence},
    };
    return kCases;
}

}  // namespace

// Link 1: the surfaces' shared expectation IS the core table, byte for byte.
TEST(BackendParity, SurfaceFixtureMatchesTheCoreTable) {
    for (const auto& c : cases()) {
        EXPECT_EQ(core::unavailabilityText(c.backend, c.kind), c.sentence);
        EXPECT_EQ(core::kindFor(c.code), c.kind);
    }
}

// Link 2: what the accessor hands a surface is that same string — no VM-level
// rewording between the table and the frontend.
TEST(BackendParity, AccessorTextIsTheTableTextForEveryBackend) {
    for (const auto& c : cases()) {
        app::BackendStatusVM vm;
        vm.observe(c.backend,
                   Error{.code = c.code, .message = "raw detail, surfaces never see me"});
        const auto note = vm.noteFor(c.backend);
        ASSERT_TRUE(note.has_value());
        EXPECT_EQ(note->text, c.sentence);
        EXPECT_EQ(note->kind, c.kind);
        // Both surfaces render note.text and disclose note.diagnostic; neither is
        // ever handed a combined string it could accidentally print whole.
        EXPECT_EQ(note->diagnostic, "raw detail, surfaces never see me");
        EXPECT_EQ(note->text.find(note->diagnostic), std::string::npos);
    }
}

// Link 3: the revealed detail is shared too — the GUI's "Details" region and the
// TUI's Diagnostics region render these same lines.
TEST(BackendParity, DiagnosticLinesAreSharedAndNameTheirBackend) {
    app::BackendStatusVM vm;
    for (const auto& c : cases())
        vm.observe(c.backend,
                   Error{.code = c.code,
                         .message = std::string("raw:") + core::backendName(c.backend).data()});
    const auto lines = app::diagnosticLines(vm.notes());
    ASSERT_EQ(lines.size(), cases().size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        // Named, so a two-backend outage is readable, and the raw text verbatim.
        EXPECT_EQ(lines[i].find(core::backendName(cases()[i].backend)), 0U);
        EXPECT_NE(lines[i].find("raw:"), std::string::npos);
    }
}

// A degraded backend never renders danger on either surface (design D3), and the
// sentences carry no machine-shaped token that would betray the table's purpose.
TEST(BackendParity, EverySentenceIsCalmAndMachineFree) {
    for (const auto backend : core::kAllBackends) {
        for (const auto kind : core::kAllUnavailabilityKinds) {
            const std::string text = core::unavailabilityText(backend, kind);
            EXPECT_FALSE(text.empty());
            for (const char* token : {"org.freedesktop", "DBus.Error", "errno", "/"})
                EXPECT_EQ(text.find(token), std::string::npos) << text;
            EXPECT_NE(app::noteRole(kind, /*blocksAttemptedVerb=*/false),
                      app::StatusSeverity::Danger);
            EXPECT_NE(app::noteRole(kind, /*blocksAttemptedVerb=*/true),
                      app::StatusSeverity::Danger);
        }
    }
}
