#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "devmgr/core/backend_wording.hpp"
#include "devmgr/core/result.hpp"

using devmgr::core::BackendId;
using devmgr::core::Error;
using devmgr::core::kAllBackends;
using devmgr::core::kAllUnavailabilityKinds;
using devmgr::core::kindFor;
using devmgr::core::UnavailabilityKind;
using devmgr::core::unavailabilityText;

namespace {

// The machine-shaped fragments docs/DESIGN.md §6 forbids as a user-visible
// explanation. "/" covers filesystem paths, which is why the DKMS sentence
// cannot name /var/lib/dkms.
constexpr std::array<std::string_view, 4> kLeakMarkers{"org.freedesktop", "DBus.Error", "errno",
                                                       "/"};

// Verbs an `unsupported` sentence may not use. The kind means the running
// platform has no implementation — a fact of the build and the machine — so any
// instruction is a promise the user cannot act on.
constexpr std::array<std::string_view, 8> kActionWords{
    "install", "start", "enable", "retry", "configure", "try again", "run ", "restart"};

// Named mechanisms an `unsupported` sentence may not contain, so the same
// sentence stays true on any platform that later lacks the same backend.
constexpr std::array<std::string_view, 12> kMechanismNames{
    "dkms",  "fwupd", "devmgrd",  "systemd", "polkit",  "d-bus",
    "sysfs", "udev",  "modprobe", "linux",   "windows", "registry"};

std::string lowered(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

// The table sentences are the contract (backend-availability spec table). A
// change to one must fail here and be made deliberately in the table.
TEST(BackendWording, TableSentencesAreByteFrozen) {
    EXPECT_EQ(unavailabilityText(BackendId::Devmgrd, UnavailabilityKind::Unreachable),
              "Device service unavailable — showing read-only system state.");
    EXPECT_EQ(unavailabilityText(BackendId::Fwupd, UnavailabilityKind::Unreachable),
              "Firmware updates unavailable — the fwupd service is not responding.");
    EXPECT_EQ(unavailabilityText(BackendId::Dkms, UnavailabilityKind::Absent),
              "DKMS status unavailable — DKMS is not installed on this system.");
    EXPECT_EQ(unavailabilityText(BackendId::Devmgrd, UnavailabilityKind::Unsupported),
              "Device management is not available on this platform — showing read-only system "
              "state.");
    EXPECT_EQ(unavailabilityText(BackendId::Fwupd, UnavailabilityKind::Unsupported),
              "Firmware updates are not available on this platform.");
    EXPECT_EQ(unavailabilityText(BackendId::Dkms, UnavailabilityKind::Unsupported),
              "Driver module status is not available on this platform.");
    EXPECT_EQ(unavailabilityText(BackendId::Snapshots, UnavailabilityKind::Unsupported),
              "Snapshots are not available on this platform.");
}

TEST(BackendWording, KindForMapsTheSpecifiedCodes) {
    EXPECT_EQ(kindFor(Error::Code::NotFound), UnavailabilityKind::Absent);
    EXPECT_EQ(kindFor(Error::Code::Io), UnavailabilityKind::Unreachable);
    EXPECT_EQ(kindFor(Error::Code::Permission), UnavailabilityKind::NotPermitted);
    EXPECT_EQ(kindFor(Error::Code::Unsupported), UnavailabilityKind::Unsupported);
}

// kindFor is total: every code yields a kind, so a new Error::Code can never
// leave a backend with no presentable sentence.
TEST(BackendWording, KindForIsTotalOverEveryCode) {
    for (const auto code : {Error::Code::Permission, Error::Code::NotFound, Error::Code::Busy,
                            Error::Code::Io, Error::Code::Network, Error::Code::Unsupported,
                            Error::Code::Conflict, Error::Code::InvalidArgs}) {
        const auto kind = kindFor(code);
        EXPECT_FALSE(unavailabilityText(BackendId::Fwupd, kind).empty())
            << "code " << static_cast<int>(code) << " produced no sentence";
    }
}

// Every pair, not only the three listed ones: the fallback must also be a calm
// sentence with nothing machine-shaped in it.
TEST(BackendWording, EveryPairYieldsACleanNonEmptySentence) {
    for (const auto backend : kAllBackends) {
        for (const auto kind : kAllUnavailabilityKinds) {
            const std::string text = unavailabilityText(backend, kind);
            const std::string where = "backend " + std::to_string(static_cast<int>(backend)) +
                                      " kind " + std::to_string(static_cast<int>(kind));
            EXPECT_FALSE(text.empty()) << where << " returned an empty sentence";
            for (const auto marker : kLeakMarkers) {
                EXPECT_EQ(text.find(marker), std::string::npos)
                    << where << " leaked \"" << marker << "\" in: " << text;
            }
        }
    }
}

// The fallback names the backend rather than saying something generic about
// "a service" — the user has to know which one. NotPermitted is the kind no
// backend has a specific row for, so it is what still reaches the fallback now
// that every backend carries an unsupported sentence.
TEST(BackendWording, FallbackNamesTheBackend) {
    for (const auto backend : kAllBackends) {
        const std::string text = unavailabilityText(backend, UnavailabilityKind::NotPermitted);
        EXPECT_NE(text.find(devmgr::core::backendName(backend)), std::string::npos) << text;
    }
}

// Nothing the user does changes an unsupported platform, so the sentence must
// not ask them to do anything (backend-availability: "Unsupported wording
// promises nothing").
TEST(BackendWording, UnsupportedSentencesInviteNoAction) {
    for (const auto backend : kAllBackends) {
        const std::string text =
            lowered(unavailabilityText(backend, UnavailabilityKind::Unsupported));
        for (const auto word : kActionWords) {
            EXPECT_EQ(text.find(word), std::string::npos)
                << "backend " << static_cast<int>(backend) << " unsupported sentence instructs \""
                << word << "\": " << text;
        }
    }
}

// A sentence naming the platform's own mechanism stops being true the moment
// another platform lacks the same backend for a different reason.
TEST(BackendWording, UnsupportedSentencesNameNoPlatformMechanism) {
    for (const auto backend : kAllBackends) {
        const std::string text =
            lowered(unavailabilityText(backend, UnavailabilityKind::Unsupported));
        for (const auto name : kMechanismNames) {
            EXPECT_EQ(text.find(name), std::string::npos)
                << "backend " << static_cast<int>(backend) << " unsupported sentence names \""
                << name << "\": " << text;
        }
    }
}

// "DKMS is not installed" and "driver module status is not available on this
// platform" are different facts with different remedies (one has a remedy at
// all), so they must not collapse to one sentence.
TEST(BackendWording, AbsentAndUnsupportedReadDifferently) {
    for (const auto backend : kAllBackends) {
        EXPECT_NE(unavailabilityText(backend, UnavailabilityKind::Absent),
                  unavailabilityText(backend, UnavailabilityKind::Unsupported))
            << "backend " << static_cast<int>(backend)
            << " gives the same sentence for absent and unsupported";
    }
    // Only the absent sentence may refer to installation — the asymmetry the
    // rule above exists to preserve.
    EXPECT_NE(
        lowered(unavailabilityText(BackendId::Dkms, UnavailabilityKind::Absent)).find("install"),
        std::string::npos);
}
