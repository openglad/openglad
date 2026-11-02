/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch world-1: the rules the review found in game_world.h,
// damage_number_event.cpp, fearless.cpp and game_world.cpp (the per-floor
// lookups, the damage-number wire codec, the banner's rally aura, the owner
// chain guard, the entity lists, grid passability and sight, and the foe
// finders). Each test is named after the rule it asserts, and each failure
// message names the arm.

#include <gtest/gtest.h>

#include "../test_game_world_fixture.h"
#include "kit_probe_family.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/order.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/damage_number_event.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/fearless.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/resources/packs.h>

#include <algorithm>
#include <initializer_list>
#include <array>
#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Staging helpers
// ---------------------------------------------------------------------------

// A plain grass field of `w` x `h` cells (16 px each).
void open_field(TestGameWorld& tw, int w = 32, int h = 32)
{
    GameWorld& world = tw.world();
    world.resize_grid(w, h);
    std::fill_n(world.grid.data.get(),
                static_cast<std::size_t>(world.grid.w) * world.grid.h,
                static_cast<std::uint8_t>(PIX_GRASS1));
    world.my_team = 0;
}

void paint(GameWorld& world, int cx, int cy, unsigned char tile)
{
    world.grid.data[static_cast<std::size_t>(cx + world.grid.w * cy)] = tile;
}

// A Living of `family` on `team`, sized 16x16 BEFORE it is placed (the obmap
// files a walker by its box at setxy time), holding still.
walker* add_living(TestGameWorld& tw, int family, unsigned char team, short x,
               short y)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_act_type(ACT_GUARD);
    w->set_sizex(16);
    w->set_sizey(16);
    w->stats()->set_max_hitpoints(40.0f);
    w->stats()->set_hitpoints(40.0f);
    w->setxy(x, y);
    return w;
}

bool same_list(const std::list<walker*>& got,
               const std::vector<walker*>& want)
{
    return std::vector<walker*>(got.begin(), got.end()) == want;
}

bool in_list(const GameWorld::EntityList& list, const walker* w)
{
    return std::any_of(list.begin(), list.end(),
                       [w](const auto& p) { return p.get() == w; });
}

// The passability probe: a 1x1 walker inside cell (5,5) of a grass field,
// over a single painted byte. `kind` picks the archetype.
enum class Probe { Plain, Flyer, TempFlight, Swimmer, Forestwalk, Arrow };

const char* probe_name(Probe p)
{
    switch (p)
    {
        case Probe::Plain: return "plain soldier";
        case Probe::Flyer: return "BIT_FLYING soldier";
        case Probe::TempFlight: return "soldier with flight_left 1";
        case Probe::Swimmer: return "BIT_SWIMMING soldier";
        case Probe::Forestwalk: return "BIT_FORESTWALK soldier";
        case Probe::Arrow: return "owned arrow";
    }
    return "?";
}

constexpr short kProbeX = 5 * GRID_SIZE + 4;
constexpr short kProbeY = 5 * GRID_SIZE + 4;

// Answers query_grid_passable for a fresh probe of `kind` over `tile`.
bool passable_over(unsigned char tile, Probe kind)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    paint(world, 5, 5, tile);
    walker* shooter = add_living(tw, FAMILY_SOLDIER, 0, 64, 300);
    walker* ob = nullptr;
    if (kind == Probe::Arrow)
    {
        ob = world.add_weap_ob(Order::Weapon, FAMILY_ARROW);
        ob->set_owner(shooter);
        ob->set_team_num(0);
    }
    else
    {
        ob = world.add_ob(Order::Living, FAMILY_SOLDIER);
    }
    ob->stats()->clear_bit_flags();
    ob->set_flight_left(0);
    ob->set_sizex(1);
    ob->set_sizey(1);
    ob->setxy(kProbeX, kProbeY);
    switch (kind)
    {
        case Probe::Flyer: ob->stats()->set_bit_flags(BIT_FLYING, 1); break;
        case Probe::TempFlight: ob->set_flight_left(1); break;
        case Probe::Swimmer: ob->stats()->set_bit_flags(BIT_SWIMMING, 1); break;
        case Probe::Forestwalk:
            ob->stats()->set_bit_flags(BIT_FORESTWALK, 1);
            break;
        default: break;
    }
    return world.query_grid_passable(kProbeX, kProbeY, ob);
}

}  // namespace

// ===========================================================================
// game_world.h: per-floor lookups and decor concealment
// ===========================================================================

// game_world.h:251. A floor index outside 1..extra floors (a hostile
// snapshot's -1, a save's 99, plain 0) reads floor 0's grid, decor and
// smoother; an index inside the stack reads that floor's own buffer.
TEST(SemWorld, a_floor_index_outside_the_stacked_floors_reads_floor_zero)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.set_floor_count(3);  // floors 1 and 2 are stacked
    const GameWorld& cw = world;

    for (int floor : {0, -1, -7, 3, 99})
    {
        EXPECT_EQ(&world.grid, &world.grid_for_floor(floor))
            << "out-of-range floor " << floor << ": grid falls back to floor 0";
        EXPECT_EQ(&world.grid, &cw.grid_for_floor(floor))
            << "out-of-range floor " << floor << ": const grid falls back";
        EXPECT_EQ(&world.decor, &world.decor_for_floor(floor))
            << "out-of-range floor " << floor << ": decor falls back";
        EXPECT_EQ(&world.decor, &cw.decor_for_floor(floor))
            << "out-of-range floor " << floor << ": const decor falls back";
        EXPECT_EQ(&world.mysmoother, &world.smoother_for_floor(floor))
            << "out-of-range floor " << floor << ": smoother falls back";
    }
    // Control: inside the stack each floor has its own buffers.
    EXPECT_EQ(&world.extra_floors_[0].grid, &world.grid_for_floor(1));
    EXPECT_EQ(&world.extra_floors_[1].grid, &world.grid_for_floor(2));
    EXPECT_EQ(&world.extra_floors_[1].decor, &world.decor_for_floor(2));
    EXPECT_EQ(&world.extra_floors_[0].floor_smoother,
              &world.smoother_for_floor(1));
}

// game_world.h:282. A decor byte past the registry (a hostile or
// future-version plane) never conceals, and is never used to subscript the
// registry; a shrub on the same plane does.
TEST(SemWorld, a_decor_id_past_the_registry_never_conceals)
{
    TestGameWorld tw;
    open_field(tw, 8, 8);
    GameWorld& world = tw.world();
    PixieData& dp = world.decor;
    dp.frames = 1;
    dp.w = world.grid.w;
    dp.h = world.grid.h;
    dp.data = std::make_unique<unsigned char[]>(64);
    dp.data[3 + 8 * 3] = DECOR_MAX;
    dp.data[4 + 8 * 3] = 255;
    dp.data[5 + 8 * 3] = DECOR_SHRUB;

    EXPECT_FALSE(world.decor_conceals_at(0, 3, 3))
        << "decor id DECOR_MAX: unknown decor never conceals";
    EXPECT_FALSE(world.decor_conceals_at(0, 4, 3))
        << "decor id 255: unknown decor never conceals";
    EXPECT_TRUE(world.decor_conceals_at(0, 5, 3))
        << "control: a shrub on the same plane conceals";
}

