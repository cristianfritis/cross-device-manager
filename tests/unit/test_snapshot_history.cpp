#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "devmgr/core/snapshot_history.hpp"

using namespace devmgr::core;

namespace {

// Ids are 64 hex chars in the store; short stand-ins keep the tests readable.
SnapshotMeta meta(std::string id, std::optional<std::string> parent = std::nullopt,
                  SnapshotHealth health = SnapshotHealth::Ok) {
    SnapshotMeta m;
    m.id = std::move(id);
    m.parent = std::move(parent);
    m.health = health;
    return m;
}

}  // namespace

TEST(SnapshotHistory, EmptyListYieldsNoRows) {
    EXPECT_TRUE(buildSnapshotChain({}).empty());
}

TEST(SnapshotHistory, NewestFirstOrderIsPreservedAndHeadIsTheTip) {
    // c -> b -> a, listed newest first.
    const std::vector<SnapshotMeta> metas{meta("c", "b"), meta("b", "a"), meta("a")};

    const auto rows = buildSnapshotChain(metas);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].meta.id, "c");
    EXPECT_TRUE(rows[0].head);
    EXPECT_FALSE(rows[1].head);
    EXPECT_FALSE(rows[2].head);
    EXPECT_EQ(rows[0].depth, 2u);
    EXPECT_EQ(rows[1].depth, 1u);
    EXPECT_EQ(rows[2].depth, 0u);
    EXPECT_TRUE(rows[2].chainStart);
    EXPECT_FALSE(rows[0].chainStart);
}

TEST(SnapshotHistory, PrunedParentBecomesAChainStartNotAnError) {
    // "a" was pruned; "b" still names it as parent.
    const std::vector<SnapshotMeta> metas{meta("c", "b"), meta("b", "a")};

    const auto rows = buildSnapshotChain(metas);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_TRUE(rows[1].chainStart);
    EXPECT_EQ(rows[1].depth, 0u);
    EXPECT_EQ(rows[0].depth, 1u);
    EXPECT_TRUE(rows[0].head);
}

TEST(SnapshotHistory, LastGoodSkipsCorruptAndUnsupportedRows) {
    const std::vector<SnapshotMeta> metas{meta("c", "b", SnapshotHealth::Corrupt),
                                          meta("b", "a", SnapshotHealth::Unsupported),
                                          meta("a", std::nullopt, SnapshotHealth::Ok)};

    const auto rows = buildSnapshotChain(metas);
    EXPECT_FALSE(rows[0].lastGood);
    EXPECT_FALSE(rows[1].lastGood);
    EXPECT_TRUE(rows[2].lastGood);
    EXPECT_TRUE(rows[0].head);  // HEAD is structural — corruption does not move it
}

TEST(SnapshotHistory, HeadAndLastGoodCanBeTheSameRow) {
    const std::vector<SnapshotMeta> metas{meta("b", "a"), meta("a")};

    const auto rows = buildSnapshotChain(metas);
    EXPECT_TRUE(rows[0].head);
    EXPECT_TRUE(rows[0].lastGood);
}

TEST(SnapshotHistory, OnlyOneRowIsMarkedHeadWhenPruningSplitsTheChain) {
    // Two disjoint segments: d->c (parent pruned) and b->a.
    const std::vector<SnapshotMeta> metas{meta("d", "c"), meta("c", "pruned"), meta("b", "a"),
                                          meta("a")};

    const auto rows = buildSnapshotChain(metas);
    int heads = 0;
    for (const auto& row : rows)
        if (row.head) ++heads;
    EXPECT_EQ(heads, 1);
    EXPECT_TRUE(rows[0].head);  // newest tip wins
    EXPECT_TRUE(rows[1].chainStart);
    EXPECT_TRUE(rows[3].chainStart);
}

TEST(SnapshotHistory, AllHealthyRowsCorruptLeavesNoLastGood) {
    const std::vector<SnapshotMeta> metas{meta("b", "a", SnapshotHealth::Corrupt),
                                          meta("a", std::nullopt, SnapshotHealth::Corrupt)};

    const auto rows = buildSnapshotChain(metas);
    for (const auto& row : rows) EXPECT_FALSE(row.lastGood);
}
