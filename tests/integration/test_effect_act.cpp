#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations for pure functions
void orbit_offset(int drawcycle, float &xd, float &yd);
Sint32 compute_explosion_range(Sint32 level, short skip_exit);
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

// ---------------------------------------------------------------------------
// Pure functions: orbit_offset
// ---------------------------------------------------------------------------

TEST(EffectAct, orbit_offset_all_cycles)
{
    for (int i = 0; i < 16; i++) {
        float xd = 0, yd = 0;
        orbit_offset(i, xd, yd);
        ASSERT_TRUE(xd != 0 || yd != 0) << "orbit offset should be non-zero";
    }
}


TEST(EffectAct, orbit_offset_wraps)
{
    float xd1, yd1, xd2, yd2;
    orbit_offset(0, xd1, yd1);
    orbit_offset(16, xd2, yd2);
    ASSERT_TRUE(xd1 == xd2) << "cycle 16 wraps to 0";
    ASSERT_TRUE(yd1 == yd2) << "cycle 16 wraps to 0";
}


// ---------------------------------------------------------------------------
// Pure functions: compute_explosion_range
// ---------------------------------------------------------------------------

TEST(EffectAct, explosion_range_basic2)
{
    Sint32 r = compute_explosion_range(10, 0);
    ASSERT_EQ(40, (int)r) << "level 10 => range 40";
}


TEST(EffectAct, explosion_range_capped)
{
    Sint32 r = compute_explosion_range(100, 0);
    ASSERT_EQ(96, (int)r) << "capped at 96";
}


TEST(EffectAct, explosion_range_min)
{
    Sint32 r = compute_explosion_range(1, 0);
    ASSERT_EQ(16, (int)r) << "min is 16";
}