// ===========================================================================
// damage_number_event.cpp
// ===========================================================================

namespace {

walker::DamageNumber dn(float x, float y, float value = 3.0f)
{
    return walker::DamageNumber(x, y, value, 40, 0u);
}

std::pair<float, float> wire_xy(float x, float y)
{
    const auto decoded = og::sim::decode_damage_number_event(
        og::sim::encode_damage_number_event(1u, dn(x, y)));
    if (!decoded.has_value())
        return {-1.0f, -1.0f};
    return {decoded->number.x, decoded->number.y};
}

}  // namespace

// damage_number_event.cpp:18. A NaN or exactly-zero coordinate crosses the
// wire as 0, and an in-range fraction is truncated, not rounded.
TEST(SemDamageNumber, a_nan_or_zero_coordinate_crosses_as_zero_and_fractions_truncate)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(std::make_pair(0.0f, 0.0f), wire_xy(nan, nan))
        << "NaN arm: a NaN x and y decode as 0";
    EXPECT_EQ(std::make_pair(0.0f, 77.0f), wire_xy(nan, 77.0f))
        << "NaN arm: a NaN x does not disturb the y half";
    EXPECT_EQ(std::make_pair(0.0f, 0.0f), wire_xy(0.0f, -0.0f))
        << "zero arm: 0 and -0 decode as 0";
    EXPECT_EQ(std::make_pair(12.0f, 65534.0f), wire_xy(12.9f, 65534.9f))
        << "in-range arm: fractions truncate (12.9 -> 12, 65534.9 -> 65534)";
}

// damage_number_event.cpp:94. A walker with entity id 0 is never lifted,
// seat-bound or not, and its pending list is still drained.
TEST(SemDamageNumber, a_walker_without_an_entity_id_is_never_lifted_but_is_drained)
{
    TestGameWorld fixture;
    GameWorld& world = fixture.world();
    walker* real = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* bound_zero = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* loose_zero = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(real && bound_zero && loose_zero);
    const std::uint32_t real_id = real->entity_id();
    ASSERT_NE(0u, real_id);
    bound_zero->set_entity_id(0u);
    loose_zero->set_entity_id(0u);
    for (walker* w : {real, bound_zero, loose_zero})
        for (int i = 0; i < 2; ++i)
            w->damage_numbers.push_back(dn(10.0f, 10.0f, static_cast<float>(1 + i)));

    std::array<walker*, og::sim::kMaxGlobalPlayers> controls{};
    controls[1] = bound_zero;
    std::vector<og::sim::Event> out;
    og::sim::lift_damage_number_events(world, controls, out);

    ASSERT_EQ(2u, out.size())
        << "entity_id 0 arm: only the real walker's two numbers are lifted";
    for (const og::sim::Event& e : out)
        EXPECT_EQ(real_id, e.a) << "entity_id 0 arm: no event names owner 0";
    EXPECT_TRUE(real->damage_numbers.empty());
    EXPECT_TRUE(bound_zero->damage_numbers.empty())
        << "a seat-bound id-0 walker's list is drained all the same";
    EXPECT_TRUE(loose_zero->damage_numbers.empty())
        << "an unbound id-0 walker's list is drained all the same";
}

// damage_number_event.cpp:131. A mirror applies only DamageNumber events:
// any other kind addressed at a known owner is ignored.
TEST(SemDamageNumber, a_mirror_ignores_events_of_any_other_kind)
{
    TestGameWorld fixture;
    walker* owner = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, owner);
    og::sim::SimEventBatch batch;
    og::sim::Event note;
    note.kind = og::sim::EventKind::Notification;
    note.a = owner->entity_id();
    note.b = (5u << 16) | 6u;
    note.c = 0x28000300u;
    batch.events.push_back(note);
    og::sim::Event sound = note;
    sound.kind = og::sim::EventKind::PlaySound;
    batch.events.push_back(sound);
    batch.events.push_back(og::sim::encode_damage_number_event(
        owner->entity_id(), dn(7.0f, 8.0f, 9.0f)));

    EXPECT_EQ(1u, og::sim::apply_damage_number_events(fixture.world(), batch))
        << "other-kind arm: only the DamageNumber event counts";
    ASSERT_EQ(1u, owner->damage_numbers.size())
        << "other-kind arm: nothing is pushed for a Notification or PlaySound";
    EXPECT_FLOAT_EQ(9.0f, owner->damage_numbers.front().value);
    EXPECT_FLOAT_EQ(7.0f, owner->damage_numbers.front().x);
}

// ===========================================================================
// fearless.cpp
// ===========================================================================

namespace {

class SemFearless : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;

    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
    }
    void TearDown() override
    {
        og::script::clear_pack_scripts();
        reset_all_registry_mod_slots();
    }

    static int install_banner(int radius)
    {
        og::data::ClasspackData data;
        const og::script::DeclareResult r = og::test::declare(
            "og.family('weapon', { id = 'kitprobe:banner', wire_id = 'auto',\n"
            "  rally_radius = " + std::to_string(radius) + " })\n",
            data);
        EXPECT_TRUE(r.ok) << r.error;
        if (!r.ok)
            return -1;
        EXPECT_EQ(1, og::resources::install_classpack_data(std::move(data)));
        return og::families::resolve_family_string_id(Order::Weapon,
                                                      "kitprobe:banner");
    }

    // A weapon made as a knife and re-tagged to `family`, sized 16x16.
    static walker* weapon(TestGameWorld& tw, int family, unsigned char team,
                          short x, short y)
    {
        walker* w = tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
        if (w == nullptr)
            return nullptr;
        w->set_order_family(Order::Weapon, static_cast<char>(family));
        w->set_team_num(team);
        w->set_sizex(16);
        w->set_sizey(16);
        w->setxy(x, y);
        return w;
    }
};

}  // namespace

// fearless.cpp:35. A rally weapon is not made fearless by its own aura: the
// banner itself, alone on the weapon list, is not fearless.
TEST_F(SemFearless, a_banner_is_not_made_fearless_by_its_own_aura)
{
    const int banner_id = install_banner(64);
    ASSERT_NE(-1, banner_id);
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 160, 160);
    walker* banner = weapon(tw, banner_id, 0, 170, 160);
    ASSERT_TRUE(hero && banner);
    ASSERT_TRUE(og::sim::fearless(*hero)) << "control: the hero is rallied";
    EXPECT_FALSE(og::sim::fearless(*banner))
        << "self arm: the banner skips itself (distance 0 would count)";
    tw.world().new_specials = 0;
    EXPECT_FALSE(og::sim::fearless(*hero))
        << "New Specials off: the same banner rallies nobody";
    EXPECT_FALSE(og::sim::fearless(*banner))
        << "New Specials off: the banner is not fearless either";
}

