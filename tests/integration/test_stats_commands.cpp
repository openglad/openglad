#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/core/irandom.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include "test_sim_random_scope.h"
#include <memory>
#include <string_view>
#include <cstdint>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{

static GameWorld& world()
{
    return og::runtime::current_session->myscreen_->world();
}

// A fresh, fully passable arena: delete_objects() also guarantees an obmap
// (GameWorld::delete_objects), which query_object_passable needs before any
// walkstep can move, and create_new_grid() fills 40x60 cells with the four
// PIX_GRASS tiles -- every one of them passable. Declared FIRST in a test so
// its destructor runs after the owned walkers are gone.
struct ScopedArena
{
    ScopedArena()
    {
        world().delete_objects();
        world().create_new_grid();
    }
    ~ScopedArena() { world().delete_objects(); }
    ScopedArena(const ScopedArena&) = delete;
    ScopedArena& operator=(const ScopedArena&) = delete;
};

// stats.cpp's rng() -- and every fire_check/attack path it reaches -- draws
// from current_game->world->rng_ (SimRandom), NOT from GameContext::rng, so
// ScopedSimRandom is the only way to script those draws. Every guard in this
// file names the draw it pins:
//   * fire_check draws the weapon's waver inside set_weapon_heading on EVERY
//     arm, the denied ones included: walker::fire_check builds the probe with
//     create_weapon() + set_weapon_heading() before any gate runs.
//   * attack() rolls its damage through combat_rng(), which falls back to the
//     sim stream when no gameplay override is installed, and the victim's
//     hit_response then reaches check_special()'s next(2) and the `!rng(3)`
//     special gate.
//   * fire() draws the waver it writes into the spawned weapon's heading.
// The two remaining sim draws in stats.cpp are pinned elsewhere, once each (no
// rule twins): COMMAND_ATTACK's force_command(COMMAND_FIRE, rng(5), ...) in
// tests/integration/test_stats_extended.cpp (FixedRandom(3), commandcount 2),
// and walk_to_foe's 30 + rng(25) in tests/integration/test_stats_more_paths.cpp
// (commandcount 31). direct_walk's own 30 + rng(25) has NO pin in this suite:
// reaching it needs a faced foe whose fire_check passes, which is a new
// scenario rather than this file's.
//
// xpos/ypos are the truncated snapshot of the authoritative float world
// position and stepsize is fractional (a level-3 soldier walks 4.33px), so
// every movement expectation is computed the way the sim computes it.
static int stepped(int base, float delta)
{
    return static_cast<int>(static_cast<float>(base) + delta);
}

static void set_tile(int tx, int ty, unsigned char tile)
{
    PixieData& grid = world().grid;
    ASSERT_TRUE(grid.valid()) << "arena grid exists";
    ASSERT_TRUE(tx >= 0 && ty >= 0 && tx < grid.w && ty < grid.h) << "tile in bounds";
    grid.data[static_cast<std::size_t>(ty * grid.w + tx)] = tile;
}

} // namespace

static std::unique_ptr<walker> make_walker(char family)
{
    guy g(family);
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
        w->setxy(100, 100);
    return w;
}

// ---------------------------------------------------------------------------
// do_command - exercises the big switch (lines 51-276 in stats.cpp)
// Each COMMAND_* case is a different branch
// ---------------------------------------------------------------------------

TEST(StatsCommands, stats_do_command_walk_steps_once_and_counts_down)
{
    ScopedArena arena;
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "walker created";

    ASSERT_TRUE(w->setxy(static_cast<std::int32_t>(GRID_SIZE * 10), static_cast<std::int32_t>(GRID_SIZE * 10))) << "actor placed on open grass";
    // walkstep() spends the call TURNING when curdir disagrees with the step
    // direction (walker::walk), so face east before asking for an east step.
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_enddir(static_cast<char>(FACE_RIGHT));
    // A whole-pixel stepsize keeps the float world position exact, so the
    // step expectations below are byte-exact instead of truncation-sensitive
    // (a level-3 soldier's own 4.33px step is asserted by the parity corpus).
    w->set_stepsize(4.0f);
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_WALK, 5, 1, 0);
    ASSERT_TRUE(w->stats()->has_commands()) << "should have commands";

    const int x0 = w->xpos();
    const int y0 = w->ypos();
    const float step = w->stepsize();
    ASSERT_LT(0.0f, step) << "a soldier has a non-zero stepsize";

    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_WALK reports success";
    EXPECT_EQ(stepped(x0, step), static_cast<int>(w->xpos()))
        << "COMMAND_WALK must walkstep(com1, com2): one stepsize east";
    EXPECT_EQ(y0, static_cast<int>(w->ypos())) << "com2 == 0 means no vertical movement";
    ASSERT_TRUE(w->stats()->has_commands()) << "5 iterations: the command survives the first";
    EXPECT_EQ(4, w->stats()->commands.front().commandcount)
        << "the do_command tail decrements commandcount exactly once";

    for (int i = 0; i < 4; i++)
        EXPECT_EQ(1, w->stats()->do_command()) << "remaining walk iteration " << i;
    EXPECT_EQ(stepped(x0, 5.0f * step), static_cast<int>(w->xpos()))
        << "five iterations walk five steps";
    EXPECT_FALSE(w->stats()->has_commands()) << "the tail pops the command at count < 1";
}


