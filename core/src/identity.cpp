#include "devmgr/core/identity.hpp"

namespace devmgr::core {

std::string identityTail(std::string_view nativeId) {
    const auto pos = nativeId.find_last_of("/\\");
    if (pos == std::string_view::npos) return std::string(nativeId);
    return std::string(nativeId.substr(pos + 1));
}

}  // namespace devmgr::core
