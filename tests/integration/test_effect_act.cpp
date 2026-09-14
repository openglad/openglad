#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/core/irandom.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {

// Combat damage rolls draw from gameplay_rng_override() before the world RNG
// (src/gameplay/walker_combat.cpp combat_rng()). Scripting it to a constant is
// what turns "the target lost SOME hit points" into an exact number.
class ZeroRandom final : public IRandom
{
public:
    Uint32 next(Uint32 /*max_exclusive*/) override { return 0; }
};

class ScopedCombatRandom
{
public:
    explicit ScopedCombatRandom(IRandom* rng) : rng_(rng)
    {
        set_gameplay_rng_override(&rng_);
    }
    ~ScopedCombatRandom() { set_gameplay_rng_override(nullptr); }
    ScopedCombatRandom(const ScopedCombatRandom&) = delete;
    ScopedCombatRandom& operator=(const ScopedCombatRandom&) = delete;

private:
    IRandom* rng_;
};

}  // namespace

// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations for pure functions
bool hits(short x1, short y1, short w1, short h1,
          short x2, short y2, short w2, short h2);

static std::unique_ptr<walker> make_living_guy(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
        w->setxy(100, 100);
    return w;
}

// The frames of one sentinel-terminated animation row, as ints.
static std::vector<int> explode_row(const signed char* seq)
{
    std::vector<int> row;
    for (int i = 0; i < 128 && seq[i] != -1; ++i)
        row.push_back(static_cast<int>(seq[i]));
    return row;
}

// ---------------------------------------------------------------------------
// Pure functions: hits (collision detection)
// ---------------------------------------------------------------------------

TEST(EffectAct, hits_overlap2)
{
    bool r = hits(0, 0, 10, 10, 5, 5, 10, 10);
    ASSERT_TRUE(r) << "overlapping rectangles should hit";
}


TEST(EffectAct, hits_no_overlap)
{
    bool r = hits(0, 0, 10, 10, 20, 20, 10, 10);
    ASSERT_TRUE(!r) << "non-overlapping rectangles should not hit";
}


// EffectAct.hits_adjacent (an x-edge-touching hits() call whose result was
// discarded with `(void)r;`) was merged into EffectExtended.hits_exact_touching
// in test_effect_extended.cpp, which pins the identical shape with
// ASSERT_EQ(1, ...); EffectMorePaths.effect_round11_... pins the y edge.


TEST(EffectAct, hits_contained2)
{
    bool r = hits(0, 0, 20, 20, 5, 5, 5, 5);
    ASSERT_TRUE(r) << "contained rectangle should hit";
}


// ---------------------------------------------------------------------------
// effect::act - various effect families
// ---------------------------------------------------------------------------

// effect::act() advances drawcycle by exactly 1 every tick and, while
// ani_type != ANI_WALK, delegates to animate(). core:explosion declares no
// on_act and has loops_animation = false, so its ANI_EXPLODE row runs out,
// animate() resets ani_type to ANI_WALK at the -1 sentinel, and the NEXT act
// kills the effect.
TEST(EffectAct, explosion_act_advances_drawcycle_animates_then_dies)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, fx) << "explosion effect spawned";
    ASSERT_NE(nullptr, fx->ani) << "the explosion family carries an animation table";
    fx->setxy(100, 100);
    fx->set_ani_type(ANI_EXPLODE);
    fx->set_curdir(static_cast<char>(FACE_RIGHT));
    fx->set_cycle(0);
    fx->set_drawcycle(0);

    const signed char* seq = fx->ani[FACE_RIGHT + ANI_EXPLODE * NUM_FACINGS];
    ASSERT_NE(nullptr, seq) << "the ANI_EXPLODE row exists for FACE_RIGHT";
    const std::vector<int> row = explode_row(seq);
    ASSERT_GT(row.size(), 1u) << "the explode row has several distinct frames to walk";

    std::vector<int> shown;
    ASSERT_TRUE(fx->act()) << "a mid-animation effect delegates to animate() and lives on";
    shown.push_back(static_cast<int>(fx->frame()));
    EXPECT_EQ(1, static_cast<int>(fx->drawcycle()))
        << "effect::act advances drawcycle by exactly 1 every tick";
    EXPECT_EQ(1, static_cast<int>(fx->cycle()))
        << "animate() advances the animation cycle by exactly 1";
    EXPECT_EQ(0, fx->dead()) << "the explosion is still animating";

    int ticks = 0;
    while (fx->ani_type() != ANI_WALK && ticks < 128)
    {
        fx->act();
        shown.push_back(static_cast<int>(fx->frame()));
        ++ticks;
    }
    ASSERT_LT(ticks, 128) << "a non-looping effect's animation must run out and reset ani_type";
    EXPECT_EQ(row, shown)
        << "every act showed the next frame of ani[curdir + ANI_EXPLODE*NUM_FACINGS]";
    EXPECT_EQ(1 + ticks, static_cast<int>(fx->drawcycle()))
        << "drawcycle advanced by exactly one per act across the whole animation";
    EXPECT_EQ(0, fx->dead()) << "reaching ANI_WALK does not itself kill the effect";

    fx->act();
    EXPECT_EQ(1, fx->dead())
        << "an effect whose animation has run out dies on its next act";

    world.delete_objects();
}


