#include <openglad/gameplay/world_snapshot.h>
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/weap.h>
#include <openglad/legacy/base.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>

#include <gtest/gtest.h>

#include "test_game_world_fixture.h"

namespace {

struct SnapshotWalker final : walker
{
    SnapshotWalker()
        : walker()
    {
    }

    bool act() override
    {
        return true;
    }
};

struct SnapshotWeapon final : weap
{
    SnapshotWeapon()
        : weap()
    {
    }

    bool act() override
    {
        return true;
    }
};

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

const og::sim::EntitySnapshot* find_entity_snapshot(
    const std::vector<og::sim::EntitySnapshot>& entities,
    std::uint32_t entity_id)
{
    const auto it = std::find_if(
        entities.begin(), entities.end(),
        [entity_id](const og::sim::EntitySnapshot& snapshot) {
            return snapshot.entity_id == entity_id;
        });
    return it == entities.end() ? nullptr : &*it;
}

} // namespace

TEST(WorldSnapshot, entity_snapshot_layout_matches_dirty_field_table)
{
    static_assert(std::is_standard_layout_v<og::sim::EntitySnapshot>);
    static_assert(std::is_trivially_copyable_v<og::sim::EntitySnapshot>);
    EXPECT_EQ(84u, og::sim::kEntitySnapshotTableFieldCount);
    EXPECT_EQ(2u, og::sim::kEntitySnapshotManualFieldCount);
    EXPECT_EQ(og::dirty::FIELD_COUNT, og::sim::kEntitySnapshotTrackedFieldCount);

    std::array<bool, og::dirty::FIELD_COUNT> seen_bits = {};
    for (const og::sim::EntitySnapshotFieldDesc& desc :
         og::sim::kEntitySnapshotFields) {
        ASSERT_LT(desc.bit_index, og::dirty::FIELD_COUNT);
        EXPECT_FALSE(og::sim::entity_snapshot_field_is_manual(desc.bit_index));
        EXPECT_FALSE(seen_bits[desc.bit_index]);
        seen_bits[desc.bit_index] = true;
        EXPECT_GT(desc.size, 0);
        EXPECT_LE(static_cast<std::size_t>(desc.snap_offset) + desc.size,
                  sizeof(og::sim::EntitySnapshot));
    }

    for (std::uint8_t bit = 0; bit < og::dirty::FIELD_COUNT; ++bit) {
        if (og::sim::entity_snapshot_field_is_manual(bit)) {
            EXPECT_FALSE(seen_bits[bit]);
        } else {
            EXPECT_TRUE(seen_bits[bit]);
        }
    }

    const auto* entity_id_desc = find_desc(og::dirty::BIT_ENTITY_ID);
    ASSERT_NE(nullptr, entity_id_desc);
    EXPECT_EQ(offsetof(og::sim::EntitySnapshot, entity_id),
              entity_id_desc->snap_offset);

    const auto* special_cost_desc = find_desc(og::dirty::BIT_SPECIAL_COST);
    ASSERT_NE(nullptr, special_cost_desc);
    EXPECT_EQ(offsetof(og::sim::EntitySnapshot, special_cost),
              special_cost_desc->snap_offset);
    EXPECT_EQ(sizeof(std::uint16_t) * NUM_SPECIALS, special_cost_desc->size);
}