TEST(StatsCommands, stats_do_command_fire_denied_without_foe_and_spends_busy_when_allowed)
{
    ScopedArena arena;
    // fire_check draws the knife's waver (set_weapon_heading) on BOTH the
    // denied and the allowed arm below; pinned so the ray geometry here is
    // independent of how many draws earlier tests in this binary made.
    FixedRandom rng_source(1);
    ScopedSimRandom rng(&rng_source);
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "walker created";
    ASSERT_TRUE(w->setxy(static_cast<std::int32_t>(GRID_SIZE * 10), static_cast<std::int32_t>(GRID_SIZE * 10))) << "actor placed on open grass";
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_enddir(static_cast<char>(FACE_RIGHT));
    w->set_busy(0.0f);
    w->set_ani_type(ANI_WALK);
    // set_weapon_heading reads the owner's lastx/lasty, NOT curdir: without
    // these the shot leaves due north and the ray denies with WallBlocked.
    w->set_lastx(w->stepsize());
    w->set_lasty(0.0f);

    // No foe: fire_check() denies with FireCheckDenial::NoFoe, so the case
    // zeroes commandcount, returns 0, and the tail pops the command.
    ASSERT_EQ(nullptr, w->foe()) << "fresh walker has no foe";
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_FIRE, 3, 1, 0);
    ASSERT_EQ(0, w->stats()->do_command()) << "COMMAND_FIRE reports failure when fire_check denies";
    EXPECT_FALSE(w->stats()->has_commands())
        << "a denied fire zeroes commandcount so the tail pops the command";
    EXPECT_EQ(0.0f, w->busy()) << "a denied fire never reaches init_fire, so no busy is spent";
    EXPECT_EQ(ANI_WALK, static_cast<int>(w->ani_type()))
        << "a denied fire never switches to the attack animation";

    // Allowed fire: an obmap-registered foe inside knife reach, faced, so the
    // fire_check ray hits it. init_fire() then spends busy and starts the
    // attack animation (it does NOT spawn the weapon on the ANI_WALK path).
    walker* foe = world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "foe created in the world";
    foe->set_team_num(1);
    ASSERT_TRUE(foe->setxy(static_cast<std::int32_t>(w->xpos() + w->sizex() + 2), static_cast<std::int32_t>(w->ypos()))) << "foe placed just east";
    // add_ob does NOT register an entity in the spatial index, and the
    // fire_check ray only counts a hit when query_object_passable finds a
    // hostile box in the obmap -- otherwise the ray runs off the map and
    // denies with WallBlocked.
    ASSERT_NE(nullptr, world().myobmap) << "arena has an obmap";
    world().myobmap->add(foe, foe->xpos(), foe->ypos());
    w->set_team_num(0);
    w->set_foe(foe);
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_FIRE, 3, 1, 0);
    ASSERT_LT(0.0f, w->fire_frequency())
        << "setup sanity: the soldier's fire_frequency is a positive busy cost";
    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_FIRE succeeds against a faced foe in reach";
    ASSERT_TRUE(w->stats()->has_commands()) << "a successful fire leaves the command queued";
    EXPECT_EQ(2, w->stats()->commands.front().commandcount)
        << "only the tail decrement touches commandcount on the success arm";
    // busy was pinned to 0 above and init_fire does set_busy(busy() + fire_frequency()).
    EXPECT_FLOAT_EQ(w->fire_frequency(), w->busy())
        << "init_fire() spends exactly fire_frequency of busy";
    EXPECT_EQ(ANI_ATTACK, static_cast<int>(w->ani_type()))
        << "init_fire() switches the walker to the attack animation";
}


TEST(StatsCommands, stats_do_command_unhandled_type_still_counts_down)
{
    ScopedArena arena;
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "walker created";
    ASSERT_TRUE(w->setxy(static_cast<std::int32_t>(GRID_SIZE * 10), static_cast<std::int32_t>(GRID_SIZE * 10))) << "actor placed on open grass";
    w->stats()->commands.clear();

    // COMMAND_RANDOM_WALK has no case in do_command's switch: it falls through
    // `default: break;` and only the post-switch tail runs.
    w->stats()->force_command(COMMAND_RANDOM_WALK, 5, 0, 0);
    const int x0 = w->xpos();
    const int y0 = w->ypos();

    ASSERT_EQ(1, w->stats()->do_command()) << "an unhandled commandtype still reports success";
    EXPECT_EQ(x0, static_cast<int>(w->xpos())) << "no case ran, so nothing walked";
    EXPECT_EQ(y0, static_cast<int>(w->ypos())) << "no case ran, so nothing walked";
    ASSERT_TRUE(w->stats()->has_commands()) << "5 iterations remain queued";
    EXPECT_EQ(4, w->stats()->commands.front().commandcount)
        << "the tail decrements the unhandled command once per call";
    EXPECT_EQ(COMMAND_RANDOM_WALK, w->stats()->commands.front().commandtype)
        << "force_command stores the unhandled type verbatim";

    for (int i = 0; i < 4; i++)
        EXPECT_EQ(1, w->stats()->do_command()) << "remaining iteration " << i;
    EXPECT_FALSE(w->stats()->has_commands()) << "the tail pops the command at count < 1";
}


TEST(StatsCommands, stats_do_command_search_without_a_live_foe_abandons_the_order)
{
    ScopedArena arena;
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "walker created";
    ASSERT_TRUE(w->setxy(static_cast<std::int32_t>(GRID_SIZE * 10), static_cast<std::int32_t>(GRID_SIZE * 10)))
        << "actor placed on open grass";

    // 10 iterations on purpose: only the no-foe branch's commandcount = 0 can
    // retire the order in a single call (the tail decrement alone leaves 9).
    // The sibling StatsMorePaths.stats_do_command_set_reset_weapon_and_search_without_foe
    // uses one iteration, which the tail pops either way.
    w->set_foe(nullptr);
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_SEARCH, 10, 0, 0);
    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_SEARCH reports success";
    EXPECT_FALSE(w->stats()->has_commands())
        << "no foe: SEARCH zeroes commandcount so the tail pops the whole order";

    // A remembered but dead foe takes the same branch.
    auto corpse = make_walker(FAMILY_ORC);
    ASSERT_NE(nullptr, corpse) << "corpse created";
    corpse->set_team_num(1);
    corpse->set_dead(1);
    w->set_foe(corpse.get());
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_SEARCH, 10, 0, 0);
    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_SEARCH reports success";
    EXPECT_FALSE(w->stats()->has_commands())
        << "dead foe: SEARCH abandons the order instead of walking to a corpse";
}


