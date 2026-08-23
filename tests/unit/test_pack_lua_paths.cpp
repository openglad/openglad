/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Pack-Lua behaviour paths that no other test reaches (headless).
//
// packs/core/families/*.lua and packs/core/lib/*.lua are game logic: a branch nothing exercises there
// is untested game behaviour, not
// untested content. The Lua coverage recorder
// (OPENGLAD_LUA_COVERAGE, scripts/coverage/) named the branches below as
// never executed by the whole suite; each one is a rule a player can
// observe, so each gets a test that asserts the RULE, not merely that the
// line ran:
//
//   * a poison cloud damaging what it overlaps (the entire box-overlap
//     helper in effect_cloud.lua had never run once),
//   * the druid's circle of protection merging into a friend's existing
//     circle instead of stacking a second one,
//   * a summoned faerie refusing to appear inside a wall,
//   * a slime split carrying its name and its player character across,
//   * clamps and guards in the small weapon/treasure hooks.
//
// Every test dispatches through og::script::hooks — the same door the sim
// uses — and asserts no hook errored (an erroring hook is a silent no-op
// now that there is no C++ fallback behind it).

#include "../test_family_hook_dispatch.h"
#include "../test_family_lookup.h"
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/resources/game_mode.h>
#include <openglad/resources/progression.h>
#include <openglad/resources/save_data.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

// Every helper below runs against a real GameWorld: the hooks call
// og.add_ob / og.find_foes_in_range / og.query_passable, which are world
// operations, so a bare `living` on the stack is not enough.
walker* spawn(GameWorld& w, Order order, int family, int tile_x, int tile_y,
              unsigned char team)
{
    walker* ob = order == Order::FX ? w.add_fx_ob(order, family)
                                    : w.add_ob(order, family);
    if (ob == nullptr)
        return nullptr;
    ob->setxy(tile_x * GRID_SIZE, tile_y * GRID_SIZE);
    ob->set_team_num(team);
    ob->set_real_team_num(team);
    return ob;
}

// A world keeps three entity lists and the mapping from Order to list is
// NOT one-to-one — add_weap_ob(Order::FX, ...) files an effect in weaplist,
// which is exactly the sort of thing a test must not assume. Scan all three
// and select on the entity's own order.
std::vector<walker*> entities_of(GameWorld& w, Order order)
{
    std::vector<walker*> out;
    for (const GameWorld::EntityList* list : {&w.oblist, &w.fxlist,
                                              &w.weaplist}) {
        for (const auto& ob : *list) {
            if (ob != nullptr && ob->dead() == 0 &&
                ob->query_order() == order)
                out.push_back(ob.get());
        }
    }
    return out;
}

std::size_t count_family(GameWorld& w, Order order, int family)
{
    std::size_t n = 0;
    for (walker* ob : entities_of(w, order)) {
        if (ob->family() == static_cast<char>(family))
            n++;
    }
    return n;
}

// The first live entity of an order/family, or nullptr.
walker* find_family(GameWorld& w, Order order, int family)
{
    for (walker* ob : entities_of(w, order)) {
        if (ob->family() == static_cast<char>(family))
            return ob;
    }
    return nullptr;
}

// Notifications are sim events, so a hook whose only observable effect is a
// message still has an assertable effect. Counting substring matches (rather
// than asserting "some notification happened") is what makes the assertion
// name the rule instead of the mechanism.
int count_notifications(const og::sim::SimEventLog& log, const char* needle)
{
    int n = 0;
    for (const auto& ev : log.events()) {
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find(needle) != std::string::npos)
            n++;
    }
    return n;
}

// Who the first notification carrying `needle` was addressed to: a global
// player index, -1 for a broadcast, or -2 when no such line was emitted at
// all (so a silent hook cannot be mistaken for a broadcast).
std::int32_t notification_target(const og::sim::SimEventLog& log,
                                 const char*                 needle)
{
    for (const auto& ev : log.events()) {
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find(needle) != std::string::npos)
            return ev.target_player;
    }
    return -2;
}

}  // namespace

// ---------------------------------------------------------------------------
// core:cloud — the overlap test and the damage loop
// ---------------------------------------------------------------------------

// A poison cloud is supposed to hurt every FOE whose box it overlaps, and
// nobody else. Before this test the whole hits() helper in
// effect_cloud.lua had never executed: every cloud in the suite drifted
// over empty ground, so "the cloud damages what it covers" — the entire
// point of the family — was unverified.
TEST(PackLuaCloud, a_cloud_damages_the_foe_it_covers_and_spares_its_own_team)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();

    walker* caster = spawn(w, Order::Living, FAMILY_DRUID, 10, 10, 1);
    ASSERT_NE(nullptr, caster);
    walker* cloud = spawn(w, Order::FX, FAMILY_CLOUD, 10, 10, 1);
    ASSERT_NE(nullptr, cloud);
    cloud->set_owner(caster);
    cloud->set_lifetime(30);
    cloud->set_damage(9.0f);

    // Directly under the cloud: must be hit.
    walker* covered = spawn(w, Order::Living, FAMILY_ORC, 10, 10, 2);
    ASSERT_NE(nullptr, covered);
    covered->stats()->set_max_hitpoints(500.0f);
    covered->stats()->set_hitpoints(500.0f);
    // Same tile, same team as the cloud: must NOT be hit.
    walker* friendly = spawn(w, Order::Living, FAMILY_ORC, 10, 10, 1);
    ASSERT_NE(nullptr, friendly);
    friendly->stats()->set_max_hitpoints(500.0f);
    friendly->stats()->set_hitpoints(500.0f);

    const float covered_before = covered->stats()->hitpoints();
    const float friendly_before = friendly->stats()->hitpoints();

    ASSERT_TRUE(cloud->act());

    EXPECT_LT(covered->stats()->hitpoints(), covered_before)
        << "a cloud must damage the foe standing in it";
    EXPECT_EQ(friendly->stats()->hitpoints(), friendly_before)
        << "a cloud must not damage its own team";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The other side of the same helper: a foe the search radius returns but
// whose box does not overlap the cloud takes nothing. hits() is what
// separates "near the cloud" from "in the cloud".
//
// The geometry is tight on purpose. find_foes_in_range uses a MANHATTAN
// radius equal to the cloud's own sizex, so a clearance that gets a foe out
// of the box without also getting it out of the radius only exists on the
// axes where the relevant sprite extent is smaller than that radius. Each
// axis is therefore attempted and skipped when the sprites make it
// impossible, rather than asserted blindly — a test that placed the foe out
// of RANGE would pass without ever consulting hits() at all.
TEST(PackLuaCloud, a_foe_in_range_but_outside_the_cloud_box_is_unharmed)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();

    walker* caster = spawn(w, Order::Living, FAMILY_DRUID, 10, 10, 1);
    ASSERT_NE(nullptr, caster);
    walker* cloud = spawn(w, Order::FX, FAMILY_CLOUD, 10, 10, 1);
    ASSERT_NE(nullptr, cloud);
    cloud->set_owner(caster);
    cloud->set_damage(9.0f);

    walker* foe = spawn(w, Order::Living, FAMILY_ORC, 10, 10, 2);
    ASSERT_NE(nullptr, foe);

    const std::int32_t radius = cloud->sizex();
    const float base_x = static_cast<float>(cloud->xpos());
    const float base_y = static_cast<float>(cloud->ypos());

    // Clearance needed on each side, and the axis it escapes along.
    const struct { const char* side; std::int32_t dx; std::int32_t dy; } cases[] = {
        {"left",  -(foe->sizex() + 1), 0},
        {"up",    0, -(foe->sizey() + 1)},
        {"down",  0, cloud->sizey() + 1},
        {"right", cloud->sizex() + 1, 0},
    };

    int exercised = 0;
    for (const auto& c : cases) {
        if (std::abs(c.dx) + std::abs(c.dy) > radius)
            continue;  // no placement is both in range and out of the box
        exercised++;
        foe->stats()->set_max_hitpoints(500.0f);
        foe->stats()->set_hitpoints(500.0f);
        foe->setxy(base_x + static_cast<float>(c.dx),
                   base_y + static_cast<float>(c.dy));
        ASSERT_LE(foe->distance_to_ob(cloud), radius)
            << c.side << ": the placement must stay inside the search radius, "
               "or hits() is never consulted";
        cloud->set_lifetime(30);
        ASSERT_TRUE(cloud->act());
        EXPECT_EQ(500.0f, foe->stats()->hitpoints())
            << c.side << ": a foe clear of the cloud's box takes nothing";
    }
    EXPECT_GT(exercised, 0)
        << "no side of the box was reachable inside the search radius";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A cloud past its lifetime dies instead of drifting for another tick.
TEST(PackLuaCloud, an_expired_cloud_dies_on_the_next_act)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* cloud = spawn(w, Order::FX, FAMILY_CLOUD, 10, 10, 1);
    ASSERT_NE(nullptr, cloud);
    cloud->set_lifetime(0);

    (void)cloud->act();
    EXPECT_NE(0, cloud->dead()) << "lifetime 0 must end the cloud";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:druid — circle of protection, faerie placement
// ---------------------------------------------------------------------------

// The circle of protection: one circle per friend in range, and a re-cast
// refreshes that circle rather than stacking another.
//
// druid.lua has always carried a merge branch meant to fold a re-cast into
// a friend's existing circle. It never ran: the scan walked og.oblist() —
// the LIVING list — while both creation paths (og.summon / og.add_ob with
// Order::Weapon) file the circle in weaplist. The historic C++ had the same
// list mismatch, so from the 2002 import until 2026 every recast minted a
// second circle. The scan now searches weaplist, which is what the branch
// always meant, so the observable rule below is REFRESH, not stack.
TEST(PackLuaDruid, each_circle_of_protection_cast_shields_every_friend_in_range)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_DRUID);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();

    walker* druid = spawn(w, Order::Living, FAMILY_DRUID, 10, 10, 1);
    ASSERT_NE(nullptr, druid);
    druid->stats()->set_level(5);
    druid->set_fire_frequency(1.0f);
    walker* friend_a = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 1);
    ASSERT_NE(nullptr, friend_a);
    walker* friend_b = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 11, 1);
    ASSERT_NE(nullptr, friend_b);

    druid->set_current_special(4);
    druid->set_busy(0);
    ASSERT_TRUE(og::test::do_special(desc, druid));
    const std::size_t after_first =
        count_family(w, Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    EXPECT_EQ(2u, after_first) << "one circle per protected friend";

    float shield_before = 0.0f;
    for (walker* ob : entities_of(w, Order::Weapon)) {
        if (ob->family() == static_cast<char>(FAMILY_CIRCLE_PROTECTION))
            shield_before += ob->stats()->hitpoints();
    }
    ASSERT_GT(shield_before, 0.0f);

    druid->set_busy(0);
    ASSERT_TRUE(og::test::do_special(desc, druid));

    std::size_t live_circles = 0;
    float shield_after = 0.0f;
    for (walker* ob : entities_of(w, Order::Weapon)) {
        if (ob->family() != static_cast<char>(FAMILY_CIRCLE_PROTECTION))
            continue;
        if (ob->dead() != 0)
            continue;
        ++live_circles;
        shield_after += ob->stats()->hitpoints();
    }
    EXPECT_EQ(after_first, live_circles)
        << "a re-cast tops up the friend's existing circle; it must not mint "
           "a second live one";
    EXPECT_GT(shield_after, shield_before)
        << "the merged circle absorbs the fresh one's strength";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A druid alone has nobody to protect: the cast must fail rather than
// spend the caster's magic on an empty crowd.
TEST(PackLuaDruid, a_lone_druid_cannot_cast_circle_of_protection)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_DRUID);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    walker* druid = spawn(tw.world(), Order::Living, FAMILY_DRUID, 10, 10, 1);
    ASSERT_NE(nullptr, druid);
    druid->stats()->set_level(5);
    druid->set_current_special(4);
    druid->set_busy(0);

    EXPECT_FALSE(og::test::do_special(desc, druid));
    EXPECT_EQ(0u, count_family(tw.world(), Order::Weapon,
                               FAMILY_CIRCLE_PROTECTION));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:slime — the split
