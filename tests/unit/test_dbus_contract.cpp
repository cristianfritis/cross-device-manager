#include <array>

#include <gtest/gtest.h>

#include "devmgr/platform/linux/dbus_contract.hpp"

using devmgr::core::Error;
using namespace devmgr::platform_linux;

TEST(DbusContractTest, DomainCodesRoundTripThroughErrorNames) {
    const std::array codes = {Error::Code::Conflict, Error::Code::Permission, Error::Code::NotFound,
                              Error::Code::Unsupported, Error::Code::Io};
    for (const auto code : codes) {
        const auto mapped = coreErrorFor(dbusErrorNameFor(code), "msg");
        EXPECT_EQ(mapped.code, code);
        EXPECT_EQ(mapped.message, "msg");
    }
}

TEST(DbusContractTest, BusyAndNetworkCollapseToIoOnTheWire) {
    EXPECT_STREQ(dbusErrorNameFor(Error::Code::Busy), kErrIo);
    EXPECT_STREQ(dbusErrorNameFor(Error::Code::Network), kErrIo);
}

TEST(DbusContractTest, TransportErrorsMapPerSpecTable) {
    const auto gone = coreErrorFor("org.freedesktop.DBus.Error.ServiceUnknown", "x");
    EXPECT_EQ(gone.code, Error::Code::Io);
    EXPECT_EQ(gone.message, "helper devmgrd is not available");

    const auto slow = coreErrorFor("org.freedesktop.DBus.Error.NoReply", "x");
    EXPECT_EQ(slow.code, Error::Code::Busy);
    EXPECT_EQ(slow.message, "helper timed out");

    const auto other = coreErrorFor("org.freedesktop.DBus.Error.NoMemory", "boom");
    EXPECT_EQ(other.code, Error::Code::Io);
    EXPECT_EQ(other.message, "boom");
}

// ApiVersion 4 added InvalidArgs. It round-trips by name, and — critically for
// pre-v4 clients — an unrecognized name still degrades to Io rather than being
// mistaken for one of the codes they do know.
TEST(DbusContractTest, InvalidArgsRoundTripsByName) {
    EXPECT_STREQ(dbusErrorNameFor(Error::Code::InvalidArgs), kErrInvalidArgs);
    const auto back = coreErrorFor(kErrInvalidArgs, "bad id");
    EXPECT_EQ(back.code, Error::Code::InvalidArgs);
    EXPECT_EQ(back.message, "bad id");
}

TEST(DbusContractTest, InvalidArgsIsDistinctFromNotFound) {
    EXPECT_STRNE(dbusErrorNameFor(Error::Code::InvalidArgs),
                 dbusErrorNameFor(Error::Code::NotFound));
}