TEST(StatsCommands, stats_do_command_rush_takes_three_steps_and_shoves_the_collider)
{
    ScopedArena arena;
    // COMMAND_RUSH -> attack() rolls damage through combat_rng(), which falls
    // back to this stream, and the victim's hit_response then draws
    // check_special()'s next(2) and the `!rng(3)` gate; rng(3)==1 keeps the
    // gate shut so the shove below is never pre-empted by a special.
    FixedRandom rng_source(1);
    ScopedSimRandom rng(&rng_source);
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "walker created";
    ASSERT_TRUE(w->setxy(static_cast<std::int32_t>(GRID_SIZE * 10), static_cast<std::int32_t>(GRID_SIZE * 10))) << "actor placed on open grass";
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_enddir(static_cast<char>(FACE_RIGHT));
    // A whole-pixel stepsize keeps the float world position exact, so the
    // step expectations below are byte-exact instead of truncation-sensitive
    // (a level-3 soldier's own 4.33px step is asserted by the parity corpus).
    w->set_stepsize(4.0f);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_RUSH, 3, 1, 0);

    const int x0 = w->xpos();
    const int y0 = w->ypos();
    const float step = w->stepsize();
    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_RUSH reports success for a living";
    EXPECT_EQ(stepped(x0, 3.0f * step), static_cast<int>(w->xpos()))
        << "the fighter's rush is THREE walksteps in (com1, com2)";
    EXPECT_EQ(y0, static_cast<int>(w->ypos())) << "com2 == 0 means no vertical movement";
    ASSERT_TRUE(w->stats()->has_commands()) << "3 iterations: two remain";
    EXPECT_EQ(2, w->stats()->commands.front().commandcount) << "the tail decrements once";

    // Second half: a rush that connects shoves its victim.
    auto target = make_walker(FAMILY_ORC);
    ASSERT_NE(nullptr, target) << "shove target created";
    ASSERT_TRUE(target->setxy(static_cast<std::int32_t>(GRID_SIZE * 20), static_cast<std::int32_t>(GRID_SIZE * 20))) << "target parked clear of the rush";
    target->set_team_num(1);
    target->stats()->commands.clear();
    target->stats()->add_command(COMMAND_WALK, 9, 0, 1);
    ASSERT_EQ(1u, target->stats()->commands.size()) << "the victim starts with its own order";

    w->set_collide_ob(target.get());
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_RUSH, 1, 1, 0);
    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_RUSH with a collide_ob still reports success";

    ASSERT_EQ(1u, target->stats()->commands.size())
        << "the shove clears the victim's queue and leaves exactly the forced walk";
    const command& shove = target->stats()->commands.front();
    EXPECT_EQ(COMMAND_WALK, shove.commandtype) << "the shove is a walk";
    EXPECT_EQ(4, shove.commandcount) << "the shove lasts 4 iterations";
    EXPECT_EQ(1, shove.com1) << "the shove is along the rush's com1";
    EXPECT_EQ(0, shove.com2) << "the shove is along the rush's com2";
    EXPECT_TRUE(shove.forced) << "the shove enters through force_command";
}


TEST(StatsCommands, stats_do_command_quick_fire_walks_and_fires_in_one_tick)
{
    ScopedArena arena;
    // fire() draws the arrow's waver from the sim stream: next(6) == 1, which
    // the arrow heading assertions at the bottom of this test spell out.
    FixedRandom rng_source(1);
    ScopedSimRandom rng(&rng_source);
    auto w = make_walker(FAMILY_ARCHER);
    ASSERT_NE(nullptr, w) << "walker created";
    ASSERT_TRUE(w->setxy(static_cast<std::int32_t>(GRID_SIZE * 10), static_cast<std::int32_t>(GRID_SIZE * 10))) << "actor placed on open grass";
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_enddir(static_cast<char>(FACE_RIGHT));
    // A whole-pixel stepsize keeps the float world position exact, so the
    // step expectations below are byte-exact instead of truncation-sensitive
    // (a level-3 soldier's own 4.33px step is asserted by the parity corpus).
    w->set_stepsize(4.0f);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_QUICK_FIRE, 1, 1, 0);

    const int x0 = w->xpos();
    const int y0 = w->ypos();
    const float step = w->stepsize();
    const std::size_t weapons0 = world().weaplist.size();

    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_QUICK_FIRE reports success";
    EXPECT_EQ(stepped(x0, step), static_cast<int>(w->xpos()))
        << "QUICK_FIRE walksteps(com1, com2) before firing";
    EXPECT_EQ(y0, static_cast<int>(w->ypos())) << "com2 == 0 means no vertical movement";
    ASSERT_EQ(weapons0 + 1, world().weaplist.size())
        << "QUICK_FIRE also calls fire(), which spawns the archer's arrow";
    walker* arrow = world().weaplist.back().get();
    ASSERT_NE(nullptr, arrow) << "the spawned weapon is in weaplist";
    EXPECT_EQ(w.get(), arrow->owner()) << "the arrow belongs to the archer that fired it";
    EXPECT_EQ(static_cast<int>(w->current_weapon()), static_cast<int>(arrow->family()))
        << "fire() spawns the current weapon family";
    // The arrow's heading is what the pinned draw buys. configure_weapon_
    // profile_base multiplies the arrow's base stepsize 8 by 362/256 on a
    // cardinal facing (walker.cpp), giving 11.3125 -- the archer's own
    // set_stepsize(4) above never reaches the weapon.
    EXPECT_FLOAT_EQ(arrow->stepsize(), arrow->lastx())
        << "FACE_RIGHT: the arrow's forward heading is one weapon stepsize";
    EXPECT_FLOAT_EQ(-1.0f, arrow->lasty())
        << "waver = draw - base/2 with base = trunc(11.3125/2) = 5 and "
           "next(6) == 1 under FixedRandom(1): 1 - 2 = -1";
    EXPECT_FALSE(w->stats()->has_commands()) << "one iteration: the tail pops the command";
}


