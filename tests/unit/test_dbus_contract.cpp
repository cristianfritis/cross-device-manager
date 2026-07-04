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
