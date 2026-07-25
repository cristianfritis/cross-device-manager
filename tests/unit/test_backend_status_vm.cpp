#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include "devmgr/app/backend_status_vm.hpp"
#include "devmgr/core/backend_wording.hpp"
#include "devmgr/core/result.hpp"

using devmgr::app::BackendNote;
using devmgr::app::BackendStatusVM;
using devmgr::app::noteRole;
using devmgr::app::StatusSeverity;
using devmgr::core::BackendId;
using devmgr::core::Error;
using devmgr::core::kAllBackends;
using devmgr::core::kAllUnavailabilityKinds;
using devmgr::core::UnavailabilityKind;
using devmgr::core::unavailabilityText;

namespace {

// The fwupd message the beta user actually saw: fwupd::mapError composes
// "<dbus-name>: <dbus-message>", so this is the exact shape that must stay out
// of every presented sentence.
constexpr const char* kFwupdRaw =
    "org.freedesktop.DBus.Error.ServiceUnknown: The name org.freedesktop.fwupd was not provided by "
    "any .service files";
// Retained verbatim as a diagnostic (dbus_contract.hpp:65) — four test sites and
// the CLI hint matcher depend on this string, so it must not be repurposed as
// presentation text.
constexpr const char* kDevmgrdRaw = "helper devmgrd is not available";
constexpr const char* kDkmsRaw = "DKMS root not found: /var/lib/dkms";

Error err(Error::Code code, const char* message) {
    return Error{code, message};
}

// "text contains no substring of its own diagnostic" read usefully: taken
// literally, every single character is a substring, so the check targets the
// whole diagnostic plus its machine-shaped tokens (anything carrying '.', '/',
// ':' or '=' — D-Bus names, paths, errno spellings). Plain English words are
// excluded deliberately: "available" is a legitimate substring of
// "unavailable", and banning it would test spelling rather than leakage.
std::vector<std::string> machineTokens(const std::string& diagnostic) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < diagnostic.size()) {
        const std::size_t start = diagnostic.find_first_not_of(" \t", i);
        if (start == std::string::npos) break;
        std::size_t end = diagnostic.find_first_of(" \t", start);
        if (end == std::string::npos) end = diagnostic.size();
        const std::string token = diagnostic.substr(start, end - start);
        if (token.size() >= 5 && token.find_first_of("./:=") != std::string::npos)
            out.push_back(token);
        i = end;
    }
    return out;
}

// Captures real spdlog output so the "logged once per transition, not per poll"
// rule is asserted against the actual logger and level, not a test seam.
class LogCapture {
   public:
    LogCapture() : previous_(spdlog::default_logger()) {
        sink_ = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(kCapacity);
        auto logger = std::make_shared<spdlog::logger>("backend-status-test", sink_);
        logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(logger);
    }
    ~LogCapture() { spdlog::set_default_logger(previous_); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    std::vector<std::string> lines() const { return sink_->last_formatted(); }
    std::size_t countContaining(std::string_view needle) const {
        std::size_t n = 0;
        for (const auto& line : lines())
            if (line.find(needle) != std::string::npos) ++n;
        return n;
    }
    std::size_t countWarnings() const {
        std::size_t n = 0;
        for (const auto& entry : sink_->last_raw())
            if (entry.level == spdlog::level::warn) ++n;
        return n;
    }

   private:
    static constexpr std::size_t kCapacity = 64;
    std::shared_ptr<spdlog::logger> previous_;
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink_;
};

}  // namespace

TEST(BackendStatusVM, NoNotesWhileEveryBackendIsHealthy) {
    BackendStatusVM vm;
    for (const auto backend : kAllBackends) vm.observe(backend, std::nullopt);
    EXPECT_TRUE(vm.notes().empty());
    EXPECT_FALSE(vm.noteFor(BackendId::Fwupd).has_value());
}

TEST(BackendStatusVM, DegradedBackendYieldsExactlyOneNote) {
    BackendStatusVM vm;
    vm.observe(BackendId::Devmgrd, std::nullopt);
    vm.observe(BackendId::Fwupd, err(Error::Code::Io, kFwupdRaw));
    vm.observe(BackendId::Dkms, std::nullopt);

    const auto notes = vm.notes();
    ASSERT_EQ(notes.size(), 1U);
    EXPECT_EQ(notes[0].backend, BackendId::Fwupd);
    EXPECT_EQ(notes[0].kind, UnavailabilityKind::Unreachable);
}

TEST(BackendStatusVM, RepeatedPollsDoNotMultiplyNotes) {
    BackendStatusVM vm;
    for (int i = 0; i < 10; ++i) vm.observe(BackendId::Fwupd, err(Error::Code::Io, kFwupdRaw));
    EXPECT_EQ(vm.notes().size(), 1U);
}

