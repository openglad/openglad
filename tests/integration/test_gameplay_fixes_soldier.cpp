// The soldier's specials with the Gameplay Fixes setting: DISARM and
// WHIRLWIND (packs/core/families/living-00-soldier.lua). Each test runs the
// same scene with the setting off (the classic game, bugs included) and on,
// and pins both answers.
#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include "test_sim_random_scope.h"
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

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

// A soldier with a guy (so its notices print) and a deep pool.
walker* make_soldier(short level)
{
    ensure_level_loaded();
    guy g(FAMILY_SOLDIER);
    g.teamnum = 1;
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
    {
        w->setxy(100, 100);
        w->stats()->set_magicpoints(500);
        w->stats()->set_max_magicpoints(500);
    }
    return w.release();
}

// Draws the listed values in turn (each taken modulo the bound), then
// repeats the last one.
class SequenceRandom : public IRandom
{
public:
    explicit SequenceRandom(std::vector<std::uint32_t> values) : values_(std::move(values)) {}
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        const std::uint32_t v = values_[at_ < values_.size() ? at_ : values_.size() - 1];
        ++at_;
        return v % max_exclusive;
    }

private:
    std::vector<std::uint32_t> values_;
    std::size_t at_ = 0;
};

int count_notifications(const char* needle)
{
    int n = 0;
    for (const og::sim::Event& e : current_game->sim_events->events())
        if (e.kind == og::sim::EventKind::Notification &&
            e.text.find(needle) != std::string::npos)
            ++n;
    return n;
}

int count_sounds()
{
    int n = 0;
    for (const og::sim::Event& e : current_game->sim_events->events())
        if (e.kind == og::sim::EventKind::PlaySound)
            ++n;
    return n;
}

class GameplayFixesSoldier : public ::testing::Test
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

// What one DISARM cast did.
struct DisarmOutcome
{
    bool cast = false;
    std::string reason;
    float foe_busy = 0.0f;
    float caster_busy = 0.0f;
    float mp_spent = 0.0f;
    int notices = 0;
    int sounds = 0;
};

// A level-4 soldier facing right with a foe of `foe_level` overlapping the
// box one pixel ahead (forward_blocked), the sim stream scripted by `draws`.
// `blocker_team` 1 puts the walker ahead on the soldier's own team: it still
// blocks the way forward, but nobody inside the reach is a foe. A non-zero
// `disarm_cost` sets the DISARM slot's price before the cast.
DisarmOutcome cast_disarm(short fixes, std::int32_t foe_level, std::vector<std::uint32_t> draws,
                          unsigned char blocker_team = 2, unsigned short disarm_cost = 0)
{
    world().delete_objects();
    DisarmOutcome out;
    walker* w = make_soldier(4);
    EXPECT_NE(nullptr, w);
    if (w == nullptr)
        return out;
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(4);
    if (disarm_cost != 0)
        w->stats()->set_special_cost(4, disarm_cost);

    walker* foe = world().add_ob(Order::Living, FAMILY_TOWER1);
    EXPECT_NE(nullptr, foe);
    if (foe == nullptr)
    {
        delete w;
        return out;
    }
    foe->set_team_num(blocker_team);
    foe->stats()->set_level(foe_level);
    foe->setxy(static_cast<Sint32>(w->xpos() + w->sizex() - 4), static_cast<Sint32>(w->ypos()));
    foe->set_busy(0);
    EXPECT_TRUE(w->stats()->forward_blocked()) << "the foe blocks the way forward";

    world().gameplay_fixes = fixes;
    current_game->sim_events->clear();
    const float mp_before = w->stats()->magicpoints();
    walker::SpecialFailure why = walker::SpecialFailure::None;
    {
        SequenceRandom rng(std::move(draws));
        ScopedSimRandom scoped(&rng);
        out.cast = w->special(&why, &out.reason);
    }
    out.foe_busy = foe->busy();
    out.caster_busy = w->busy();
    out.mp_spent = mp_before - w->stats()->magicpoints();
    out.notices = count_notifications("Disarmed");
    out.sounds = count_sounds();
    delete w;
    return out;
}

} // namespace