TEST(StatsCommands, stats_do_command_attack_without_foe_pops_and_faces_a_foe_it_cannot_shoot)
{
    ScopedArena arena;
    // Both fire_checks below draw the weapon's waver even on the arm that
    // denies with Facing (create_weapon + set_weapon_heading run before the
    // gate), so the stream is pinned to keep the denial deterministic.
    FixedRandom rng_source(1);
    ScopedSimRandom rng(&rng_source);
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "walker created";
    ASSERT_TRUE(w->setxy(static_cast<std::int32_t>(GRID_SIZE * 10), static_cast<std::int32_t>(GRID_SIZE * 10))) << "actor placed on open grass";
    w->set_curdir(static_cast<signed char>(FACE_UP));
    w->set_enddir(static_cast<char>(FACE_UP));

    // No foe: the guard zeroes commandcount and returns 1, so the tail pops it.
    ASSERT_EQ(nullptr, w->foe()) << "fresh walker has no foe";
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_ATTACK, 5, 1, 0);
    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_ATTACK without a foe still returns 1";
    EXPECT_FALSE(w->stats()->has_commands())
        << "the no-foe guard zeroes commandcount so the tail pops the command";

    // With a live foe in reach but the wrong facing, fire_check denies with
    // FireCheckDenial::Facing: the guard-standoff rule TURNS toward the foe
    // instead of walking (docs/GAMEPLAY_FIXES_FROM_CLASSIC.md).
    walker* foe = world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "foe created in the world (obmap)";
    foe->set_team_num(1);
    ASSERT_TRUE(foe->setxy(static_cast<std::int32_t>(w->xpos() + w->sizex() + 2), static_cast<std::int32_t>(w->ypos()))) << "foe placed just east";
    w->set_team_num(0);
    w->set_foe(foe);
    walker::FireCheckDenial denial = walker::FireCheckDenial::None;
    ASSERT_FALSE(w->fire_check(1, 0, &denial)) << "the shot is denied while facing up";
    ASSERT_EQ(static_cast<int>(walker::FireCheckDenial::Facing), static_cast<int>(denial))
        << "an in-reach foe off our facing denies with Facing";

    const int x0 = w->xpos();
    const int y0 = w->ypos();
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_ATTACK, 5, 1, 0);
    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_ATTACK with a foe returns 1";
    EXPECT_EQ(x0, static_cast<int>(w->xpos())) << "a Facing denial must NOT walk (no wall-slide orbit)";
    EXPECT_EQ(y0, static_cast<int>(w->ypos())) << "a Facing denial must NOT walk";
    // face_delta SNAP-faces: curdir, enddir and the lastx/lasty weapon heading
    // all point at the foe this tick. The classic fallback (walkstep) would
    // only aim enddir and creep curdir one step per tick (living::walk), which
    // is the standoff this rule exists to break.
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(w->enddir()))
        << "face_delta aims enddir at the foe so the swing lands next tick";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(w->curdir()))
        << "face_delta SNAPS curdir at the foe (a gradual turn would not)";
    // face_delta sets lastx = xdelta * stepsize(), and COMMAND_ATTACK
    // normalizes the foe delta to xdelta == 1 for a foe due east.
    EXPECT_FLOAT_EQ(w->stepsize(), w->lastx())
        << "face_delta points the weapon heading one stepsize east";
    EXPECT_EQ(0.0f, w->lasty()) << "same row: no vertical component in the heading";
    ASSERT_TRUE(w->stats()->has_commands()) << "the attack command survives";
    EXPECT_EQ(4, w->stats()->commands.front().commandcount) << "the tail decrements once";
}


TEST(StatsCommands, stats_do_command_right_walk_distance_gate_picks_the_walker)
{
    ScopedArena arena;
    // direct_walk's fire_check draws the weapon's waver on its denied arm too;
    // pinned so the right_walk branch below is reached deterministically.
    FixedRandom rng_source(1);
    ScopedSimRandom rng(&rng_source);
    auto w = make_walker(FAMILY_SOLDIER);
    auto foe = make_walker(FAMILY_ORC);
    ASSERT_NE(nullptr, w) << "walker created";
    ASSERT_NE(nullptr, foe) << "foe created";
    foe->set_team_num(1);
    w->set_team_num(0);

    // Wall off the column immediately east of the actor so right_blocked() is
    // true for FACE_UP while forward_blocked() stays false. right_walk() then
    // takes its forward branch and steps along lastx/lasty (north), which is
    // observably different from direct_walk()'s response to a foe due south.
    const int ax = 12;
    const int ay = 20;
    const int wall_col = ax;
    ASSERT_TRUE(w->setxy(static_cast<std::int32_t>(wall_col * GRID_SIZE - w->sizex()), static_cast<std::int32_t>(ay * GRID_SIZE + 4)))
        << "actor's east probe reaches into the wall column";
    for (int ty = 0; ty < world().grid.h; ty++)
        set_tile(wall_col, ty, PIX_H_WALL1);
    // A whole-pixel stepsize keeps the float world position exact, so the
    // step expectations below are byte-exact instead of truncation-sensitive
    // (a level-3 soldier's own 4.33px step is asserted by the parity corpus).
    w->set_stepsize(4.0f);
    w->set_curdir(static_cast<signed char>(FACE_UP));
    w->set_enddir(static_cast<char>(FACE_UP));
    w->set_lastx(0);
    w->set_lasty(-w->stepsize());
    ASSERT_TRUE(w->stats()->right_blocked()) << "geometry: our right is walled";
    ASSERT_FALSE(w->stats()->forward_blocked()) << "geometry: north is open";

    const float step = w->stepsize();

    // Distance inside (120, 240): the gate calls right_walk() directly, which
    // (right blocked, forward open) walks north -- AWAY from the foe.
    ASSERT_TRUE(foe->setxy(static_cast<std::int32_t>(w->xpos()), static_cast<std::int32_t>(w->ypos() + 150))) << "foe 150px south";
    w->set_foe(foe.get());
    int x0 = w->xpos();
    int y0 = w->ypos();
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_RIGHT_WALK, 1, 0, 0);
    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_RIGHT_WALK reports success";
    EXPECT_EQ(stepped(y0, -step), static_cast<int>(w->ypos()))
        << "distance in (120,240) uses right_walk(), which steps along lastx/lasty";
    EXPECT_EQ(x0, static_cast<int>(w->xpos())) << "right_walk's forward branch keeps our column";
    EXPECT_EQ(FACE_UP, static_cast<int>(w->curdir())) << "right_walk did not re-aim us at the foe";

    // Distance outside the gate: direct_walk() runs first and answers the foe
    // instead -- it turns us to face south (walkstep spends the call turning).
    ASSERT_TRUE(foe->setxy(static_cast<std::int32_t>(w->xpos()), static_cast<std::int32_t>(w->ypos() + 20))) << "foe 20px south";
    x0 = w->xpos();
    y0 = w->ypos();
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_RIGHT_WALK, 1, 0, 0);
    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_RIGHT_WALK reports success";
    EXPECT_EQ(FACE_DOWN, static_cast<int>(w->enddir()))
        << "distance outside (120,240) uses direct_walk(), which aims at the foe";
    // living::walk turns ONE facing step per call toward enddir instead of
    // snapping, so aiming south from north lands on FACE_UP_RIGHT this tick.
    EXPECT_EQ(FACE_UP_RIGHT, static_cast<int>(w->curdir()))
        << "the aim costs a gradual turn, not a step";
    EXPECT_EQ(y0, static_cast<int>(w->ypos())) << "the turn consumes the step";
    EXPECT_EQ(x0, static_cast<int>(w->xpos())) << "the turn consumes the step";
}


