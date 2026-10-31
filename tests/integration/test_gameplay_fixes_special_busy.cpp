// A hero's specials must never be stuck "busy" (Gameplay Fixes, fix S).
//
// A hero fires on a fractional delay: its fire_frequency is the class's delay
// less dexterity / 47 (src/gameplay/guy.cpp). Firing adds that delay to busy
// (walker::init_fire), and busy counts down by one per act only while it is
// above zero (living::act), so after one shot it rests a fraction BELOW zero
// and stays there. The soldier's WHIRLWIND and DISARM and the archer's
// BARRAGE and EXPLODING BOLT refuse as "SPECIAL BUSY" while busy is not zero
// (packs/core/families/living-00-soldier.lua, living-02-archer.lua), so in
// the classic game a hero that has fired once can never cast them again.
// The maintainer hit it playing: WHIRLWIND cast once, then NOT ENOUGH MP,
// then SPECIAL BUSY for good.
//
// Each test plays the hero's real path (cast or shot, then the acts that
// count busy down) with the setting off, where the classic refusal is
// pinned, and on, where the special casts again.
#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include "test_sim_random_scope.h"
#include <cmath>
#include <string>

namespace {

GameWorld& world()
{
    return og::runtime::current_session->myscreen_->world();
}

void ensure_level_loaded()
{
    if (world().grid.valid() && world().pixmaxx > 0 && world().pixmaxy > 0)
        return;
    world().id = 1;
    (void)og::runtime::current_session->myscreen_->load_level();
}

// Dexterity 20 takes 20 / 47 = 0.4255... off the class's whole-number delay,
// so the hero's fire_frequency is fractional (soldier 5.574, archer 4.574).
constexpr short kHeroDexterity = 20;

// A level-10 hero of `family` with a character record, a deep pool that does
// not refill, driven as a player is (ACT_CONTROL: act() counts busy down and
// runs queued commands, and does nothing of its own).
walker* make_hero(int family)
{
    ensure_level_loaded();
    guy g(family);
    g.teamnum = 1;
    g.upgrade_to_level(10, true);
    g.dexterity = kHeroDexterity;
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
    {
        w->setxy(100, 100);
        w->stats()->set_magicpoints(500);
        w->stats()->set_max_magicpoints(500);
        w->set_act_type(ACT_CONTROL);
        w->set_busy(0);
        // No magic comes back on its own, so every pool reading is exact.
        w->stats()->set_magic_per_round(0.0f);
        w->stats()->set_max_magic_delay(1 << 30);
        w->stats()->set_current_magic_delay(0);
    }
    return w.release();
}

void face(walker* w, int facing_dir)
{
    const float step = w->stepsize();
    w->set_lastx(facing_dir == FACE_RIGHT ? step : -step);
    w->set_lasty(0);
    w->set_curdir(static_cast<signed char>(facing_dir));
    w->set_enddir(static_cast<char>(facing_dir));
}

void act_ticks(walker* w, int ticks)
{
    FixedRandom rng(0u);
    ScopedSimRandom scoped(&rng);
    for (int i = 0; i < ticks; ++i)
        (void)w->act();
}

// One shot away from `toward`, then the acts that finish the throw and count
// busy down. Returns false (with a gtest failure) if the shot did not start.
bool shoot_once(walker* w, int toward)
{
    face(w, toward);
    EXPECT_FLOAT_EQ(0.0f, w->busy()) << "the hero is idle before the shot";
    const bool started = w->init_fire();
    EXPECT_TRUE(started) << "the shot starts";
    act_ticks(w, 40);
    return started;
}

struct Cast
{
    bool cast = false;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
};

Cast try_special(walker* w)
{
    Cast c;
    FixedRandom rng(0u);
    ScopedSimRandom scoped(&rng);
    c.cast = w->special(&c.why, &c.reason);
    return c;
}

// A stationary foe of `level` on team 2 at (x, y).
walker* add_foe(Sint32 x, Sint32 y, std::int32_t level = 1)
{
    walker* foe = world().add_ob(Order::Living, FAMILY_TOWER1);
    EXPECT_NE(nullptr, foe);
    if (foe == nullptr)
        return nullptr;
    foe->set_team_num(2);
    foe->stats()->set_level(level);
    foe->setxy(x, y);
    foe->set_busy(0);
    return foe;
}

class GameplayFixesSpecialBusy : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensure_level_loaded();
        fixes_before_ = world().gameplay_fixes;
        world().delete_objects();
    }
    void TearDown() override
    {
        world().gameplay_fixes = fixes_before_;
        world().delete_objects();
    }