// fearless.cpp:41. A friendly weapon whose family declares no rally radius
// (a knife: rally_radius 0) rallies nobody, even standing on the walker.
TEST_F(SemFearless, a_weapon_without_a_rally_radius_rallies_nobody)
{
    const WeaponFamilyDescriptor* knife_wfd =
        get_weapon_family_descriptor(FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife_wfd);
    ASSERT_EQ(0, knife_wfd->rally_radius) << "the knife declares no aura";
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 160, 160);
    walker* knife = weapon(tw, FAMILY_KNIFE, 0, 160, 160);
    ASSERT_TRUE(hero && knife);
    ASSERT_EQ(0, hero->distance_to_ob_center(knife));
    EXPECT_FALSE(og::sim::fearless(*hero))
        << "rally_radius <= 0 arm: a friendly knife at distance 0 is no banner";
    tw.world().new_specials = 0;
    EXPECT_FALSE(og::sim::fearless(*hero))
        << "New Specials off: the knife rallies nobody either";
}

// fearless.cpp:46. The rally radius is inclusive: a walker at exactly r
// (squared distance r*r) is rallied, one pixel further is not; with New
// Specials off the same spot rallies nobody.
TEST_F(SemFearless, the_rally_radius_is_inclusive_at_exactly_r)
{
    const int banner_id = install_banner(64);
    ASSERT_NE(-1, banner_id);
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.new_specials = 1;
    walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 160, 160);
    walker* banner = weapon(tw, banner_id, 0, 224, 160);
    ASSERT_TRUE(hero && banner);
    ASSERT_EQ(64 * 64, hero->distance_to_ob_center(banner));
    EXPECT_TRUE(og::sim::fearless(*hero))
        << "boundary arm: squared distance exactly r*r rallies";
    banner->setxy(225, 160);
    ASSERT_EQ(65 * 65, hero->distance_to_ob_center(banner));
    EXPECT_FALSE(og::sim::fearless(*hero)) << "one pixel past r does not";
    banner->setxy(224, 160);
    world.new_specials = 0;
    EXPECT_FALSE(og::sim::fearless(*hero))
        << "New Specials off: the same spot rallies nobody";
}

// ===========================================================================
// game_world.cpp: the owner chain guard
// ===========================================================================

// game_world.cpp:50. A self-owned walker (the ownerless-explosion shape) and
// an ownerless walker keep their owner links through a foe scan.
TEST(SemWorld, a_self_owner_or_no_owner_survives_the_owner_chain_guard)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* self_owned = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    walker* orc = add_living(tw, FAMILY_ORC, 1, 140, 100);
    ASSERT_TRUE(self_owned && orc);
    self_owned->set_owner(self_owned);
    ASSERT_EQ(nullptr, orc->owner());

    EXPECT_EQ(orc, world.find_nearest_foe(self_owned));
    EXPECT_EQ(self_owned, self_owned->owner())
        << "self arm: a self-owner is not cut";
    EXPECT_EQ(nullptr, orc->owner()) << "null arm: stays ownerless";
    EXPECT_EQ(orc, world.find_near_foe(self_owned));
    EXPECT_EQ(self_owned, self_owned->owner())
        << "self arm (quick search): a self-owner is not cut";
}

// game_world.cpp:52. An owner that is in no entity list (it left the world)
// is cut before the foe scan, and the scan still answers.
TEST(SemWorld, an_owner_that_left_the_world_is_cut_before_a_foe_scan)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* summon = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    walker* pet = add_living(tw, FAMILY_SOLDIER, 0, 120, 100);
    walker* orc = add_living(tw, FAMILY_ORC, 1, 140, 100);
    ASSERT_TRUE(summon && pet && orc);
    std::unique_ptr<walker> stray =
        world.entity_factory(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, stray);
    ASSERT_FALSE(world.tracks(stray.get()));
    summon->set_owner(stray.get());
    pet->set_owner(summon);

    EXPECT_EQ(orc, world.find_nearest_foe(pet));
    EXPECT_EQ(nullptr, summon->owner())
        << "untracked arm: the link to an owner outside the world is cut";
    EXPECT_EQ(summon, pet->owner())
        << "control: the tracked link below it survives";
}

// ===========================================================================
// game_world.cpp: the entity lists
// ===========================================================================

// game_world.cpp:592. Only a Living added to the object list counts toward
// the living census.
TEST(SemWorld, only_a_living_added_to_the_object_list_counts_as_living)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    const int before = world.living_count;
    ASSERT_NE(nullptr, world.add_ob(Order::Living, FAMILY_SOLDIER));
    ASSERT_EQ(before + 1, world.living_count) << "control: a Living counts";

    ASSERT_NE(nullptr, world.add_ob(Order::Generator, FAMILY_TENT));
    EXPECT_EQ(before + 1, world.living_count) << "add_ob(Generator)";
    ASSERT_NE(nullptr, world.add_ob(Order::Treasure, FAMILY_GOLD_BAR));
    EXPECT_EQ(before + 1, world.living_count) << "add_ob(Treasure)";
    ASSERT_NE(nullptr, world.add_ob(Order::FX, FAMILY_EXPLOSION));
    EXPECT_EQ(before + 1, world.living_count) << "add_ob(FX)";
    ASSERT_NE(nullptr, world.add_ob(Order::Weapon, FAMILY_KNIFE));
    EXPECT_EQ(before + 1, world.living_count) << "add_ob(Weapon)";
    ASSERT_NE(nullptr, world.add_fx_ob(Order::FX, FAMILY_EXPLOSION));
    EXPECT_EQ(before + 1, world.living_count) << "add_fx_ob";
    ASSERT_NE(nullptr, world.add_weap_ob(Order::Weapon, FAMILY_KNIFE));
    EXPECT_EQ(before + 1, world.living_count) << "add_weap_ob";
}

namespace {

// The level-script fixture of test_level_scripts.cpp: a bare world (level
// id 42) whose factory hands out plain 16x16 walkers.
class SemAddOb : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;

    SemAddOb() : world(7)
    {
        world.id = 42;
        world.entity_factory = [](Order order, std::int32_t family)
            -> std::unique_ptr<walker> {
            auto entity = std::make_unique<walker>();
            entity->set_order_family(order, static_cast<char>(family));
            entity->set_sizex(16);
            entity->set_sizey(16);
            return entity;
        };
        context.world = &world;
        context.sim_events = &events;
        previous_ = current_game;
        current_game = &context;
        og::script::clear_pack_scripts();
        og::script::register_pack_script(
            {"test.level", "lvl.lua",
             "og.register_level_hooks(42, {\n"
             "  on_entity_spawn = function(ent) og.log('spawn') end,\n"
             "})\n"});
        world.tick();
    }

    ~SemAddOb() override
    {
        og::script::clear_pack_scripts();
        current_game = previous_;
    }

    const std::vector<std::string>& vm_log()
    {
        return world.scripts().host().log();
    }

    GameWorld world;
    GameplayContext context{};
    og::sim::SimEventLog events;
    GameplayContext* previous_ = nullptr;
};

}  // namespace