TEST(BackendStatusVM, RecoveryClearsTheNote) {
    BackendStatusVM vm;
    vm.observe(BackendId::Devmgrd, err(Error::Code::Io, kDevmgrdRaw));
    ASSERT_EQ(vm.notes().size(), 1U);
    vm.observe(BackendId::Devmgrd, std::nullopt);
    EXPECT_TRUE(vm.notes().empty());
}

TEST(BackendStatusVM, TextIsTheCoreTableEntry) {
    BackendStatusVM vm;
    vm.observe(BackendId::Devmgrd, err(Error::Code::Io, kDevmgrdRaw));
    vm.observe(BackendId::Fwupd, err(Error::Code::Io, kFwupdRaw));
    vm.observe(BackendId::Dkms, err(Error::Code::NotFound, kDkmsRaw));

    const auto notes = vm.notes();
    ASSERT_EQ(notes.size(), 3U);
    for (const auto& note : notes)
        EXPECT_EQ(note.text, unavailabilityText(note.backend, note.kind));
}

TEST(BackendStatusVM, NotesAreInBackendIdOrder) {
    BackendStatusVM vm;
    vm.observe(BackendId::Dkms, err(Error::Code::NotFound, kDkmsRaw));
    vm.observe(BackendId::Fwupd, err(Error::Code::Io, kFwupdRaw));
    vm.observe(BackendId::Devmgrd, err(Error::Code::Io, kDevmgrdRaw));

    const auto notes = vm.notes();
    ASSERT_EQ(notes.size(), 3U);
    EXPECT_EQ(notes[0].backend, BackendId::Devmgrd);
    EXPECT_EQ(notes[1].backend, BackendId::Fwupd);
    EXPECT_EQ(notes[2].backend, BackendId::Dkms);
}

TEST(BackendStatusVM, DiagnosticCarriesTheRawMessageVerbatim) {
    BackendStatusVM vm;
    vm.observe(BackendId::Fwupd, err(Error::Code::Io, kFwupdRaw));
    vm.observe(BackendId::Dkms, err(Error::Code::NotFound, kDkmsRaw));

    const auto fwupd = vm.noteFor(BackendId::Fwupd);
    ASSERT_TRUE(fwupd.has_value());
    EXPECT_EQ(fwupd->diagnostic, kFwupdRaw);

    const auto dkms = vm.noteFor(BackendId::Dkms);
    ASSERT_TRUE(dkms.has_value());
    EXPECT_EQ(dkms->diagnostic, kDkmsRaw);
}

TEST(BackendStatusVM, TextNeverCarriesItsOwnDiagnostic) {
    BackendStatusVM vm;
    vm.observe(BackendId::Devmgrd, err(Error::Code::Io, kDevmgrdRaw));
    vm.observe(BackendId::Fwupd, err(Error::Code::Io, kFwupdRaw));
    vm.observe(BackendId::Dkms, err(Error::Code::NotFound, kDkmsRaw));

    for (const auto& note : vm.notes()) {
        EXPECT_EQ(note.text.find(note.diagnostic), std::string::npos) << note.text;
        for (const auto& token : machineTokens(note.diagnostic))
            EXPECT_EQ(note.text.find(token), std::string::npos)
                << "leaked \"" << token << "\" in: " << note.text;
    }
}

// Design D3: Danger is not in the range of the mapping, for any input.
TEST(BackendStatusVM, DangerIsUnreachableForEveryKindAndContext) {
    for (const auto kind : kAllUnavailabilityKinds) {
        for (const bool blocked : {false, true}) {
            const auto role = noteRole(kind, blocked);
            EXPECT_NE(role, StatusSeverity::Danger)
                << "kind " << static_cast<int>(kind) << " blocked=" << blocked;
            EXPECT_TRUE(role == StatusSeverity::Info || role == StatusSeverity::Warning)
                << "kind " << static_cast<int>(kind) << " blocked=" << blocked;
        }
    }
}

TEST(BackendStatusVM, PresentButNotServingWarns) {
    EXPECT_EQ(noteRole(UnavailabilityKind::Unreachable, false), StatusSeverity::Warning);
    EXPECT_EQ(noteRole(UnavailabilityKind::NotPermitted, false), StatusSeverity::Warning);
}

TEST(BackendStatusVM, AbsentOptionalServiceStaysCalm) {
    EXPECT_EQ(noteRole(UnavailabilityKind::Absent, false), StatusSeverity::Info);
    EXPECT_EQ(noteRole(UnavailabilityKind::Unsupported, false), StatusSeverity::Info);
}

