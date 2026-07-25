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

}  // namespace

// The three sentences are the contract (backend-availability spec table). A
// change to one must fail here and be made deliberately in the table.
TEST(BackendWording, TableSentencesAreByteFrozen) {
    EXPECT_EQ(unavailabilityText(BackendId::Devmgrd, UnavailabilityKind::Unreachable),
              "Device service unavailable — showing read-only system state.");
    EXPECT_EQ(unavailabilityText(BackendId::Fwupd, UnavailabilityKind::Unreachable),
              "Firmware updates unavailable — the fwupd service is not responding.");
    EXPECT_EQ(unavailabilityText(BackendId::Dkms, UnavailabilityKind::Absent),
              "DKMS status unavailable — DKMS is not installed on this system.");
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
// "a service" — the user has to know which one.
TEST(BackendWording, FallbackNamesTheBackend) {
    for (const auto backend : kAllBackends) {
        const std::string text = unavailabilityText(backend, UnavailabilityKind::Unsupported);
        EXPECT_NE(text.find(devmgr::core::backendName(backend)), std::string::npos) << text;
    }
}
