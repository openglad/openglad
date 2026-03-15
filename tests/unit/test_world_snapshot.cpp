#include <openglad/gameplay/world_snapshot.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

const og::sim::EntitySnapshotFieldDesc* find_desc(std::uint8_t bit)
{
    const auto it = std::find_if(
        std::begin(og::sim::kEntitySnapshotFields),
        std::end(og::sim::kEntitySnapshotFields),
        [bit](const og::sim::EntitySnapshotFieldDesc& desc) {
            return desc.bit_index == bit;
        });
    return it == std::end(og::sim::kEntitySnapshotFields) ? nullptr : &*it;
}

} // namespace

TEST(WorldSnapshot, entity_snapshot_layout_matches_dirty_field_table)
{
    static_assert(std::is_standard_layout_v<og::sim::EntitySnapshot>);
    static_assert(std::is_trivially_copyable_v<og::sim::EntitySnapshot>);

    std::array<bool, og::dirty::FIELD_COUNT> seen_bits = {};
    for (const og::sim::EntitySnapshotFieldDesc& desc :
         og::sim::kEntitySnapshotFields) {
        ASSERT_LT(desc.bit_index, og::dirty::FIELD_COUNT);
        EXPECT_FALSE(seen_bits[desc.bit_index]);
        seen_bits[desc.bit_index] = true;
        EXPECT_GT(desc.size, 0);
        EXPECT_LE(static_cast<std::size_t>(desc.snap_offset) + desc.size,
                  sizeof(og::sim::EntitySnapshot));
    }

    EXPECT_TRUE(std::all_of(seen_bits.begin(), seen_bits.end(),
                            [](bool seen) { return seen; }));

    const auto* entity_id_desc = find_desc(og::dirty::BIT_ENTITY_ID);
    ASSERT_NE(nullptr, entity_id_desc);
    EXPECT_EQ(offsetof(og::sim::EntitySnapshot, entity_id),
              entity_id_desc->snap_offset);

    const auto* special_cost_desc = find_desc(og::dirty::BIT_SPECIAL_COST);
    ASSERT_NE(nullptr, special_cost_desc);
    EXPECT_EQ(offsetof(og::sim::EntitySnapshot, special_cost),
              special_cost_desc->snap_offset);
    EXPECT_EQ(sizeof(std::uint16_t) * NUM_SPECIALS, special_cost_desc->size);

    const auto* bounce_desc = find_desc(og::dirty::BIT_DO_BOUNCE);
    ASSERT_NE(nullptr, bounce_desc);
    EXPECT_EQ(offsetof(og::sim::EntitySnapshot, do_bounce),
              bounce_desc->snap_offset);
}

TEST(WorldSnapshot, guy_linkage_is_not_dirty_mask_tracked)
{
    const bool guy_id_in_field_table = std::any_of(
        std::begin(og::sim::kEntitySnapshotFields),
        std::end(og::sim::kEntitySnapshotFields),
        [](const og::sim::EntitySnapshotFieldDesc& desc) {
            return desc.snap_offset == offsetof(og::sim::EntitySnapshot, guy_id);
        });

    EXPECT_FALSE(guy_id_in_field_table);
}

TEST(WorldSnapshot, world_snapshot_can_hold_world_and_guy_state)
{
    og::sim::WorldSnapshot snapshot;
    snapshot.tick_count = 42;
    snapshot.rng_state = 1234;
    snapshot.level_tick_count = 7;
    snapshot.current_palette_id = 1;
    snapshot.pending_exit_prompt = true;
    snapshot.paused = true;
    snapshot.pause_player_index = 2;
    snapshot.grid_dirty = true;
    snapshot.grid_full_resend = false;
    snapshot.grid_dirty_tiles.push_back({3, 4, 5});
    snapshot.removed_entity_ids.push_back(17);

    og::sim::GuySnapshot guy_snapshot;
    guy_snapshot.guy_id = 9;
    guy_snapshot.name = "Aldo";
    guy_snapshot.exp = 123;
    guy_snapshot.scen_damage = 4.5f;
    snapshot.guy_snapshots.push_back(guy_snapshot);

    og::sim::EntitySnapshot entity_snapshot;
    entity_snapshot.guy_id = 9;
    entity_snapshot.entity_id = 17;
    entity_snapshot.order = Order::Living;
    entity_snapshot.family = 3;
    entity_snapshot.special_cost[0] = 11;
    snapshot.oblist.push_back(entity_snapshot);

    ASSERT_EQ(1u, snapshot.guy_snapshots.size());
    EXPECT_EQ(9, snapshot.guy_snapshots.front().guy_id);
    EXPECT_EQ("Aldo", snapshot.guy_snapshots.front().name);
    ASSERT_EQ(1u, snapshot.oblist.size());
    EXPECT_EQ(17u, snapshot.oblist.front().entity_id);
    EXPECT_EQ(9, snapshot.oblist.front().guy_id);
    EXPECT_EQ(11u, snapshot.oblist.front().special_cost[0]);
    ASSERT_EQ(1u, snapshot.grid_dirty_tiles.size());
    EXPECT_EQ(5u, snapshot.grid_dirty_tiles.front().value);
}