// game_world.cpp:610. add_ob files a Weapon in the weapon list and fires no
// spawn hook; a Treasure goes to the object list silently; a Living goes to
// the object list and fires on_entity_spawn exactly once.
TEST_F(SemAddOb, add_ob_files_weapons_apart_and_only_livings_fire_the_spawn_hook)
{
    ASSERT_TRUE(vm_log().empty());
    walker* knife = world.add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    EXPECT_TRUE(in_list(world.weaplist, knife)) << "Weapon arm: in weaplist";
    EXPECT_FALSE(in_list(world.oblist, knife)) << "Weapon arm: not in oblist";
    EXPECT_TRUE(vm_log().empty()) << "Weapon arm: no on_entity_spawn";

    walker* gold = world.add_ob(Order::Treasure, FAMILY_GOLD_BAR);
    ASSERT_NE(nullptr, gold);
    EXPECT_TRUE(in_list(world.oblist, gold)) << "Treasure arm: in oblist";
    EXPECT_TRUE(vm_log().empty()) << "Treasure arm: no on_entity_spawn";

    walker* soldier = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier);
    EXPECT_TRUE(in_list(world.oblist, soldier)) << "Living arm: in oblist";
    ASSERT_EQ(1u, vm_log().size()) << "Living arm: dispatched exactly once";
    EXPECT_EQ("spawn", vm_log().front());
}

// ===========================================================================
// game_world.cpp: grid passability
// ===========================================================================

// game_world.cpp:697. With no grid loaded, an ethereal walker still passes
// (ethereal is checked first); everyone else is stopped.
TEST(SemWorld, an_ethereal_walker_passes_a_floor_with_no_grid)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* ghost = add_living(tw, FAMILY_SOLDIER, 0, 84, 84);
    walker* plain = add_living(tw, FAMILY_SOLDIER, 0, 120, 84);
    ASSERT_TRUE(ghost && plain);
    ghost->stats()->clear_bit_flags();
    ghost->stats()->set_bit_flags(BIT_ETHEREAL, 1);
    plain->stats()->clear_bit_flags();
    world.grid.free();  // pixmaxx/pixmaxy stay: only the grid is missing
    ASSERT_FALSE(world.grid.valid());
    EXPECT_TRUE(world.query_grid_passable(84, 84, ghost))
        << "grid missing, ethereal arm: passes";
    EXPECT_FALSE(world.query_grid_passable(120, 84, plain))
        << "control: grid missing, not ethereal: stopped";
}

// game_world.cpp:709. A box whose far edge lies exactly on a cell boundary
// does not touch the next cell: a 16x16 walker aligned to cell (1,1) ignores
// walls in cells (2,1) and (1,2); one pixel over, it overhangs into them.
TEST(SemWorld, a_box_ending_on_a_cell_boundary_does_not_touch_the_next_cell)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    paint(world, 2, 1, PIX_H_WALL1);
    paint(world, 1, 2, PIX_H_WALL1);
    walker* s = add_living(tw, FAMILY_SOLDIER, 0, 16, 16);
    ASSERT_NE(nullptr, s);
    s->stats()->clear_bit_flags();
    EXPECT_TRUE(world.query_grid_passable(16, 16, s))
        << "aligned arm: the box 16..32 tests cell 1 only, on both axes";
    EXPECT_FALSE(world.query_grid_passable(17, 16, s))
        << "control: x 17..33 overhangs into the wall at (2,1)";
    EXPECT_FALSE(world.query_grid_passable(16, 17, s))
        << "control: y 17..33 overhangs into the wall at (1,2)";
}

// game_world.cpp:722. Every ground byte of the walkable case list is open to
// a plain walker.
TEST(SemWorld, every_ground_tile_is_open_to_a_plain_walker)
{
    static constexpr unsigned char kGround[] = {
        PIX_GRASS1, PIX_GRASS2, PIX_GRASS3, PIX_GRASS4, PIX_GRASS_DARK_1,
        PIX_GRASS_DARK_2, PIX_GRASS_DARK_3, PIX_GRASS_DARK_4,
        PIX_GRASS_DARK_LL, PIX_GRASS_DARK_UR, PIX_GRASS_DARK_B1,
        PIX_GRASS_DARK_B2, PIX_GRASS_DARK_BR, PIX_GRASS_DARK_R1,
        PIX_GRASS_DARK_R2, PIX_GRASS_RUBBLE, PIX_GRASS1_DAMAGED,
        PIX_GRASS_LIGHT_1, PIX_GRASS_LIGHT_TOP, PIX_GRASS_LIGHT_RIGHT_TOP,
        PIX_GRASS_LIGHT_RIGHT, PIX_GRASS_LIGHT_RIGHT_BOTTOM,
        PIX_GRASS_LIGHT_BOTTOM, PIX_GRASS_LIGHT_LEFT_BOTTOM,
        PIX_GRASS_LIGHT_LEFT, PIX_GRASS_LIGHT_LEFT_TOP, PIX_GRASSWATER_LL,
        PIX_GRASSWATER_LR, PIX_GRASSWATER_UL, PIX_GRASSWATER_UR,
        PIX_PAVEMENT1, PIX_PAVEMENT2, PIX_PAVEMENT3, PIX_COBBLE_1,
        PIX_COBBLE_2, PIX_COBBLE_3, PIX_COBBLE_4, PIX_FLOOR_PAVEL,
        PIX_FLOOR_PAVER, PIX_FLOOR_PAVEU, PIX_FLOOR_PAVED, PIX_PAVESTEPS1,
        PIX_PAVESTEPS2, PIX_PAVESTEPS2L, PIX_PAVESTEPS2R, PIX_FLOOR1,
        PIX_CARPET_LL, PIX_CARPET_B, PIX_CARPET_LR, PIX_CARPET_UR,
        PIX_CARPET_U, PIX_CARPET_UL, PIX_CARPET_L, PIX_CARPET_M,
        PIX_CARPET_M2, PIX_CARPET_R, PIX_CARPET_SMALL_HOR,
        PIX_CARPET_SMALL_VER, PIX_CARPET_SMALL_CUP, PIX_CARPET_SMALL_CAP,
        PIX_CARPET_SMALL_LEFT, PIX_CARPET_SMALL_RIGHT, PIX_CARPET_SMALL_TINY,
        PIX_DIRT_1, PIX_DIRTGRASS_UL1, PIX_DIRTGRASS_UR1, PIX_DIRTGRASS_LL1,
        PIX_DIRTGRASS_LR1, PIX_DIRT_DARK_1, PIX_DIRTGRASS_DARK_UL1,
        PIX_DIRTGRASS_DARK_UR1, PIX_DIRTGRASS_DARK_LL1,
        PIX_DIRTGRASS_DARK_LR1, PIX_PATH_1, PIX_PATH_2, PIX_PATH_3,
        PIX_PATH_4,
    };
    static_assert(std::size(kGround) == 77);
    for (unsigned char tile : kGround)
        EXPECT_TRUE(passable_over(tile, Probe::Plain))
            << "ground byte " << int(tile) << " must be open to a plain walker";
}