// A won roll against a foe three levels up: the classic penalty
// 6 * (4 - 7 + 1) = -12 is added to the foe's busy, so the "disarmed" foe
// swings sooner, and the cast still reports a disarm. With the setting on a
// stagger below zero is not applied and does not count, so nobody was
// disarmed and the cast is refused at half price with the usual busy.
TEST_F(GameplayFixesSoldier, disarm_never_speeds_up_a_higher_foe)
{
    // Every draw 0: og.rand(4) = 0 >= og.rand(7) = 0, the caster wins.
    const DisarmOutcome off = cast_disarm(0, 7, {0});
    EXPECT_TRUE(off.cast) << "the classic cast reports a disarm";
    EXPECT_FLOAT_EQ(-12.0f, off.foe_busy) << "and speeds the higher foe up";
    EXPECT_EQ(1, off.notices) << "with the notice";
    EXPECT_FLOAT_EQ(150.0f, off.mp_spent) << "for the slot's 150 MP";
    EXPECT_FLOAT_EQ(5.0f, off.caster_busy);

    const DisarmOutcome on = cast_disarm(1, 7, {0});
    EXPECT_FALSE(on.cast) << "with the setting on nobody was staggered";
    EXPECT_EQ(std::string("DISARM RESISTED"), on.reason);
    EXPECT_FLOAT_EQ(0.0f, on.foe_busy) << "the foe is not sped up";
    EXPECT_FLOAT_EQ(75.0f, on.mp_spent) << "and the try costs half the slot's 150 MP";
    EXPECT_EQ(0, on.notices) << "no notice";
    EXPECT_EQ(0, on.sounds) << "no sound";
    EXPECT_FLOAT_EQ(5.0f, on.caster_busy) << "and the usual 5 busy";

    // The same won roll against a lower foe staggers it on both values.
    for (const short fixes : {short{0}, short{1}})
    {
        const DisarmOutcome lower = cast_disarm(fixes, 2, {0});
        EXPECT_TRUE(lower.cast) << "fixes=" << fixes;
        EXPECT_FLOAT_EQ(18.0f, lower.foe_busy) << "6 * (4 - 2 + 1), fixes=" << fixes;
        EXPECT_EQ(1, lower.notices) << "fixes=" << fixes;
    }
}

// An equal-level foe that wins the roll: the classic cast prints "Fighter
// Disarmed Enemy!" and spends 150 MP on a foe it did not touch. With the
// setting on the notice needs a win: the cast is refused, at half price and
// the usual busy.
TEST_F(GameplayFixesSoldier, disarm_notice_needs_a_win)
{
    // Left-first: og.rand(4) draws 0, og.rand(4) for the foe draws 3.
    const DisarmOutcome off = cast_disarm(0, 4, {0, 3});
    EXPECT_TRUE(off.cast) << "the classic cast succeeds on a lost roll";
    EXPECT_EQ(1, off.notices) << "and claims a disarm";
    EXPECT_FLOAT_EQ(0.0f, off.foe_busy) << "that did not happen";
    EXPECT_FLOAT_EQ(150.0f, off.mp_spent);

    const DisarmOutcome on = cast_disarm(1, 4, {0, 3});
    EXPECT_FALSE(on.cast) << "with the setting on a lost roll is refused";
    EXPECT_EQ(std::string("DISARM RESISTED"), on.reason);
    EXPECT_EQ(0, on.notices);
    EXPECT_EQ(0, on.sounds);
    EXPECT_FLOAT_EQ(75.0f, on.mp_spent);
    EXPECT_FLOAT_EQ(5.0f, on.caster_busy);
}

// Something blocks the way forward but nobody inside the reach is a foe (the
// walker ahead is on the soldier's own team). Both values refuse for free;
// with the setting on the message says there is nobody to disarm.
TEST_F(GameplayFixesSoldier, disarm_with_nobody_in_reach_is_free)
{
    const DisarmOutcome off = cast_disarm(0, 7, {0}, /*blocker_team=*/1);
    EXPECT_FALSE(off.cast) << "the classic cast finds no foe";
    EXPECT_EQ(std::string("NO FOE IN RANGE"), off.reason);
    EXPECT_FLOAT_EQ(0.0f, off.mp_spent);
    EXPECT_FLOAT_EQ(0.0f, off.caster_busy);
    EXPECT_EQ(0, off.notices);
    EXPECT_EQ(0, off.sounds);

    const DisarmOutcome on = cast_disarm(1, 7, {0}, /*blocker_team=*/1);
    EXPECT_FALSE(on.cast);
    EXPECT_EQ(std::string("NO ONE TO DISARM"), on.reason);
    EXPECT_FLOAT_EQ(0.0f, on.mp_spent) << "nothing is spent";
    EXPECT_FLOAT_EQ(0.0f, on.caster_busy) << "and there is no busy";
    EXPECT_EQ(0, on.notices);
    EXPECT_EQ(0, on.sounds);
}