TEST(StatsCommands, stats_do_command_die_sets_delete_me)
{
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_dead(1); // avoid log spam; COMMAND_DIE expects a dead controller
    w->stats()->set_delete_me(0);
    w->stats()->force_command(COMMAND_DIE, 1, 0, 0);
    ASSERT_EQ(1, w->stats()->do_command())
        << "a command that ran reports 1 — the COMMAND_DIE arm never clears result";
    ASSERT_EQ(1, w->stats()->delete_me()) << "COMMAND_DIE should set delete_me when count < 2";
    ASSERT_TRUE(w->stats()->commands.empty())
        << "the one-iteration command is popped after it runs";
}


// COMMAND_SPECIAL doesn't exist as a constant - specials are called directly

// ---------------------------------------------------------------------------
// forward_blocked / right_blocked / right_forward_blocked / right_back_blocked
// Each is an 8-way direction switch that probes ONE cell at the
// CHECK_STEP_SIZE (== 1 pixel) offset for curdir.
// ---------------------------------------------------------------------------

namespace
{
// Expected probe offset, in CHECK_STEP_SIZE units, indexed by facing
// (FACE_UP == 0 .. FACE_UP_LEFT == 7). See statistics::right_blocked and
// friends in src/gameplay/stats.cpp.
struct ProbeOffset
{
    int dx;
    int dy;
};

constexpr ProbeOffset kRightBlocked[8] = {
    {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
};
constexpr ProbeOffset kRightForwardBlocked[8] = {
    {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}
};
constexpr ProbeOffset kRightBackBlocked[8] = {
    {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {1, 0}
};
constexpr ProbeOffset kForwardBlocked[8] = {
    {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}
};
} // namespace

TEST(StatsCommands, stats_blocked_helpers_probe_the_exact_cell_for_every_facing)
{
    ScopedArena arena;
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "walker created";

    // A 1x1 footprint makes query_grid_passable consult EXACTLY the cell that
    // contains the probed pixel (GameWorld::query_grid_passable), so a single
    // walled cell answers exactly one probe.
    w->set_sizex(1);
    w->set_sizey(1);

    const int ax = 12;
    const int ay = 20;
    // The four cells around the shared corner of (ax-1, ay-1) and (ax, ay).
    const int cells_x[2] = { ax - 1, ax };
    const int cells_y[2] = { ay - 1, ay };

    // Two placements pin the offset SIGN exactly: on the high corner
    // (ax*G-1, ay*G-1) a +1 probe crosses into cell ax, on the low corner
    // (ax*G, ay*G) a -1 probe crosses back into cell ax-1.
    struct Placement
    {
        int px;
        int py;
        bool high;
    };
    const Placement placements[2] = {
        { ax * GRID_SIZE - 1, ay * GRID_SIZE - 1, true },
        { ax * GRID_SIZE, ay * GRID_SIZE, false }
    };

    struct Helper
    {
        int id;
        const char* name;
        const ProbeOffset* table;
        bool falls_back_to_open; // default: returns false instead of probing (0,0)
    };
    const Helper helpers[4] = {
        { 0, "right_blocked", kRightBlocked, false },
        { 1, "right_forward_blocked", kRightForwardBlocked, true },
        { 2, "right_back_blocked", kRightBackBlocked, true },
        { 3, "forward_blocked", kForwardBlocked, false }
    };

    auto call_helper = [&w](int id) -> bool {
        switch (id)
        {
            case 0: return w->stats()->right_blocked();
            case 1: return w->stats()->right_forward_blocked();
            case 2: return w->stats()->right_back_blocked();
            default: return w->stats()->forward_blocked();
        }
    };

    for (const Placement& place : placements)
    {
        ASSERT_TRUE(w->setxy(static_cast<std::int32_t>(place.px), static_cast<std::int32_t>(place.py))) << "actor placed on the cell corner";

        for (const Helper& helper : helpers)
        {
            for (int dir = 0; dir < 8; dir++)
            {
                w->set_curdir(static_cast<signed char>(dir));
                const ProbeOffset expected = helper.table[dir];
                // Which of the four candidate cells the expected offset lands in.
                const int want_x = place.high ? (ax - 1 + (expected.dx > 0 ? 1 : 0))
                                              : (ax + (expected.dx < 0 ? -1 : 0));
                const int want_y = place.high ? (ay - 1 + (expected.dy > 0 ? 1 : 0))
                                              : (ay + (expected.dy < 0 ? -1 : 0));

                for (int ix = 0; ix < 2; ix++)
                    for (int iy = 0; iy < 2; iy++)
                    {
                        const int tx = cells_x[ix];
                        const int ty = cells_y[iy];
                        for (int cx = 0; cx < 2; cx++)
                            for (int cy = 0; cy < 2; cy++)
                                set_tile(cells_x[cx], cells_y[cy], PIX_GRASS1);
                        set_tile(tx, ty, PIX_H_WALL1);

                        SCOPED_TRACE(testing::Message()
                                     << helper.name << " facing " << dir
                                     << (place.high ? " high corner" : " low corner")
                                     << " wall at cell (" << tx << "," << ty << ")"
                                     << " expected probe (" << expected.dx << ","
                                     << expected.dy << ")");
                        EXPECT_EQ(tx == want_x && ty == want_y, call_helper(helper.id))
                            << "the helper must probe exactly the CHECK_STEP_SIZE cell for curdir";
                    }
            }

            // An out-of-range curdir: right_blocked/forward_blocked fall to
            // their default (0,0) offset (our own cell); the two 45/135-degree
            // helpers return false without probing at all.
            w->set_curdir(static_cast<signed char>(99));
            const int own_x = place.high ? ax - 1 : ax;
            const int own_y = place.high ? ay - 1 : ay;
            for (int ix = 0; ix < 2; ix++)
                for (int iy = 0; iy < 2; iy++)
                {
                    const int tx = cells_x[ix];
                    const int ty = cells_y[iy];
                    for (int cx = 0; cx < 2; cx++)
                        for (int cy = 0; cy < 2; cy++)
                            set_tile(cells_x[cx], cells_y[cy], PIX_GRASS1);
                    set_tile(tx, ty, PIX_H_WALL1);

                    SCOPED_TRACE(testing::Message()
                                 << helper.name << " invalid curdir "
                                 << (place.high ? " high corner" : " low corner")
                                 << " wall at cell (" << tx << "," << ty << ")");
                    const bool expect_blocked =
                        !helper.falls_back_to_open && tx == own_x && ty == own_y;
                    EXPECT_EQ(expect_blocked, call_helper(helper.id))
                        << "invalid curdir: default (0,0) probe, or no probe at all";
                }
        }
    }

    // Folded in from StatsMorePaths.stats_forward_and_side_blocked_invalid_
    // direction_defaults (deleted): its four "all open" answers for an
    // out-of-range facing are the degenerate corner of the matrix above, and
    // its one claim of its own was what right_walk() does on top of them.
    // Kept with the deleted test's own setup — a fresh grid and a fresh
    // walker — rather than reusing the actor the matrix just walled in.
    og::runtime::current_session->myscreen_->world().create_new_grid();
    auto invalid_facing = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, invalid_facing) << "walker created";
    ASSERT_TRUE(invalid_facing->setxy(static_cast<std::int32_t>(GRID_SIZE * 4),
                                      static_cast<std::int32_t>(GRID_SIZE * 4)))
        << "actor placed on open ground";
    invalid_facing->set_curdir(static_cast<signed char>(127));
    invalid_facing->set_enddir(static_cast<char>(127));
    EXPECT_FALSE(invalid_facing->stats()->forward_blocked())
        << "an invalid curdir probes our own open cell: nothing forward-blocked";
    EXPECT_FALSE(invalid_facing->stats()->right_blocked())
        << "an invalid curdir probes our own open cell: nothing right-blocked";
    EXPECT_FALSE(invalid_facing->stats()->right_forward_blocked())
        << "the 45-degree helper refuses to probe an invalid curdir at all";
    EXPECT_FALSE(invalid_facing->stats()->right_back_blocked())
        << "the 135-degree helper refuses to probe an invalid curdir at all";
    EXPECT_TRUE(invalid_facing->stats()->right_walk())
        << "right_walk still reports a step with an invalid facing";
}


// ---------------------------------------------------------------------------
// hit_response for different families (line ~305+)
// ---------------------------------------------------------------------------

TEST(StatsCommands, stats_hit_response_all_families)
{
    ScopedArena arena;
    // next(2) -> shifter_down 1 and rng(3) == 1 keeps the `!rng(3)` gate shut
    // (a family whose check_special_ai hook declines never reaches the roll).
    FixedRandom rng_source(1);
    ScopedSimRandom rng(&rng_source);

    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };

