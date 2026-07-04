#pragma once
#include "devmgr/pal/criticality.hpp"

namespace devmgr::test {

class FakeCriticalityProber final : public pal::ICriticalityProber {
   public:
    core::Result<pal::CriticalityFacts> probe() override { return next; }
    core::Result<pal::CriticalityFacts> next = pal::CriticalityFacts{};
};

}  // namespace devmgr::test