private:
    short fixes_before_ = 0;
};

// After a shot, busy rests a fraction below zero, and stays there.
void expect_stuck_below_zero(walker* w)
{
    const float frac = w->fire_frequency() - std::floor(w->fire_frequency());
    EXPECT_GT(frac, 0.0f) << "the hero's fire delay is fractional: " << w->fire_frequency();
    EXPECT_LT(w->busy(), 0.0f) << "busy ran a fraction past zero";
    EXPECT_GT(w->busy(), -1.0f);
    const float rest = w->busy();
    act_ticks(w, 50);
    EXPECT_FLOAT_EQ(rest, w->busy()) << "and nothing brings it back to zero";
}

} // namespace

// The maintainer's case: WHIRLWIND, then NOT ENOUGH MP, then a shot, then
// with the magic back the spin is refused "SPECIAL BUSY" on every act for
// as long as anyone waits (the classic game), or casts (the setting on).
TEST_F(GameplayFixesSpecialBusy, whirlwind_after_a_shot)
{
    for (const short fixes : {short{0}, short{1}})
    {
        world().delete_objects();
        world().gameplay_fixes = fixes;
        walker* w = make_hero(FAMILY_SOLDIER);
        ASSERT_NE(nullptr, w);
        // Inside the spin (32 + 2 * 10 px) so the setting's fix K casts too.
        ASSERT_NE(nullptr, add_foe(static_cast<Sint32>(w->xpos() + 30),
                                   static_cast<Sint32>(w->ypos())));
        w->set_current_special(3);

        const Cast first = try_special(w);
        EXPECT_TRUE(first.cast) << "the first WHIRLWIND, fixes=" << fixes << " " << first.reason;
        act_ticks(w, 40);
        EXPECT_FLOAT_EQ(0.0f, w->busy()) << "its 8 busy counts down to exactly zero";

        const float pool = w->stats()->magicpoints();
        w->stats()->set_magicpoints(0);
        const Cast broke = try_special(w);
        EXPECT_FALSE(broke.cast);
        EXPECT_EQ(walker::SpecialFailure::NoMP, broke.why);
        w->stats()->set_magicpoints(pool);

        ASSERT_TRUE(shoot_once(w, FACE_LEFT));
        expect_stuck_below_zero(w);
        const float ready = w->stats()->magicpoints();
        EXPECT_FLOAT_EQ(pool - 2.0f, ready) << "the throw cost its 2 MP";

        if (fixes == 0)
        {
            int refused = 0;
            for (int i = 0; i < 200; ++i)
            {
                act_ticks(w, 1);
                const Cast c = try_special(w);
                if (!c.cast && c.why == walker::SpecialFailure::ScriptDeclined &&
                    c.reason == "SPECIAL BUSY")
                    ++refused;
            }
            EXPECT_EQ(200, refused) << "the classic spin is refused on every act";
            EXPECT_FLOAT_EQ(ready, w->stats()->magicpoints()) << "and spends nothing";
            EXPECT_LT(w->busy(), 0.0f);
        }
        else
        {
            const Cast again = try_special(w);
            EXPECT_TRUE(again.cast) << "with the setting on the hero spins again: " << again.reason;
            EXPECT_FLOAT_EQ(ready - 120.0f, w->stats()->magicpoints()) << "for its price";
            EXPECT_GT(w->busy(), 7.0f) << "and its 8 busy";
        }
        delete w;
    }
}

