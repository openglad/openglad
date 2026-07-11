#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <algorithm>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>
#include <gtest/gtest.h>
#include "test_gameplay_context_scope.h"

namespace {

struct SpecialsFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    SpecialsFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

living* add_living(SpecialsFixture& fx, char family, unsigned char team)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(96, 96);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

walker* add_marker(SpecialsFixture& fx, walker* owner, int x, int y, int life)
{
    walker* m = fx.level.add_ob(Order::FX, FAMILY_MARKER);
    m->set_owner(owner);
    m->set_dead(0);
    m->setxy(x, y);
    m->set_lifetime(life);
    return m;
}

} // namespace

TEST(WalkerSpecialsUnit, walker_specials_r11_special_and_teleport_paths)
{
    SpecialsFixture fx;
    living* w = add_living(fx, FAMILY_CLERIC, 0);
    ASSERT_TRUE(w != nullptr);

    w->set_dead(1);
    ASSERT_TRUE(!w->special());
    w->set_dead(0);

    walker* weapon = fx.level.add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(weapon != nullptr);
    if (weapon) {
        weapon->set_dead(0);
        ASSERT_TRUE(!weapon->special());
    }

    // marker teleport success with marker expiry (lines 86-90)
    w->setxy(20, 20);
    walker* marker = add_marker(fx, w, 140, 140, 1);
    ASSERT_TRUE(marker != nullptr);
    ASSERT_TRUE(w->teleport());
    ASSERT_TRUE(marker->dead() == 1);

    // no marker path: random passable placement
    ASSERT_TRUE(w->teleport());

    // ranged teleport success path
    (void)w->teleport_ranged(40);
}

TEST(WalkerSpecialsUnit, walker_specials_r11_turn_undead_paths)
{
    SpecialsFixture fx;
    living* cleric = add_living(fx, FAMILY_CLERIC, 0);
    ASSERT_TRUE(cleric != nullptr);

    // No targets branch -> -1
    ASSERT_TRUE(cleric->turn_undead(40, 5) == -1);

    // Undead target in range triggers kill path.
    living* skeleton = add_living(fx, FAMILY_SKELETON, 1);
    skeleton->setxy(100, 96);
    skeleton->stats()->set_level(1);
    const std::int32_t killed = cleric->turn_undead(40, 5);
    ASSERT_TRUE(killed >= 0);
}

// ---------------------------------------------------------------------------
// Teleport destination probing (bugs A6/A7).
//
// A6: the destination probe must be ground-rules (the transient flight_left
//     bypass may not bless trees/boulders/water as landing spots) and
//     side-effect-free (a rejected probe must not eat treasures overlapping
//     the probed spot, the legacy ob_pass_check behavior).
// A7: on stacked-floor levels the random blink considers EVERY floor; on
//     single-floor levels the RNG stream is byte-identical to the legacy code
//     (exactly two draws per attempt, no floor draw).
// ---------------------------------------------------------------------------

namespace {

// Spawn a living with sane sizes BEFORE the obmap-registering setxy (the
// legacy add_living helper above registers at default size first).
living* add_actor(SpecialsFixture& fx, char family, unsigned char team,
                  int x, int y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    out->setxy(x, y);
    return out;
}

// Fill a floor's grid with one tile. Floor 0 repaints the base grid in
// place; upper floors get a fresh grid of the base dimensions (PixieData
// takes ownership of the heap buffer), mirroring tests/unit/test_zaxis.cpp.
void paint_floor(GameWorld& w, int floor, unsigned char tile)
{
    const int gw = w.grid.w;
    const int gh = w.grid.h;
    const std::size_t size = static_cast<std::size_t>(gw) * gh;
    if (floor == 0)
    {
        std::fill(w.grid.data.get(), w.grid.data.get() + size, tile);
        return;
    }
    auto* buf = new unsigned char[size];
    std::fill(buf, buf + size, tile);
    w.grid_for_floor(floor) = PixieData(1, static_cast<unsigned char>(gw),
                                        static_cast<unsigned char>(gh), buf);
    w.smoother_for_floor(floor).set_target(w.grid_for_floor(floor));
}

void paint_cell(GameWorld& w, int floor, int cx, int cy, unsigned char tile)
{
    w.grid_for_floor(floor).data[cx + cy * w.grid.w] = tile;
}

// Ground-rules landing check used by the assertions below: grid passability
// with the transient flight bypass masked.
bool grounded_passable(GameWorld& w, walker* ob)
{
    const short saved = ob->flight_left();
    ob->set_flight_left(0);
    const bool ok = w.query_grid_passable(static_cast<float>(ob->xpos()),
                                          static_cast<float>(ob->ypos()),
                                          ob, ob->floor());
    ob->set_flight_left(saved);
    return ok;
}

} // namespace

