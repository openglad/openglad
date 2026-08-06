#include <gtest/gtest.h>

#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/world_snapshot.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "test_game_world_fixture.h"

namespace {

using DirtyMask =
    std::array<std::uint64_t, og::sim::kEntitySnapshotDirtyMaskWords>;
using SnapshotMap = std::unordered_map<std::uint32_t, og::sim::EntitySnapshot>;

void set_mask_bit(DirtyMask& mask, std::uint8_t bit)
{
    mask[bit / 64] |= (1ULL << (bit % 64));
}

DirtyMask full_dirty_mask()
{
    DirtyMask mask = {};
    for (std::uint8_t bit = 0; bit < og::dirty::FIELD_COUNT; ++bit)
        set_mask_bit(mask, bit);
    return mask;
}

DirtyMask snapshot_dirty_mask(const og::sim::EntitySnapshot& snapshot)
{
    return {snapshot.dirty_mask[0], snapshot.dirty_mask[1]};
}

bool mask_is_superset(const DirtyMask& actual, const DirtyMask& expected)
{
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        if ((actual[i] & expected[i]) != expected[i])
            return false;
    }
    return true;
}

std::string missing_bits_string(const DirtyMask& actual, const DirtyMask& expected)
{
    std::ostringstream out;
    bool first = true;
    for (std::uint8_t bit = 0; bit < og::dirty::FIELD_COUNT; ++bit)
    {
        const std::uint64_t bit_mask = 1ULL << (bit % 64);
        const bool expected_set = (expected[bit / 64] & bit_mask) != 0;
        const bool actual_set = (actual[bit / 64] & bit_mask) != 0;
        if (!expected_set || actual_set)
            continue;

        if (!first)
            out << ',';
        out << static_cast<int>(bit);
        first = false;
    }
    return out.str();
}

template <typename EntityList>
void append_snapshot_list(const EntityList& source, SnapshotMap& snapshots)
{
    for (const auto& snapshot : source)
    {
        const auto [it, inserted] =
            snapshots.emplace(snapshot.entity_id, snapshot);
        ASSERT_TRUE(inserted)
            << "duplicate snapshot entity_id " << snapshot.entity_id;
        (void)it;
    }
}

SnapshotMap build_snapshot_map(const og::sim::WorldSnapshot& snapshot)
{
    SnapshotMap snapshots;
    snapshots.reserve(snapshot.oblist.size() + snapshot.fxlist.size() +
                      snapshot.weaplist.size());
    append_snapshot_list(snapshot.oblist, snapshots);
    append_snapshot_list(snapshot.fxlist, snapshots);
    append_snapshot_list(snapshot.weaplist, snapshots);
    return snapshots;
}

DirtyMask compute_reference_mask(const og::sim::EntitySnapshot* previous,
                                 const og::sim::EntitySnapshot& current)
{
    if (previous == nullptr)
        return full_dirty_mask();

    DirtyMask mask = {};
    const auto* const previous_bytes =
        reinterpret_cast<const unsigned char*>(previous);
    const auto* const current_bytes =
        reinterpret_cast<const unsigned char*>(&current);

    for (const auto& field : og::sim::kEntitySnapshotFields)
    {
        if (std::memcmp(previous_bytes + field.snap_offset,
                        current_bytes + field.snap_offset,
                        field.size) != 0)
        {
            set_mask_bit(mask, field.bit_index);
        }
    }

    if (previous->regen_delay != current.regen_delay)
        set_mask_bit(mask, og::dirty::BIT_REGEN_DELAY);
    if (previous->do_bounce != current.do_bounce)
        set_mask_bit(mask, og::dirty::BIT_DO_BOUNCE);

    return mask;
}

living* find_live_actor(GameWorld& world,
                        const std::array<std::uint32_t, 4>& actor_ids,
                        std::size_t start_index)
{
    for (std::size_t offset = 0; offset < actor_ids.size(); ++offset)
    {
        const std::uint32_t actor_id =
            actor_ids[(start_index + offset) % actor_ids.size()];
        auto* actor = dynamic_cast<living*>(world.find_by_id(actor_id));
        if (actor != nullptr && !actor->dead())
            return actor;
    }
    return nullptr;
}