TEST(BackendStatusVM, BlockedVerbEscalatesAnOtherwiseCalmNote) {
    EXPECT_EQ(noteRole(UnavailabilityKind::Absent, true), StatusSeverity::Warning);

    BackendStatusVM vm;
    vm.observe(BackendId::Dkms, err(Error::Code::NotFound, kDkmsRaw));
    const auto calm = vm.noteFor(BackendId::Dkms);
    ASSERT_TRUE(calm.has_value());
    EXPECT_EQ(calm->role, StatusSeverity::Info);

    const auto blocked = vm.noteFor(BackendId::Dkms, /*blocksAttemptedVerb=*/true);
    ASSERT_TRUE(blocked.has_value());
    EXPECT_EQ(blocked->role, StatusSeverity::Warning);
    // Same sentence, raised role — the verb does not get its own wording.
    EXPECT_EQ(blocked->text, calm->text);
}

// Channel separation: the persistent note's role is a pure function of kind, so
// asking for the blocked-verb reason (the transient §5.3 channel) cannot leave
// the standing note escalated. No banner pulse on a verb attempt.
TEST(BackendStatusVM, PersistentNoteNeverEscalatesFromAVerbAttempt) {
    BackendStatusVM vm;
    vm.observe(BackendId::Dkms, err(Error::Code::NotFound, kDkmsRaw));

    ASSERT_EQ(vm.notes().size(), 1U);
    EXPECT_EQ(vm.notes()[0].role, StatusSeverity::Info);

    ASSERT_TRUE(vm.noteFor(BackendId::Dkms, /*blocksAttemptedVerb=*/true).has_value());
    EXPECT_EQ(vm.noteFor(BackendId::Dkms, true)->role, StatusSeverity::Warning);

    // ...and the standing note is exactly as calm as before the attempt.
    ASSERT_EQ(vm.notes().size(), 1U);
    EXPECT_EQ(vm.notes()[0].role, StatusSeverity::Info);
    // Re-observing the same state does not escalate it either.
    vm.observe(BackendId::Dkms, err(Error::Code::NotFound, kDkmsRaw));
    EXPECT_EQ(vm.notes()[0].role, StatusSeverity::Info);
}

TEST(BackendStatusVM, RolesOnNotesMatchTheMapping) {
    BackendStatusVM vm;
    vm.observe(BackendId::Devmgrd, err(Error::Code::Io, kDevmgrdRaw));
    vm.observe(BackendId::Dkms, err(Error::Code::NotFound, kDkmsRaw));

    const auto notes = vm.notes();
    ASSERT_EQ(notes.size(), 2U);
    for (const auto& note : notes)
        EXPECT_EQ(note.role, noteRole(note.kind, /*blocksAttemptedVerb=*/false));
}

TEST(BackendStatusVM, DiagnosticIsLoggedOnceAtWarnLevelNotPerPoll) {
    LogCapture log;
    BackendStatusVM vm;
    for (int i = 0; i < 12; ++i) vm.observe(BackendId::Fwupd, err(Error::Code::Io, kFwupdRaw));

    EXPECT_EQ(log.countContaining(kFwupdRaw), 1U);
    EXPECT_EQ(log.countWarnings(), 1U);
}

TEST(BackendStatusVM, EachTransitionLogsAgain) {
    LogCapture log;
    BackendStatusVM vm;
    vm.observe(BackendId::Fwupd, err(Error::Code::Io, kFwupdRaw));
    vm.observe(BackendId::Fwupd, err(Error::Code::Io, kFwupdRaw));
    // Same backend, different kind — a new (backend, kind) transition.
    vm.observe(BackendId::Fwupd, err(Error::Code::Permission, "not permitted"));
    // Recovery, then the same outage again: logged once more, not suppressed
    // forever by the first occurrence.
    vm.observe(BackendId::Fwupd, std::nullopt);
    vm.observe(BackendId::Fwupd, err(Error::Code::Io, kFwupdRaw));

    EXPECT_EQ(log.countContaining(kFwupdRaw), 2U);
    EXPECT_EQ(log.countWarnings(), 3U);
}

TEST(BackendStatusVM, LogNamesTheBackendWithoutRewordingThePresentedSentence) {
    LogCapture log;
    BackendStatusVM vm;
    vm.observe(BackendId::Devmgrd, err(Error::Code::Io, kDevmgrdRaw));

    const auto lines = log.lines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_NE(lines[0].find("devmgrd"), std::string::npos) << lines[0];
    EXPECT_NE(lines[0].find(kDevmgrdRaw), std::string::npos) << lines[0];
}