// magic_shield_on_act (packs/core/lib/effect_shield.lua): center_on(owner)
// then offset by the fixed 16-step orbit table at index drawcycle % 16.
// effect::act bumps drawcycle 0 -> 1 BEFORE the hook, so the first step is
// ORBIT[1] = (-9, -22). guard_tail then ages the shield by one tick.
TEST(EffectAct, magic_shield_orbit_step_and_ageing)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    auto owner = make_living_guy(FAMILY_MAGE, 0);
    ASSERT_NE(nullptr, owner.get()) << "owner created";

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_NE(nullptr, fx) << "magic shield spawned";
    fx->set_owner(owner.get());
    fx->set_team_num(owner->team_num());
    fx->set_lifetime(100);
    fx->stats()->set_hitpoints(100);
    fx->set_drawcycle(0);

    // The orbit is applied from center_on(owner), so take the centred position
    // as the baseline, then park the shield somewhere else: a hook that stops
    // orbiting is then distinguishable from one that orbits correctly.
    fx->center_on(owner.get());
    const float bx = fx->worldx();
    const float by = fx->worldy();
    fx->setxy(100, 100);

    ASSERT_TRUE(fx->act()) << "magic_shield_on_act handles the tick itself";
    EXPECT_EQ(1, static_cast<int>(fx->drawcycle()))
        << "effect::act advances drawcycle by exactly 1 before the family hook";
    EXPECT_FLOAT_EQ(bx - 9.0f, fx->worldx())
        << "the shield sits at owner-centre + ORBIT_X[drawcycle % 16]";
    EXPECT_FLOAT_EQ(by - 22.0f, fx->worldy())
        << "the shield sits at owner-centre + ORBIT_Y[drawcycle % 16]";
    EXPECT_EQ(100, fx->lifetime() + 1)
        << "guard_tail ages the shield by exactly one tick while its hitpoints hold";
    EXPECT_FLOAT_EQ(100.0f, fx->stats()->hitpoints())
        << "with no foe weapons and no foes in range the shield loses no hitpoints";
    EXPECT_EQ(0, fx->dead()) << "a shield with an owner, hitpoints and lifetime survives";

    world.delete_objects();
}


TEST(EffectAct, magic_shield_no_owner)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_NE(nullptr, fx) << "magic shield spawned";
    fx->setxy(100, 100);
    fx->set_owner(nullptr);
    fx->set_lifetime(100);
    fx->stats()->set_hitpoints(100);
    ASSERT_TRUE(fx->act()) << "magic_shield_on_act handles the tick itself";
    ASSERT_EQ(1, fx->dead()) << "an ownerless shield dies on its first act";

    world.delete_objects();
}


// boomerang_on_act: same orbit table as the shield, but the radius is scaled
// by (drawcycle + 4) / 48. drawcycle 10 is bumped to 11 by effect::act, so the
// step is ORBIT[11] = (22, 9) * 15/48 = (6.875, 2.8125) from owner centre.
TEST(EffectAct, boomerang_orbit_radius_scales_with_drawcycle)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    auto owner = make_living_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, owner.get()) << "owner created";

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, fx) << "boomerang spawned";
    fx->set_owner(owner.get());
    fx->set_team_num(owner->team_num());
    fx->set_lifetime(100);
    fx->stats()->set_hitpoints(100);
    fx->set_drawcycle(10);

    fx->center_on(owner.get());
    const float bx = fx->worldx();
    const float by = fx->worldy();
    fx->setxy(100, 100);

    ASSERT_TRUE(fx->act()) << "boomerang_on_act handles the tick itself";
    EXPECT_EQ(11, static_cast<int>(fx->drawcycle()))
        << "effect::act advances drawcycle by exactly 1 before the family hook";
    EXPECT_FLOAT_EQ(bx + 6.875f, fx->worldx())
        << "orbit x = ORBIT_X[11] * (drawcycle + 4) / 48 from the owner's centre";
    EXPECT_FLOAT_EQ(by + 2.8125f, fx->worldy())
        << "orbit y = ORBIT_Y[11] * (drawcycle + 4) / 48 from the owner's centre";
    EXPECT_EQ(100, fx->lifetime() + 1)
        << "guard_tail ages the blade by exactly one tick";
    EXPECT_EQ(0, fx->dead()) << "a blade under the drawcycle cap survives";

    world.delete_objects();
}


