#pragma once
#include <string>
#include <utility>

#include <tl/expected.hpp>

namespace devmgr::core {

struct Error {
    enum class Code { Permission, NotFound, Busy, Io, Network, Unsupported, Conflict };
    Code code;
    std::string message;
};

template <class T>
using Result = tl::expected<T, Error>;

inline tl::unexpected<Error> makeError(Error::Code code, std::string message) {
    return tl::unexpected<Error>(Error{code, std::move(message)});
}

}  // namespace devmgr::core