// game_world.cpp:803. Tree crowns stop a plain walker and an arrow; a
// forest-walker, a permanent flyer and a walker with flight left pass. All
// five crown bytes.
TEST(SemWorld, tree_crowns_pass_forestwalkers_and_flyers_only)
{
    for (unsigned char tile : std::initializer_list<unsigned char>{PIX_TREE_M1, PIX_TREE_ML, PIX_TREE_MR,
                               PIX_TREE_MT, PIX_TREE_T1})
    {
        EXPECT_FALSE(passable_over(tile, Probe::Plain))
            << "crown " << int(tile) << ": plain arm";
        EXPECT_TRUE(passable_over(tile, Probe::Forestwalk))
            << "crown " << int(tile) << ": forestwalk arm";
        EXPECT_TRUE(passable_over(tile, Probe::Flyer))
            << "crown " << int(tile) << ": BIT_FLYING arm";
        EXPECT_TRUE(passable_over(tile, Probe::TempFlight))
            << "crown " << int(tile) << ": flight_left arm";
        EXPECT_FALSE(passable_over(tile, Probe::Arrow))
            << "crown " << int(tile) << ": weapon arm (crowns stop arrows)";
    }
}

// game_world.cpp:813. A trunk stops a plain walker but lets weapons,
// forest-walkers and flyers (permanent or temporary) through.
TEST(SemWorld, a_tree_trunk_passes_weapons_forestwalkers_and_flyers)
{
    EXPECT_FALSE(passable_over(PIX_TREE_B1, Probe::Plain)) << "plain arm";
    EXPECT_TRUE(passable_over(PIX_TREE_B1, Probe::Arrow)) << "weapon arm";
    EXPECT_TRUE(passable_over(PIX_TREE_B1, Probe::Forestwalk))
        << "forestwalk arm";
    EXPECT_TRUE(passable_over(PIX_TREE_B1, Probe::Flyer)) << "BIT_FLYING arm";
    EXPECT_TRUE(passable_over(PIX_TREE_B1, Probe::TempFlight))
        << "flight_left arm";
}

// game_world.cpp:824. Solid walls stop every non-ethereal walker and weapon.
TEST(SemWorld, solid_walls_stop_everything_not_ethereal)
{
    for (unsigned char tile : std::initializer_list<unsigned char>{PIX_H_WALL1, PIX_WALL2, PIX_WALL3,
                               PIX_WALL_LL, PIX_WALLTOP_H})
        for (Probe p : {Probe::Plain, Probe::Flyer, Probe::TempFlight,
                        Probe::Swimmer, Probe::Forestwalk, Probe::Arrow})
            EXPECT_FALSE(passable_over(tile, p))
                << "wall " << int(tile) << " must stop a " << probe_name(p);
}

// game_world.cpp:899. Low obstacles (wall sides, torches, braziers, columns,
// boulders) stop ground walkers, swimmers and forest-walkers included;
// weapons and flyers (permanent or temporary) cross them.
TEST(SemWorld, low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers)
{
    for (unsigned char tile :
         std::initializer_list<unsigned char>{PIX_WALLSIDE_L, PIX_WALLSIDE1, PIX_WALLSIDE_R, PIX_WALLSIDE_C,
          PIX_WALLSIDE_CRACK_C1, PIX_TORCH1, PIX_TORCH2, PIX_TORCH3,
          PIX_BRAZIER1, PIX_COLUMN1, PIX_COLUMN2, PIX_BOULDER_1,
          PIX_BOULDER_2, PIX_BOULDER_3, PIX_BOULDER_4})
    {
        for (Probe p : {Probe::Plain, Probe::Swimmer, Probe::Forestwalk})
            EXPECT_FALSE(passable_over(tile, p))
                << "obstacle " << int(tile) << " must stop a "
                << probe_name(p);
        for (Probe p : {Probe::Flyer, Probe::TempFlight, Probe::Arrow})
            EXPECT_TRUE(passable_over(tile, p))
                << "obstacle " << int(tile) << " must pass a "
                << probe_name(p);
    }
}

// game_world.cpp:919. The multi-floor bytes are walkable at the grid layer.
TEST(SemWorld, the_multi_floor_tiles_are_walkable_at_the_grid_layer)
{
    for (unsigned char tile :
         std::initializer_list<unsigned char>{PIX_AIR, PIX_GLASS, PIX_DROPBLOCK_UP, PIX_DROPBLOCK_RIGHT,
          PIX_DROPBLOCK_DOWN, PIX_DROPBLOCK_LEFT, PIX_ZSTAIR_UP,
          PIX_ZSTAIR_DOWN})
        EXPECT_TRUE(passable_over(tile, Probe::Plain))
            << "multi-floor byte " << int(tile) << " must be walkable";
}

// game_world.cpp:960. A decor plane whose size does not match the grid is
// ignored, and a decor byte past the registry changes nothing; the base tile
// answers.
TEST(SemWorld, a_mismatched_decor_plane_or_unknown_decor_id_is_ignored)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* s = add_living(tw, FAMILY_SOLDIER, 0, kProbeX, kProbeY);
    ASSERT_NE(nullptr, s);
    s->stats()->clear_bit_flags();
    s->set_sizex(1);
    s->set_sizey(1);

    const auto plane = [&world](int w, int h, unsigned char id) {
        PixieData& dp = world.decor;
        dp.free();
        dp.frames = 1;
        dp.w = static_cast<unsigned char>(w);
        dp.h = static_cast<unsigned char>(h);
        dp.data = std::make_unique<unsigned char[]>(
            static_cast<std::size_t>(w * h));
        std::fill_n(dp.data.get(), w * h, id);
    };

    plane(world.grid.w, world.grid.h, DECOR_TORCH1);
    ASSERT_FALSE(world.query_grid_passable(kProbeX, kProbeY, s))
        << "control: a matching torch plane blocks the ground walker";

    plane(40, 40, DECOR_TORCH1);
    EXPECT_TRUE(world.query_grid_passable(kProbeX, kProbeY, s))
        << "size mismatch arm: a 40x40 plane over a 32x32 grid is ignored";

    plane(world.grid.w, world.grid.h, DECOR_MAX);
    EXPECT_TRUE(world.query_grid_passable(kProbeX, kProbeY, s))
        << "id >= DECOR_MAX arm: decor id DECOR_MAX is ignored";
    plane(world.grid.w, world.grid.h, 255);
    EXPECT_TRUE(world.query_grid_passable(kProbeX, kProbeY, s))
        << "id >= DECOR_MAX arm: decor id 255 is ignored";
}