// Zardus's 2002 cap: a boomerang whose byte-sized drawcycle passes 253 is
// retired instead of wrapping back onto its owner. Both sides of the boundary
// are pinned so a cap that never fires (and a cap that fires a tick early)
// are both caught.
TEST(EffectAct, boomerang_expired)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    auto owner = make_living_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, owner.get()) << "owner created";

    walker* alive = world.add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, alive) << "control boomerang spawned";
    alive->setxy(100, 100);
    alive->set_owner(owner.get());
    alive->set_team_num(owner->team_num());
    alive->set_lifetime(100);
    alive->stats()->set_hitpoints(100);
    alive->set_drawcycle(252);
    alive->act();
    ASSERT_EQ(253, static_cast<int>(alive->drawcycle()));
    EXPECT_EQ(0, alive->dead()) << "drawcycle 253 is still inside the > 253 cap";

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, fx) << "expiring boomerang spawned";
    fx->setxy(100, 100);
    fx->set_owner(owner.get());
    fx->set_team_num(owner->team_num());
    fx->set_lifetime(100);
    fx->stats()->set_hitpoints(100);
    fx->set_drawcycle(253);
    ASSERT_TRUE(fx->act()) << "boomerang_on_act handles the tick itself";
    ASSERT_EQ(254, static_cast<int>(fx->drawcycle()));
    ASSERT_EQ(1, fx->dead())
        << "a boomerang past drawcycle 253 dies even with a live owner";

    world.delete_objects();
}

// Regression: a freshly-spawned boomerang must SPIRAL OUTWARD from its owner as
// the sim ticks. Its orbit radius scales with drawcycle ((drawcycle+4)/48), which
// must advance every act(). On master the render loop bumped drawcycle each frame;
// the authoritative sim is now headless, so effect::act() advances it. Earlier it
// was frozen at spawn, so the boomerang hung on the owner. Unlike EffectAct.boomerang
// above, this deliberately does NOT pre-set drawcycle — it relies on the sim to
// advance it, which is exactly the mechanism that regressed (and which the
// pre-set-drawcycle unit tests and the headless parity harness both missed).
TEST(EffectAct, boomerang_spirals_outward_as_the_sim_ticks)
{
    auto owner = make_living_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, owner.get()) << "owner created";
    owner->setxy(160, 160);

    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(
        Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, fx) << "boomerang created";
    fx->set_owner(owner.get());
    fx->set_team_num(owner->team_num());
    fx->set_lifetime(300);
    fx->stats()->set_hitpoints(100);
    fx->center_on(owner.get());

    // Spawned at drawcycle 0 (the real spawn state), NOT pre-advanced — so the
    // test exercises the sim advancing it, not a preset value.
    ASSERT_EQ(0, static_cast<int>(fx->drawcycle()));

    const float ox = static_cast<float>(owner->worldx());
    const float oy = static_cast<float>(owner->worldy());
    const auto sq_dist_from_owner = [&]() -> float {
        const float dx = static_cast<float>(fx->worldx()) - ox;
        const float dy = static_cast<float>(fx->worldy()) - oy;
        return dx * dx + dy * dy;
    };

    float early_sq = 0.0f, late_sq = 0.0f;
    int ticked = 0;
    for (int tick = 0; tick < 60 && !fx->dead(); ++tick)
    {
        fx->act();
        ++ticked;
        if (tick == 5)
            early_sq = sq_dist_from_owner();
        late_sq = sq_dist_from_owner();
    }

    ASSERT_EQ(60, ticked) << "a 300-tick boomerang survives all 60 acts";
    EXPECT_EQ(60, static_cast<int>(fx->drawcycle()))
        << "effect::act advances the boomerang's drawcycle by exactly 1 per tick";
    EXPECT_GT(late_sq, early_sq)
        << "the boomerang must spiral OUTWARD from its owner over its lifetime, "
           "not hang stationary on the owner (the reported regression)";

    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}

