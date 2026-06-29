#include <gtest/gtest.h>
#include "devmgr/core/version.hpp"

TEST(Version, ReportsSemver) {
    EXPECT_EQ(devmgr::core::version(), "0.1.0");
}
