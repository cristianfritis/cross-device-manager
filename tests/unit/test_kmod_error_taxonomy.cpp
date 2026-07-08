#include <gtest/gtest.h>

#include <cerrno>

#include "devmgr/platform/linux/kmod_error_taxonomy.hpp"

using devmgr::core::Error;
namespace pl = devmgr::platform_linux;

TEST(KmodErrorTaxonomy, SignatureRejectionNamesLockdownMode) {
    const auto e = pl::describeLoadFailure(EKEYREJECTED, "nvidia", {}, "integrity");
    EXPECT_EQ(e.code, Error::Code::Permission);
    EXPECT_EQ(e.message,
              "module 'nvidia' rejected: unsigned module (Secure Boot / lockdown: integrity)");
}

TEST(KmodErrorTaxonomy, UnloadedDependencyIsNamedAsCulprit) {
    const auto e = pl::describeLoadFailure(
        EKEYREJECTED, "parent", {{"dep_ok", true}, {"dep_bad", false}}, "none");
    EXPECT_NE(e.message.find("dependency 'dep_bad'"), std::string::npos) << e.message;
}

TEST(KmodErrorTaxonomy, EnoentIsNotFoundForThisKernel) {
    const auto e = pl::describeLoadFailure(ENOENT, "ghost", {}, "none");
    EXPECT_EQ(e.code, Error::Code::NotFound);
    EXPECT_EQ(e.message, "module 'ghost' not found for this kernel");
}

TEST(KmodErrorTaxonomy, UnloadBusyListsHolders) {
    const auto e = pl::describeUnloadFailure(EBUSY, "usbcore", {"usbhid", "xhci_hcd"});
    EXPECT_EQ(e.code, Error::Code::Busy);
    EXPECT_EQ(e.message, "module 'usbcore' is in use by usbhid, xhci_hcd");
}