// Sibling of the boomerang test: the FAMILY_MAGIC_SHIELD effect ORBITS its owner
// (fixed radius, no spiral). It uses the same drawcycle counter, so the same
// headless-sim freeze hung it at a single point. Assert its position actually
// moves around the owner as the sim ticks.
TEST(EffectAct, magic_shield_orbits_as_the_sim_ticks)
{
    auto owner = make_living_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, owner.get()) << "owner created";
    owner->setxy(160, 160);

    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(
        Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_NE(nullptr, fx) << "magic shield created";
    fx->set_owner(owner.get());
    fx->set_team_num(owner->team_num());
    fx->set_lifetime(300);
    fx->stats()->set_hitpoints(100);
    fx->center_on(owner.get());
    ASSERT_EQ(0, static_cast<int>(fx->drawcycle()));

    float x_early = 0.0f, y_early = 0.0f, x_late = 0.0f, y_late = 0.0f;
    int ticked = 0;
    for (int tick = 0; tick < 12 && !fx->dead(); ++tick)
    {
        fx->act();
        ++ticked;
        if (tick == 1) { x_early = static_cast<float>(fx->worldx());
                         y_early = static_cast<float>(fx->worldy()); }
        if (tick == 9) { x_late = static_cast<float>(fx->worldx());
                         y_late = static_cast<float>(fx->worldy()); }
    }

    ASSERT_EQ(12, ticked) << "a 300-tick shield survives all 12 acts";
    EXPECT_EQ(12, static_cast<int>(fx->drawcycle()))
        << "effect::act advances the shield's drawcycle by exactly 1 per tick";

    // The shield rides ORBIT[drawcycle % 16] around a stationary owner, so the
    // displacement between two ticks is the difference of two table entries:
    // tick 1 lands on drawcycle 2 = (-17, -17) and tick 9 on drawcycle 10 =
    // (17, 17), i.e. exactly (+34, +34). "Moved more than 10 px" accepted any
    // wandering; this accepts only the table.
    const float dx = x_late - x_early;
    const float dy = y_late - y_early;
    EXPECT_FLOAT_EQ(34.0f, dx)
        << "ORBIT[10].x - ORBIT[2].x = 17 - (-17): the shield walks the table";
    EXPECT_FLOAT_EQ(34.0f, dy)
        << "ORBIT[10].y - ORBIT[2].y = 17 - (-17): the shield walks the table";

    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}