    for (int i = 0; i < 14; i++)
    {
        SCOPED_TRACE(testing::Message() << "family " << static_cast<int>(families[i]));
        auto target = make_walker(families[i]);
        auto attacker = make_walker(FAMILY_SOLDIER);
        ASSERT_NE(nullptr, target) << "target created";
        ASSERT_NE(nullptr, attacker) << "attacker created";

        attacker->set_team_num(1);
        target->set_team_num(0);
        ASSERT_TRUE(target->setxy(static_cast<std::int32_t>(GRID_SIZE * 10), static_cast<std::int32_t>(GRID_SIZE * 10))) << "target placed";
        ASSERT_TRUE(attacker->setxy(static_cast<std::int32_t>(target->xpos() + 5), static_cast<std::int32_t>(target->ypos()))) << "attacker 5px east";
        target->stats()->set_last_distance(0);
        target->stats()->set_current_distance(0);
        target->stats()->commands.clear();
        ASSERT_EQ(nullptr, target->foe()) << "target starts without a foe";

        trace_clear();
        target->stats()->hit_response(attacker.get());

        // Every family answers a new attacker by taking it as its foe.
        EXPECT_EQ(attacker.get(), target->foe()) << "hit_response targets our attacker";

        if (families[i] == FAMILY_ARCHER)
        {
            // packs/core/families/living-02-archer.lua: the Lua hook runs
            // INSTEAD of the C++ default -- it resets the distances to 15000
            // and backpedals away from a melee-range attacker.
            EXPECT_EQ(15000u, target->stats()->last_distance())
                << "the archer hook resets last_distance to 15000";
            EXPECT_EQ(15000, target->stats()->current_distance())
                << "the archer hook resets current_distance to 15000";
            ASSERT_FALSE(target->stats()->commands.empty())
                << "the archer hook forces a backpedal walk";
            const command& flee = target->stats()->commands.front();
            EXPECT_EQ(COMMAND_WALK, flee.commandtype) << "the backpedal is a walk";
            EXPECT_EQ(8, flee.commandcount) << "the backpedal lasts 8 iterations";
            EXPECT_EQ(-1, flee.com1) << "the attacker is east, so the archer flees west";
            EXPECT_EQ(0, flee.com2) << "same row, so no vertical flee";
            EXPECT_TRUE(flee.forced) << "the backpedal enters through force_command";
            EXPECT_EQ(0, target->shifter_down())
                << "the Lua hook returns before check_special(): no next(2) reaches shifter_down";
        }
        else if (families[i] == FAMILY_MAGE)
        {
            // packs/core/families/living-03-mage.lua: above the flee
            // threshold the mage hook sets both foes and 15000 distances.
            EXPECT_EQ(target.get(), attacker->foe()) << "the mage hook answers back";
            EXPECT_EQ(15000u, target->stats()->last_distance())
                << "the mage hook resets last_distance to 15000";
            EXPECT_EQ(15000, target->stats()->current_distance())
                << "the mage hook resets current_distance to 15000";
            EXPECT_EQ(0, target->shifter_down())
                << "the Lua hook returns before check_special(): no next(2) reaches shifter_down";
        }
        else
        {
            // stats.cpp's default: clear commands, foe each other, 32000.
            EXPECT_EQ(target.get(), attacker->foe())
                << "the default hit_response makes the attacker our foe's foe";
            EXPECT_EQ(32000u, target->stats()->last_distance())
                << "the default hit_response resets last_distance to 32000";
            EXPECT_EQ(32000, target->stats()->current_distance())
                << "the default hit_response resets current_distance to 32000";
            EXPECT_EQ(1, target->shifter_down())
                << "check_special() draws next(2) from the sim stream into "
                   "shifter_down before the special gate; FixedRandom(1) answers 1";
            EXPECT_FALSE(trace_contains("walker", "special: family="))
                << "rng(3) == 1 keeps the `!rng(3)` gate shut: walker::special() "
                   "is never entered";
        }
    }
}