living* add_combatant(TestGameWorld& fx,
                      char family,
                      unsigned char team,
                      short x,
                      short y,
                      const char* name)
{
    auto* actor = dynamic_cast<living*>(fx.level.add_ob(Order::Living, family));
    if (actor == nullptr || actor->stats() == nullptr)
        return nullptr;

    actor->setxy(x, y);
    actor->set_team_num(team);
    actor->set_real_team_num(255);
    actor->set_dead(0);
    actor->set_user(-1);
    actor->set_act_type(ACT_RANDOM);
    actor->set_lineofsight(180);
    actor->set_stepsize(2.0f);
    actor->set_normal_stepsize(2.0f);
    actor->set_fire_frequency(1.0f);
    actor->set_damage(14.0f + static_cast<float>(team));
    actor->set_current_special(1);
    actor->set_weapons_left(8);

    actor->stats()->set_level(12);
    actor->stats()->set_max_hitpoints(160.0f);
    actor->stats()->set_hitpoints(160.0f);
    actor->stats()->set_max_magicpoints(400.0f);
    actor->stats()->set_magicpoints(400.0f);
    actor->stats()->set_magic_per_round(3.0f);
    actor->stats()->set_heal_per_round(1.5f);
    actor->stats()->set_max_heal_delay(4);
    actor->stats()->set_current_heal_delay(0);
    actor->stats()->set_max_magic_delay(4);
    actor->stats()->set_current_magic_delay(0);
    actor->stats()->set_armor(6.0f);
    actor->stats()->set_weapon_cost(1);
    for (int i = 0; i < NUM_SPECIALS; ++i)
        actor->stats()->set_special_cost(i, static_cast<std::uint16_t>(1 + i));

    auto guy_data = std::make_unique<guy>(family);
    guy_data->name = name;
    guy_data->strength = 80;
    guy_data->dexterity = 80;
    guy_data->constitution = 80;
    guy_data->intelligence = 90;
    actor->set_owned_myguy(std::move(guy_data));

    return actor;
}

void apply_scripted_setter_churn(GameWorld& world,
                                 const std::array<std::uint32_t, 4>& actor_ids,
                                 int tick)
{
    living* actor =
        find_live_actor(world, actor_ids, static_cast<std::size_t>(tick) %
                                            actor_ids.size());
    if (actor == nullptr || actor->stats() == nullptr)
        return;

    living* controller =
        find_live_actor(world, actor_ids,
                        (static_cast<std::size_t>(tick) + 1) % actor_ids.size());
    if (controller == nullptr)
        controller = actor;

    actor->set_bonus_rounds(static_cast<short>(tick % 3));
    actor->set_invulnerable_left(static_cast<short>(tick % 4));
    actor->set_invisibility_left(static_cast<short>((tick + 1) % 3));
    actor->set_flight_left(static_cast<short>(tick % 2));
    actor->set_view_all(static_cast<short>(tick % 5));
    actor->set_keys(static_cast<std::uint32_t>(tick * 17) + actor->entity_id());
    actor->set_speed_bonus(0.5f +
                           0.25f * static_cast<float>(tick % 4));
    actor->set_speed_bonus_left(2 + (tick % 6));
    actor->set_lineofsight(140 + (tick % 25));
    actor->set_path_check_counter(1 + (tick % 9));
    actor->set_regen_delay(tick % 7);
    actor->set_current_special(static_cast<char>(1 + (tick % 3)));
    actor->set_outline(static_cast<unsigned char>(tick % 4));
    actor->set_hurt_flash((tick % 2) == 0);
    actor->set_shifter_down(static_cast<short>(tick % 2));
    actor->set_yo_delay(static_cast<short>(tick % 6));
    actor->set_skip_exit(static_cast<short>(tick % 3));
    actor->set_weapons_left(static_cast<short>(1 + (tick % 4)));

    actor->stats()->set_max_heal_delay(3 + (tick % 5));
    actor->stats()->set_current_heal_delay(tick % 5);
    actor->stats()->set_max_magic_delay(4 + (tick % 6));
    actor->stats()->set_current_magic_delay(tick % 4);
    actor->stats()->set_magic_per_round(1.0f +
                                        0.5f * static_cast<float>(tick % 3));
    actor->stats()->set_heal_per_round(0.5f +
                                       0.25f * static_cast<float>(tick % 4));
    actor->stats()->set_weapon_cost(static_cast<short>(1 + (tick % 5)));
    actor->stats()->set_special_cost(
        (tick / 2) % NUM_SPECIALS,
        static_cast<std::uint16_t>(1 + (tick % 7)));
    actor->stats()->set_old_order((tick % 2) == 0 ? Order::Living
                                                   : Order::Weapon);
    actor->stats()->set_old_family((tick % 2) == 0 ? FAMILY_SOLDIER
                                                    : FAMILY_ROCK);
    actor->stats()->set_last_distance(
        static_cast<std::uint32_t>(tick * 13) + actor->entity_id());
    actor->stats()->set_current_distance(tick * 9);
    actor->stats()->set_controller(controller);
}