TEST(WorldSnapshot, manual_entity_fields_stay_out_of_the_generic_field_table)
{
    const bool regen_delay_in_field_table = std::any_of(
        std::begin(og::sim::kEntitySnapshotFields),
        std::end(og::sim::kEntitySnapshotFields),
        [](const og::sim::EntitySnapshotFieldDesc& desc) {
            return desc.bit_index == og::dirty::BIT_REGEN_DELAY;
        });
    const bool do_bounce_in_field_table = std::any_of(
        std::begin(og::sim::kEntitySnapshotFields),
        std::end(og::sim::kEntitySnapshotFields),
        [](const og::sim::EntitySnapshotFieldDesc& desc) {
            return desc.bit_index == og::dirty::BIT_DO_BOUNCE;
        });

    EXPECT_FALSE(regen_delay_in_field_table);
    EXPECT_FALSE(do_bounce_in_field_table);
    EXPECT_TRUE(
        og::sim::entity_snapshot_field_is_manual(og::dirty::BIT_REGEN_DELAY));
    EXPECT_TRUE(
        og::sim::entity_snapshot_field_is_manual(og::dirty::BIT_DO_BOUNCE));
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

TEST(WorldSnapshot, guy_linkage_uses_negative_sentinel_for_unlinked_entities)
{
    og::sim::EntitySnapshot npc_snapshot;
    og::sim::GuySnapshot guy_snapshot;
    guy_snapshot.guy_id = 0;

    EXPECT_EQ(og::sim::kNoGuyId, npc_snapshot.guy_id);
    EXPECT_NE(guy_snapshot.guy_id, npc_snapshot.guy_id);
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

TEST(WorldSnapshot, capture_snapshot_matches_live_world_and_drains_bookkeeping)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    world.entity_factory = [](Order order, std::int32_t family) -> std::unique_ptr<walker> {
        std::unique_ptr<walker> entity;
        if (order == Order::Weapon)
            entity = std::make_unique<SnapshotWeapon>();
        else
            entity = std::make_unique<SnapshotWalker>();

        entity->order = order;
        entity->family = static_cast<char>(family);
        entity->sizex = 16;
        entity->sizey = 16;
        return entity;
    };

    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    walker* leader = world.add_ob(Order::Living, FAMILY_ORC);
    walker* owner = world.add_ob(Order::Living, FAMILY_ORC);
    walker* collide = world.add_ob(Order::Living, FAMILY_ORC);
    walker* controller = world.add_ob(Order::Living, FAMILY_ORC);
    walker* fx_entity = world.add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* weapon = world.add_weap_ob(Order::Weapon, FAMILY_ARROW);

    ASSERT_NE(nullptr, actor);
    ASSERT_NE(nullptr, foe);
    ASSERT_NE(nullptr, leader);
    ASSERT_NE(nullptr, owner);
    ASSERT_NE(nullptr, collide);
    ASSERT_NE(nullptr, controller);
    ASSERT_NE(nullptr, fx_entity);
    ASSERT_NE(nullptr, weapon);

    world.tick();

    actor->setxy(48, 64);
    actor->setworldxy(48.5f, 64.25f);
    actor->lastx = 1.25f;
    actor->lasty = -0.5f;
    actor->stepsize = 2.5f;
    actor->normal_stepsize = 2.0f;
    actor->curdir = FACE_RIGHT;
    actor->enddir = FACE_LEFT;
    actor->damage = 9.5f;
    actor->fire_frequency = 3.0f;
    actor->busy = 1.0f;
    actor->current_weapon = FAMILY_ARROW;
    actor->default_weapon = FAMILY_KNIFE;
    actor->attack_lunge = 4.0f;
    actor->attack_lunge_angle = 0.75f;
    actor->hit_recoil = 2.0f;
    actor->hit_recoil_angle = 1.25f;
    actor->last_hitpoints = 17.0f;
    actor->action = 3;
    actor->act_type = ACT_CONTROL;
    actor->old_act_type = ACT_RANDOM;
    actor->ani_type = ANI_WALK;
    actor->cycle = 2;
    actor->drawcycle = 3;
    actor->current_special = 1;
    actor->ignore = 0;
    actor->in_act = true;
    actor->shifter_down = 1;
    actor->yo_delay = 4;
    actor->skip_exit = 5;
    actor->outline = 1;
    actor->hurt_flash = true;
    actor->lifetime = 123;
    actor->speed_bonus = 1.5f;
    actor->speed_bonus_left = 22;
    actor->charm_left = 7;
    actor->weapons_left = 8;
    actor->keys = 0x1234;
    actor->view_all = 1;
    actor->lineofsight = 33;
    actor->path_check_counter = 44;

    actor->stats()->hitpoints = 19.0f;
    actor->stats()->max_hitpoints = 20.0f;
    actor->stats()->magicpoints = 6.0f;
    actor->stats()->max_magicpoints = 7.0f;
    actor->stats()->max_heal_delay = 101;
    actor->stats()->current_heal_delay = 51;
    actor->stats()->max_magic_delay = 201;
    actor->stats()->current_magic_delay = 91;
    actor->stats()->magic_per_round = 0.5f;
    actor->stats()->heal_per_round = 0.25f;
    actor->stats()->armor = 4.0f;
    actor->stats()->level = 9;
    actor->stats()->bit_flags = BIT_FORESTWALK;
    actor->stats()->delete_me = 0;
    actor->stats()->frozen_delay = 2;
    actor->stats()->weapon_cost = 3;
    actor->stats()->special_cost[0] = 11;
    actor->stats()->special_cost[1] = 12;
    actor->stats()->old_order = Order::Living;
    actor->stats()->old_family = FAMILY_ARCHER;
    actor->stats()->last_distance = 99;
    actor->stats()->current_distance = 88;

    auto player_guy = std::make_unique<guy>(FAMILY_SOLDIER);
    player_guy->name = "Aldo";
    player_guy->exp = 1234;
    player_guy->kills = 9;
    player_guy->level_kills = 17;
    player_guy->total_damage = 42;
    player_guy->total_hits = 8;
    player_guy->total_shots = 10;
    player_guy->teamnum = 2;
    player_guy->scen_damage = 5.5f;
    player_guy->scen_kills = 6;
    player_guy->scen_damage_taken = 2.0f;
    player_guy->scen_min_hp = 11.0f;
    player_guy->scen_shots = 7;
    player_guy->scen_hits = 4;
    player_guy->level = 3;
    const int guy_id = player_guy->id;
    actor->set_owned_myguy(std::move(player_guy));

    static_cast<weap*>(weapon)->do_bounce = 7;

    actor->set_foe(foe);
    actor->set_leader(leader);
    actor->set_owner(owner);
    actor->set_collide_ob(collide);
    actor->stats()->set_controller(controller);

    actor->clear_dirty();
    weapon->clear_dirty();
    actor->foe_id = 0;
    actor->leader_id = 0;
    actor->owner_id = 0;
    actor->collide_ob_id = 0;
    actor->stats()->controller_id = 0;

    walker* removed = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, removed);
    const std::uint32_t removed_id = removed->entity_id();
    ASSERT_EQ(1, world.remove_ob(removed));

    world.tick_count_ = 42;
    world.rng_.state_ = 777;
    world.set_level_tick_count(7);
    world.game_ended = true;
    world.level_done = 1;
    world.end = 1;
    world.retry = true;
    world.next_level = 8;
    world.ending = 2;
    world.enemy_freeze = 3;
    world.timer_wait = 4;
    world.control_hp = 15.5f;
    world.withdraw_requested = true;
    world.withdraw_level = 9;
    world.guy_id_counter = 123;
    world.current_palette_id = 1;
    world.pending_exit_prompt = true;
    world.paused = true;
    world.pause_player_index = 2;
    world.m_score[0] = 100;
    world.m_score[1] = 200;
    world.m_score[2] = 300;
    world.m_score[3] = 400;

    const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(world);

    EXPECT_EQ(world.tick_count_, snapshot.tick_count);
    EXPECT_EQ(world.rng_.state_, snapshot.rng_state);
    EXPECT_EQ(world.level_tick_count(), snapshot.level_tick_count);
    EXPECT_EQ(world.level_done, snapshot.level_done);
    EXPECT_EQ(world.game_ended, snapshot.game_ended);
    EXPECT_EQ(world.end, snapshot.end);
    EXPECT_EQ(world.retry, snapshot.retry);
    EXPECT_EQ(world.next_level, snapshot.next_level);
    EXPECT_EQ(world.ending, snapshot.ending);
    EXPECT_EQ(world.enemy_freeze, snapshot.enemy_freeze);
    EXPECT_EQ(world.timer_wait, snapshot.timer_wait);
    EXPECT_EQ(world.living_count, snapshot.living_count);
    EXPECT_FLOAT_EQ(world.control_hp, snapshot.control_hp);
    EXPECT_EQ(world.withdraw_requested, snapshot.withdraw_requested);
    EXPECT_EQ(world.withdraw_level, snapshot.withdraw_level);
    EXPECT_EQ(world.guy_id_counter, snapshot.guy_id_counter);
    EXPECT_EQ(world.current_palette_id, snapshot.current_palette_id);
    EXPECT_EQ(world.pending_exit_prompt, snapshot.pending_exit_prompt);
    EXPECT_EQ(world.paused, snapshot.paused);
    EXPECT_EQ(world.pause_player_index, snapshot.pause_player_index);
    EXPECT_EQ(world.m_score[0], snapshot.m_score[0]);
    EXPECT_EQ(world.m_score[3], snapshot.m_score[3]);

    EXPECT_EQ(world.oblist.size(), snapshot.oblist.size());
    EXPECT_EQ(world.fxlist.size(), snapshot.fxlist.size());
    EXPECT_EQ(world.weaplist.size(), snapshot.weaplist.size());
    ASSERT_EQ(1u, snapshot.guy_snapshots.size());
    EXPECT_EQ(guy_id, snapshot.guy_snapshots.front().guy_id);
    EXPECT_EQ("Aldo", snapshot.guy_snapshots.front().name);
    EXPECT_EQ(1234u, snapshot.guy_snapshots.front().exp);
    EXPECT_FLOAT_EQ(5.5f, snapshot.guy_snapshots.front().scen_damage);

    const og::sim::EntitySnapshot* actor_snapshot =
        find_entity_snapshot(snapshot.oblist, actor->entity_id());
    const og::sim::EntitySnapshot* weapon_snapshot =
        find_entity_snapshot(snapshot.weaplist, weapon->entity_id());
    const og::sim::EntitySnapshot* fx_snapshot =
        find_entity_snapshot(snapshot.fxlist, fx_entity->entity_id());
    ASSERT_NE(nullptr, actor_snapshot);
    ASSERT_NE(nullptr, weapon_snapshot);
    ASSERT_NE(nullptr, fx_snapshot);

    EXPECT_EQ(actor->entity_id(), actor_snapshot->entity_id);
    EXPECT_EQ(guy_id, actor_snapshot->guy_id);
    EXPECT_EQ(foe->entity_id(), actor_snapshot->foe_id);
    EXPECT_EQ(leader->entity_id(), actor_snapshot->leader_id);
    EXPECT_EQ(owner->entity_id(), actor_snapshot->owner_id);
    EXPECT_EQ(collide->entity_id(), actor_snapshot->collide_ob_id);
    EXPECT_EQ(controller->entity_id(), actor_snapshot->controller_id);
    EXPECT_FLOAT_EQ(actor->worldx(), actor_snapshot->worldx);
    EXPECT_FLOAT_EQ(actor->worldy(), actor_snapshot->worldy);
    EXPECT_EQ(actor->path_check_counter, actor_snapshot->path_check_counter);
    EXPECT_EQ(actor->view_all, actor_snapshot->view_all);
    EXPECT_EQ(actor->stats()->special_cost[1], actor_snapshot->special_cost[1]);
    EXPECT_EQ(0, actor_snapshot->do_bounce);
    EXPECT_EQ(7, weapon_snapshot->do_bounce);
    EXPECT_EQ(0, fx_snapshot->do_bounce);

    EXPECT_NE(snapshot.removed_entity_ids.end(),
              std::find(snapshot.removed_entity_ids.begin(),
                        snapshot.removed_entity_ids.end(),
                        removed_id));
    EXPECT_TRUE(world.removed_entity_ids().empty());

    const auto mask_has_bit = [](const og::sim::EntitySnapshot& entity_snapshot,
                                 std::uint8_t bit) {
        return (entity_snapshot.dirty_mask[bit / 64] &
                (1ULL << (bit % 64))) != 0;
    };
    EXPECT_TRUE(mask_has_bit(*actor_snapshot, og::dirty::BIT_FOE_ID));
    EXPECT_TRUE(mask_has_bit(*actor_snapshot, og::dirty::BIT_LEADER_ID));
    EXPECT_TRUE(mask_has_bit(*actor_snapshot, og::dirty::BIT_OWNER_ID));
    EXPECT_TRUE(mask_has_bit(*actor_snapshot, og::dirty::BIT_COLLIDE_OB_ID));
    EXPECT_TRUE(mask_has_bit(*actor_snapshot, og::dirty::BIT_CONTROLLER_ID));

    EXPECT_EQ(0ULL, actor->dirty_mask_word(0));
    EXPECT_EQ(0ULL, actor->dirty_mask_word(1));
    EXPECT_EQ(0ULL, weapon->dirty_mask_word(0));
    EXPECT_EQ(0ULL, weapon->dirty_mask_word(1));
}