// The gate the test above holds SHUT, opened. FAMILY_ELF registers no
// check_special_ai hook (packs/core/families/living-01-elf.lua), so
// living::check_special() falls through to its "Default: always allow" and the
// `!rng(3)` roll in statistics::hit_response is actually reached -- the hooked
// families (soldier foe_in_window, orc/archer/elemental/ghost foe_within)
// answer false with no foe, because walkers owned by a test are outside the
// oblist that find_near_foe scans.
TEST(StatsCommands, stats_hit_response_default_arm_special_gate_opens_when_rng3_is_zero)
{
    ScopedArena arena;

    struct Pair
    {
        std::unique_ptr<walker> target;
        std::unique_ptr<walker> attacker;
    };

    auto make_pair = [](Pair& pair) {
        pair.target = make_walker(FAMILY_ELF);
        pair.attacker = make_walker(FAMILY_SOLDIER);
        ASSERT_NE(nullptr, pair.target) << "elf target created";
        ASSERT_NE(nullptr, pair.attacker) << "attacker created";
        pair.attacker->set_team_num(1);
        pair.target->set_team_num(0);
        ASSERT_TRUE(pair.target->setxy(static_cast<std::int32_t>(GRID_SIZE * 10),
                                       static_cast<std::int32_t>(GRID_SIZE * 10)))
            << "target placed";
        ASSERT_TRUE(pair.attacker->setxy(static_cast<std::int32_t>(pair.target->xpos() + 5),
                                         static_cast<std::int32_t>(pair.target->ypos())))
            << "attacker 5px east";
        pair.target->stats()->set_last_distance(0);
        pair.target->stats()->set_current_distance(0);
        pair.target->stats()->commands.clear();
        ASSERT_EQ(nullptr, pair.target->foe()) << "target starts without a foe";
    };

    // Leg A: rng(3) == 1 -> the gate stays shut (the contrast leg).
    {
        Pair shut;
        make_pair(shut);
        ASSERT_FALSE(testing::Test::HasFatalFailure()) << "leg A fixture built";
        FixedRandom one(1);
        ScopedSimRandom sim(&one);
        trace_clear();
        shut.target->stats()->hit_response(shut.attacker.get());
        EXPECT_EQ(1, shut.target->shifter_down())
            << "check_special()'s next(2) answers 1 under FixedRandom(1)";
        EXPECT_FALSE(trace_contains("walker", "special: family="))
            << "rng(3) == 1 keeps the `!rng(3)` gate shut";
        EXPECT_EQ(shut.attacker.get(), shut.target->foe())
            << "the retarget block runs whether or not the special fires";
    }

    // Leg B: rng(3) == 0 -> the gate opens and hit_response enters special().
    {
        Pair open;
        make_pair(open);
        ASSERT_FALSE(testing::Test::HasFatalFailure()) << "leg B fixture built";
        FixedRandom zero(0);
        ScopedSimRandom sim(&zero);
        trace_clear();
        open.target->stats()->hit_response(open.attacker.get());
        EXPECT_EQ(0, open.target->shifter_down())
            << "check_special()'s next(2) answers 0 under FixedRandom(0)";
        EXPECT_TRUE(trace_contains("walker", "special: family="))
            << "rng(3) == 0 opens the `!rng(3)` gate: hit_response enters "
               "walker::special() (the TRACE fires before the MP check and the "
               "Lua dispatch, so this pins 'entered', not 'succeeded')";
        EXPECT_EQ(open.attacker.get(), open.target->foe())
            << "the retarget block still runs after the special";
    }
    // ScopedArena's delete_objects() sweeps whatever the elf's ROCKS special
    // spawned into the world lists.
}


// ---------------------------------------------------------------------------
// try_command and command management
// ---------------------------------------------------------------------------

// Characterization pin, not a bug report: try_command has appended
// unconditionally and returned 0 since the 2002 initial revision, and the
// "only if the queue is empty" invariant is upheld by the callers (see the
// note over statistics::try_command in src/gameplay/stats.cpp). This goes red
// the day someone implements that sentence in the body -- which would move
// 560 command insertions across the parity corpus.
TEST(StatsCommands, stats_try_command_appends_onto_a_non_empty_queue)
{
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";

    // Fill up commands
    for (int i = 0; i < 20; i++) {
        w->stats()->add_command(COMMAND_WALK, 1, 1, 0);
    }

    const short result = w->stats()->try_command(COMMAND_WALK, 5, 1, 0);
    ASSERT_EQ(0, result) << "try_command has returned 0 unconditionally since 2002";
    ASSERT_EQ(21u, w->stats()->commands.size())
        << "try_command appends even when the queue is already full";
    ASSERT_EQ(COMMAND_WALK, w->stats()->commands.back().commandtype)
        << "the appended command is the one that was asked for";
}


TEST(StatsCommands, stats_clear_command)
{
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";

    w->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    ASSERT_TRUE(w->stats()->has_commands()) << "should have commands";

    w->stats()->clear_command();
    ASSERT_TRUE(!w->stats()->has_commands()) << "should be empty after clear";

}


TEST(StatsCommands, stats_force_command_prepends_a_clamped_forced_walk)
{
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";

    w->stats()->commands.clear();
    w->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    ASSERT_EQ(1u, w->stats()->commands.size()) << "one queued order to jump ahead of";
    ASSERT_FALSE(w->stats()->commands.front().forced)
        << "add_command never marks an entry forced";

    w->stats()->force_command(COMMAND_WALK, 2, 7, -9);
    ASSERT_EQ(2u, w->stats()->commands.size())
        << "force_command PREPENDS; it does not replace the queue";
    const command& forced = w->stats()->commands.front();
    EXPECT_EQ(COMMAND_WALK, forced.commandtype) << "the forced entry is at the front";
    EXPECT_EQ(2, forced.commandcount) << "the forced entry keeps its iteration count";
    EXPECT_EQ(1, forced.com1) << "walk deltas clamp to [-1, 1]";
    EXPECT_EQ(-1, forced.com2) << "walk deltas clamp to [-1, 1]";
    EXPECT_TRUE(forced.forced) << "only force_command sets the forced flag";
    EXPECT_EQ(5, w->stats()->commands.back().commandcount)
        << "the pre-existing order is untouched behind the forced one";

    // A degenerate (0,0) walk is rewritten to (1,1) so the victim always moves.
    w->stats()->force_command(COMMAND_WALK, 3, 0, 0);
    ASSERT_EQ(3u, w->stats()->commands.size()) << "force_command prepends again";
    EXPECT_EQ(1, w->stats()->commands.front().com1) << "(0,0) is rewritten to (1,1)";
    EXPECT_EQ(1, w->stats()->commands.front().com2) << "(0,0) is rewritten to (1,1)";
}