void manage_transient_entities(TestGameWorld& fx,
                               const std::array<std::uint32_t, 4>& actor_ids,
                               int tick,
                               std::vector<std::uint32_t>& transient_ids)
{
    GameWorld& world = fx.world();
    living* anchor =
        find_live_actor(world, actor_ids, static_cast<std::size_t>(tick) %
                                            actor_ids.size());
    const short anchor_x =
        (anchor != nullptr) ? anchor->xpos() : static_cast<short>(96);
    const short anchor_y =
        (anchor != nullptr) ? anchor->ypos() : static_cast<short>(96);
    const unsigned char anchor_team =
        (anchor != nullptr) ? anchor->team_num() : 0;

    if ((tick % 9) == 0)
    {
        auto* weapon = dynamic_cast<weap*>(
            fx.level.add_weap_ob(Order::Weapon, FAMILY_ROCK));
        ASSERT_NE(nullptr, weapon);
        weapon->setxy(static_cast<short>(anchor_x + 8), anchor_y);
        weapon->set_team_num(anchor_team);
        weapon->set_real_team_num(255);
        weapon->set_lastx((tick % 2) == 0 ? 1.0f : -1.0f);
        weapon->set_lasty((tick % 3) == 0 ? 1.0f : 0.0f);
        weapon->set_stepsize(1.0f);
        weapon->set_normal_stepsize(1.0f);
        weapon->set_do_bounce(tick % 5);
        weapon->set_owner(anchor);
        transient_ids.push_back(weapon->entity_id());
    }

    if ((tick % 14) == 0)
    {
        walker* flash = fx.level.add_fx_ob(Order::FX, FAMILY_FLASH);
        ASSERT_NE(nullptr, flash);
        flash->setxy(static_cast<short>(anchor_x - 8), anchor_y);
        flash->set_team_num(anchor_team);
        flash->set_real_team_num(255);
        transient_ids.push_back(flash->entity_id());
    }

    if (!transient_ids.empty() && (tick % 11) == 0)
    {
        const std::uint32_t remove_id = transient_ids.front();
        transient_ids.erase(transient_ids.begin());
        if (walker* entity = world.find_by_id(remove_id); entity != nullptr)
        {
            ASSERT_EQ(1, world.remove_ob(entity));
        }
    }
}

TEST(DirtyTrackingSafety,
     dirty_masks_are_a_superset_of_bruteforce_field_diffs_in_combat)
{
    constexpr int kTicks = 240;

    TestGameWorld fx;
    GameWorld& world = fx.world();
    world.rng_.state_ = 0xC0FFEEu;
    world.allied_mode = 0;

    living* mage = add_combatant(fx, FAMILY_MAGE, 0, 64, 72, "mage");
    living* cleric = add_combatant(fx, FAMILY_CLERIC, 0, 72, 120, "cleric");
    living* archer = add_combatant(fx, FAMILY_ARCHER, 1, 176, 72, "archer");
    living* thief = add_combatant(fx, FAMILY_THIEF, 1, 168, 120, "thief");

    ASSERT_NE(nullptr, mage);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, archer);
    ASSERT_NE(nullptr, thief);

    mage->set_foe(archer);
    cleric->set_foe(thief);
    archer->set_foe(mage);
    thief->set_foe(cleric);

    const std::array<std::uint32_t, 4> actor_ids = {
        mage->entity_id(),
        cleric->entity_id(),
        archer->entity_id(),
        thief->entity_id(),
    };

    SnapshotMap previous = build_snapshot_map(og::sim::capture_snapshot(world));
    std::vector<std::uint32_t> transient_ids;

    bool saw_new_entity = false;
    bool saw_removed_entity = false;
    bool saw_weapon_snapshot = false;
    bool saw_fx_snapshot = false;

    for (int tick = 0; tick < kTicks; ++tick)
    {
        world.tick();
        apply_scripted_setter_churn(world, actor_ids, tick);
        manage_transient_entities(fx, actor_ids, tick, transient_ids);

        const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(world);
        const SnapshotMap current = build_snapshot_map(snapshot);
        const std::unordered_set<std::uint32_t> removed_ids(
            snapshot.removed_entity_ids.begin(), snapshot.removed_entity_ids.end());

        saw_weapon_snapshot = saw_weapon_snapshot || !snapshot.weaplist.empty();
        saw_fx_snapshot = saw_fx_snapshot || !snapshot.fxlist.empty();
        saw_removed_entity = saw_removed_entity || !snapshot.removed_entity_ids.empty();

        for (const auto& [entity_id, current_entity] : current)
        {
            const auto previous_it = previous.find(entity_id);
            if (previous_it == previous.end())
                saw_new_entity = true;

            const og::sim::EntitySnapshot* const previous_entity =
                (previous_it != previous.end()) ? &previous_it->second : nullptr;
            const DirtyMask expected =
                compute_reference_mask(previous_entity, current_entity);
            const DirtyMask actual = snapshot_dirty_mask(current_entity);

            EXPECT_TRUE(mask_is_superset(actual, expected))
                << "tick=" << tick << " entity_id=" << entity_id
                << " missing_bits=" << missing_bits_string(actual, expected)
                << " actual_mask=[" << actual[0] << ',' << actual[1] << ']'
                << " expected_mask=[" << expected[0] << ',' << expected[1]
                << ']';
        }

        for (const auto& [entity_id, previous_entity] : previous)
        {
            (void)previous_entity;
            if (current.find(entity_id) != current.end())
                continue;

            saw_removed_entity = true;
            EXPECT_TRUE(removed_ids.contains(entity_id))
                << "tick=" << tick
                << " removed entity missing from removed_entity_ids: "
                << entity_id;
        }

        previous = current;
    }

    EXPECT_TRUE(saw_new_entity);
    EXPECT_TRUE(saw_removed_entity);
    EXPECT_TRUE(saw_weapon_snapshot);
    EXPECT_TRUE(saw_fx_snapshot);
}

} // namespace
