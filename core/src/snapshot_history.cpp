#include "devmgr/core/snapshot_history.hpp"

#include <map>
#include <set>
#include <string>

namespace devmgr::core {
namespace {

// Depth walks up the parent links, stopping at a pruned ancestor. Cycles
// cannot occur in a hash-chained store, but the visited set keeps a corrupted
// listing from looping forever.
std::size_t depthOf(const SnapshotMeta& meta,
                    const std::map<std::string, const SnapshotMeta*>& byId) {
    std::set<std::string> visited{meta.id};
    std::size_t depth = 0;
    const SnapshotMeta* current = &meta;
    while (current->parent) {
        const auto parent = byId.find(*current->parent);
        if (parent == byId.end()) break;                   // pruned — this row restarts the chain
        if (!visited.insert(parent->first).second) break;  // corrupt listing, stop
        current = parent->second;
        ++depth;
    }
    return depth;
}

}  // namespace

std::vector<SnapshotChainRow> buildSnapshotChain(const std::vector<SnapshotMeta>& metas) {
    std::map<std::string, const SnapshotMeta*> byId;
    std::set<std::string> claimedAsParent;
    for (const auto& meta : metas) {
        byId.emplace(meta.id, &meta);
        if (meta.parent) claimedAsParent.insert(*meta.parent);
    }

    std::vector<SnapshotChainRow> rows;
    rows.reserve(metas.size());
    bool headTaken = false;
    bool lastGoodTaken = false;
    for (const auto& meta : metas) {
        SnapshotChainRow row;
        row.meta = meta;
        row.chainStart = !meta.parent || !byId.contains(*meta.parent);
        row.depth = depthOf(meta, byId);
        if (!headTaken && !claimedAsParent.contains(meta.id)) {
            row.head = true;
            headTaken = true;
        }
        if (!lastGoodTaken && meta.health == SnapshotHealth::Ok) {
            row.lastGood = true;
            lastGoodTaken = true;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace devmgr::core