// ---------------------------------------------------------------------------
// right_walk and walk_to_foe
// ---------------------------------------------------------------------------

TEST(StatsCommands, stats_right_walk_steps_along_curdir_when_direct_walk_fails)
{
    ScopedArena arena;
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "walker created";
    w->set_foe(nullptr); // direct_walk() returns 0 immediately without a foe
    // A whole-pixel stepsize keeps the float world position exact, so the
    // step expectations below are byte-exact instead of truncation-sensitive
    // (a level-3 soldier's own 4.33px step is asserted by the parity corpus).
    w->set_stepsize(4.0f);

    const float step = w->stepsize();
    const ProbeOffset unit[8] = {
        {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}
    };

    for (int dir = 0; dir < 8; dir++)
    {
        SCOPED_TRACE(testing::Message() << "facing " << dir);
        ASSERT_TRUE(w->setxy(static_cast<std::int32_t>(GRID_SIZE * 20), static_cast<std::int32_t>(GRID_SIZE * 20))) << "actor re-centred on open grass";
        w->set_curdir(static_cast<signed char>(dir));
        w->set_enddir(static_cast<char>(dir));
        const int x0 = w->xpos();
        const int y0 = w->ypos();

        EXPECT_TRUE(w->stats()->right_walk()) << "nothing is blocked, so the step succeeds";
        EXPECT_EQ(stepped(x0, static_cast<float>(unit[dir].dx) * step),
                  static_cast<int>(w->xpos()))
            << "right_walk with no foe walksteps the unit vector of curdir";
        EXPECT_EQ(stepped(y0, static_cast<float>(unit[dir].dy) * step),
                  static_cast<int>(w->ypos()))
            << "right_walk with no foe walksteps the unit vector of curdir";
        EXPECT_EQ(dir, static_cast<int>(w->curdir())) << "the step kept our facing";
    }
}


TEST(StatsCommands, stats_walk_to_foe_no_foe_resets_distances)
{
    ScopedArena arena;
    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "walker created";
    w->set_foe(nullptr);
    w->stats()->commands.clear();
    w->stats()->set_last_distance(7);
    w->stats()->set_current_distance(7);

    EXPECT_FALSE(w->stats()->walk_to_foe()) << "walk_to_foe fails when there is no foe";
    EXPECT_EQ(15000u, w->stats()->last_distance())
        << "the no-foe branch resets last_distance to 15000";
    EXPECT_EQ(15000, w->stats()->current_distance())
        << "the no-foe branch resets current_distance to 15000";
    EXPECT_FALSE(w->stats()->has_commands()) << "the no-foe branch queues nothing";
    EXPECT_EQ(nullptr, w->foe()) << "the no-foe branch invents no foe";
}


TEST(StatsCommands, stats_yell_for_help_flees_and_recruits_friends)
{
    ScopedArena arena;
    auto w = make_walker(FAMILY_SOLDIER);
    auto enemy = make_walker(FAMILY_ORC);
    ASSERT_NE(nullptr, w) << "walker created";
    ASSERT_NE(nullptr, enemy) << "enemy created";

    w->set_team_num(0);
    enemy->set_team_num(1);
    ASSERT_TRUE(w->setxy(static_cast<std::int32_t>(GRID_SIZE * 10), static_cast<std::int32_t>(GRID_SIZE * 10))) << "yeller placed";
    ASSERT_TRUE(enemy->setxy(static_cast<std::int32_t>(w->xpos() + 20), static_cast<std::int32_t>(w->ypos()))) << "enemy 20px east";
    w->set_yo_delay(0);
    w->stats()->commands.clear();

    // find_friends_in_range walks the world's oblist, so the ally has to live
    // there (the owned walkers above are deliberately outside it).
    walker* ally = world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, ally) << "ally created in the world";
    ally->set_team_num(0);
    ASSERT_TRUE(ally->setxy(static_cast<std::int32_t>(w->xpos() + 40), static_cast<std::int32_t>(w->ypos() + 40))) << "ally within range 160";
    ally->set_foe(nullptr);
    ally->set_leader(nullptr);
    ally->stats()->set_last_distance(0);
    ally->stats()->set_current_distance(0);

    // yell_for_help is the one path in this file that draws NOTHING, so it is
    // pinned by the LCG standing still instead of by a scripted stream. While
    // any override is installed SimRandom::next never touches state_
    // (game_world.h), which would make the pin below pass for the wrong
    // reason -- hence the override check, which also polices guard leaks out
    // of the neighbouring tests.
    ASSERT_EQ(nullptr, og::sim::sim_random_override())
        << "no sim override may be installed here: with one the LCG never steps "
           "and the no-draw pin below is vacuous";
    const std::uint32_t lcg_before = current_game->world->rng_.state_;

    w->stats()->yell_for_help(enemy.get());

    EXPECT_EQ(lcg_before, current_game->world->rng_.state_)
        << "yell_for_help draws nothing (stats.cpp: set_yo_delay, "
           "find_friends_in_range, force_command, push_notification): the sim "
           "LCG must not step -- the day it does, this test needs a guard again";

    EXPECT_EQ(80, static_cast<int>(w->yo_delay())) << "yelling adds 80 to yo_delay";
    ASSERT_FALSE(w->stats()->commands.empty()) << "yelling forces a run-away walk";
    const command& flee = w->stats()->commands.front();
    EXPECT_EQ(COMMAND_WALK, flee.commandtype) << "the flee is a walk";
    EXPECT_EQ(16, flee.commandcount) << "the flee lasts 16 iterations";
    EXPECT_EQ(-1, flee.com1) << "the enemy is east, so we run west";
    EXPECT_EQ(0, flee.com2) << "same row, so no vertical flee";
    EXPECT_TRUE(flee.forced) << "the flee enters through force_command";

    EXPECT_EQ(enemy.get(), ally->foe()) << "friends in range 160 are pointed at our foe";
    EXPECT_EQ(w.get(), ally->leader()) << "recruited friends follow the yeller";
    EXPECT_EQ(32000u, ally->stats()->last_distance())
        << "a recruited friend's last_distance is reset to 32000";
    EXPECT_EQ(32000, ally->stats()->current_distance())
        << "a recruited friend's current_distance is reset to 32000";
}