// ---------------------------------------------------------------------------

// A named slime hands its identity across the split, and a slime that IS a
// player character moves that character onto the offspring rather than
// dropping it (which would silently delete a hired man on death).
TEST(PackLuaSlime, a_split_carries_the_name_and_the_player_character_across)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_SLIME);
    ASSERT_TRUE(og::test::has_on_death(desc));
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* slime = spawn(w, Order::Living, FAMILY_SLIME, 10, 10, 2);
    ASSERT_NE(nullptr, slime);
    slime->stats()->set_level(3);
    slime->stats()->name = "BLOBBY";
    slime->stats()->set_max_hitpoints(80.0f);
    slime->stats()->set_hitpoints(1.0f);

    auto character = std::make_unique<guy>(FAMILY_SLIME);
    character->teamnum = 2;
    slime->set_owned_myguy(std::move(character));
    ASSERT_NE(nullptr, slime->myguy);

    ASSERT_TRUE(og::test::on_death(desc, slime));

    EXPECT_EQ(1u, count_family(w, Order::Living, FAMILY_MEDIUM_SLIME))
        << "a dying slime leaves one offspring";
    EXPECT_EQ(nullptr, slime->myguy)
        << "the player character must move to the offspring, not vanish";
    walker* child = find_family(w, Order::Living, FAMILY_MEDIUM_SLIME);
    ASSERT_NE(nullptr, child);
    EXPECT_NE(nullptr, child->myguy)
        << "the offspring inherits the character";
    // Pinned deliberately: the C++ this was transliterated from copies the
    // name the WRONG way (the dying blob takes the offspring's fresh name).
    // The behaviour is load-bearing for save/parity compatibility, so the
    // test documents it rather than asserting the intuitive direction.
    EXPECT_EQ(std::string(child->stats()->name),
              std::string(slime->stats()->name))
        << "the dying slime adopts the offspring's name (sic, from the C++)";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The split animation hook must ignore every animation that is not the
// split, or a slime would fission on any ordinary animation wrap.
TEST(PackLuaSlime, the_split_hook_ignores_other_animations)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_SLIME);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* slime = spawn(w, Order::Living, FAMILY_SLIME, 10, 10, 2);
    ASSERT_NE(nullptr, slime);
    slime->set_ani_type(ANI_WALK);

    const std::optional<bool> handled =
        og::script::hooks::on_ani_complete(&desc, slime);
    ASSERT_TRUE(handled.has_value()) << "the hook must be registered";
    EXPECT_FALSE(*handled) << "a walk cycle is not a split";
    EXPECT_EQ(0u, count_family(w, Order::Living, FAMILY_SMALL_SLIME));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:cleric — the whole kit
// ---------------------------------------------------------------------------
//
// Heal, raise skeleton, raise ghost and resurrect had all never completed
// SUCCESSFULLY anywhere in the suite: every existing cleric test stopped at
// a refusal (busy, no target, not enough intelligence). The tests below take
// each one to the end.

namespace {

// A bloodstain is the corpse marker the raise/resurrect specials look for:
// an Order::Treasure FAMILY_STAIN entity filed in the effects list.
//
// The specials only fire on a stain that is both within `max_distance`
// (Manhattan) AND on ground the raised body could stand on, and the caster's
// own body blocks the tile it is standing on. Search outward for the first
// spot that satisfies both rather than hard-coding an offset that depends on
// sprite sizes.
walker* drop_bloodstain(GameWorld& w, walker* caster, unsigned char team,
                        char old_family, std::int32_t max_distance)
{
    walker* stain = w.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (stain == nullptr)
        return nullptr;
    stain->set_team_num(team);
    stain->set_real_team_num(team);
    stain->stats()->set_old_family(old_family);

    for (std::int32_t d = 1; d < max_distance; d++) {
        const float x = static_cast<float>(caster->xpos() + d);
        const float y = static_cast<float>(caster->ypos());
        stain->setxy(x, y);
        if (caster->distance_to_ob(stain) < max_distance &&
            w.query_passable(x, y, stain))
            return stain;
    }
    ADD_FAILURE() << "no passable bloodstain spot within " << max_distance;
    return nullptr;
}

walker* make_cleric(GameWorld& w, short level)
{
    walker* cleric = spawn(w, Order::Living, FAMILY_CLERIC, 10, 10, 1);
    if (cleric == nullptr)
        return nullptr;
    cleric->stats()->set_level(level);
    cleric->stats()->set_max_magicpoints(9000.0f);
    cleric->stats()->set_magicpoints(9000.0f);
    cleric->set_busy(0);
    cleric->set_shifter_down(0);
    return cleric;
}

}  // namespace

TEST(PackLuaCleric, a_heal_restores_a_wounded_friend_and_spends_magic)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* cleric = make_cleric(w, 5);
    ASSERT_NE(nullptr, cleric);
    walker* hurt = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 1);
    ASSERT_NE(nullptr, hurt);
    hurt->stats()->set_max_hitpoints(200.0f);
    hurt->stats()->set_hitpoints(20.0f);

    cleric->set_current_special(1);
    const float mp_before = cleric->stats()->magicpoints();

    ASSERT_TRUE(og::test::do_special(desc, cleric)) << "the heal must land";
    EXPECT_GT(hurt->stats()->hitpoints(), 20.0f)
        << "the wounded friend is healed";
    EXPECT_LT(cleric->stats()->magicpoints(), mp_before)
        << "and the heal is paid for";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The magic-pool shortfall branch: a cleric whose remaining magic is under
// the heal's cost heals a reduced amount instead of going negative.
TEST(PackLuaCleric, a_heal_beyond_the_magic_pool_is_scaled_down_not_refused)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* cleric = make_cleric(w, 20);
    ASSERT_NE(nullptr, cleric);
    walker* hurt = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 1);
    ASSERT_NE(nullptr, hurt);
    hurt->stats()->set_max_hitpoints(900.0f);
    hurt->stats()->set_hitpoints(1.0f);

    // Enough to cast, not enough to pay the full heal.
    cleric->stats()->set_magicpoints(
        cleric->stats()->special_cost(1) + 1.0f);
    cleric->set_current_special(1);

    (void)og::test::do_special(desc, cleric);
    EXPECT_GE(cleric->stats()->magicpoints(), 0.0f)
        << "a shortfall must not push the pool negative";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(PackLuaCleric, a_skeleton_rises_from_a_bloodstain_in_reach)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    // Level 0 also drives the rand_level() guard: og.rand errors on a
    // non-positive bound, so the script must not call it at level 0.
    walker* cleric = make_cleric(w, 0);
    ASSERT_NE(nullptr, cleric);
    walker* stain = drop_bloodstain(w, cleric, 2, FAMILY_SOLDIER, 60);
    ASSERT_NE(nullptr, stain);

    cleric->set_current_special(2);
    ASSERT_TRUE(og::test::do_special(desc, cleric));

    EXPECT_EQ(1u, count_family(w, Order::Living, FAMILY_SKELETON))
        << "the bloodstain must yield a skeleton";
    EXPECT_NE(0, stain->dead()) << "and the bloodstain is consumed";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(PackLuaCleric, a_ghost_rises_from_a_bloodstain_in_reach)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* cleric = make_cleric(w, 6);
    ASSERT_NE(nullptr, cleric);
    walker* stain = drop_bloodstain(w, cleric, 2, FAMILY_SOLDIER, 30);
    ASSERT_NE(nullptr, stain);

    cleric->set_current_special(3);
    ASSERT_TRUE(og::test::do_special(desc, cleric));

    EXPECT_EQ(1u, count_family(w, Order::Living, FAMILY_GHOST))
        << "the bloodstain must yield a ghost";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Resurrecting a FRIENDLY corpse brings its own family back at half health;
// the ghost path above is what an enemy corpse gets instead.
TEST(PackLuaCleric, resurrecting_a_friendly_corpse_restores_its_family)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* cleric = make_cleric(w, 6);
    ASSERT_NE(nullptr, cleric);
    // Same team as the cleric, and it died as an archer.
    walker* stain = drop_bloodstain(w, cleric, 1, FAMILY_ARCHER, 30);
    ASSERT_NE(nullptr, stain);
    stain->stats()->set_max_hitpoints(120.0f);

    cleric->set_current_special(4);
    ASSERT_TRUE(og::test::do_special(desc, cleric));

    walker* raised = find_family(w, Order::Living, FAMILY_ARCHER);
    ASSERT_NE(nullptr, raised) << "the corpse returns as what it was";
    EXPECT_EQ(1, raised->team_num()) << "on the team it died on";
    EXPECT_GT(raised->stats()->hitpoints(), 0.0f);
    EXPECT_LE(raised->stats()->hitpoints(),
              raised->stats()->max_hitpoints() / 2.0f + 0.01f)
        << "resurrection returns a man at half health";
    EXPECT_NE(0, stain->dead());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Permadeath removes a deployed character only when they finish the level