// game_world.cpp:1001. A dead walker never collides: its object check
// passes on a spot a live foe occupies, without touching the foe, and even
// where the world has no collision map at all.
TEST(SemWorld, a_dead_walker_never_collides)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* soldier = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    walker* orc = add_living(tw, FAMILY_ORC, 1, 100, 100);
    ASSERT_TRUE(soldier && orc);
    EXPECT_FALSE(world.query_object_passable(100, 100, soldier, 0, true))
        << "control: a live walker on the orc's spot is blocked";

    soldier->set_dead(1);
    const float orc_hp = orc->stats()->hitpoints();
    EXPECT_TRUE(world.query_object_passable(100, 100, soldier))
        << "dead arm: an occupied spot passes";
    EXPECT_EQ(orc_hp, orc->stats()->hitpoints());
    EXPECT_EQ(nullptr, soldier->collide_ob());

    std::unique_ptr<obmap> saved = std::move(world.myobmap);
    const bool dead_without_map =
        world.query_object_passable(100, 100, soldier);
    soldier->set_dead(0);
    const bool live_without_map =
        world.query_object_passable(100, 100, soldier);
    world.myobmap = std::move(saved);
    EXPECT_TRUE(dead_without_map)
        << "dead arm: passes before the collision map is consulted";
    EXPECT_FALSE(live_without_map)
        << "control: a live walker with no collision map is stopped";
}

// ===========================================================================
// game_world.cpp: sight
// ===========================================================================

namespace {

// Two soldiers on row 5, at cells (2,5) and (8,5), with `tile` at (5,5).
bool sight_across(unsigned char tile)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    paint(world, 5, 5, tile);
    walker* a = add_living(tw, FAMILY_SOLDIER, 0, 2 * GRID_SIZE, 5 * GRID_SIZE);
    walker* b = add_living(tw, FAMILY_SOLDIER, 0, 8 * GRID_SIZE, 5 * GRID_SIZE);
    return world.clear_sight_line(a, b);
}

}  // namespace

// game_world.cpp:1043 (opaque arm). Every tree crown, solid wall and
// arrow-slit wall between two walkers blocks their sight.
TEST(SemWorld, crowns_walls_and_arrow_walls_block_sight)
{
    for (unsigned char tile :
         std::initializer_list<unsigned char>{PIX_TREE_M1, PIX_TREE_ML, PIX_TREE_MR, PIX_TREE_MT, PIX_TREE_T1,
          PIX_H_WALL1, PIX_WALL2, PIX_WALL3, PIX_WALL_LL, PIX_WALLTOP_H,
          PIX_WALL4, PIX_WALL5, PIX_WALL_ARROW_GRASS, PIX_WALL_ARROW_FLOOR,
          PIX_WALL_ARROW_GRASS_DARK})
        EXPECT_FALSE(sight_across(tile))
            << "opaque arm: byte " << int(tile) << " must block sight";
}

// game_world.cpp:1043 (transparent arm). Trunks, water, lava, boulders,
// columns, torches, wall sides and unknown bytes do not block sight.
TEST(SemWorld, trunks_water_lava_and_low_obstacles_do_not_block_sight)
{
    for (unsigned char tile :
         std::initializer_list<unsigned char>{PIX_GRASS1, PIX_TREE_B1, PIX_WATER1, PIX_LAVA1, PIX_BOULDER_1,
          PIX_COLUMN1, PIX_WALLSIDE_L, PIX_TORCH1, static_cast<unsigned char>(255)})
        EXPECT_TRUE(sight_across(tile))
            << "transparent arm: byte " << int(tile) << " must not block";
}

// game_world.cpp:1072. Only the cells strictly between the walkers are
// tested: walkers in adjacent cells, or in one cell, see each other even
// with walls under their own feet.
TEST(SemWorld, walkers_in_the_same_or_adjacent_cells_always_see_each_other)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    paint(world, 2, 5, PIX_H_WALL1);
    paint(world, 3, 5, PIX_H_WALL1);
    walker* a = add_living(tw, FAMILY_SOLDIER, 0, 2 * GRID_SIZE, 5 * GRID_SIZE);
    walker* b = add_living(tw, FAMILY_SOLDIER, 0, 3 * GRID_SIZE, 5 * GRID_SIZE);
    walker* c = add_living(tw, FAMILY_SOLDIER, 0, 2 * GRID_SIZE + 2, 5 * GRID_SIZE);
    walker* d = add_living(tw, FAMILY_SOLDIER, 0, 4 * GRID_SIZE, 5 * GRID_SIZE);
    ASSERT_TRUE(a && b && c && d);
    EXPECT_TRUE(world.clear_sight_line(a, b))
        << "adjacent arm: walls under both walkers' own cells never block";
    EXPECT_TRUE(world.clear_sight_line(b, a)) << "adjacent arm, reversed";
    EXPECT_TRUE(world.clear_sight_line(a, c))
        << "same-cell arm: no intermediate cell";
    EXPECT_FALSE(world.clear_sight_line(a, d))
        << "control: the wall at (3,5) between (2,5) and (4,5) blocks";
}

// ===========================================================================
// game_world.cpp: floors
// ===========================================================================

// game_world.cpp:1122. Shrinking the floor count drops the upper floors (a
// later grow brings back empty grids, not the old ones); growing adds floors
// with empty grids.
TEST(SemWorld, shrinking_the_floor_count_drops_the_upper_floors)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.set_floor_count(3);
    ASSERT_EQ(3, world.floor_count());
    PixieData& top = world.grid_for_floor(2);
    top.frames = 1;
    top.w = 4;
    top.h = 4;
    top.data = std::make_unique<unsigned char[]>(16);
    ASSERT_TRUE(world.grid_for_floor(2).valid());

    world.set_floor_count(2);
    EXPECT_EQ(2, world.floor_count()) << "shrink arm: count follows";
    EXPECT_EQ(&world.grid, &world.grid_for_floor(2))
        << "shrink arm: floor 2 is gone (falls back to floor 0)";
    world.set_floor_count(3);
    EXPECT_EQ(3, world.floor_count());
    EXPECT_FALSE(world.grid_for_floor(2).valid())
        << "shrink arm: the dropped floor's grid did not survive";

    world.set_floor_count(1);
    ASSERT_EQ(1, world.floor_count());
    world.set_floor_count(3);
    EXPECT_EQ(3, world.floor_count()) << "grow arm: count follows";
    EXPECT_FALSE(world.grid_for_floor(1).valid()) << "grow arm: empty floor 1";
    EXPECT_FALSE(world.grid_for_floor(2).valid()) << "grow arm: empty floor 2";
}

// ===========================================================================
// game_world.cpp: the foe finders
// ===========================================================================

// game_world.cpp:1146 (spiral order). The quick search answers the first foe
// in spiral order, not the nearest: from (320,320) the spiral reaches the
// pile at (352,352) on its second probe, before (288,320) on its fifth.
TEST(SemWorld, the_quick_foe_search_answers_the_first_foe_in_spiral_order)
{
    TestGameWorld tw;
    open_field(tw, 40, 40);
    GameWorld& world = tw.world();
    walker* searcher = add_living(tw, FAMILY_SOLDIER, 0, 320, 320);
    walker* spiral_first = add_living(tw, FAMILY_ORC, 1, 352, 352);
    walker* nearer = add_living(tw, FAMILY_ORC, 1, 288, 320);
    ASSERT_TRUE(searcher && spiral_first && nearer);
    ASSERT_EQ(32, world.myobmap->obmapres);
    ASSERT_EQ(64, searcher->distance_to_ob(spiral_first));
    ASSERT_EQ(32, searcher->distance_to_ob(nearer));
    EXPECT_EQ(spiral_first, world.find_near_foe(searcher))
        << "spiral arm: the foe the spiral reaches first, though farther";
    EXPECT_EQ(nearer, world.find_nearest_foe(searcher))
        << "control: the full scan answers the nearer one";
}

