#include <gtest/gtest.h>
#include "devmgr/core/result.hpp"

using devmgr::core::Error;
using devmgr::core::makeError;
using devmgr::core::Result;

Result<int> doubleIfPositive(int n) {
    if (n <= 0) return makeError(Error::Code::NotFound, "non-positive");
    return n * 2;
}

TEST(Result, CarriesValueOnSuccess) {
    auto r = doubleIfPositive(21);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 42);
}

TEST(Result, CarriesErrorOnFailure) {
    auto r = doubleIfPositive(-1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
    EXPECT_EQ(r.error().message, "non-positive");
}