// dead. RESURRECT transfers the character record from the real bloodstain to
// a new living body, so the ordinary local win fold must put that same person
// back in the company roster. This deliberately uses no respawn or network
// ownership state.
TEST(PackLuaCleric,
     a_resurrected_character_returns_to_the_roster_with_permadeath_on)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& world = tw.world();
    SaveData& save = tw.save;
    save.reset();
    save.keep_fallen_heroes = 0;
    world.keep_fallen_heroes = 0;
    save.current_campaign = "gladiator";
    save.scen_num = 1;

    guy original(FAMILY_SOLDIER);
    original.id = 73;
    original.name = "RETURNING HERO";
    original.exp = 100u;
    original.deployed = true;
    save.team_list[0] = std::make_unique<guy>(original);
    save.team_size = 1;

    walker* cleric = make_cleric(world, 10);
    ASSERT_NE(nullptr, cleric);
    walker* fallen = spawn(
        world, Order::Living, FAMILY_SOLDIER, 11, 10, 1);
    ASSERT_NE(nullptr, fallen);
    fallen->set_owned_myguy(std::make_unique<guy>(original));
    fallen->set_real_team_num(255);
    constexpr std::uint32_t kSessionExp = 777u;
    fallen->myguy->exp = kSessionExp;

    fallen->set_dead(1);
    ASSERT_TRUE(fallen->death());
    walker* stain = find_family(world, Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, stain) << "a roster character must leave a bloodstain";
    ASSERT_NE(nullptr, stain->myguy)
        << "the real death path must put the character record on the stain";
    EXPECT_EQ(original.id, stain->myguy->id);
    EXPECT_EQ(original.name, stain->myguy->name);
    EXPECT_EQ(kSessionExp, stain->myguy->exp);

    cleric->set_current_special(4);
    ASSERT_TRUE(og::test::do_special(desc, cleric));

    walker* resurrected = find_family(world, Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, resurrected);
    ASSERT_NE(fallen, resurrected);
    ASSERT_NE(nullptr, resurrected->myguy);
    EXPECT_EQ(0, resurrected->dead());
    EXPECT_EQ(original.id, resurrected->myguy->id);
    EXPECT_EQ(original.name, resurrected->myguy->name);
    EXPECT_EQ(kSessionExp, resurrected->myguy->exp);
    EXPECT_NE(0, fallen->dead());

    int dead_copies = 0;
    int live_copies = 0;
    for (const auto& ob : world.oblist)
    {
        if (ob == nullptr || ob->myguy == nullptr ||
            ob->myguy->id != original.id)
        {
            continue;
        }
        if (ob->dead())
            ++dead_copies;
        else
            ++live_copies;
    }
    EXPECT_EQ(1, dead_copies);
    EXPECT_EQ(1, live_copies);

    og::progression::WinFoldContext fold;
    fold.finished_level = save.scen_num;
    fold.outcome.next_level = -1;
    og::progression::apply_win_fold(
        save, world, fold, og::mode::classic_progression());

    ASSERT_EQ(1, static_cast<int>(save.team_size))
        << "permadeath must drop the dead body and retain its live resurrection";
    ASSERT_NE(nullptr, save.team_list[0]);
    EXPECT_EQ(original.id, save.team_list[0]->id);
    EXPECT_EQ(original.name, save.team_list[0]->name);
    EXPECT_EQ(original.family, save.team_list[0]->family);
    EXPECT_EQ(kSessionExp, save.team_list[0]->exp)
        << "the live resurrection, not the stale pre-level entry, was folded";
    EXPECT_TRUE(save.team_list[0]->deployed);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:archmage — the wounded-boss response chain
// ---------------------------------------------------------------------------

namespace {

// The world RNG is the LCG og.rand draws from. Find a seed whose FIRST
// rand(n) draw is `want`, so a test can steer a coin flip inside a hook
// without duplicating the generator's formula.
std::uint32_t seed_whose_first_draw_is(std::int32_t n, std::uint32_t want)
{
    for (std::uint32_t seed = 0; seed < 4096u; seed++) {
        og::sim::SimRandom probe(seed);
        if (probe.next(static_cast<std::uint32_t>(n)) == want)
            return seed;
    }
    ADD_FAILURE() << "no seed produced the wanted first draw";
    return 0;
}

// An archmage that has been hit, healthy enough not to teleport out, with a
// foe in reach and the magic to answer. Returns the archmage.
walker* wounded_archmage_setup(GameWorld& w, walker** foe_out)
{
    walker* mage = spawn(w, Order::Living, FAMILY_ARCHMAGE, 10, 10, 1);
    if (mage == nullptr)
        return nullptr;
    // Level 4 puts specials 0..2 in reach and leaves 3 out of it, which is
    // what routes the response into the heartburst/chain-lightning arm.
    mage->stats()->set_level(4);
    mage->stats()->set_max_hitpoints(400.0f);
    mage->stats()->set_hitpoints(400.0f);  // above the panic threshold
    mage->stats()->set_max_magicpoints(9000.0f);
    mage->stats()->set_magicpoints(9000.0f);
    mage->set_busy(0);

    walker* foe = spawn(w, Order::Living, FAMILY_ORC, 10, 10, 2);
    if (foe == nullptr)
        return nullptr;
    // Inside the heartburst radius (80 + 2*level) as well as the chain's.
    foe->setxy(static_cast<float>(mage->xpos() + 30),
               static_cast<float>(mage->ypos()));
    foe->stats()->set_max_hitpoints(900.0f);
    foe->stats()->set_hitpoints(900.0f);
    *foe_out = foe;
    return mage;
}

}  // namespace

// Hitting an archmage that is still healthy makes it answer with its area
// attack. Which of the two arms it picks is a coin flip inside the hook, and
// both arms were unreachable from the suite before this: the whole
// possible[2] block of archmage.lua had never executed.
TEST(PackLuaArchmage, a_struck_archmage_answers_with_chain_lightning)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_ARCHMAGE);
    ASSERT_TRUE(og::test::has_hit_response(desc));
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* foe = nullptr;
    walker* mage = wounded_archmage_setup(w, &foe);
    ASSERT_NE(nullptr, mage);
    ASSERT_NE(nullptr, foe);

    // Coin flip non-zero: the shifter-down (chain lightning) arm.
    w.rng_ = og::sim::SimRandom(seed_whose_first_draw_is(2, 1));

    og::test::hit_response(desc, mage->stats(), foe);

    EXPECT_EQ(1u, count_family(w, Order::FX, FAMILY_CHAIN))
        << "the chain-lightning arm must launch a bolt";
    EXPECT_EQ(foe, mage->foe()) << "being hit acquires the attacker as foe";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(PackLuaArchmage, a_struck_archmage_answers_with_heartburst)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_ARCHMAGE);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* foe = nullptr;
    walker* mage = wounded_archmage_setup(w, &foe);
    ASSERT_NE(nullptr, mage);
    ASSERT_NE(nullptr, foe);

    // Coin flip zero: falls through to the shifter-up (heartburst) arm.
    w.rng_ = og::sim::SimRandom(seed_whose_first_draw_is(2, 0));
    const float mp_before = mage->stats()->magicpoints();

    og::test::hit_response(desc, mage->stats(), foe);

    EXPECT_GE(count_family(w, Order::FX, FAMILY_EXPLOSION), 1u)
        << "the heartburst arm must burst on the acquired foe";
    EXPECT_LT(mage->stats()->magicpoints(), mp_before)
        << "the burst is paid for out of the mage's magic pool";
    EXPECT_EQ(0, mage->shifter_down())
        << "the heartburst arm leaves the shifter released";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// Small living hooks: fall-through and clamp branches
// ---------------------------------------------------------------------------

// A soldier's charge succeeds when the way ahead is clear. Every existing
// test hit the blocked case, so the successful charge — the special's whole
// purpose — was unexercised.
TEST(PackLuaSoldier, an_unblocked_charge_queues_a_rush_command)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_SOLDIER);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    walker* soldier =
        spawn(tw.world(), Order::Living, FAMILY_SOLDIER, 10, 10, 1);
    ASSERT_NE(nullptr, soldier);
    soldier->set_current_special(1);
    soldier->set_lastx(1.0f);
    soldier->set_lasty(0.0f);
    soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));

    EXPECT_TRUE(og::test::do_special(desc, soldier))
        << "an unblocked charge must succeed";
    EXPECT_TRUE(soldier->stats()->has_commands())
        << "the charge queues a rush";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The thief's bomb AI refuses only inside a band of ranges: too close (the
// blast would catch the thief) and far away are both fine. Only the refusal
// was covered.
TEST(PackLuaThief, the_bomb_ai_allows_a_foe_outside_the_refusal_band)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_THIEF);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* thief = spawn(w, Order::Living, FAMILY_THIEF, 10, 10, 1);
    ASSERT_NE(nullptr, thief);
    walker* foe = spawn(w, Order::Living, FAMILY_ORC, 10, 10, 2);
    ASSERT_NE(nullptr, foe);
    thief->set_current_special(1);
    thief->set_foe(foe);

    // Point blank (distance <= 35): below the band, so the bomb is allowed.
    foe->setxy(static_cast<float>(thief->xpos() + 4),
               static_cast<float>(thief->ypos()));
    EXPECT_TRUE(og::test::check_special_ai(desc, static_cast<living*>(thief)));

    // Inside the band: refused.
    foe->setxy(static_cast<float>(thief->xpos() + 60),
               static_cast<float>(thief->ypos()));
    EXPECT_FALSE(og::test::check_special_ai(desc, static_cast<living*>(thief)));

    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A level-0 barbarian's derived step size computes to zero. The floor at 1
// is what stops a summoned weapon from standing still forever.
TEST(PackLuaBarbarian, a_zero_step_summon_is_floored_to_one)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_BARBARIAN);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* barb = spawn(w, Order::Living, FAMILY_BARBARIAN, 10, 10, 1);
    ASSERT_NE(nullptr, barb);
    barb->stats()->set_level(0);   // 0 * 2.0f == 0 step size
    barb->stats()->set_magicpoints(500.0f);
    barb->set_current_special(1);
    barb->set_busy(0);
    barb->set_lastx(1.0f);
    barb->set_lasty(0.0f);

    if (!og::test::do_special(desc, barb)) {
        GTEST_SKIP() << "the barbarian special did not fire in this world";
    }
    bool saw_summon = false;
    for (walker* ob : entities_of(w, Order::Weapon)) {
        if (ob->owner() == barb &&
            ob->family() == static_cast<char>(FAMILY_BOULDER)) {
            saw_summon = true;
            EXPECT_GE(ob->stepsize(), 1.0f)
                << "a summon must never be given a zero step size";
        }
    }
    EXPECT_TRUE(saw_summon) << "the barbarian special must throw a boulder";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The fire elemental drains its owner to keep itself alive. An owner too
// weak to pay burns the elemental's lifetime down instead — otherwise a
// summon would be immortal beside a dying master.
TEST(PackLuaFireElemental, an_owner_too_weak_to_pay_costs_the_summon_lifetime)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_FIREELEMENTAL);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* owner = spawn(w, Order::Living, FAMILY_MAGE, 10, 10, 1);
    ASSERT_NE(nullptr, owner);
    owner->stats()->set_max_hitpoints(90.0f);
    owner->stats()->set_hitpoints(5.0f);   // below max/3: cannot pay
    owner->stats()->set_magicpoints(0.0f);

    walker* elemental =
        spawn(w, Order::Living, FAMILY_FIREELEMENTAL, 10, 10, 1);
    ASSERT_NE(nullptr, elemental);
    elemental->set_owner(owner);
    elemental->set_lifetime(40);
    elemental->stats()->set_max_hitpoints(60.0f);
    elemental->stats()->set_hitpoints(10.0f);

    const std::int32_t lifetime_before = elemental->lifetime();
    const float owner_hp_before = owner->stats()->hitpoints();
    og::test::on_act_living(desc, static_cast<living*>(elemental));

    EXPECT_LT(static_cast<std::int32_t>(elemental->lifetime()), lifetime_before)
        << "an unpayable drain must cost the summon lifetime";
    EXPECT_EQ(owner->stats()->hitpoints(), owner_hp_before)
        << "a too-weak owner must not be drained below the floor";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// Treasure / weapon hooks: boundary values
