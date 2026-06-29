#include <gtest/gtest.h>
#include "devmgr/runtime/logging.hpp"

using devmgr::runtime::LogLevel;
using devmgr::runtime::toSpdlogLevel;

TEST(Logging, MapsLevelsToSpdlog) {
    EXPECT_EQ(toSpdlogLevel(LogLevel::Trace), spdlog::level::trace);
    EXPECT_EQ(toSpdlogLevel(LogLevel::Debug), spdlog::level::debug);
    EXPECT_EQ(toSpdlogLevel(LogLevel::Info), spdlog::level::info);
    EXPECT_EQ(toSpdlogLevel(LogLevel::Warn), spdlog::level::warn);
    EXPECT_EQ(toSpdlogLevel(LogLevel::Error), spdlog::level::err);
}

TEST(Logging, InitSetsDefaultLoggerLevel) {
    devmgr::runtime::init(LogLevel::Warn);
    EXPECT_EQ(spdlog::default_logger()->level(), spdlog::level::warn);
}