// A6: a mage blinking while a flight potion is active must never land inside
// boulders — the legacy probe accepted them under the flight bypass and the
// mage got permanently stuck when the flight ticks ran out. 30-seed sweep.
TEST(WalkerSpecialsUnit, teleport_never_lands_on_obstacles_despite_flight)
{
    SpecialsFixture fx;
    GameWorld& w = fx.level.world();
    living* mage = add_actor(fx, FAMILY_MAGE, 0, 96, 96);

    // Boulder arena with a single grass row at cell y=30.
    paint_floor(w, 0, PIX_BOULDER_1);
    for (int cx = 0; cx < w.grid.w; ++cx)
        paint_cell(w, 0, cx, 30, PIX_GRASS1);

    mage->set_flight_left(200); // transient flight ACTIVE during the blink

    int landings = 0;
    for (std::uint32_t seed = 1; seed <= 30; ++seed)
    {
        mage->setxy(96, 96);
        w.rng_.state_ = seed * 2654435761u;
        const bool ok = mage->teleport();
        ASSERT_EQ(200, mage->flight_left())
            << "the probe must restore flight_left, seed " << seed;
        if (!ok)
            continue; // a fully-blocked draw run is legal, just unlikely
        ++landings;
        EXPECT_TRUE(grounded_passable(w, mage))
            << "flight-assisted blink landed on impassable ground, seed "
            << seed << " at " << mage->xpos() << "," << mage->ypos();
        EXPECT_EQ(30 * GRID_SIZE, mage->ypos())
            << "only the grass row is a legal landing, seed " << seed;
    }
    EXPECT_GE(landings, 20) << "sweep barely exercised the landing path";
}

// A6: probing a blocked marker destination must neither eat treasure lying
// there nor land the caster on the occupant. The legacy probe routed through
// ob_pass_check, which consumed the potion as a probe side effect.
TEST(WalkerSpecialsUnit, teleport_blocked_marker_denied_without_probe_eat)
{
    SpecialsFixture fx;
    GameWorld& w = fx.level.world();

    // Boulder arena with exactly one clear cell at (20,20) -> pixel 320,320.
    paint_floor(w, 0, PIX_BOULDER_1);
    paint_cell(w, 0, 20, 20, PIX_GRASS1);

    living* mage = add_actor(fx, FAMILY_MAGE, 0, 96, 96);
    walker* marker = add_marker(fx, mage, 320, 320, 5);
    ASSERT_NE(marker, nullptr);

    // Register the loot BEFORE the occupant: the legacy ob_pass_check walks
    // the obmap pile in insertion order and ate the treasure before the
    // occupant blocked the probe.
    walker* loot = fx.level.add_ob(Order::Treasure, FAMILY_FLIGHT_POTION);
    ASSERT_NE(loot, nullptr);
    loot->set_dead(0);
    loot->set_sizex(16);
    loot->set_sizey(16);
    loot->setxy(320, 320);

    living* occupant = add_actor(fx, FAMILY_SOLDIER, 1, 320, 320);
    ASSERT_NE(occupant, nullptr);

    w.rng_.state_ = 42;
    EXPECT_FALSE(mage->teleport())
        << "sole clear cell is occupied; the blink must fail outright";
    EXPECT_EQ(96, mage->xpos());
    EXPECT_EQ(96, mage->ypos());
    EXPECT_FALSE(loot->dead())
        << "destination probing must not eat treasure at the probed spot";
    EXPECT_EQ(0, mage->flight_left())
        << "probe-eaten flight potion granted flight to the caster";
    EXPECT_EQ(5, marker->lifetime())
        << "a denied marker blink must not consume the marker";
}

