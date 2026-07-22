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

// Daemon-not-present / not-activatable errors are recognized by their stable
// error NAME (authoritative, localization-proof) and carried as Busy — the
// domain code the CLI routes to "unreachable" (exit 4) — with the real cause
// preserved so the user still sees why. Covers Scenario 8's masked unit.
TEST(DbusContractTest, DaemonUnavailableNamesMapToUnreachable) {
    const std::array names = {"org.freedesktop.systemd1.UnitMasked",
                              "org.freedesktop.systemd1.NoSuchUnit",
                              "org.freedesktop.systemd1.UnitInactive",
                              "org.freedesktop.DBus.Error.NameHasNoOwner",
                              "org.freedesktop.DBus.Error.NoServer",
                              "org.freedesktop.DBus.Error.Disconnected",
                              "org.freedesktop.DBus.Error.Spawn.ExecFailed",
                              "org.freedesktop.DBus.Error.Spawn.ServiceNotValid",
                              "org.freedesktop.DBus.Error.Spawn.FileNotFound"};
    for (const auto* name : names) {
        const auto mapped = coreErrorFor(name, "Unit devmgrd.service is masked.");
        EXPECT_EQ(mapped.code, Error::Code::Busy) << name;
        // Real cause preserved (not overwritten with a generic marker).
        EXPECT_EQ(mapped.message, "Unit devmgrd.service is masked.") << name;
    }
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