// game_world.cpp:1146 (exhaustion). With no foe in ten legs of the spiral
// (and no map edge touched), the quick search falls back to the full scan.
TEST(SemWorld, the_quick_foe_search_falls_back_when_the_spiral_is_exhausted)
{
    TestGameWorld tw;
    open_field(tw, 60, 60);
    GameWorld& world = tw.world();
    walker* searcher = add_living(tw, FAMILY_SOLDIER, 0, 480, 480);
    walker* far_orc = add_living(tw, FAMILY_ORC, 1, 900, 900);
    ASSERT_TRUE(searcher && far_orc);
    EXPECT_EQ(far_orc, world.find_near_foe(searcher))
        << "exhausted arm: the full scan finds the foe outside the spiral";
}

// game_world.cpp:1146 (no obmap). A world without a collision map answers
// the full nearest-foe scan.
TEST(SemWorld, the_quick_foe_search_without_an_obmap_answers_the_nearest)
{
    TestGameWorld tw;
    open_field(tw, 40, 40);
    GameWorld& world = tw.world();
    walker* searcher = add_living(tw, FAMILY_SOLDIER, 0, 320, 320);
    walker* spiral_first = add_living(tw, FAMILY_ORC, 1, 352, 352);
    walker* nearer = add_living(tw, FAMILY_ORC, 1, 288, 320);
    ASSERT_TRUE(searcher && spiral_first && nearer);
    std::unique_ptr<obmap> saved = std::move(world.myobmap);
    walker* found = world.find_near_foe(searcher);
    world.myobmap = std::move(saved);
    EXPECT_EQ(nearer, found)
        << "no-obmap arm: the nearest foe, not the spiral's first";
}

namespace {

// The quick-search candidate stage: searcher at (320,320), a hostile orc Z
// at (288,320) (spiral probe 5), and the candidate in the first probe's pile
// (352,320).
struct NearFoeStage
{
    TestGameWorld tw;
    walker* searcher = nullptr;
    walker* later = nullptr;

    NearFoeStage()
    {
        open_field(tw, 40, 40);
        searcher = add_living(tw, FAMILY_SOLDIER, 0, 320, 320);
        later = add_living(tw, FAMILY_ORC, 1, 288, 320);
    }

    bool in_first_pile(const walker* w)
    {
        const std::list<walker*>& pile =
            tw.world().myobmap->obmap_get_list(352, 320, 0);
        return std::find(pile.begin(), pile.end(), w) != pile.end();
    }
};

}  // namespace

// game_world.cpp:1192. The quick search acquires a hostile Generator.
TEST(SemWorld, the_quick_foe_search_acquires_a_hostile_generator)
{
    NearFoeStage st;
    walker* tent = st.tw.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(1);
    tent->set_sizex(16);
    tent->set_sizey(16);
    tent->setxy(352, 320);
    ASSERT_TRUE(st.in_first_pile(tent));
    EXPECT_EQ(tent, st.tw.world().find_near_foe(st.searcher))
        << "Generator arm: a hostile tent in the first pile is acquired";
}

// game_world.cpp:1192. The quick search skips a friendly walker, a dead foe
// and a hostile weapon in a pile it probes, and goes on to the later foe.
TEST(SemWorld, the_quick_foe_search_skips_friends_corpses_and_weapons)
{
    {
        NearFoeStage st;
        walker* friend_ = add_living(st.tw, FAMILY_SOLDIER, 0, 352, 320);
        ASSERT_TRUE(st.in_first_pile(friend_));
        EXPECT_EQ(st.later, st.tw.world().find_near_foe(st.searcher))
            << "friendly arm: skipped";
    }
    {
        NearFoeStage st;
        walker* corpse = add_living(st.tw, FAMILY_ORC, 1, 352, 320);
        ASSERT_TRUE(st.in_first_pile(corpse));
        corpse->set_dead(1);
        EXPECT_EQ(st.later, st.tw.world().find_near_foe(st.searcher))
            << "dead arm: skipped";
    }
    {
        NearFoeStage st;
        walker* knife = st.tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
        ASSERT_NE(nullptr, knife);
        knife->set_team_num(1);
        knife->set_sizex(16);
        knife->set_sizey(16);
        knife->setxy(352, 320);
        ASSERT_TRUE(st.in_first_pile(knife))
            << "the staging needs the knife filed in the probed pile";
        EXPECT_EQ(st.later, st.tw.world().find_near_foe(st.searcher))
            << "weapon arm: a hostile knife in the pile is skipped";
    }
}

// game_world.cpp:1192. A cloaked foe (invisibility 300: die of 15) whose roll
// comes up non-zero is skipped, at the cost of exactly one draw.
TEST(SemWorld, the_quick_foe_search_skips_a_cloaked_foe_that_wins_its_roll)
{
    NearFoeStage st;
    GameWorld& world = st.tw.world();
    walker* cloaked = add_living(st.tw, FAMILY_ORC, 1, 352, 320);
    ASSERT_TRUE(st.in_first_pile(cloaked));
    cloaked->set_invisibility_left(300);

    std::uint32_t seed = 1;
    for (;; ++seed)
    {
        og::sim::SimRandom probe(seed);
        if (probe.next(15) != 0)
            break;
    }
    og::sim::SimRandom expected(seed);
    (void)expected.next(15);  // the one draw the cloak roll costs

    world.rng_.state_ = seed;
    EXPECT_EQ(st.later, world.find_near_foe(st.searcher))
        << "cloak arm: a non-zero roll hides the foe";
    EXPECT_EQ(expected.state_, world.rng_.state_)
        << "cloak arm: exactly one draw for the cloaked candidate";
}

// game_world.cpp:1236. On equal distance the full scan keeps the walker
// earlier in the object list.
TEST(SemWorld, the_full_foe_scan_breaks_a_tie_by_list_order)
{
    for (bool a_first : {true, false})
    {
        TestGameWorld tw;
        open_field(tw);
        GameWorld& world = tw.world();
        walker* searcher = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
        walker* first = nullptr;
        walker* second = nullptr;
        if (a_first)
        {
            first = add_living(tw, FAMILY_ORC, 1, 100, 140);
            second = add_living(tw, FAMILY_ORC, 1, 140, 100);
        }
        else
        {
            first = add_living(tw, FAMILY_ORC, 1, 140, 100);
            second = add_living(tw, FAMILY_ORC, 1, 100, 140);
        }
        ASSERT_TRUE(searcher && first && second);
        ASSERT_EQ(searcher->distance_to_ob(first),
                  searcher->distance_to_ob(second));
        EXPECT_EQ(first, world.find_nearest_foe(searcher))
            << "tie arm: the earlier walker in oblist wins (a_first="
            << a_first << ")";
    }
}