// A6: a marker sitting on impassable ground is a denied destination even
// while the caster has flight ticks (legacy code accepted it and stranded
// the caster there once flight expired).
TEST(WalkerSpecialsUnit, teleport_marker_over_boulder_denied_despite_flight)
{
    SpecialsFixture fx;
    GameWorld& w = fx.level.world();

    paint_floor(w, 0, PIX_GRASS1);
    paint_cell(w, 0, 20, 20, PIX_BOULDER_1); // marker cell: solid

    living* mage = add_actor(fx, FAMILY_MAGE, 0, 96, 96);
    mage->set_flight_left(50);
    walker* marker = add_marker(fx, mage, 320, 320, 5);
    ASSERT_NE(marker, nullptr);

    w.rng_.state_ = 7;
    ASSERT_TRUE(mage->teleport()) << "random fallback over a grass arena";
    EXPECT_FALSE(mage->xpos() == 320 && mage->ypos() == 320)
        << "blink landed on the boulder under the marker";
    EXPECT_TRUE(grounded_passable(w, mage));
    EXPECT_EQ(5, marker->lifetime()) << "denied marker must not be consumed";
}

// A7: on a stacked-floor level whose caster floor has no clear cell, the
// random blink must reach the other floors — and across a 30-seed sweep it
// must reach EVERY upper floor. Also pins the obmap re-bucketing order
// (change_floor before setxy).
TEST(WalkerSpecialsUnit, teleport_reaches_all_floors_on_multifloor_levels)
{
    SpecialsFixture fx;
    GameWorld& w = fx.level.world();
    w.set_floor_count(3);
    ASSERT_EQ(3, w.floor_count());

    paint_floor(w, 0, PIX_BOULDER_1); // caster floor: fully blocked
    paint_floor(w, 1, PIX_GRASS1);
    paint_floor(w, 2, PIX_GRASS1);

    living* mage = add_actor(fx, FAMILY_MAGE, 0, 96, 96);

    bool saw_floor1 = false;
    bool saw_floor2 = false;
    for (std::uint32_t seed = 1; seed <= 30; ++seed)
    {
        mage->change_floor(0);
        mage->setxy(96, 96);
        w.rng_.state_ = seed * 2654435761u + 17u;
        ASSERT_TRUE(mage->teleport()) << "grass upper floors exist, seed " << seed;
        ASSERT_NE(0, mage->floor())
            << "floor 0 has no clear cell; the blink must cross floors, seed "
            << seed;
        EXPECT_TRUE(grounded_passable(w, mage)) << "seed " << seed;
        saw_floor1 |= (mage->floor() == 1);
        saw_floor2 |= (mage->floor() == 2);

        // The walker must be obmap-bucketed on its NEW floor at its NEW spot.
        ASSERT_NE(w.myobmap, nullptr);
        const std::list<walker*>& pile = w.myobmap->obmap_get_list(
            mage->xpos(), mage->ypos(), mage->floor());
        EXPECT_TRUE(std::find(pile.begin(), pile.end(), mage) != pile.end())
            << "teleported walker not re-bucketed on its landing floor, seed "
            << seed;
    }
    EXPECT_TRUE(saw_floor1) << "uniform floor draw never chose floor 1 in 30 seeds";
    EXPECT_TRUE(saw_floor2) << "uniform floor draw never chose floor 2 in 30 seeds";
}

// A7 parity gate: on single-floor levels the blink must consume EXACTLY the
// legacy two RNG draws per attempt (x then y, no floor draw) and land where
// the legacy stream lands — byte-identity of every single-floor golden.
TEST(WalkerSpecialsUnit, teleport_single_floor_rng_stream_is_legacy_shaped)
{
    SpecialsFixture fx;
    GameWorld& w = fx.level.world();
    paint_floor(w, 0, PIX_GRASS1); // uniformly clear: first attempt accepted

    living* mage = add_actor(fx, FAMILY_MAGE, 0, 96, 96);

    const std::uint32_t seed = 0xC0FFEEu;
    std::uint32_t s = seed;
    auto lcg = [&s](std::uint32_t max) {
        s = s * 1103515245u + 12345u;
        return (s >> 16) % max;
    };
    const std::int32_t expect_x =
        static_cast<std::int32_t>(lcg(static_cast<std::uint32_t>(w.grid.w))) * GRID_SIZE;
    const std::int32_t expect_y =
        static_cast<std::int32_t>(lcg(static_cast<std::uint32_t>(w.grid.h))) * GRID_SIZE;

    w.rng_.state_ = seed;
    ASSERT_TRUE(mage->teleport());
    EXPECT_EQ(expect_x, mage->xpos());
    EXPECT_EQ(expect_y, mage->ypos());
    EXPECT_EQ(0, mage->floor());
    EXPECT_EQ(s, w.rng_.state_)
        << "single-floor teleport consumed extra RNG draws (parity breaker)";
}