// LIVING-path counterpart (living::act, not effect::act): FAMILY_ARCHMAGE grants
// itself bonus map-viewing once every ~temp ticks via `drawcycle() % temp`. With
// drawcycle frozen at 0 in the headless sim, `0 % temp == 0` fired EVERY tick, so
// the archmage saw the whole map permanently. The sim must advance a living's
// drawcycle so the periodic gate works.
TEST(EffectAct, archmage_bonus_view_is_periodic_not_every_tick)
{
    auto arch = make_living_guy(FAMILY_ARCHMAGE, 0);
    ASSERT_NE(nullptr, arch.get()) << "archmage created";
    ASSERT_EQ(3, static_cast<int>(arch->stats()->level()))
        << "make_living_guy upgrades to level 3, so the grant period is 40 - 3 = 37";
    arch->set_view_all(0);
    arch->set_drawcycle(0);

    for (int tick = 0; tick < 20 && !arch->dead(); ++tick)
        arch->act();

    EXPECT_EQ(20, static_cast<int>(arch->drawcycle()))
        << "the headless sim advances a living's drawcycle by exactly 1 per act";

    // living::act decays view_all by 1 every tick BEFORE the family hook runs,
    // so the grant is observable as a single 1 on exactly the tick whose
    // post-bump drawcycle is a multiple of the period, and 0 on its neighbours.
    ASSERT_FALSE(arch->dead()) << "the archmage survived the warm-up ticks";
    arch->set_view_all(0);
    arch->set_drawcycle(35);
    arch->act();
    EXPECT_EQ(36, static_cast<int>(arch->drawcycle()));
    EXPECT_EQ(0, static_cast<int>(arch->view_all()))
        << "drawcycle 36 is not a multiple of 37: no bonus view";
    arch->act();
    EXPECT_EQ(37, static_cast<int>(arch->drawcycle()));
    EXPECT_EQ(1, static_cast<int>(arch->view_all()))
        << "exactly one bonus-view grant on drawcycle 37 for a level-3 archmage";
    arch->act();
    EXPECT_EQ(38, static_cast<int>(arch->drawcycle()));
    EXPECT_EQ(0, static_cast<int>(arch->view_all()))
        << "the grant decays away and does NOT repeat on the following tick "
           "(the frozen-drawcycle regression made it fire every tick)";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


// cloud on_act (packs/core/lib/effect_cloud.lua): a live cloud ages by one
// tick and, when it has no queued command, rolls a non-zero (xd, yd) drift and
// QUEUES a COMMAND_WALK. The walk is executed on later ticks, so tick 1 leaves
// the cloud where it stood.
TEST(EffectAct, cloud_ages_and_queues_its_drift_walk)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    auto owner = make_living_guy(FAMILY_DRUID, 0);
    ASSERT_NE(nullptr, owner.get()) << "owner created";

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_CLOUD);
    ASSERT_NE(nullptr, fx) << "cloud spawned";
    fx->setxy(100, 100);
    fx->set_owner(owner.get());
    fx->set_team_num(0);
    fx->set_lifetime(50);
    fx->stats()->set_hitpoints(50);
    fx->stats()->clear_command();
    ASSERT_FALSE(fx->stats()->has_commands()) << "a fresh cloud starts with an empty queue";

    ASSERT_TRUE(fx->act()) << "cloud on_act handles the tick itself";
    EXPECT_EQ(49, fx->lifetime()) << "a live cloud ages by exactly one tick";
    EXPECT_TRUE(fx->stats()->has_commands())
        << "a cloud with no queued command queues its drift walk on the first tick";
    EXPECT_EQ(100, static_cast<int>(fx->xpos()))
        << "the drift walk is QUEUED on tick 1, not executed";
    EXPECT_EQ(100, static_cast<int>(fx->ypos()))
        << "the drift walk is QUEUED on tick 1, not executed";
    EXPECT_EQ(0, fx->dead()) << "a cloud with lifetime left survives";

    world.delete_objects();
}


TEST(EffectAct, cloud_expired)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_CLOUD);
    ASSERT_NE(nullptr, fx) << "cloud spawned";
    fx->setxy(100, 100);
    fx->set_owner(fx);
    fx->set_lifetime(0);
    ASSERT_TRUE(fx->act()) << "cloud on_act handles the tick itself";
    ASSERT_EQ(1, fx->dead()) << "a cloud with no lifetime left dies on its next act";
    EXPECT_EQ(0, fx->lifetime()) << "the expired branch does not age the cloud further";

    world.delete_objects();
}


// Issue #233: a poison cloud wedged into the arena corner FREEZES. Clouds
// fly over interior tiles, so only the map boundary blocks them; heading
// UP_LEFT in the NW corner fails the first walk, the baby step and BOTH
// halves of walkstep's NPC deflection on the off-map bounds check, then
// set_curdir restores the old facing (walker_movement.cpp:310) and the
// queued walk command re-executes the same no-op for its whole count. The
// Lua drift step must detect the genuinely frozen tick (position AND
// facing unchanged after s_do_command) and re-roll a fresh heading. A wall
// SLIDE along one edge (position changes) and a pure turn tick (curdir
// changes) must NOT re-roll — those paths are pinned to master by the
// parity weapon-track goldens.
TEST(EffectAct, cloud_wedged_against_wall_keeps_moving)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();  // a real 40x60 grid (this binary loads no map)

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_CLOUD);
    ASSERT_NE(nullptr, fx);
    fx->set_owner(fx);
    fx->set_team_num(0);
    fx->set_lifetime(50);
    fx->stats()->set_hitpoints(50);

    fx->setxy(0, 0);
    fx->set_curdir(FACE_UP_LEFT);
    fx->stats()->add_command(COMMAND_WALK, 19, -1, -1);

    // Pin the sim RNG so the post-fix escape roll is deterministic.
    world.rng_.state_ = 12345u;

    const short held_x = fx->xpos();
    const short held_y = fx->ypos();
    int moved_after = -1;
    for (int i = 0; i < 12 && moved_after < 0; ++i)
    {
        fx->act();
        if (fx->xpos() != held_x || fx->ypos() != held_y)
            moved_after = i + 1;
    }
    EXPECT_GE(moved_after, 0)
        << "a corner-wedged cloud must escape within 12 acts instead of "
           "freezing for the whole 19-tick walk command (issue #233)";
    world.remove_ob(fx);
}


