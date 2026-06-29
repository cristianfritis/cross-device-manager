#pragma once
#include <functional>
#include <string>

namespace devmgr::runtime {

struct ProgressUpdate {
    int percent = 0;
    std::string stage;
};

using ProgressReporter = std::function<void(const ProgressUpdate&)>;

}  // namespace devmgr::runtime
