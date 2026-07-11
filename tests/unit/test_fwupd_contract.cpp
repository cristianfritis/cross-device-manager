#include <gtest/gtest.h>
#include <sdbus-c++/sdbus-c++.h>

#include "devmgr/platform/linux/fwupd_contract.hpp"

namespace fw = devmgr::platform_linux::fwupd;
using devmgr::core::Error;

namespace {
fw::Dict deviceDict() {
    fw::Dict d;
    d["DeviceId"] = sdbus::Variant{std::string{"aabb"}};
    d["Name"] = sdbus::Variant{std::string{"Webcam"}};
    d["Vendor"] = sdbus::Variant{std::string{"ACME"}};
    d["Version"] = sdbus::Variant{std::string{"1.2.2"}};
    d["Flags"] = sdbus::Variant{std::uint64_t{fw::kDeviceFlagUpdatable | fw::kDeviceFlagSupported}};
    return d;
}
}  // namespace

TEST(FwupdContract, ParsesWellFormedDevice) {
    const auto p = fw::parseDevice(deviceDict());
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->deviceId, "aabb");
    EXPECT_TRUE(p->facts.updatable);
    EXPECT_TRUE(p->facts.supported);
    EXPECT_FALSE(p->facts.needsRebootAfterUpdate);
}
TEST(FwupdContract, EmptyDeviceIdDropsRow) {
    auto d = deviceDict();
    d["DeviceId"] = sdbus::Variant{std::string{}};
    EXPECT_FALSE(fw::parseDevice(d).has_value());
}
TEST(FwupdContract, MissingVersionDropsRow) {
    auto d = deviceDict();
    d.erase("Version");
    EXPECT_FALSE(fw::parseDevice(d).has_value());
}
TEST(FwupdContract, WrongVariantTypeForKnownKeyIsSkippedNotFatal) {
    auto d = deviceDict();
    d["Flags"] = sdbus::Variant{std::string{"not-a-number"}};  // wrong type
    const auto p = fw::parseDevice(d);
    ASSERT_TRUE(p.has_value());        // row survives
    EXPECT_FALSE(p->facts.updatable);  // flag field defaulted
}
TEST(FwupdContract, UnknownKeysIgnored) {
    auto d = deviceDict();
    d["FutureKey2027"] = sdbus::Variant{std::uint64_t{42}};
    EXPECT_TRUE(fw::parseDevice(d).has_value());
}
TEST(FwupdContract, ReleaseLocationsPreferredUriFallback) {
    fw::Dict r;
    r["Version"] = sdbus::Variant{std::string{"1.2.4"}};
    r["RemoteId"] = sdbus::Variant{std::string{"fwupd-tests"}};
    r["Checksum"] = sdbus::Variant{std::string{"deadbeef"}};
    r["Uri"] = sdbus::Variant{std::string{"./fakedevice124.cab"}};
    const auto a = fw::parseRelease(r);
    ASSERT_TRUE(a.has_value());
    ASSERT_EQ(a->locations.size(), 1U);
    EXPECT_EQ(a->locations[0], "./fakedevice124.cab");
    r["Locations"] = sdbus::Variant{std::vector<std::string>{"https://x/y.cab"}};
    const auto b = fw::parseRelease(r);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->locations, std::vector<std::string>{"https://x/y.cab"});  // Locations wins
}
TEST(FwupdContract, ReleaseUpgradeFlagAndDuration) {
    fw::Dict r;
    r["Version"] = sdbus::Variant{std::string{"2"}};
    r["RemoteId"] = sdbus::Variant{std::string{"lvfs"}};
    r["Checksum"] = sdbus::Variant{std::string{"c"}};
    r["Flags"] = sdbus::Variant{std::uint64_t{fw::kReleaseFlagIsUpgrade}};
    r["InstallDuration"] = sdbus::Variant{std::uint32_t{120}};
    r["Size"] = sdbus::Variant{std::uint64_t{4096}};
    const auto p = fw::parseRelease(r);
    ASSERT_TRUE(p.has_value());
    EXPECT_TRUE(p->isUpgrade);
    EXPECT_EQ(p->installDurationSec, std::uint32_t{120});
    EXPECT_EQ(p->sizeBytes, 4096U);
}
TEST(FwupdContract, ErrorTable) {
    EXPECT_EQ(fw::mapError("org.freedesktop.fwupd.AuthFailed", "denied").code,
              Error::Code::Permission);
    EXPECT_EQ(fw::mapError("org.freedesktop.fwupd.NothingToDo", "").code, Error::Code::Conflict);
    EXPECT_EQ(fw::mapError("org.freedesktop.fwupd.NeedsUserAction", "replug").code,
              Error::Code::Busy);
    EXPECT_EQ(fw::mapError("org.freedesktop.fwupd.VersionNewer", "").code, Error::Code::Conflict);
    const auto unknown = fw::mapError("org.freedesktop.fwupd.Whatever2030", "boom");
    EXPECT_EQ(unknown.code, Error::Code::Io);
    EXPECT_NE(unknown.message.find("org.freedesktop.fwupd.Whatever2030"), std::string::npos);
    EXPECT_NE(unknown.message.find("boom"), std::string::npos);  // name + msg both preserved
    EXPECT_EQ(fw::mapError("", "").code, Error::Code::Io);       // empty name/msg safe
    EXPECT_TRUE(fw::isNothingToDo("org.freedesktop.fwupd.NothingToDo"));
}
TEST(FwupdContract, UpdateStateToDisposition) {
    EXPECT_EQ(fw::dispositionFromUpdateState(fw::kUpdateStateSuccess),
              devmgr::core::InstallDisposition::Completed);
    EXPECT_EQ(fw::dispositionFromUpdateState(fw::kUpdateStateNeedsReboot),
              devmgr::core::InstallDisposition::NeedsReboot);
    EXPECT_EQ(fw::dispositionFromUpdateState(fw::kUpdateStatePending),
              devmgr::core::InstallDisposition::Scheduled);
}
