#pragma once
#include <string>
#include <utility>

#include <tl/expected.hpp>

namespace devmgr::core {

struct Error {
    // InvalidArgs (ApiVersion 4) means the request was malformed — caps, charset,
    // or size — and was refused before any verb logic ran, so no state changed.
    // Distinct from NotFound, which means a well-formed request named something
    // that does not exist.
    enum class Code { Permission, NotFound, Busy, Io, Network, Unsupported, Conflict, InvalidArgs };
    Code code;
    std::string message;
};

template <class T>
using Result = tl::expected<T, Error>;

inline tl::unexpected<Error> makeError(Error::Code code, std::string message) {
    return tl::unexpected<Error>(Error{code, std::move(message)});
}

}  // namespace devmgr::core
