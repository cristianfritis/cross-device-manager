#include <gtest/gtest.h>
#include <string>
#include "devmgr/core/version.hpp"

// No literal version anywhere: the single source is the root CMakeLists
// project() call, so these tests only check internal consistency of the
// generated header (release-versioning spec).

TEST(Version, ComposedFromSingleSourceComponents) {
    std::string expected = std::to_string(devmgr::core::kVersionMajor) + "." +
                           std::to_string(devmgr::core::kVersionMinor) + "." +
                           std::to_string(devmgr::core::kVersionPatch);
    if (!devmgr::core::kPrerelease.empty()) {
        expected += "-";
        expected += devmgr::core::kPrerelease;
    }
    EXPECT_EQ(devmgr::core::version(), expected);
}

TEST(Version, VersionLineIsNameSpaceVersion) {
    const std::string expected = "devmgrd " + std::string(devmgr::core::kVersion);
    EXPECT_EQ(devmgr::core::versionLine("devmgrd"), expected);
}