TEST(WorldSnapshot, keyframe_capture_marks_all_fields_and_sends_full_grid)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    world.entity_factory = [](Order order, std::int32_t family) -> std::unique_ptr<walker> {
        if (order == Order::Weapon)
        {
            auto entity = std::make_unique<SnapshotWeapon>();
            entity->order = order;
            entity->family = static_cast<char>(family);
            entity->sizex = 16;
            entity->sizey = 16;
            return entity;
        }

        auto entity = std::make_unique<SnapshotWalker>();
        entity->order = order;
        entity->family = static_cast<char>(family);
        entity->sizex = 16;
        entity->sizey = 16;
        return entity;
    };

    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->clear_dirty();

    const og::sim::WorldSnapshot snapshot = og::sim::capture_keyframe_snapshot(world);
    ASSERT_EQ(1u, snapshot.oblist.size());
    EXPECT_EQ(~0ULL, snapshot.oblist.front().dirty_mask[0]);
    EXPECT_EQ(~0ULL, snapshot.oblist.front().dirty_mask[1]);
    EXPECT_TRUE(snapshot.grid_dirty);
    EXPECT_TRUE(snapshot.grid_full_resend);
    EXPECT_EQ(static_cast<std::size_t>(world.grid.w) * world.grid.h,
              snapshot.full_grid_data.size());
    EXPECT_EQ(0ULL, actor->dirty_mask_word(0));
    EXPECT_EQ(0ULL, actor->dirty_mask_word(1));
}