// game_world.cpp:1236. The full scan is floor-blind: a foe on another floor
// is still found.
TEST(SemWorld, the_full_foe_scan_finds_a_foe_on_another_floor)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    world.set_floor_count(2);
    walker* searcher = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    walker* orc = add_living(tw, FAMILY_ORC, 1, 160, 100);
    ASSERT_TRUE(searcher && orc);
    orc->set_floor(1);
    ASSERT_EQ(0, searcher->floor());
    ASSERT_EQ(1, orc->floor());
    EXPECT_EQ(orc, world.find_nearest_foe(searcher))
        << "floor arm: the full scan does not filter by floor";
}

// game_world.cpp:1270 (reach arm only; the floor arm is issue #456). The
// corpse finder reaches a stain only under squared distance 800: exactly 800
// and 841 are out of reach, 784 is in.
TEST(SemWorld, the_corpse_finder_reaches_only_under_squared_distance_800)
{
    TestGameWorld tw;
    open_field(tw, 60, 60);
    GameWorld& world = tw.world();
    walker* finder = add_living(tw, FAMILY_SOLDIER, 0, 400, 400);
    walker* stain = world.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_TRUE(finder && stain);
    stain->set_sizex(16);
    stain->set_sizey(16);

    stain->setxy(400, 429);
    ASSERT_EQ(841, finder->distance_to_ob_center(stain));
    EXPECT_EQ(nullptr, world.find_nearest_blood(finder)) << "841: out of reach";
    stain->setxy(420, 420);
    ASSERT_EQ(800, finder->distance_to_ob_center(stain));
    EXPECT_EQ(nullptr, world.find_nearest_blood(finder))
        << "exactly 800: out of reach (strict)";
    stain->setxy(428, 400);
    ASSERT_EQ(784, finder->distance_to_ob_center(stain));
    EXPECT_EQ(stain, world.find_nearest_blood(finder)) << "784: in reach";
}

// game_world.cpp:1297. Classic quirk (kept): the leader finder has no dead
// filter, so a hero corpse that still carries its seat is the answer.
TEST(SemWorld, classic_the_player_finder_takes_a_dead_hero_with_its_seat)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* follower = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    walker* corpse = add_living(tw, FAMILY_SOLDIER, 0, 120, 100);
    walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 200, 100);
    ASSERT_TRUE(follower && corpse && hero);
    corpse->set_user(0);
    hero->set_user(1);
    corpse->set_dead(1);
    EXPECT_EQ(corpse, world.find_nearest_player(follower))
        << "dead arm (classic): the nearer seat corpse is returned";
}

// game_world.cpp:1297. A dormant seat walker is skipped by the leader finder.
TEST(SemWorld, the_player_finder_skips_a_dormant_seat_walker)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* follower = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    walker* sleeper = add_living(tw, FAMILY_SOLDIER, 0, 120, 100);
    walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 200, 100);
    ASSERT_TRUE(follower && sleeper && hero);
    sleeper->set_user(0);
    hero->set_user(1);
    ASSERT_EQ(sleeper, world.find_nearest_player(follower));
    sleeper->set_dormant(true);
    EXPECT_EQ(hero, world.find_nearest_player(follower))
        << "dormant arm: skipped, the farther awake one answers";
}

// game_world.cpp:1325. find_in_range lists every live, awake, visible walker
// within range inclusive, the searcher itself included.
TEST(SemWorld, find_in_range_is_inclusive_counts_the_searcher_and_skips_the_inert)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    constexpr std::int32_t kRange = 50;
    walker* s = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    walker* at_range = add_living(tw, FAMILY_ORC, 1, 150, 100);
    walker* past = add_living(tw, FAMILY_ORC, 1, 151, 100);
    walker* dormant = add_living(tw, FAMILY_ELF, 0, 101, 100);
    walker* dead = add_living(tw, FAMILY_ORC, 1, 102, 100);
    walker* hidden = add_living(tw, FAMILY_ORC, 1, 103, 100);
    ASSERT_TRUE(s && at_range && past && dormant && dead && hidden);
    dormant->set_dormant(true);
    dead->set_dead(1);
    hidden->set_hidden(true);
    ASSERT_EQ(kRange, s->distance_to_ob(at_range));

    std::int32_t n = -1;
    const std::list<walker*> got = world.find_in_range(world.oblist, kRange, &n, s);
    EXPECT_TRUE(same_list(got, {s, at_range}))
        << "exactly {searcher, walker at range}: range inclusive, searcher "
           "included, dead/dormant/hidden excluded (got " << got.size() << ")";
    EXPECT_EQ(2, n);
}

// game_world.cpp:1349. find_foes_in_range lists the hostile Livings and
// Generators in range, cloaked ones included with no roll; friends, corpses
// and weapons are left out.
TEST(SemWorld, find_foes_in_range_keeps_hostile_livings_and_generators_only)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* s = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    walker* orc = add_living(tw, FAMILY_ORC, 1, 120, 100);
    walker* tent = world.add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(1);
    tent->setxy(140, 100);
    walker* friend_ = add_living(tw, FAMILY_SOLDIER, 0, 110, 100);
    std::unique_ptr<walker> knife_owner =
        world.entity_factory(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife_owner);
    walker* knife = knife_owner.get();
    knife->set_team_num(1);
    knife->setxy(105, 100);
    world.oblist.push_back(std::move(knife_owner));
    walker* cloaked = add_living(tw, FAMILY_ORC, 1, 130, 100);
    walker* corpse = add_living(tw, FAMILY_ORC, 1, 115, 100);
    ASSERT_TRUE(s && orc && friend_ && cloaked && corpse);
    cloaked->set_invisibility_left(300);
    corpse->set_dead(1);

    world.rng_.state_ = 12345u;
    std::int32_t n = -1;
    const std::list<walker*> got =
        world.find_foes_in_range(world.oblist, 64, &n, s);
    EXPECT_TRUE(same_list(got, {orc, tent, cloaked}))
        << "exactly {orc, tent, cloaked orc}: friend, hostile weapon and "
           "corpse excluded (got " << got.size() << ")";
    EXPECT_EQ(3, n);
    EXPECT_EQ(12345u, world.rng_.state_) << "no cloak roll is drawn";
}

// game_world.cpp:1377. find_foe_weapons_in_range keeps hostile weapons only:
// a hostile Living filed in the weapon list is left out.
TEST(SemWorld, find_foe_weapons_in_range_leaves_out_a_hostile_living)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* s = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    walker* orc = world.add_weap_ob(Order::Living, FAMILY_ORC);
    walker* knife = world.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(s && orc && knife);
    orc->set_team_num(1);
    orc->setxy(110, 100);
    knife->set_team_num(1);
    knife->setxy(120, 100);
    std::int32_t n = -1;
    const std::list<walker*> got =
        world.find_foe_weapons_in_range(world.weaplist, 64, &n, s);
    EXPECT_TRUE(same_list(got, {knife}))
        << "hostile-living arm: only the knife (got " << got.size() << ")";
    EXPECT_EQ(1, n);
}