// ghost_scare on_act rides its caster (center_on(owner)) and then returns
// false, delegating to effect::act's default path: with ani_type ANI_WALK the
// scare cloud ends its own life that same tick. The caster is moved AWAY from
// the cloud first, so a hook that stops riding is observable.
TEST(EffectAct, ghost_scare_rides_its_caster_then_expires)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    auto owner = make_living_guy(FAMILY_GHOST, 0);
    ASSERT_NE(nullptr, owner.get()) << "owner created";

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_GHOST_SCARE);
    ASSERT_NE(nullptr, fx) << "ghost scare spawned";
    fx->setxy(100, 100);
    fx->set_owner(owner.get());
    owner->setxy(200, 150);
    ASSERT_EQ(ANI_WALK, static_cast<int>(fx->ani_type()))
        << "add_fx_ob leaves the scare on the walk animation";

    const int centred_x = owner->xpos() + owner->sizex() / 2 - fx->sizex() / 2;
    const int centred_y = owner->ypos() + owner->sizey() / 2 - fx->sizey() / 2;
    ASSERT_NE(centred_x, static_cast<int>(fx->xpos()))
        << "the scare starts away from its caster so a no-op act is visible";

    fx->act();
    EXPECT_EQ(centred_x, static_cast<int>(fx->xpos()))
        << "the scare cloud rides its caster (center_on(owner))";
    EXPECT_EQ(centred_y, static_cast<int>(fx->ypos()))
        << "the scare cloud rides its caster (center_on(owner))";
    EXPECT_EQ(1, fx->dead())
        << "on_act returns false, so an ANI_WALK scare dies on effect::act's default path";

    world.delete_objects();
}


// ---------------------------------------------------------------------------
// effect::animate
// ---------------------------------------------------------------------------

// effect::animate(): frame = ani[dir + ani_type*NUM_FACINGS][cycle], cycle
// advances by 1, and for a loops_animation = false family (core:explosion) the
// -1 sentinel resets ani_type to ANI_WALK.
TEST(EffectAct, effect_animate_explosion_reads_the_row_and_ends_at_the_sentinel)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, fx) << "explosion effect spawned";
    ASSERT_NE(nullptr, fx->ani) << "the explosion family carries an animation table";
    fx->setxy(100, 100);
    fx->set_ani_type(ANI_EXPLODE);
    fx->set_curdir(static_cast<char>(FACE_RIGHT));
    fx->set_cycle(0);

    const signed char* seq = fx->ani[FACE_RIGHT + ANI_EXPLODE * NUM_FACINGS];
    ASSERT_NE(nullptr, seq) << "the ANI_EXPLODE row exists for FACE_RIGHT";
    const std::vector<int> row = explode_row(seq);
    ASSERT_GT(row.size(), 1u) << "the explode row has several distinct frames to walk";

    std::vector<int> shown;
    ASSERT_TRUE(fx->animate()) << "animate() reports it drew a frame";
    shown.push_back(static_cast<int>(fx->frame()));
    EXPECT_EQ(1, static_cast<int>(fx->cycle())) << "the cycle advances by exactly 1";

    int steps = 1;
    while (fx->ani_type() != ANI_WALK && steps < 128)
    {
        ASSERT_TRUE(fx->animate()) << "animate() keeps drawing until the sentinel";
        shown.push_back(static_cast<int>(fx->frame()));
        ++steps;
    }
    ASSERT_LT(steps, 128)
        << "a non-looping effect's animation ends by resetting ani_type to ANI_WALK";
    EXPECT_EQ(static_cast<int>(row.size()), steps)
        << "the animation is exactly as long as the sentinel-terminated row";
    EXPECT_EQ(row, shown)
        << "frame is read from ani[curdir + ani_type*NUM_FACINGS][cycle] on every step";

    world.delete_objects();
}