TEST(WorldSnapshot, drain_sim_events_moves_events_out_of_the_log)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 99;
    log.push_sound(7);
    log.push_notification("snapshot");

    const og::sim::SimEventBatch batch = og::sim::drain_sim_events(log);

    EXPECT_EQ(99u, batch.sequence);
    ASSERT_EQ(2u, batch.events.size());
    EXPECT_EQ(og::sim::EventKind::PlaySound, batch.events[0].kind);
    EXPECT_EQ(og::sim::EventKind::Notification, batch.events[1].kind);
    EXPECT_TRUE(log.empty());
}

TEST(WorldSnapshot, capture_snapshot_collects_grid_damage_from_explosion)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();

    walker* explosion = world.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, explosion);

    explosion->set_owner(nullptr);
    explosion->setxy(4 * GRID_SIZE, 5 * GRID_SIZE);
    explosion->dead = 1;

    const short damage_x =
        static_cast<short>(explosion->xpos + (explosion->sizex / 2));
    const short damage_y =
        static_cast<short>(explosion->ypos + (explosion->sizey / 2));
    const short tile_x = static_cast<short>(damage_x / GRID_SIZE);
    const short tile_y = static_cast<short>(damage_y / GRID_SIZE);
    const std::size_t tile_index =
        static_cast<std::size_t>(tile_y) * world.grid.w + tile_x;
    world.grid.data[tile_index] = PIX_GRASS1;

    (void)explosion->death();

    const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(world);

    EXPECT_TRUE(snapshot.grid_dirty);
    EXPECT_FALSE(snapshot.grid_full_resend);
    ASSERT_EQ(1u, snapshot.grid_dirty_tiles.size());
    EXPECT_EQ(tile_x, snapshot.grid_dirty_tiles.front().x);
    EXPECT_EQ(tile_y, snapshot.grid_dirty_tiles.front().y);
    EXPECT_EQ(PIX_GRASS1_DAMAGED, snapshot.grid_dirty_tiles.front().value);
    EXPECT_TRUE(world.grid_dirty_tiles().empty());
}

TEST(WorldSnapshot, grid_dirty_overflow_falls_back_to_full_grid_send)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();

    int damaged = 0;
    for (short y = 0; y < world.grid.h && damaged <= static_cast<int>(og::sim::MAX_GRID_DIRTY_TILES); ++y)
    {
        for (short x = 0; x < world.grid.w && damaged <= static_cast<int>(og::sim::MAX_GRID_DIRTY_TILES); ++x)
        {
            const std::size_t tile_index =
                static_cast<std::size_t>(y) * world.grid.w + x;
            world.grid.data[tile_index] = PIX_GRASS1;
            world.damage_tile(static_cast<short>(x * GRID_SIZE),
                              static_cast<short>(y * GRID_SIZE));
            ++damaged;
        }
    }

    const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(world);

    EXPECT_TRUE(snapshot.grid_dirty);
    EXPECT_TRUE(snapshot.grid_full_resend);
    EXPECT_TRUE(snapshot.grid_dirty_tiles.empty());
    EXPECT_EQ(static_cast<std::size_t>(world.grid.w) * world.grid.h,
              snapshot.full_grid_data.size());
    EXPECT_TRUE(world.grid_dirty_tiles().empty());
}
