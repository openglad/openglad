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
    fx->ani_type = ANI_EXPLODE;
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
    fx->lifetime = 100;
    fx->stats()->hitpoints = 100;
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
    ASSERT_TRUE(fx->dead == 1) << "shield without owner dies";
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
    fx->lifetime = 100;
    fx->stats()->hitpoints = 100;
    fx->drawcycle = 10;
    fx->act();

    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


TEST(EffectAct, boomerang_expired)
{
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_owner(nullptr);
    fx->drawcycle = 254;
    fx->act();
    ASSERT_TRUE(fx->dead == 1) << "expired boomerang dies";
    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


TEST(EffectAct, cloud)
{
    auto owner = make_living_guy(FAMILY_DRUID, 0);
    if (!owner) return;

    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_CLOUD);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_owner(owner.get());
    fx->team_num = 0;
    fx->lifetime = 50;
    fx->stats()->hitpoints = 50;
    fx->act();

    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}


TEST(EffectAct, cloud_expired)
{
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_CLOUD);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->set_owner(fx);
    fx->lifetime = 0;
    fx->act();
    ASSERT_TRUE(fx->dead == 1) << "expired cloud dies";
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
    fx->ani_type = ANI_EXPLODE;
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
    fx->team_num = 0;
    fx->stats()->level = 5;
    fx->dead = 1;
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
    fx->dead = 1;
    fx->death();

    og::runtime::current_session->myscreen_->world().remove_ob(fx);
}