// ---------------------------------------------------------------------------

// A life gem banks a dead character's hitpoints for its own team to claim.
// The banked figure is clamped at zero on pickup, so a gem whose stored
// value went negative cannot SUBTRACT from the team's score — and a gem
// belonging to another team awards nothing at all.
TEST(PackLuaTreasure, a_life_gem_never_awards_negative_score)
{
    og::test::mount_core_pack();
    const TreasureFamilyDescriptor* tfd =
        get_treasure_family_descriptor(FAMILY_LIFE_GEM);
    ASSERT_NE(nullptr, tfd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* eater = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 0);
    ASSERT_NE(nullptr, eater);

    // Same team, negative bank: clamped to zero, not subtracted.
    walker* gem = spawn(w, Order::Treasure, FAMILY_LIFE_GEM, 10, 10, 0);
    ASSERT_NE(nullptr, gem);
    gem->stats()->set_hitpoints(-50.0f);
    const std::int32_t before = static_cast<std::int32_t>(w.m_score[0]);
    (void)og::test::on_eat(*tfd, static_cast<treasure*>(gem), eater);
    EXPECT_EQ(before, w.m_score[0])
        << "a negative bank must not cost the team score";

    // A healthy gem does pay out, so the clamp above is not just a no-op
    // path that never awards anything.
    walker* good = spawn(w, Order::Treasure, FAMILY_LIFE_GEM, 10, 10, 0);
    ASSERT_NE(nullptr, good);
    good->stats()->set_hitpoints(120.0f);
    (void)og::test::on_eat(*tfd, static_cast<treasure*>(good), eater);
    EXPECT_GT(w.m_score[0], before) << "a real gem pays out";

    // Another team's gem pays nobody.
    const std::int32_t mid = static_cast<std::int32_t>(w.m_score[0]);
    walker* theirs = spawn(w, Order::Treasure, FAMILY_LIFE_GEM, 10, 10, 3);
    ASSERT_NE(nullptr, theirs);
    theirs->stats()->set_hitpoints(500.0f);
    (void)og::test::on_eat(*tfd, static_cast<treasure*>(theirs), eater);
    EXPECT_EQ(mid, w.m_score[0])
        << "a life gem is claimable only by its own team";

    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A key's level indexes a 31-bit mask. Levels outside 0..30 are clamped, so
// a level-99 key stays a key rather than shifting a 1 off the end of the
// word (undefined behaviour in the C++ this was transliterated from).
TEST(PackLuaTreasure, key_levels_outside_the_mask_are_clamped)
{
    og::test::mount_core_pack();
    const TreasureFamilyDescriptor* tfd =
        get_treasure_family_descriptor(FAMILY_KEY);
    ASSERT_NE(nullptr, tfd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();

    for (int level : {-5, 99}) {
        walker* eater = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 0);
        ASSERT_NE(nullptr, eater);
        eater->set_keys(0);
        walker* key = spawn(w, Order::Treasure, FAMILY_KEY, 10, 10, 0);
        ASSERT_NE(nullptr, key);
        key->stats()->set_level(static_cast<short>(level));

        (void)og::test::on_eat(*tfd, static_cast<treasure*>(key), eater);
        const std::int32_t expected = level < 0 ? 1 : (1 << 30);
        EXPECT_EQ(expected, eater->keys())
            << "key level " << level << " must clamp into the mask";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A door with a wall directly above opens sideways; without one it opens
// upward. Only the second case had ever run, so the wall probe — the whole
// reason the hook reads the map — was untested.
TEST(PackLuaWeaponDoor, a_door_under_a_wall_opens_sideways)
{
    og::test::mount_core_pack();
    const WeaponFamilyDescriptor* wfd =
        get_weapon_family_descriptor(FAMILY_DOOR);
    ASSERT_NE(nullptr, wfd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    PixieData& grid = w.grid_for_floor(0);
    ASSERT_NE(nullptr, grid.data);
    ASSERT_GT(grid.w, 10);
    ASSERT_GT(grid.h, 10);
    grid.data[static_cast<std::size_t>(9) * grid.w + 10] =
        static_cast<unsigned char>(PIX_WALL4);

    walker* door = spawn(w, Order::Weapon, FAMILY_DOOR, 10, 10, 0);
    ASSERT_NE(nullptr, door);
    ASSERT_TRUE(og::test::on_death(*wfd, static_cast<weap*>(door)));

    walker* opened = find_family(w, Order::FX, FAMILY_DOOR_OPEN);
    ASSERT_NE(nullptr, opened) << "the door must leave an opened-door effect";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(opened->curdir()))
        << "a door under a wall opens sideways";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// Second sweep: branches that RUN but whose effect nothing asserts.
// ===========================================================================
//
// Line coverage says 97 % of the core pack executes. That is not the same
// claim as "97 % of the game's rules are checked": a branch whose only
// witness is a test that asserts a tautology, or asserts nothing at all, is
// as unverified as a branch that never ran. Each test below replaces such a
// witness with one that names the rule a player can observe.

// ---------------------------------------------------------------------------
// core:#8 slime — the two-way split
// ---------------------------------------------------------------------------

// The split animation completing is the slime's signature move: one blob
// becomes two. Three tests touched this path before and none checked it —
// test_walker_combat asserts `small_after >= small_before` (true even if the
// hook did nothing), test_family_behaviors stops at the animation being set,
// and the sibling test in this file covers only the "not a split" refusal.
// So "a slime splits" was, in fact, unverified.
TEST(PackLuaSlime, a_completed_split_animation_leaves_two_small_slimes)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_SLIME);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* prey = spawn(w, Order::Living, FAMILY_SOLDIER, 14, 10, 1);
    ASSERT_NE(nullptr, prey);
    walker* leader = spawn(w, Order::Living, FAMILY_ORC, 14, 12, 3);
    ASSERT_NE(nullptr, leader);

    walker* slime = spawn(w, Order::Living, FAMILY_SLIME, 10, 10, 3);
    ASSERT_NE(nullptr, slime);
    slime->stats()->set_level(4);
    slime->set_ani_type(static_cast<char>(ANI_SLIME_SPLIT));
    slime->set_cycle(static_cast<signed char>(5));
    slime->set_foe(prey);
    slime->set_leader(leader);

    const std::optional<bool> handled =
        og::script::hooks::on_ani_complete(&desc, slime);
    ASSERT_TRUE(handled.has_value()) << "the hook must be registered";
    EXPECT_TRUE(*handled);

    EXPECT_EQ(0u, count_family(w, Order::Living, FAMILY_SLIME))
        << "the big blob is gone once it has split";
    ASSERT_EQ(2u, count_family(w, Order::Living, FAMILY_SMALL_SLIME))
        << "a split turns one blob into two";
    EXPECT_EQ(ANI_WALK, static_cast<int>(slime->ani_type()))
        << "the split animation must hand back to the walk cycle";
    // The hook rewinds the cycle to 0 and transform_to() then steps the new
    // family's first frame, so the observable rule is "inside the walk
    // sequence", not a literal zero.
    EXPECT_GE(static_cast<int>(slime->cycle()), 0);
    EXPECT_LT(static_cast<int>(slime->cycle()), 16);

    walker* sibling = nullptr;
    for (walker* ob : entities_of(w, Order::Living)) {
        if (ob != slime && ob->family() == static_cast<char>(FAMILY_SMALL_SLIME))
            sibling = ob;
    }
    ASSERT_NE(nullptr, sibling);
    // The halves must land apart — two blobs stacked on one tile would read
    // as a single slime and fight as one.
    EXPECT_EQ(slime->xpos() + 12, sibling->xpos());
    EXPECT_EQ(slime->ypos() - 12, sibling->ypos());
    EXPECT_EQ(static_cast<int>(slime->team_num()),
              static_cast<int>(sibling->team_num()))
        << "the offspring fights for the same side";
    EXPECT_EQ(prey, sibling->foe()) << "both halves keep the same quarry";
    EXPECT_EQ(leader, sibling->leader());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A hired slime that splits must not clone the player's character into both
// halves: the offspring of a low-experience character is stripped back to an
// ordinary monster. That branch had never executed anywhere in the suite, so
// nothing stopped a split from duplicating a roster character.
TEST(PackLuaSlime, a_low_experience_split_strips_the_offspring_to_a_monster)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_SLIME);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* slime = spawn(w, Order::Living, FAMILY_SLIME, 10, 10, 0);
    ASSERT_NE(nullptr, slime);
    slime->stats()->set_level(4);            // threshold 1000 * 4
    slime->set_ani_type(static_cast<char>(ANI_SLIME_SPLIT));

    auto character = std::make_unique<guy>(FAMILY_SLIME);
    character->exp = 10;                     // far below the threshold
    character->teamnum = 0;
    slime->set_owned_myguy(std::move(character));

    const std::optional<bool> handled =
        og::script::hooks::on_ani_complete(&desc, slime);
    ASSERT_TRUE(handled.has_value());
    EXPECT_TRUE(*handled);

    walker* sibling = nullptr;
    for (walker* ob : entities_of(w, Order::Living)) {
        if (ob != slime && ob->family() == static_cast<char>(FAMILY_SMALL_SLIME))
            sibling = ob;
    }
    ASSERT_NE(nullptr, sibling);
    EXPECT_NE(nullptr, slime->myguy)
        << "the original half keeps the hired character";
    EXPECT_EQ(nullptr, sibling->myguy)
        << "a cheap split must not duplicate a roster character";
    EXPECT_EQ(std::string("SLIME"), std::string(sibling->stats()->name))
        << "the stripped half reverts to a plain slime";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:wave — the mage's three-stage energy wave
// ---------------------------------------------------------------------------

// Each wave stage un-deads itself and promotes to the next family. The only
// existing witnesses (test_weap_behavior's weap_death_wave{,2}_transforms)
// assert `dead() == 0` — which is the FIRST statement of both hooks, so the
// promotion and the hitpoint refill behind it were never checked. A wave
// that failed to promote would silently reduce the mage's special 4 to a
// single short-lived stage.
TEST(PackLuaWeaponWave, an_energy_wave_promotes_through_its_three_stages)
{
    og::test::mount_core_pack();
    const WeaponFamilyDescriptor* wave = get_weapon_family_descriptor(FAMILY_WAVE);
    const WeaponFamilyDescriptor* wave2 = get_weapon_family_descriptor(FAMILY_WAVE2);
    const WeaponFamilyDescriptor* wave3 = get_weapon_family_descriptor(FAMILY_WAVE3);
    ASSERT_NE(nullptr, wave);
    ASSERT_NE(nullptr, wave2);
    ASSERT_NE(nullptr, wave3);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* front = spawn(w, Order::Weapon, FAMILY_WAVE, 10, 10, 0);
    ASSERT_NE(nullptr, front);
    front->stats()->set_max_hitpoints(60.0f);
    front->stats()->set_hitpoints(0.0f);     // spent: this is what kills it
    front->set_dead(1);

    ASSERT_TRUE(og::test::on_death(*wave, static_cast<weap*>(front)));
    EXPECT_EQ(FAMILY_WAVE2, static_cast<int>(front->family()))
        << "a spent wave grows into its second stage";
    EXPECT_EQ(0, static_cast<int>(front->dead()))
        << "the promoted stage must be alive again";
    EXPECT_GT(front->stats()->hitpoints(), 0.0f)
        << "the new stage starts refilled, or it dies on its first tick";
    EXPECT_FLOAT_EQ(front->stats()->max_hitpoints(),
                    front->stats()->hitpoints());

    front->stats()->set_hitpoints(0.0f);
    front->set_dead(1);
    ASSERT_TRUE(og::test::on_death(*wave2, static_cast<weap*>(front)));
    EXPECT_EQ(FAMILY_WAVE3, static_cast<int>(front->family()))
        << "the second stage grows into the third";
    EXPECT_EQ(0, static_cast<int>(front->dead()));
    EXPECT_GT(front->stats()->hitpoints(), 0.0f);

    EXPECT_FALSE(og::test::has_on_death(*wave3))
        << "the third stage is the last: it must expire, not promote again";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:explosion — who a blast actually hurts
// ---------------------------------------------------------------------------

// A blast wounds its own side too, but softened: a quarter of the damage on
// the thrower, half on an ally, full on everyone else. Nothing asserted any
// of the three, so the whole friendly-fire ladder — the reason a thief can
// use bombs at all without deleting his own team — was unverified.
//
// A note on fidelity, because it decides the answer: the sim always runs
// this hook with the effect's own dead() already set (effect::death() is
// called after set_dead(1)), and walker::is_friendly() returns 0 for a dead
// attacker. Dispatching on a LIVE effect instead takes the friendly-refusal
// path in attack() and reports "allies take no damage", which is not what
// the game does. The set_dead(1) below is load-bearing.
TEST(PackLuaExplosion, a_blast_softens_but_does_not_spare_its_own_side)
{
    og::test::mount_core_pack();
    const EffectFamilyDescriptor* efd =
        get_effect_family_descriptor(FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, efd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* thrower = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 1);
    walker* ally = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 1);
    walker* foe = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 2);
    ASSERT_NE(nullptr, thrower);
    ASSERT_NE(nullptr, ally);
    ASSERT_NE(nullptr, foe);
    // Identical bodies at identical range, so the only thing separating the
    // three hitpoint losses is which arm of the ladder each one took.
    for (walker* who : {thrower, ally, foe}) {
        who->stats()->set_max_hitpoints(40000.0f);
        who->stats()->set_hitpoints(40000.0f);
        who->stats()->set_level(1);
        who->stats()->set_armor(0);
        who->stats()->clear_command();
    }
    ally->setxy(static_cast<float>(thrower->xpos() + 10),
                static_cast<float>(thrower->ypos()));
    foe->setxy(static_cast<float>(thrower->xpos() - 10),
               static_cast<float>(thrower->ypos()));

    walker* blast = spawn(w, Order::FX, FAMILY_EXPLOSION, 10, 10, 1);
    ASSERT_NE(nullptr, blast);
    blast->setxy(static_cast<float>(thrower->xpos()),
                 static_cast<float>(thrower->ypos()));
    blast->set_owner(thrower);
    blast->set_skip_exit(0);
    blast->set_damage(4000.0f);
    blast->set_dead(1);   // the state the sim always dispatches death() in

    ASSERT_TRUE(og::test::on_death(*efd, static_cast<effect*>(blast)));

    const float thrower_loss = 40000.0f - thrower->stats()->hitpoints();
    const float ally_loss = 40000.0f - ally->stats()->hitpoints();
    const float foe_loss = 40000.0f - foe->stats()->hitpoints();

    EXPECT_GT(thrower_loss, 0.0f)
        << "standing on your own bomb is not free";
    EXPECT_GT(ally_loss, thrower_loss)
        << "the thrower is spared more than a bystander on the same side";
    EXPECT_GT(foe_loss, ally_loss)
        << "an ally takes less than an enemy at the same range";
    // 1 : 2 : 4 is the ladder; allow the combat pipeline's rounding either
    // way rather than pinning a float.
    EXPECT_NEAR(2.0f, ally_loss / thrower_loss, 0.2f);
    EXPECT_NEAR(4.0f, foe_loss / thrower_loss, 0.4f);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The blast also throws everything it touches away from the centre. Asserting
// that on a wounded victim is unreliable — the damage feeds back through
// hit_response, which clears the command queue — so the witness here is an
// invulnerable bystander: the attack bounces off, and the shove is all that
// is left to observe.
TEST(PackLuaExplosion, a_blast_throws_even_an_invulnerable_bystander_clear)
{
    og::test::mount_core_pack();
    const EffectFamilyDescriptor* efd =
        get_effect_family_descriptor(FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, efd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* thrower = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 1);
    walker* bystander = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 2);
    ASSERT_NE(nullptr, thrower);
    ASSERT_NE(nullptr, bystander);
    bystander->stats()->set_max_hitpoints(4000.0f);
    bystander->stats()->set_hitpoints(4000.0f);
    bystander->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
    bystander->stats()->clear_command();
    bystander->setxy(static_cast<float>(thrower->xpos() - 10),
                     static_cast<float>(thrower->ypos()));

    walker* blast = spawn(w, Order::FX, FAMILY_EXPLOSION, 10, 10, 1);
    ASSERT_NE(nullptr, blast);
    blast->setxy(static_cast<float>(thrower->xpos()),
                 static_cast<float>(thrower->ypos()));
    blast->set_owner(thrower);
    blast->set_skip_exit(0);
    blast->set_damage(4000.0f);
    blast->set_dead(1);

    ASSERT_TRUE(og::test::on_death(*efd, static_cast<effect*>(blast)));
    EXPECT_FLOAT_EQ(4000.0f, bystander->stats()->hitpoints())
        << "invulnerability holds against a blast";
    EXPECT_TRUE(bystander->stats()->has_commands())
        << "but the blast still throws them clear of the centre";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// skip_exit marks a blast whose owner must be spared entirely (an exploding
// projectile, not a dropped bomb): the owner is skipped by the loop, so it
// is not even shoved.
TEST(PackLuaExplosion, a_skip_exit_blast_leaves_its_owner_alone_entirely)
{
    og::test::mount_core_pack();
    const EffectFamilyDescriptor* efd =
        get_effect_family_descriptor(FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, efd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* thrower = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 1);
    walker* foe = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 2);
    ASSERT_NE(nullptr, thrower);
    ASSERT_NE(nullptr, foe);
    thrower->stats()->clear_command();
    thrower->stats()->set_max_hitpoints(4000.0f);
    thrower->stats()->set_hitpoints(4000.0f);
    foe->stats()->clear_command();
    foe->stats()->set_max_hitpoints(4000.0f);
    foe->stats()->set_hitpoints(4000.0f);
    foe->setxy(static_cast<float>(thrower->xpos() - 10),
               static_cast<float>(thrower->ypos()));

    walker* blast = spawn(w, Order::FX, FAMILY_EXPLOSION, 10, 10, 1);
    ASSERT_NE(nullptr, blast);
    blast->setxy(static_cast<float>(thrower->xpos()),
                 static_cast<float>(thrower->ypos()));
    blast->set_owner(thrower);
    blast->set_skip_exit(1);
    blast->set_damage(300.0f);
    blast->set_dead(1);

    ASSERT_TRUE(og::test::on_death(*efd, static_cast<effect*>(blast)));
    EXPECT_FALSE(thrower->stats()->has_commands())
        << "a skip_exit blast must not even shove its owner";
    EXPECT_FLOAT_EQ(4000.0f, thrower->stats()->hitpoints())
        << "nor wound them";
    EXPECT_LT(foe->stats()->hitpoints(), 4000.0f)
        << "it still hits everyone else";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:archmage — mind control and the teleport marker
// ---------------------------------------------------------------------------

// Mind control has two outcomes and only one had ever run. A victim the
// archmage cannot dominate goes BERSERK instead: it keeps its old colour in
// real_team_num (so the charm can be undone) but fights under a random one.
// Nothing had ever executed that branch, so a bug there would have shipped
// as "high-level monsters are simply immune to mind control".
TEST(PackLuaArchmage, an_undominated_victim_goes_berserk_rather_than_joining)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_ARCHMAGE);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* mage = spawn(w, Order::Living, FAMILY_ARCHMAGE, 10, 10, 1);
    ASSERT_NE(nullptr, mage);
    mage->stats()->set_level(1);
    mage->stats()->set_max_magicpoints(9000.0f);
    mage->stats()->set_magicpoints(9000.0f);
    mage->set_busy(0);
    mage->set_current_special(4);

    walker* victim = spawn(w, Order::Living, FAMILY_ORC, 10, 10, 2);
    ASSERT_NE(nullptr, victim);
    victim->setxy(static_cast<float>(mage->xpos() + 20),
                  static_cast<float>(mage->ypos()));
    // Out-levelling the caster is what makes the domination fail outright
    // (the level delta goes negative and the resist roll is skipped).
    victim->stats()->set_level(9);
    victim->set_real_team_num(255);
    victim->set_charm_left(0);

    // The berserk colour is the hook's first draw; pin it so the assertion
    // can name a team instead of "something changed".
    w.rng_ = og::sim::SimRandom(seed_whose_first_draw_is(8, 5));

    ASSERT_TRUE(og::test::do_special(desc, mage));

    EXPECT_EQ(5, static_cast<int>(victim->team_num()))
        << "a berserk victim fights under a randomly drawn colour";
    EXPECT_EQ(2, static_cast<int>(victim->real_team_num()))
        << "its true colour is banked so the charm can wear off";
    EXPECT_NE(0, victim->charm_left())
        << "the berserk state must be timed, not permanent";
    EXPECT_EQ(1, count_notifications(tw.events, "has controlled 1 men"));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// An archmage may hold exactly one teleport marker. Placing a second retires
// the first — the loop that finds and kills it runs on every re-cast, and
// nothing asserted that it works. A regression there would let a player
// carpet the map with markers.
TEST(PackLuaArchmage, a_second_teleport_marker_retires_the_first)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_ARCHMAGE);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* mage = spawn(w, Order::Living, FAMILY_ARCHMAGE, 10, 10, 0);
    ASSERT_NE(nullptr, mage);
    mage->set_user(3);           // seat 3 is doing the casting
    mage->stats()->set_level(8);
    mage->stats()->set_max_magicpoints(9000.0f);
    mage->stats()->set_magicpoints(9000.0f);
    mage->set_current_special(1);
    mage->set_shifter_down(1);   // marker mode, not teleport
    mage->set_busy(0);

    ASSERT_TRUE(og::test::do_special(desc, mage));
    ASSERT_EQ(1u, count_family(w, Order::FX, FAMILY_MARKER))
        << "the first cast plants a marker";
    walker* first = find_family(w, Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, first);
    EXPECT_EQ(0, count_notifications(tw.events, "Old Marker Removed"))
        << "there was nothing to remove yet";

    mage->set_busy(0);
    ASSERT_TRUE(og::test::do_special(desc, mage));

    EXPECT_EQ(1u, count_family(w, Order::FX, FAMILY_MARKER))
        << "a caster may hold only one marker at a time";
    EXPECT_NE(0, static_cast<int>(first->dead()))
        << "the previous marker must actually be retired";
    EXPECT_EQ(1, count_notifications(tw.events, "Old Marker Removed"));

    // #230: a marker is one caster's private bookkeeping. Every line the flow
    // writes names the seat that pressed the key; drop the `self` argument at
    // any of these og.emit_notification calls and the addressee falls back to
    // -1 and lands in all four feeds again.
    EXPECT_EQ(3, notification_target(tw.events, "Teleport Marker Placed"));
    EXPECT_EQ(3, notification_target(tw.events, "Uses)"));
    EXPECT_EQ(3, notification_target(tw.events, "Old Marker Removed"));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The same rule on the mage, which additionally gates the Int refusal on