// A resisted disarm pays half of the slot's own price, whatever that price
// is: 75 of the classic 150, and 150 when the slot costs 300.
TEST_F(GameplayFixesSoldier, a_resisted_disarm_pays_half_the_slots_own_price)
{
    // Every draw 0: the caster wins against a foe three levels up, but the
    // stagger is not positive, so the try is resisted.
    const DisarmOutcome classic_price = cast_disarm(1, 7, {0});
    EXPECT_EQ(std::string("DISARM RESISTED"), classic_price.reason);
    EXPECT_FLOAT_EQ(75.0f, classic_price.mp_spent) << "half of the soldier's 150";

    const DisarmOutcome dearer = cast_disarm(1, 7, {0}, 2, /*disarm_cost=*/300);
    EXPECT_FALSE(dearer.cast);
    EXPECT_EQ(std::string("DISARM RESISTED"), dearer.reason);
    EXPECT_FLOAT_EQ(150.0f, dearer.mp_spent) << "half of the slot's own 300";
    EXPECT_FLOAT_EQ(5.0f, dearer.caster_busy);
}

// WHIRLWIND with nobody inside its ring. The classic cast spins at nothing:
// 8 busy, eight walk orders and 120 MP. With the setting on it is refused
// before it writes anything. The setting-off case is also pinned in
// test_walker_specials.cpp (soldier_whirlwind_drives_simulation).
TEST_F(GameplayFixesSoldier, whirlwind_refuses_with_no_foe_when_on)
{
    for (const short fixes : {short{0}, short{1}})
    {
        world().delete_objects();
        walker* w = make_soldier(4);
        ASSERT_NE(nullptr, w);
        w->set_lastx(w->stepsize());
        w->set_lasty(0);
        w->set_busy(0);
        w->set_current_special(3);
        world().gameplay_fixes = fixes;
        current_game->sim_events->clear();
        const int cmds_before = static_cast<int>(w->stats()->commands.size());
        const float mp_before = w->stats()->magicpoints();
        walker::SpecialFailure why = walker::SpecialFailure::None;
        std::string reason;
        const bool cast = w->special(&why, &reason);
        const int queued = static_cast<int>(w->stats()->commands.size()) - cmds_before;
        if (fixes == 0)
        {
            EXPECT_TRUE(cast) << "the classic spin at nobody";
            EXPECT_FLOAT_EQ(8.0f, w->busy());
            EXPECT_EQ(8, queued);
            EXPECT_FLOAT_EQ(mp_before - 120.0f, w->stats()->magicpoints());
        }
        else
        {
            EXPECT_FALSE(cast) << "with the setting on it is refused";
            EXPECT_EQ(walker::SpecialFailure::ScriptDeclined, why);
            EXPECT_EQ(std::string("NO FOE IN RANGE"), reason);
            EXPECT_FLOAT_EQ(0.0f, w->busy()) << "no busy";
            EXPECT_EQ(0, queued) << "no walk orders";
            EXPECT_FLOAT_EQ(mp_before, w->stats()->magicpoints()) << "no MP";
            EXPECT_EQ(0, count_sounds()) << "no sound";
            EXPECT_EQ(0, static_cast<int>(w->special_rest_left())) << "no rest";
        }
        delete w;
    }
}

// The setting does not stop a WHIRLWIND that has a foe in its ring (32 +
// 2 * 4 = 40 px at level 4): it spins and shoves the foe on both values.
TEST_F(GameplayFixesSoldier, whirlwind_with_a_foe_spins_on_both_values)
{
    for (const short fixes : {short{0}, short{1}})
    {
        world().delete_objects();
        walker* w = make_soldier(4);
        ASSERT_NE(nullptr, w);
        w->set_busy(0);
        w->set_current_special(3);
        walker* foe = world().add_ob(Order::Living, FAMILY_TOWER1);
        ASSERT_NE(nullptr, foe);
        foe->set_team_num(2);
        foe->setxy(static_cast<Sint32>(w->xpos() + 30), static_cast<Sint32>(w->ypos()));
        world().gameplay_fixes = fixes;
        const int foe_cmds_before = static_cast<int>(foe->stats()->commands.size());
        bool cast = false;
        {
            FixedRandom rng(0u);
            ScopedSimRandom scoped(&rng);
            cast = w->special();
        }
        EXPECT_TRUE(cast) << "fixes=" << fixes;
        EXPECT_FLOAT_EQ(8.0f, w->busy()) << "fixes=" << fixes;
        EXPECT_EQ(foe_cmds_before + 1, static_cast<int>(foe->stats()->commands.size()))
            << "the foe in the ring is shoved, fixes=" << fixes;
        delete w;
    }
}
