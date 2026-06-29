#pragma once
#include <spdlog/spdlog.h>

namespace devmgr::runtime {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

spdlog::level::level_enum toSpdlogLevel(LogLevel level);
void init(LogLevel level = LogLevel::Info);

}  // namespace devmgr::runtime