// user() != -1: a roster mage too dim to place a marker is told so privately,
// and so is every line of the placement that follows.
TEST(PackLuaMage, the_marker_flow_talks_only_to_the_casting_seat)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_MAGE);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* mage = spawn(w, Order::Living, FAMILY_MAGE, 10, 10, 0);
    ASSERT_NE(nullptr, mage);
    mage->set_user(1);
    mage->stats()->set_level(8);
    mage->stats()->set_max_magicpoints(9000.0f);
    mage->stats()->set_magicpoints(9000.0f);
    mage->set_current_special(1);
    mage->set_shifter_down(1);   // marker mode, not teleport
    mage->set_busy(0);
    mage->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    ASSERT_NE(nullptr, mage->myguy);
    mage->myguy->intelligence = 10;  // below marker_int_req (75)

    EXPECT_FALSE(og::test::do_special(desc, mage))
        << "10 Int cannot place a marker";
    ASSERT_EQ(1, count_notifications(tw.events, "Int for Marker!"));
    EXPECT_EQ(1, notification_target(tw.events, "Int for Marker!"))
        << "a refusal is for the hand that pressed the key, nobody else";

    mage->myguy->intelligence = 200;
    mage->set_busy(0);
    ASSERT_TRUE(og::test::do_special(desc, mage));
    EXPECT_EQ(1, notification_target(tw.events, "Teleport Marker Placed"));
    EXPECT_EQ(1, notification_target(tw.events, "Uses)"));

    mage->set_busy(0);
    ASSERT_TRUE(og::test::do_special(desc, mage));
    ASSERT_EQ(1, count_notifications(tw.events, "Old Marker Removed"));
    EXPECT_EQ(1, notification_target(tw.events, "Old Marker Removed"));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The cleric's two Int refusals are the same class of line: a private "you