// core:magic_shield declares loops_animation = true, so animate() takes the
// looping branch: the cycle wraps back to 0 at the -1 sentinel and ani_type is
// never reset.
TEST(EffectAct, effect_animate_magic_shield_loops_without_resetting_ani_type)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_NE(nullptr, fx) << "magic shield spawned";
    ASSERT_NE(nullptr, fx->ani) << "the shield family carries an animation table";
    fx->setxy(100, 100);
    fx->set_ani_type(static_cast<char>(ANI_WALK));
    fx->set_curdir(static_cast<char>(FACE_RIGHT));
    fx->set_cycle(0);

    const signed char* seq = fx->ani[FACE_RIGHT + ANI_WALK * NUM_FACINGS];
    ASSERT_NE(nullptr, seq) << "the ANI_WALK row exists for FACE_RIGHT";
    const std::vector<int> row = explode_row(seq);
    const int len = static_cast<int>(row.size());
    ASSERT_GT(len, 0) << "the walk row has at least one frame";

    std::vector<int> shown;
    ASSERT_TRUE(fx->animate()) << "animate() reports it drew a frame";
    shown.push_back(static_cast<int>(fx->frame()));
    EXPECT_EQ(len > 1 ? 1 : 0, static_cast<int>(fx->cycle()))
        << "the cycle advances by 1, wrapping immediately on a one-frame row";

    int steps = 1;
    while (fx->cycle() != 0 && steps < 128)
    {
        ASSERT_TRUE(fx->animate());
        ASSERT_EQ(ANI_WALK, static_cast<int>(fx->ani_type()))
            << "a looping effect never resets its ani_type";
        shown.push_back(static_cast<int>(fx->frame()));
        ++steps;
    }
    ASSERT_LT(steps, 128) << "a looping animation wraps its cycle back to 0";
    EXPECT_EQ(len, steps)
        << "the wrap happens exactly at the sequence sentinel, after one pass";
    EXPECT_EQ(row, shown)
        << "every step showed the next frame of ani[curdir + ANI_WALK*NUM_FACINGS]";
    EXPECT_EQ(ANI_WALK, static_cast<int>(fx->ani_type()))
        << "loops_animation = true never resets ani_type";

    world.delete_objects();
}


