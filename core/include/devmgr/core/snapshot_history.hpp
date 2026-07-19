#pragma once
#include <cstddef>
#include <vector>

#include "devmgr/core/snapshot_models.hpp"

namespace devmgr::core {

// Parent-chain presentation over the metadata SnapshotList already returns
// (snapshot-history spec, design decision 2): no new IPC, no store migration —
// `id` + `parent` are enough. Lives in core so SnapshotsVM (both UIs) and the
// recovery CLI derive the same chain, order, HEAD and last-good.
struct SnapshotChainRow {
    SnapshotMeta meta;
    // The parent id is absent from the list — either this is the first
    // snapshot ever, or the parent was pruned. Renders as a chain start, never
    // as an error.
    bool chainStart = false;
    // Tip of the chain: no other snapshot in the list claims it as parent.
    bool head = false;
    // Newest snapshot whose health is Ok — the one a recovery would aim at.
    bool lastGood = false;
    // Distance from the chain start, for indented chain rendering. Rows whose
    // ancestry is broken by pruning restart at 0.
    std::size_t depth = 0;
    friend bool operator==(const SnapshotChainRow&, const SnapshotChainRow&) = default;
};

// Input order is preserved verbatim (SnapshotList is newest-first and every
// surface shows it that way); only the markers are derived. Metadata that
// names a parent outside the list is a chain start. When several rows qualify
// as tips — possible after a prune splits the chain — the first (newest) wins,
// matching what a restore would treat as current.
std::vector<SnapshotChainRow> buildSnapshotChain(const std::vector<SnapshotMeta>& metas);

}  // namespace devmgr::core
