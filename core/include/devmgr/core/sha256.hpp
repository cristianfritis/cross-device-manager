#pragma once
#include <string>
#include <string_view>

namespace devmgr::core {

// FIPS 180-4 SHA-256 of `data`, returned as 64 lowercase hex characters.
// Self-contained implementation: snapshot ids need a content hash and the
// dependency budget for the backup-rollback engine is zero new libraries.
std::string sha256Hex(std::string_view data);

}  // namespace devmgr::core
