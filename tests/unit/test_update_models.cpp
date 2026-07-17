#include <gtest/gtest.h>

#include "devmgr/core/update_models.hpp"
#include "devmgr/pal/interfaces.hpp"
#include "fakes/fake_update_provider.hpp"

using namespace devmgr;

TEST(UpdateModels, ReleaseRefEqualityIsRemotePlusChecksum) {
    core::ReleaseInfo a{.version = "1.2.3",
                        .summary = "",
                        .remoteId = "lvfs",
                        .checksum = "abc",
                        .locations = {},
                        .localCab = false,
                        .sizeBytes = 0,
                        .isUpgrade = true,
                        .installDurationSec = {}};
    core::ReleaseInfo b = a;
    b.version = "9.9.9";  // version differs — identity must NOT (spec §2)
    EXPECT_EQ(a.ref(), b.ref());
}

TEST(UpdateModels, CapsBitOps) {
    using pal::UpdateProviderCaps;
    const auto both = UpdateProviderCaps::Query | UpdateProviderCaps::Install;
    EXPECT_TRUE(pal::hasCap(both, UpdateProviderCaps::Install));
    EXPECT_FALSE(pal::hasCap(UpdateProviderCaps::Query, UpdateProviderCaps::Install));
}

TEST(UpdateModels, FakeProviderRoundTrip) {
    tests::FakeUpdateProvider fake;
    fake.enumerateResult_ = std::vector<core::UpdateCandidate>{
        {.providerId = "fake",
         .id = "dev1",
         .displayName = "Dev",
         .currentVersion = "1",
         .candidateVersion = "2",
         .facts = {.updatable = true, .supported = true, .needsRebootAfterUpdate = false},
         .releases = {},
         .details = {}}};
    auto r = fake.enumerate();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 1U);
    EXPECT_EQ(fake.enumerateCalls_.load(), 1);
}