TEST(EffectAct, effect_animate_handles_malicious_indices_safely)
{
    // effect::animate() indexes its animation table with curdir/ani_type/cycle,
    // which arrive straight off a snapshot. Out-of-range values must be bounded
    // (facing, table length, sequence sentinel) instead of reading out of bounds
    // and dereferencing a wild pointer.
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(fx != nullptr);
    if (!fx)
        return;
    ASSERT_TRUE(fx->ani != nullptr);
    ASSERT_GT(fx->ani_count, 0);

    // ani_type far beyond the table + out-of-range facing and cycle.
    fx->set_ani_type(static_cast<char>(40));
    fx->set_curdir(static_cast<char>(100));
    fx->set_cycle(static_cast<signed char>(120));
    (void)fx->animate(); // must not crash / read OOB (verified under sanitizers)

    // Negative facing and ani_type.
    fx->set_ani_type(static_cast<char>(-1));
    fx->set_curdir(static_cast<char>(-5));
    fx->set_cycle(static_cast<signed char>(-9));
    (void)fx->animate();

    // A null animation table must be a graceful no-op, not a null deref.
    fx->ani = nullptr;
    ASSERT_TRUE(!fx->animate()) << "null ani must return false";

    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


// ---------------------------------------------------------------------------
// effect::death
// ---------------------------------------------------------------------------

// explosion_on_death (packs/core/lib/effect_bomb.lua): every non-FX,
// non-treasure walker on the same floor within 15 + compute_explosion_range
// (Manhattan, on xpos/ypos) is shoved with COMMAND_WALK and attacked; anything
// outside that radius is untouched. A level-3 owner gives range 16 => 31 px.
// A bare add_ob(Living, ORC) carries no guy, so its armor row is 0 and the
// armor roll subtracts nothing; the blast's 40 damage becomes
// compute_base_damage(40, rng -> 0) = 40 - sqrt(40)/2 = 36.8377, which
// damage_to_hit_points floors (36.8377 + 0.5) to 37 hit points.
static constexpr float kOrcArmor = 0.0f;
static constexpr float kExplosionHitPoints = 37.0f;

TEST(EffectAct, effect_death_explosion_blasts_only_inside_its_radius)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    auto owner = make_living_guy(FAMILY_MAGE, 0);
    ASSERT_NE(nullptr, owner.get()) << "owner created";
    ASSERT_EQ(3, static_cast<int>(owner->stats()->level()))
        << "a level-3 owner yields explosion range 16, i.e. a 31 px blast";

    // Damage rolls draw from the combat RNG; scripted to 0 the blast's damage
    // is exactly compute_base_damage(40) = 40 - sqrt(40)/2, so the hit is an
    // exact number of hit points rather than "fewer than before".
    ZeroRandom zero_rng;
    ScopedCombatRandom scoped_rng(&zero_rng);

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, fx) << "explosion spawned";
    fx->setxy(100, 100);
    fx->set_owner(owner.get());
    fx->set_team_num(0);
    fx->set_skip_exit(0);
    fx->set_damage(40.0f);

    walker* near_ob = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, near_ob) << "near target created";
    ASSERT_FLOAT_EQ(kOrcArmor, near_ob->stats()->armor())
        << "the orc's loader armor row the reduction below is computed against";
    near_ob->set_team_num(1);
    near_ob->setxy(124, 100);   // Manhattan 24 <= 31
    near_ob->stats()->clear_command();
    // hit_response clears commands for a NEW foe, so pre-seed the relationship
    // (the same reason EffectMorePaths.effect_death_explosion_shoves_nearby_targets
    // gives) and the shove stays queued deterministically.
    near_ob->set_foe(owner.get());
    const float near_hp = near_ob->stats()->hitpoints();

    walker* far_ob = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, far_ob) << "far target created";
    far_ob->set_team_num(1);
    far_ob->setxy(100, 164);    // Manhattan 64 > 31
    far_ob->stats()->clear_command();
    far_ob->set_foe(owner.get());
    const float far_hp = far_ob->stats()->hitpoints();

    fx->set_dead(1);
    ASSERT_TRUE(fx->death()) << "the blast runs once";

    EXPECT_TRUE(near_ob->stats()->has_commands())
        << "a target inside the blast radius is shoved with COMMAND_WALK";
    EXPECT_FLOAT_EQ(near_hp - kExplosionHitPoints, near_ob->stats()->hitpoints())
        << "a target inside the blast radius loses exactly the blast's damage "
           "roll (scripted RNG), not merely 'some' hit points";
    EXPECT_FALSE(far_ob->stats()->has_commands())
        << "a target outside the blast radius is never shoved";
    EXPECT_FLOAT_EQ(far_hp, far_ob->stats()->hitpoints())
        << "a target outside the blast radius takes no damage";
    EXPECT_FALSE(fx->death())
        << "the death_called guard makes a second death() a no-op";

    world.delete_objects();
}


// ghost_scare on_death: every LIVING foe within og.scare_radius(owner.level)
// of the CASTER (level 3 => 80 px Manhattan) is forced into a flee-walk;
// friendlies and out-of-range foes are left alone.
TEST(EffectAct, effect_death_ghost_scare_frights_only_foes_in_radius)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    auto owner = make_living_guy(FAMILY_GHOST, 0);
    ASSERT_NE(nullptr, owner.get()) << "owner created";
    ASSERT_EQ(3, static_cast<int>(owner->stats()->level()))
        << "a level-3 caster scares within 50 + 10*3 = 80 px";

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_GHOST_SCARE);
    ASSERT_NE(nullptr, fx) << "ghost scare spawned";
    fx->setxy(100, 100);
    fx->set_owner(owner.get());
    fx->set_team_num(owner->team_num());

    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "in-range foe created";
    foe->set_team_num(1);
    foe->setxy(140, 100);       // Manhattan 40 from the caster <= 80
    foe->stats()->clear_command();

    walker* ally = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, ally) << "ally created";
    ally->set_team_num(0);
    ally->setxy(120, 100);      // well inside the radius, but friendly
    ally->stats()->clear_command();

    walker* far_foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, far_foe) << "out-of-range foe created";
    far_foe->set_team_num(1);
    far_foe->setxy(100, 300);   // Manhattan 200 > 80
    far_foe->stats()->clear_command();

    fx->set_dead(1);
    ASSERT_TRUE(fx->death()) << "the scare runs once";

    EXPECT_TRUE(foe->stats()->has_commands())
        << "a foe inside the scare radius is forced into a flee-walk";
    EXPECT_FALSE(ally->stats()->has_commands())
        << "friendlies are never frightened by their own ghost";
    EXPECT_FALSE(far_foe->stats()->has_commands())
        << "a foe outside the scare radius is untouched";

    world.delete_objects();
}