// cannot do that", never a line about the party's state.
TEST(PackLuaCleric, an_int_refusal_reaches_only_the_cleric_it_refused)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* cleric = spawn(w, Order::Living, FAMILY_CLERIC, 10, 10, 0);
    ASSERT_NE(nullptr, cleric);
    cleric->set_user(2);
    cleric->stats()->set_level(5);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    ASSERT_NE(nullptr, cleric->myguy);
    cleric->myguy->intelligence = 10;  // below both Int requirements

    cleric->set_current_special(2);  // RAISE UNDEAD / TURN UNDEAD
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    EXPECT_FALSE(og::test::do_special(desc, cleric));
    ASSERT_EQ(1, count_notifications(tw.events, "Int to Turn Undead"));
    EXPECT_EQ(2, notification_target(tw.events, "Int to Turn Undead"));

    cleric->set_current_special(1);  // HEAL / MYSTIC MACE
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    EXPECT_FALSE(og::test::do_special(desc, cleric));
    ASSERT_EQ(1, count_notifications(tw.events, "Mystic Mace!"));
    EXPECT_EQ(2, notification_target(tw.events, "Mystic Mace!"));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:mage — freeze time cast by someone who is not the player
// ---------------------------------------------------------------------------

// A mage on the player's colour banks a global time stop; any other mage
// grants bonus rounds to its own side instead. The existing witness asserts
// `ally->bonus_rounds() >= before`, which stays true if the grant loop is
// deleted. These pin the amount, the cap, and who is left out.
TEST(PackLuaMage, an_enemy_freeze_time_banks_bonus_rounds_for_its_own_side)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_MAGE);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.my_team = 0;
    w.enemy_freeze = 0;

    walker* mage = spawn(w, Order::Living, FAMILY_MAGE, 10, 10, 1);
    ASSERT_NE(nullptr, mage);
    mage->stats()->set_level(5);              // 5 + 2*5 == 15 rounds
    mage->stats()->set_max_magicpoints(9000.0f);
    mage->stats()->set_magicpoints(9000.0f);
    mage->set_current_special(3);

    walker* ally = spawn(w, Order::Living, FAMILY_ORC, 11, 10, 1);
    walker* victim = spawn(w, Order::Living, FAMILY_ORC, 9, 10, 0);
    ASSERT_NE(nullptr, ally);
    ASSERT_NE(nullptr, victim);
    ally->set_bonus_rounds(0);
    victim->set_bonus_rounds(0);

    ASSERT_TRUE(og::test::do_special(desc, mage));

    EXPECT_EQ(15, static_cast<int>(ally->bonus_rounds()))
        << "every friend banks 5 + 2*level extra rounds";
    EXPECT_EQ(0, static_cast<int>(victim->bonus_rounds()))
        << "the other side gains nothing";
    EXPECT_EQ(0, w.enemy_freeze)
        << "a foreign-colour caster must not touch the player's time stop";
    EXPECT_EQ(1, count_notifications(tw.events, "TIME IS FROZEN! (15 rounds)"));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(PackLuaMage, an_enemy_freeze_time_is_capped_at_fifty_rounds)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_MAGE);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.my_team = 0;
    w.enemy_freeze = 0;

    walker* mage = spawn(w, Order::Living, FAMILY_MAGE, 10, 10, 1);
    ASSERT_NE(nullptr, mage);
    mage->stats()->set_level(40);             // 5 + 80 == 85, over the cap
    mage->stats()->set_max_magicpoints(9000.0f);
    mage->stats()->set_magicpoints(9000.0f);
    mage->set_current_special(3);
    walker* ally = spawn(w, Order::Living, FAMILY_ORC, 11, 10, 1);
    ASSERT_NE(nullptr, ally);
    ally->set_bonus_rounds(0);

    ASSERT_TRUE(og::test::do_special(desc, mage));
    EXPECT_EQ(50, static_cast<int>(ally->bonus_rounds()))
        << "a high-level caster cannot bank an unbounded time stop";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:door_open — the settled-door handoff
// ---------------------------------------------------------------------------