// A6 for the ranged blink (skeletons): destination follows the same
// ground-rules probe, and the hop stays on the caster's floor by design.
TEST(WalkerSpecialsUnit, teleport_ranged_stays_on_floor_and_off_obstacles)
{
    SpecialsFixture fx;
    GameWorld& w = fx.level.world();
    w.set_floor_count(2);

    paint_floor(w, 0, PIX_GRASS1);
    paint_floor(w, 1, PIX_GRASS1);
    // Sprinkle boulders around the caster so rejected draws exist.
    for (int cx = 8; cx <= 16; cx += 2)
        for (int cy = 8; cy <= 16; cy += 2)
            paint_cell(w, 1, cx, cy, PIX_BOULDER_1);

    living* skeleton = add_actor(fx, FAMILY_SKELETON, 1, 0, 0);
    skeleton->change_floor(1);
    skeleton->setxy(12 * GRID_SIZE, 12 * GRID_SIZE);
    skeleton->set_flight_left(30); // flight must not bless boulder landings

    for (std::uint32_t seed = 1; seed <= 30; ++seed)
    {
        skeleton->setxy(12 * GRID_SIZE, 12 * GRID_SIZE);
        w.rng_.state_ = seed * 40503u + 3u;
        if (!skeleton->teleport_ranged(90))
            continue;
        ASSERT_EQ(1, skeleton->floor())
            << "ranged escape hop must stay on the caster's floor, seed " << seed;
        EXPECT_TRUE(grounded_passable(w, skeleton)) << "seed " << seed;
    }
}

// Probe fidelity with ob_pass_check corner cases: BIT_NO_COLLIDE casters may
// blink onto occupied ground (legacy behavior kept), and a height-disjoint
// hoverer above the landing spot does not block a grounded arrival.
TEST(WalkerSpecialsUnit, teleport_probe_keeps_no_collide_and_height_gate)
{
    SpecialsFixture fx;
    GameWorld& w = fx.level.world();

    // Sole clear cell at (20,20) -> pixel 320,320, occupied by a soldier.
    paint_floor(w, 0, PIX_BOULDER_1);
    paint_cell(w, 0, 20, 20, PIX_GRASS1);
    living* occupant = add_actor(fx, FAMILY_SOLDIER, 1, 320, 320);
    ASSERT_NE(occupant, nullptr);

    // A BIT_NO_COLLIDE caster passes every overlap, exactly like the legacy
    // ob_pass_check did. Marker path: deterministic destination.
    living* ghost = add_actor(fx, FAMILY_MAGE, 0, 96, 96);
    ghost->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
    walker* ghost_marker = add_marker(fx, ghost, 320, 320, 10);
    ASSERT_NE(ghost_marker, nullptr);
    EXPECT_TRUE(ghost->teleport())
        << "NO_COLLIDE blink onto occupied ground must stay allowed";
    EXPECT_EQ(320, ghost->xpos());
    EXPECT_EQ(320, ghost->ypos());
    ghost->setxy(96, 96);
    ghost_marker->set_dead(1); // retire ghost's marker

    // Height-disjoint pair: raise the occupant into a hover with a bounded
    // cylinder; a grounded bounded caster no longer overlaps it in z.
    occupant->set_sizez(8);
    occupant->set_worldz(40.0f);
    living* mage = add_actor(fx, FAMILY_MAGE, 0, 96, 96);
    mage->set_sizez(8);
    walker* marker = add_marker(fx, mage, 320, 320, 10);
    ASSERT_NE(marker, nullptr);
    EXPECT_TRUE(mage->teleport())
        << "hoverer above the landing spot must not block a grounded blink";
    EXPECT_EQ(320, mage->xpos());
    EXPECT_EQ(320, mage->ypos());

    // Grounded again (full-height sentinel), the occupant blocks the marker
    // AND the boulder-walled random fallback: the blink fails outright.
    occupant->set_sizez(0);
    occupant->set_worldz(0.0f);
    mage->set_sizez(0);
    mage->setxy(96, 96);
    w.rng_.state_ = 5;
    EXPECT_FALSE(mage->teleport())
        << "grounded occupant on the sole clear cell must block the blink";
    EXPECT_EQ(96, mage->xpos());
}