// DISARM: a hero soldier that has fired once, then faces a foe at close
// range.
TEST_F(GameplayFixesSpecialBusy, disarm_after_a_shot)
{
    for (const short fixes : {short{0}, short{1}})
    {
        world().delete_objects();
        world().gameplay_fixes = fixes;
        walker* w = make_hero(FAMILY_SOLDIER);
        ASSERT_NE(nullptr, w);
        w->set_current_special(4);
        ASSERT_TRUE(shoot_once(w, FACE_LEFT));
        expect_stuck_below_zero(w);

        face(w, FACE_RIGHT);
        walker* foe = add_foe(static_cast<Sint32>(w->xpos() + w->sizex() - 4),
                              static_cast<Sint32>(w->ypos()));
        ASSERT_NE(nullptr, foe);
        // The foe overlaps the box one pixel ahead, so it blocks the way
        // forward. (Not asked here: with the setting off the probe is a bump
        // that swings at the foe and makes the hero busy; on, it is quiet.)
        const float pool = w->stats()->magicpoints();

        const Cast c = try_special(w);
        if (fixes == 0)
        {
            EXPECT_FALSE(c.cast) << "the classic DISARM is refused";
            EXPECT_EQ(std::string("SPECIAL BUSY"), c.reason);
            EXPECT_FLOAT_EQ(pool, w->stats()->magicpoints());
            EXPECT_FLOAT_EQ(0.0f, foe->busy()) << "the foe is not staggered";
        }
        else
        {
            // Every draw 0: og.rand(10) = 0 >= og.rand(1) = 0, the hero wins
            // against the level-1 foe: 6 * (10 - 1 + 1) = 60.
            EXPECT_TRUE(c.cast) << "with the setting on the hero disarms: " << c.reason;
            EXPECT_FLOAT_EQ(60.0f, foe->busy()) << "the foe is staggered";
            EXPECT_FLOAT_EQ(pool - 150.0f, w->stats()->magicpoints());
        }
        delete w;
    }
}

// The archer's BARRAGE (slot 2) and EXPLODING BOLT (slot 3) after a shot.
TEST_F(GameplayFixesSpecialBusy, archer_specials_after_a_shot)
{
    for (const char slot : {char{2}, char{3}})
    {
        for (const short fixes : {short{0}, short{1}})
        {
            world().delete_objects();
            world().gameplay_fixes = fixes;
            walker* w = make_hero(FAMILY_ARCHER);
            ASSERT_NE(nullptr, w);
            w->set_current_special(slot);
            ASSERT_TRUE(shoot_once(w, FACE_LEFT));
            expect_stuck_below_zero(w);
            face(w, FACE_RIGHT);
            const float pool = w->stats()->magicpoints();
            const float rest = w->busy();

            const Cast c = try_special(w);
            if (fixes == 0)
            {
                EXPECT_FALSE(c.cast) << "slot " << int{slot} << ": the classic cast is refused";
                EXPECT_EQ(std::string("SPECIAL BUSY"), c.reason) << "slot " << int{slot};
                EXPECT_FLOAT_EQ(pool, w->stats()->magicpoints()) << "slot " << int{slot};
                EXPECT_FLOAT_EQ(rest, w->busy()) << "slot " << int{slot};
            }
            else
            {
                EXPECT_TRUE(c.cast) << "slot " << int{slot} << ": the setting on casts: " << c.reason;
                EXPECT_LT(w->stats()->magicpoints(), pool) << "slot " << int{slot} << " pays";
                if (slot == 2)
                {
                    EXPECT_GT(w->busy(), 0.0f) << "BARRAGE adds twice the fire delay";
                }
                else
                {
                    EXPECT_FLOAT_EQ(rest, w->busy()) << "EXPLODING BOLT adds no busy";
                }
            }
            delete w;
        }
    }
}

// The rule is the setting's only: a whole-number busy behaves as it always
// did. A hero that never fired (busy exactly 0) casts on both values, and a
// hero still busy after its spin is refused on both.
TEST_F(GameplayFixesSpecialBusy, positive_and_zero_busy_are_unchanged)
{
    for (const short fixes : {short{0}, short{1}})
    {
        world().delete_objects();
        world().gameplay_fixes = fixes;
        walker* w = make_hero(FAMILY_ARCHER);
        ASSERT_NE(nullptr, w);
        w->set_current_special(3);
        face(w, FACE_RIGHT);
        const Cast idle = try_special(w);
        EXPECT_TRUE(idle.cast) << "busy 0 casts, fixes=" << fixes << " " << idle.reason;

        w->set_busy(0.25f);
        const Cast busy = try_special(w);
        EXPECT_FALSE(busy.cast) << "busy above zero is busy, fixes=" << fixes;
        EXPECT_EQ(std::string("SPECIAL BUSY"), busy.reason);
        delete w;
    }
}