// Once the opening animation finishes, the effect hands its sprite to a
// fresh, ignored copy and retires itself; that copy is the open doorway the
// player then walks through. The path ran (test_effect_chain_and_door drives
// it) but nothing checked its result, so a broken handoff would leave either
// no doorway at all or an animating one that keeps re-spawning.
TEST(PackLuaEffectDoorOpen, a_finished_opening_settles_into_an_ignored_copy)
{
    og::test::mount_core_pack();
    const EffectFamilyDescriptor* efd =
        get_effect_family_descriptor(FAMILY_DOOR_OPEN);
    ASSERT_NE(nullptr, efd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* opening = spawn(w, Order::FX, FAMILY_DOOR_OPEN, 10, 10, 2);
    ASSERT_NE(nullptr, opening);
    opening->set_ani_type(static_cast<char>(ANI_WALK));  // animation finished
    opening->set_curdir(static_cast<signed char>(FACE_RIGHT));
    opening->setworldxy(200.0f, 120.0f);
    opening->stats()->set_level(3);
    opening->set_ignore(0);

    ASSERT_TRUE(og::test::on_act(*efd, static_cast<effect*>(opening)));

    EXPECT_NE(0, static_cast<int>(opening->dead()))
        << "the animating effect retires once it has handed off";
    walker* settled = nullptr;
    for (walker* ob : entities_of(w, Order::FX)) {
        if (ob != opening && ob->family() == static_cast<char>(FAMILY_DOOR_OPEN))
            settled = ob;
    }
    ASSERT_NE(nullptr, settled) << "the open doorway must survive the handoff";
    // Pinned as a rule, not as coverage: effect::effect() already defaults
    // ignore to 1, so the hook's set_ignore(1) restates the default and
    // deleting it changes nothing. The assertion still guards the rule
    // against a change to that default.
    EXPECT_EQ(1, static_cast<int>(settled->ignore()))
        << "an open doorway must not block or be collided with";
    EXPECT_FLOAT_EQ(200.0f, settled->worldx());
    EXPECT_FLOAT_EQ(120.0f, settled->worldy());
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(settled->curdir()))
        << "the settled copy keeps the facing the door opened toward";
    EXPECT_EQ(2, static_cast<int>(settled->team_num()));
    EXPECT_EQ(3, static_cast<int>(settled->stats()->level()));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:knife — the returning blade
// ---------------------------------------------------------------------------

// A soldier's thrown knife comes back; anyone else's does not. Both cases
// had witnesses in test_weap_behavior whose bodies end at the death() call
// with the expectation written only as a comment.
TEST(PackLuaWeaponKnife, only_a_returning_weapon_family_gets_its_blade_back)
{
    og::test::mount_core_pack();
    const WeaponFamilyDescriptor* wfd =
        get_weapon_family_descriptor(FAMILY_KNIFE);
    ASSERT_NE(nullptr, wfd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* soldier = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 1);
    ASSERT_NE(nullptr, soldier);
    walker* thrown = spawn(w, Order::Weapon, FAMILY_KNIFE, 12, 10, 1);
    ASSERT_NE(nullptr, thrown);
    thrown->set_owner(soldier);
    thrown->set_damage(42.0f);
    thrown->set_dead(1);   // weap::death() always runs with dead() set

    ASSERT_TRUE(og::test::on_death(*wfd, static_cast<weap*>(thrown)));
    walker* returning = find_family(w, Order::FX, FAMILY_KNIFE_BACK);
    ASSERT_NE(nullptr, returning)
        << "a soldier's knife must leave a blade flying home";
    EXPECT_EQ(soldier, returning->owner());
    EXPECT_FLOAT_EQ(42.0f, returning->damage())
        << "the return trip keeps the blade's bite";

    // An archer throws the same knife family and gets nothing back.
    walker* archer = spawn(w, Order::Living, FAMILY_ARCHER, 10, 12, 1);
    ASSERT_NE(nullptr, archer);
    walker* other = spawn(w, Order::Weapon, FAMILY_KNIFE, 12, 12, 1);
    ASSERT_NE(nullptr, other);
    other->set_owner(archer);
    other->set_dead(1);
    const std::size_t before = count_family(w, Order::FX, FAMILY_KNIFE_BACK);
    EXPECT_FALSE(og::test::on_death(*wfd, static_cast<weap*>(other)))
        << "a family without returning weapons declines the hook";
    EXPECT_EQ(before, count_family(w, Order::FX, FAMILY_KNIFE_BACK));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The blade that reaches its thrower hands the shot back. weapons_left is a
// hard resource — soldier.lua refuses to fire at zero — so a lost refund
// means a soldier who permanently runs out of knives mid-level.
TEST(PackLuaKnifeBack, a_blade_that_reaches_its_thrower_returns_the_shot)
{
    og::test::mount_core_pack();
    const EffectFamilyDescriptor* efd =
        get_effect_family_descriptor(FAMILY_KNIFE_BACK);
    ASSERT_NE(nullptr, efd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* soldier = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 1);
    ASSERT_NE(nullptr, soldier);
    soldier->set_weapons_left(0);

    walker* blade = spawn(w, Order::FX, FAMILY_KNIFE_BACK, 10, 10, 1);
    ASSERT_NE(nullptr, blade);
    blade->set_owner(soldier);
    blade->setxy(static_cast<float>(soldier->xpos()),
                 static_cast<float>(soldier->ypos()));
    blade->set_ani_type(static_cast<char>(ANI_ATTACK));

    ASSERT_TRUE(og::test::on_act(*efd, static_cast<effect*>(blade)));
    EXPECT_EQ(1, soldier->weapons_left())
        << "the caught blade is a throw the soldier gets to make again";
    EXPECT_NE(0, static_cast<int>(blade->dead()));
    EXPECT_EQ(ANI_WALK, static_cast<int>(blade->ani_type()));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A blade whose thrower died must expire rather than refund a shot to a
// corpse (or, worse, dereference one).
TEST(PackLuaKnifeBack, a_blade_whose_thrower_died_simply_expires)
{
    og::test::mount_core_pack();
    const EffectFamilyDescriptor* efd =
        get_effect_family_descriptor(FAMILY_KNIFE_BACK);
    ASSERT_NE(nullptr, efd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* soldier = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 1);
    ASSERT_NE(nullptr, soldier);
    soldier->set_weapons_left(0);
    soldier->set_dead(1);

    walker* blade = spawn(w, Order::FX, FAMILY_KNIFE_BACK, 10, 10, 1);
    ASSERT_NE(nullptr, blade);
    blade->set_owner(soldier);

    ASSERT_TRUE(og::test::on_act(*efd, static_cast<effect*>(blade)));
    EXPECT_NE(0, static_cast<int>(blade->dead()));
    EXPECT_EQ(0, soldier->weapons_left())
        << "a dead thrower gets no shot back";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:rock — the elf's bouncing rocks
// ---------------------------------------------------------------------------

// Specials 2-4 of the elf set do_bounce; the reflection is the whole point
// of them. It runs in the parity arenas but nothing ever asserted a rock
// changed direction instead of dying, and the only unit witness
// (weap_death_rock_no_bounce) asserts nothing at all.
TEST(PackLuaWeaponRock, a_bouncing_rock_reflects_off_a_wall_instead_of_dying)
{
    og::test::mount_core_pack();
    const WeaponFamilyDescriptor* wfd =
        get_weapon_family_descriptor(FAMILY_ROCK);
    ASSERT_NE(nullptr, wfd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    PixieData& grid = w.grid_for_floor(0);
    ASSERT_NE(nullptr, grid.data);
    ASSERT_GT(grid.w, 12);
    ASSERT_GT(grid.h, 12);
    // Wall directly to the right of the rock: forward is shut, the mirrored
    // step is open.
    grid.data[static_cast<std::size_t>(10) * grid.w + 11] =
        static_cast<unsigned char>(PIX_WALL4);

    walker* rock = spawn(w, Order::Weapon, FAMILY_ROCK, 10, 10, 0);
    ASSERT_NE(nullptr, rock);
    static_cast<weap*>(rock)->set_do_bounce(1);
    rock->set_lineofsight(5);
    rock->set_lastx(static_cast<float>(GRID_SIZE));
    rock->set_lasty(0.0f);
    rock->set_death_called(1);
    rock->set_dead(1);
    const short x_before = rock->xpos();

    EXPECT_TRUE(og::test::on_death(*wfd, static_cast<weap*>(rock)))
        << "a blocked bouncing rock survives its own death";
    EXPECT_EQ(0, static_cast<int>(rock->dead()));
    EXPECT_FLOAT_EQ(-static_cast<float>(GRID_SIZE), rock->lastx())
        << "the rock reverses along the blocked axis";
    EXPECT_EQ(x_before - GRID_SIZE, rock->xpos())
        << "and is placed on the reflected step";
    EXPECT_EQ(0, static_cast<int>(rock->death_called()))
        << "the reflected rock is live again and may die later";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A plain (non-bouncing) rock declines the hook so the caller's normal death
// runs — the ordinary case for every rock the elf throws without a special.
TEST(PackLuaWeaponRock, a_plain_rock_declines_the_bounce_hook)
{
    og::test::mount_core_pack();
    const WeaponFamilyDescriptor* wfd =
        get_weapon_family_descriptor(FAMILY_ROCK);
    ASSERT_NE(nullptr, wfd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    walker* rock = spawn(tw.world(), Order::Weapon, FAMILY_ROCK, 10, 10, 0);
    ASSERT_NE(nullptr, rock);
    static_cast<weap*>(rock)->set_do_bounce(0);
    rock->set_dead(1);

    EXPECT_FALSE(og::test::on_death(*wfd, static_cast<weap*>(rock)));
    EXPECT_NE(0, static_cast<int>(rock->dead()))
        << "a rock that cannot bounce stays dead";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:tree — the shared animation stepper's hardening
// ---------------------------------------------------------------------------

// curdir/ani_type/cycle can arrive from a snapshot a peer sent, so
// weapon_animate_step bounds all three before addressing the animation
// table. Those guards had never executed: a hostile ani_type would have been
// an out-of-bounds row index on the way to a frame read.
TEST(PackLuaWeaponAnimate, a_hostile_animation_type_cannot_index_out_of_range)
{
    og::test::mount_core_pack();
    const WeaponFamilyDescriptor* wfd =
        get_weapon_family_descriptor(FAMILY_TREE);
    ASSERT_NE(nullptr, wfd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    walker* tree = spawn(tw.world(), Order::Weapon, FAMILY_TREE, 10, 10, 0);
    ASSERT_NE(nullptr, tree);
    // Negative type, out-of-range facing, out-of-range cycle: all three must
    // be folded back into range before the row is addressed.
    //
    // The load-bearing one is the CYCLE bound. A cycle past the end of the
    // sequence indexes a nil out of the row table and hands it to set_frame,
    // which is a script error — and a script error is now a silent no-op,
    // because there is no C++ callback left behind the hook. So the assertion
    // that carries this test is "the hook did not raise".
    //
    // The facing and ani_type folds are defence in depth: og.ani_row rejects a
    // negative or over-long row itself, so deleting either Lua-side guard is
    // not separately observable here. Said out loud rather than dressed up as
    // coverage.
    tree->set_ani_type(static_cast<char>(-3));
    tree->set_curdir(static_cast<signed char>(-1));
    tree->set_cycle(static_cast<signed char>(120));

    EXPECT_TRUE(og::test::on_animate(*wfd, static_cast<weap*>(tree)))
        << "the tree always handles its own animation";
    EXPECT_EQ(0u, guard.count())
        << "a bounded step must not raise: " << guard.message();
    EXPECT_GE(static_cast<int>(tree->cycle()), 0)
        << "the cycle is left inside a sequence, never negative";
    EXPECT_LT(static_cast<int>(tree->cycle()), 16);
    EXPECT_GE(static_cast<int>(tree->ani_type()), 0)
        << "and the animation type settles back into a legal row";
}

// ---------------------------------------------------------------------------
// core treasures — the guards that make a pickup a no-op
// ---------------------------------------------------------------------------

// A potion that cannot help its eater must stay on the ground. Nothing had
// asserted either refusal, so a regression would have silently eaten the
// player's spare potions on any flying or invincible character.
TEST(PackLuaTreasure, a_potion_that_cannot_help_its_eater_is_left_untouched)
{
    og::test::mount_core_pack();
    const TreasureFamilyDescriptor* flight =
        get_treasure_family_descriptor(FAMILY_FLIGHT_POTION);
    const TreasureFamilyDescriptor* invuln =
        get_treasure_family_descriptor(FAMILY_INVULNERABLE_POTION);
    ASSERT_NE(nullptr, flight);
    ASSERT_NE(nullptr, invuln);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* flier = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 0);
    ASSERT_NE(nullptr, flier);
    flier->stats()->set_bit_flags(BIT_FLYING, 1);
    flier->set_flight_left(0);
    walker* wings = spawn(w, Order::Treasure, FAMILY_FLIGHT_POTION, 10, 10, 0);
    ASSERT_NE(nullptr, wings);
    wings->stats()->set_level(3);

    (void)og::test::on_eat(*flight, static_cast<treasure*>(wings), flier);
    EXPECT_EQ(0, flier->flight_left())
        << "a natural flier gains nothing from a flight potion";
    EXPECT_EQ(0, static_cast<int>(wings->dead()))
        << "and must leave it on the ground for someone who needs it";

    walker* stone = spawn(w, Order::Living, FAMILY_SOLDIER, 12, 10, 0);
    ASSERT_NE(nullptr, stone);
    stone->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
    stone->set_invulnerable_left(0);
    walker* ward =
        spawn(w, Order::Treasure, FAMILY_INVULNERABLE_POTION, 12, 10, 0);
    ASSERT_NE(nullptr, ward);
    ward->stats()->set_level(3);

    (void)og::test::on_eat(*invuln, static_cast<treasure*>(ward), stone);
    EXPECT_EQ(0, stone->invulnerable_left());
    EXPECT_EQ(0, static_cast<int>(ward->dead()));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Re-touching a key already held is silent: the mask is unchanged and no
// second pickup message fires. A regression here spams the player with one
// notification per frame for as long as they stand on the key.
TEST(PackLuaTreasure, a_key_already_held_is_a_silent_no_op)
{
    og::test::mount_core_pack();
    const TreasureFamilyDescriptor* tfd =
        get_treasure_family_descriptor(FAMILY_KEY);
    ASSERT_NE(nullptr, tfd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* eater = spawn(w, Order::Living, FAMILY_SOLDIER, 10, 10, 0);
    ASSERT_NE(nullptr, eater);
    eater->set_keys(0);
    eater->set_user(2); // a seat owns this walker; the line is still party news
    walker* key = spawn(w, Order::Treasure, FAMILY_KEY, 10, 10, 0);
    ASSERT_NE(nullptr, key);
    key->stats()->set_level(2);

    (void)og::test::on_eat(*tfd, static_cast<treasure*>(key), eater);
    EXPECT_EQ(1 << 2, eater->keys());
    ASSERT_EQ(1, count_notifications(tw.events, "picks up key 2"));
    EXPECT_EQ(-1, notification_target(tw.events, "picks up key 2"))
        << "keys are per-walker: the party must be told who is carrying one, "
           "so this line stays a broadcast and is never addressed to a seat";

    (void)og::test::on_eat(*tfd, static_cast<treasure*>(key), eater);
    EXPECT_EQ(1 << 2, eater->keys()) << "the mask is unchanged";
    EXPECT_EQ(1, count_notifications(tw.events, "picks up key 2"))
        << "standing on a key you already hold must stay silent";

    // A monster picking one up gets the bit but no player-facing message.
    walker* monster = spawn(w, Order::Living, FAMILY_ORC, 12, 10, 2);
    ASSERT_NE(nullptr, monster);
    monster->set_keys(0);
    walker* other = spawn(w, Order::Treasure, FAMILY_KEY, 12, 10, 2);
    ASSERT_NE(nullptr, other);
    other->stats()->set_level(4);
    (void)og::test::on_eat(*tfd, static_cast<treasure*>(other), monster);
    EXPECT_EQ(1 << 4, monster->keys());
    EXPECT_EQ(0, count_notifications(tw.events, "picks up key 4"))
        << "only the player's own pickups are announced";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Only team 0 or a roster character can cash a bar in. A wandering monster
// that walks over the treasure must leave it there for the player.
TEST(PackLuaTreasure, a_monster_cannot_cash_in_a_gold_bar)
{
    og::test::mount_core_pack();
    const TreasureFamilyDescriptor* tfd =
        get_treasure_family_descriptor(FAMILY_GOLD_BAR);
    ASSERT_NE(nullptr, tfd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* monster = spawn(w, Order::Living, FAMILY_ORC, 10, 10, 2);
    ASSERT_NE(nullptr, monster);
    walker* bar = spawn(w, Order::Treasure, FAMILY_GOLD_BAR, 10, 10, 2);
    ASSERT_NE(nullptr, bar);
    bar->stats()->set_level(3);

    const std::int32_t score_before = static_cast<std::int32_t>(w.m_score[2]);
    (void)og::test::on_eat(*tfd, static_cast<treasure*>(bar), monster);
    EXPECT_EQ(score_before, w.m_score[2])
        << "an ownerless monster banks nothing";
    EXPECT_EQ(0, static_cast<int>(bar->dead()))
        << "and leaves the bar for whoever can";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:druid — the faerie that cannot fit
// ---------------------------------------------------------------------------

// The summoned faerie is placed where the druid's shot landed and then
// probed; a blocked spot must cancel the summon rather than leave a faerie
// standing inside a wall. That refusal had never executed.
TEST(PackLuaDruid, a_faerie_summoned_into_a_wall_is_cancelled)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_DRUID);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    PixieData& grid = w.grid_for_floor(0);
    ASSERT_NE(nullptr, grid.data);
    ASSERT_GT(grid.w, 14);
    ASSERT_GT(grid.h, 14);

    walker* druid = spawn(w, Order::Living, FAMILY_DRUID, 10, 10, 1);
    ASSERT_NE(nullptr, druid);
    druid->stats()->set_level(5);
    druid->stats()->set_max_magicpoints(9000.0f);
    druid->stats()->set_magicpoints(9000.0f);
    druid->set_fire_frequency(1.0f);
    druid->set_current_special(2);
    druid->set_busy(0);
    druid->set_lastx(1.0f);
    druid->set_lasty(0.0f);
    druid->set_curdir(static_cast<signed char>(FACE_RIGHT));

    // Wall off every tile the shot could land on to the right of the druid.
    for (int col = 10; col < grid.w; col++) {
        for (int row = 9; row <= 11 && row < grid.h; row++)
            grid.data[static_cast<std::size_t>(row) * grid.w + static_cast<std::size_t>(col)] =
                static_cast<unsigned char>(PIX_WALL4);
    }

    const bool cast = og::test::do_special(desc, druid);
    EXPECT_FALSE(cast) << "a summon with nowhere to stand must fail";
    EXPECT_EQ(0u, count_family(w, Order::Living, FAMILY_FAERIE))
        << "no faerie may be left alive inside a wall";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:#10 medium slime — the other half of the shrink chain
// ---------------------------------------------------------------------------

// A dying medium slime leaves a SMALL one. Its witness in test_family_behaviors
// is a bare death() call whose comment states the expectation and whose body
// asserts nothing, so the offspring family — the one line that differs from
// the big slime's hook — was never checked.
TEST(PackLuaSlime, a_dying_medium_slime_leaves_a_small_one)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_MEDIUM_SLIME);
    ASSERT_TRUE(og::test::has_on_death(desc));
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* slime = spawn(w, Order::Living, FAMILY_MEDIUM_SLIME, 10, 10, 2);
    ASSERT_NE(nullptr, slime);
    slime->stats()->set_level(6);

    ASSERT_TRUE(og::test::on_death(desc, slime));
    ASSERT_EQ(1u, count_family(w, Order::Living, FAMILY_SMALL_SLIME))
        << "a medium slime shrinks, it does not simply die";
    EXPECT_EQ(0u, count_family(w, Order::Living, FAMILY_MEDIUM_SLIME));
    walker* child = find_family(w, Order::Living, FAMILY_SMALL_SLIME);
    ASSERT_NE(nullptr, child);
    EXPECT_EQ(2, static_cast<int>(child->team_num()));
    EXPECT_EQ(6, static_cast<int>(child->stats()->level()))
        << "the offspring keeps the parent's level";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// core:archmage — the illusion table and the true summon's placement probe
// ---------------------------------------------------------------------------

// Illusion summoning picks from a table that widens with the magic left after
// the cast. Only one rung of that ladder can be reached per parity arena, so
// the mapping from magic to conjured family was effectively unpinned: a
// mis-ordered table would silently swap what a high-level archmage conjures.
TEST(PackLuaArchmage, the_illusion_summoned_widens_with_the_magic_behind_it)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_ARCHMAGE);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* mage = spawn(w, Order::Living, FAMILY_ARCHMAGE, 10, 10, 1);
    ASSERT_NE(nullptr, mage);
    mage->stats()->set_level(6);
    mage->stats()->set_special_cost(3, 0.0f);
    mage->set_current_special(3);
    mage->set_shifter_down(0);   // illusion, not the true summon
    mage->set_busy(0);

    // Cheapest rung: under 100 magic left, the table has one entry and no
    // draw is made at all.
    mage->stats()->set_max_magicpoints(9000.0f);
    mage->stats()->set_magicpoints(50.0f);
    ASSERT_TRUE(og::test::do_special(desc, mage));
    ASSERT_EQ(1u, count_family(w, Order::Living, FAMILY_ELF))
        << "a nearly-spent archmage can only manage an elf";
    walker* phantom = find_family(w, Order::Living, FAMILY_ELF);
    ASSERT_NE(nullptr, phantom);
    EXPECT_EQ(std::string("Phantom"), std::string(phantom->stats()->name));
    EXPECT_EQ(mage, phantom->owner());
    EXPECT_EQ(1, static_cast<int>(phantom->team_num()));
    EXPECT_FLOAT_EQ(0.0f, phantom->stats()->hitpoints())
        << "an illusion pops on the first hit it takes";

    // Widest rung: over 1000 magic left the table reaches the orc captain,
    // which no cheaper rung can produce.
    mage->set_busy(0);
    mage->stats()->set_magicpoints(4000.0f);
    w.rng_ = og::sim::SimRandom(seed_whose_first_draw_is(9, 8));
    ASSERT_TRUE(og::test::do_special(desc, mage));
    EXPECT_EQ(1u, count_family(w, Order::Living, FAMILY_BIG_ORC))
        << "the widest illusion table reaches the orc captain";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The true summon probes the eight tiles around the caster and cancels if
// none is free. That refusal had never executed, so nothing stopped a
// summoned elemental from being left alive inside a wall.
TEST(PackLuaArchmage, a_true_summon_with_nowhere_to_stand_is_cancelled)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_ARCHMAGE);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    PixieData& grid = w.grid_for_floor(0);
    ASSERT_NE(nullptr, grid.data);
    ASSERT_GT(grid.w, 14);
    ASSERT_GT(grid.h, 14);
    for (int row = 8; row <= 12 && row < grid.h; row++) {
        for (int col = 8; col <= 12 && col < grid.w; col++)
            grid.data[static_cast<std::size_t>(row) * grid.w + static_cast<std::size_t>(col)] =
                static_cast<unsigned char>(PIX_WALL4);
    }

    walker* mage = spawn(w, Order::Living, FAMILY_ARCHMAGE, 10, 10, 1);
    ASSERT_NE(nullptr, mage);
    mage->stats()->set_level(30);           // past the 150-intelligence gate
    mage->stats()->set_max_magicpoints(9000.0f);
    mage->stats()->set_magicpoints(9000.0f);
    mage->stats()->set_special_cost(3, 0.0f);
    mage->set_current_special(3);
    mage->set_shifter_down(1);              // the true summon
    mage->set_busy(0);

    EXPECT_FALSE(og::test::do_special(desc, mage))
        << "a summon with nowhere to stand must fail";
    EXPECT_EQ(0u, count_family(w, Order::Living, FAMILY_FIREELEMENTAL))
        << "no elemental may be left alive inside a wall";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A veteran hired slime splits its experience — and therefore its level —
// evenly across both halves. That arithmetic re-enters the level-up machinery
// through g_upgrade_to_level, so a fault here corrupts a saved character's
// level rather than just misbehaving for one fight; nothing had asserted the
// resulting exp or level on either half.
TEST(PackLuaSlime, a_veteran_split_halves_the_experience_across_both_bodies)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_SLIME);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* slime = spawn(w, Order::Living, FAMILY_SLIME, 10, 10, 0);
    ASSERT_NE(nullptr, slime);
    slime->stats()->set_level(1);            // threshold 1000
    slime->set_ani_type(static_cast<char>(ANI_SLIME_SPLIT));

    auto character = std::make_unique<guy>(FAMILY_SLIME);
    character->exp = 60000;                  // well past the threshold
    character->teamnum = 0;
    slime->set_owned_myguy(std::move(character));

    const std::optional<bool> handled =
        og::script::hooks::on_ani_complete(&desc, slime);
    ASSERT_TRUE(handled.has_value());
    EXPECT_TRUE(*handled);

    walker* sibling = nullptr;
    for (walker* ob : entities_of(w, Order::Living)) {
        if (ob != slime && ob->family() == static_cast<char>(FAMILY_SMALL_SLIME))
            sibling = ob;
    }
    ASSERT_NE(nullptr, sibling);
    ASSERT_NE(nullptr, slime->myguy);
    ASSERT_NE(nullptr, sibling->myguy)
        << "a veteran split keeps a character on BOTH halves";
    EXPECT_EQ(30000u, slime->myguy->exp)
        << "each half carries exactly half the experience";
    EXPECT_EQ(30000u, sibling->myguy->exp);
    // calculate_level(30000) == 3 on the legacy 8000/2000/4000 ladder.
    EXPECT_EQ(3, static_cast<int>(slime->myguy->level));
    EXPECT_EQ(3, static_cast<int>(sibling->myguy->level))
        << "both halves are re-levelled from the halved experience";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