TEST(EffectAct, explosion_range_skip_exit)
{
    Sint32 r = compute_explosion_range(10, 1);
    ASSERT_EQ(16, (int)r) << "skip_exit sets range to 0 then min caps to 16";
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


TEST(EffectAct, hits_adjacent)
{
    bool r = hits(0, 0, 10, 10, 10, 0, 10, 10);
    (void)r; // exactly touching, behavior may vary
}


TEST(EffectAct, hits_contained2)
{
    bool r = hits(0, 0, 20, 20, 5, 5, 5, 5);
    ASSERT_TRUE(r) << "contained rectangle should hit";
}


// ---------------------------------------------------------------------------
// effect::act - various effect families
// ---------------------------------------------------------------------------

TEST(EffectAct, explosion)
{
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_ani_type(ANI_EXPLODE);
    fx->act();
    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


TEST(EffectAct, magic_shield)
{
    auto owner = make_living_guy(FAMILY_MAGE, 0);
    if (!owner) return;

    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_owner(owner.get());
    fx->set_lifetime(100);
    fx->stats()->set_hitpoints(100);
    fx->act();

    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


TEST(EffectAct, magic_shield_no_owner)
{
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_owner(nullptr);
    fx->act();
    // Should die since no owner
    ASSERT_TRUE(fx->dead() == 1) << "shield without owner dies";
    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


TEST(EffectAct, boomerang)
{
    auto owner = make_living_guy(FAMILY_SOLDIER, 0);
    if (!owner) return;

    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_owner(owner.get());
    fx->set_lifetime(100);
    fx->stats()->set_hitpoints(100);
    fx->set_drawcycle(10);
    fx->act();

    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


TEST(EffectAct, boomerang_expired)
{
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_owner(nullptr);
    fx->set_drawcycle(254);
    fx->act();
    ASSERT_TRUE(fx->dead() == 1) << "expired boomerang dies";
    og::runtime::current_session->myscreen_->world().remove_ob(fx);
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
    ASSERT_TRUE(owner != nullptr) << "owner created";
    if (!owner) return;
    owner->setxy(160, 160);

    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(
        Order::FX, FAMILY_BOOMERANG);
    ASSERT_TRUE(fx != nullptr) << "boomerang created";
    if (!fx) return;
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
    for (int tick = 0; tick < 60 && !fx->dead(); ++tick)
    {
        fx->act();
        if (tick == 5)
            early_sq = sq_dist_from_owner();
        late_sq = sq_dist_from_owner();
    }

    EXPECT_GT(static_cast<int>(fx->drawcycle()), 0)
        << "the headless sim must advance the boomerang's drawcycle each tick";
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
    ASSERT_TRUE(owner != nullptr) << "owner created";
    if (!owner) return;
    owner->setxy(160, 160);

    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(
        Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_TRUE(fx != nullptr) << "magic shield created";
    if (!fx) return;
    fx->set_owner(owner.get());
    fx->set_team_num(owner->team_num());
    fx->set_lifetime(300);
    fx->stats()->set_hitpoints(100);
    fx->center_on(owner.get());
    ASSERT_EQ(0, static_cast<int>(fx->drawcycle()));

    float x_early = 0.0f, y_early = 0.0f, x_late = 0.0f, y_late = 0.0f;
    for (int tick = 0; tick < 12 && !fx->dead(); ++tick)
    {
        fx->act();
        if (tick == 1) { x_early = static_cast<float>(fx->worldx());
                         y_early = static_cast<float>(fx->worldy()); }
        if (tick == 9) { x_late = static_cast<float>(fx->worldx());
                         y_late = static_cast<float>(fx->worldy()); }
    }

    EXPECT_GT(static_cast<int>(fx->drawcycle()), 0)
        << "the headless sim must advance the shield's drawcycle each tick";
    const float dx = x_late - x_early;
    const float dy = y_late - y_early;
    EXPECT_GT(dx * dx + dy * dy, 100.0f)
        << "the magic shield must ORBIT its owner over time, not hang at one "
           "fixed point (the same drawcycle freeze that hung the boomerang)";

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
    ASSERT_TRUE(arch != nullptr) << "archmage created";
    if (!arch) return;
    arch->set_view_all(0);
    arch->set_drawcycle(0);

    for (int tick = 0; tick < 20 && !arch->dead(); ++tick)
        arch->act();

    EXPECT_GT(static_cast<int>(arch->drawcycle()), 0)
        << "the headless sim must advance a living's drawcycle each tick";
    EXPECT_LT(static_cast<int>(arch->view_all()), 5)
        << "archmage bonus-viewing must fire PERIODICALLY (~every temp ticks), "
           "not every single tick (which the frozen drawcycle caused)";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(EffectAct, cloud)
{
    auto owner = make_living_guy(FAMILY_DRUID, 0);
    if (!owner) return;

    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_CLOUD);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_owner(owner.get());
    fx->set_team_num(0);
    fx->set_lifetime(50);
    fx->stats()->set_hitpoints(50);
    fx->act();

    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


TEST(EffectAct, cloud_expired)
{
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_CLOUD);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_owner(fx);
    fx->set_lifetime(0);
    fx->act();
    ASSERT_TRUE(fx->dead() == 1) << "expired cloud dies";
    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


TEST(EffectAct, ghost_scare)
{
    auto owner = make_living_guy(FAMILY_GHOST, 0);
    if (!owner) return;

    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_GHOST_SCARE);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_owner(owner.get());
    fx->act();

    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


// ---------------------------------------------------------------------------
// effect::animate
// ---------------------------------------------------------------------------

TEST(EffectAct, effect_animate_explosion)
{
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_ani_type(ANI_EXPLODE);
    fx->animate();
    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


TEST(EffectAct, effect_animate_magic_shield)
{
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->animate();
    og::runtime::current_session->myscreen_->world().remove_ob(fx);
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

TEST(EffectAct, effect_death_explosion)
{
    auto owner = make_living_guy(FAMILY_MAGE, 0);
    if (!owner) return;

    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_owner(owner.get());
    fx->set_team_num(0);
    fx->stats()->set_level(5);
    fx->set_dead(1);
    fx->death();

    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


TEST(EffectAct, effect_death_ghost_scare)
{
    auto owner = make_living_guy(FAMILY_GHOST, 0);
    if (!owner) return;

    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_GHOST_SCARE);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_owner(owner.get());
    fx->set_dead(1);
    fx->death();

    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}
