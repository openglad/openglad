// The soldier's specials with the Gameplay Fixes setting: DISARM, CHARGE and
// WHIRLWIND (packs/core/families/living-00-soldier.lua). Each test runs the
// same scene with the setting off (the classic game, bugs included) and on,
// and pins both answers.
#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
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
#include <cstddef>
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

// ---------------------------------------------------------------------------
// "Is my front blocked?" (Gameplay Fixes U)
//
// CHARGE and DISARM both ask statistics::forward_blocked: the soldier's box
// moved one pixel along curdir, through the passability probe a walking
// soldier uses. That probe runs the collision handler of whatever it meets,
// and a living's handler is a bump: against a foe it starts a swing
// (busy += the fire delay, the attack row, and two acts later a real knife
// blow), against anything it sets collide_ob; it eats a treasure and opens a
// door the soldier holds the key to. With the setting on the question is
// asked quietly: the same answer and nothing else.
//
// The scene: a level-10 soldier bot at (96, 96) on cleared grass, facing
// right with a heading it holds (curdir = enddir = facing(lastx, lasty)),
// busy 0, 500 MP with no regeneration; a sitting level-1 foe soldier with
// 400 HP one pixel into its front (x = 111, the 16 px boxes overlap by one).
// ---------------------------------------------------------------------------

namespace {

// Counts the draws between two states of the world's stream.
int draws_between(std::uint32_t from, std::uint32_t to)
{
    std::uint32_t s = from;
    for (int n = 0; n < 100000; ++n)
    {
        if (s == to)
            return n;
        s = s * 1103515245u + 12345u;
    }
    return -1;
}

int count_sound(int id)
{
    int n = 0;
    for (const og::sim::Event& e : current_game->sim_events->events())
        if (e.kind == og::sim::EventKind::PlaySound && e.a == static_cast<std::uint32_t>(id))
            ++n;
    return n;
}

// Grass under the scene (the loaded level's own tiles may be anything), put
// back afterwards.
class Ground
{
public:
    Ground()
    {
        for (int tx = 4; tx <= 10; ++tx)
            for (int ty = 4; ty <= 9; ++ty)
                paint(tx, ty, PIX_GRASS1);
    }
    ~Ground()
    {
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it)
            world().grid.data[index(it->tx, it->ty)] = it->v;
    }
    void paint(int tx, int ty, unsigned char v)
    {
        saved_.push_back({tx, ty, static_cast<unsigned char>(world().grid.data[index(tx, ty)])});
        world().grid.data[index(tx, ty)] = v;
    }

private:
    struct Saved
    {
        int tx, ty;
        unsigned char v;
    };
    static std::size_t index(int tx, int ty)
    {
        return static_cast<std::size_t>(tx + world().grid.w * ty);
    }
    std::vector<Saved> saved_;
};

// The level-10 soldier bot of the scene, facing right with the heading it
// holds (`stale` leaves enddir pointing up instead).
walker* front_soldier(short fixes, int slot, bool stale = false)
{
    world().delete_objects();
    world().gameplay_fixes = fixes;
    current_game->sim_events->clear();
    walker* w = world().add_ob(Order::Living, FAMILY_SOLDIER);
    EXPECT_NE(nullptr, w);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(1);
    w->stats()->set_level(10);
    w->stats()->set_max_magicpoints(500);
    w->stats()->set_magicpoints(500);
    w->stats()->set_magic_per_round(0.0f);
    w->stats()->set_max_magic_delay(1 << 30);
    w->stats()->set_current_magic_delay(0);
    w->setxy(96, 96);
    w->set_act_type(ACT_RANDOM);
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_enddir(static_cast<char>(stale ? FACE_UP : FACE_RIGHT));
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_ani_type(ANI_WALK);
    w->set_collide_ob(nullptr);
    w->set_current_special(static_cast<char>(slot));
    return w;
}

// A sitting body of `team` with 400 HP, `gap` px past the soldier's right
// edge (-1: one pixel into the box one step ahead).
walker* body_ahead(const walker* soldier, unsigned char team, int gap = -1)
{
    walker* b = world().add_ob(Order::Living, FAMILY_SOLDIER);
    EXPECT_NE(nullptr, b);
    if (b == nullptr)
        return nullptr;
    b->set_team_num(team);
    b->stats()->set_level(1);
    b->setxy(static_cast<Sint32>(soldier->xpos() + soldier->sizex() + gap),
             static_cast<Sint32>(soldier->ypos()));
    b->set_act_type(ACT_SIT);
    b->set_busy(0);
    b->stats()->set_max_hitpoints(400);
    b->stats()->set_hitpoints(400);
    return b;
}

struct Cast
{
    bool cast = false;
    std::string reason;
    int draws = 0;
};

Cast cast_now(walker* w)
{
    Cast c;
    const std::uint32_t before = world().rng_.state_;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    c.cast = w->special(&why, &c.reason);
    c.draws = draws_between(before, world().rng_.state_);
    return c;
}

} // namespace

// The reporter's scene: a bot's CHARGE into a foe pressed against its
// front. The cast refuses PATH BLOCKED on both values. With the setting off
// the refusal is not free: the probe's bump starts a swing (busy 6, the
// attack row) and two acts later the knife lands (2 MP, four draws, the
// clang, the foe's HP falls). With the setting on nothing at all happens.
// After the cast the soldier is sat down so the two acts that follow play
// only what the cast left behind (an attack row plays out before the act
// type is read).
//
// RED on the unfixed game (the probe asked loud on both values): with the
// setting on busy is 6, not 0, and the foe ends at the blow's HP, not 400.
TEST_F(GameplayFixesSoldier, reporter_charge_refused_by_a_foe_in_front_swings_only_with_the_setting_off)
{
    Ground ground;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes ? "setting on" : "setting off");
        walker* w = front_soldier(fixes, 1);
        ASSERT_NE(nullptr, w);
        walker* foe = body_ahead(w, 2);
        ASSERT_NE(nullptr, foe);
        world().rng_.state_ = 20261030u;

        const Cast c = cast_now(w);
        EXPECT_FALSE(c.cast);
        EXPECT_EQ(std::string("PATH BLOCKED"), c.reason);
        EXPECT_EQ(0, c.draws) << "the probe draws nothing on the cast's tick";
        w->set_act_type(ACT_SIT);
        if (fixes == 0)
        {
            EXPECT_FLOAT_EQ(6.0f, w->busy()) << "the bump's fire delay";
            EXPECT_EQ(ANI_ATTACK, w->ani_type()) << "and a swing under way";
            EXPECT_EQ(foe, w->collide_ob());
        }
        else
        {
            EXPECT_FLOAT_EQ(0.0f, w->busy()) << "busy 6 != 0 before the fix";
            EXPECT_EQ(ANI_WALK, w->ani_type());
            EXPECT_EQ(nullptr, w->collide_ob());
        }

        const std::uint32_t s0 = world().rng_.state_;
        (void)w->act();
        const std::uint32_t s1 = world().rng_.state_;
        EXPECT_FLOAT_EQ(400.0f, foe->stats()->hitpoints()) << "nothing lands on the first act";
        EXPECT_EQ(0, draws_between(s0, s1));
        (void)w->act();
        const int act2_draws = draws_between(s1, world().rng_.state_);
        if (fixes == 0)
        {
            EXPECT_EQ(4, act2_draws) << "the knife's blow: waver, damage, the foe's hit response";
            EXPECT_FLOAT_EQ(381.0f, foe->stats()->hitpoints()) << "the blow lands";
            EXPECT_FLOAT_EQ(498.0f, w->stats()->magicpoints()) << "the knife's 2 MP";
            EXPECT_EQ(1, count_sound(SOUND_CLANG)) << "the clang";
            EXPECT_EQ(ANI_WALK, w->ani_type()) << "the swing is over";
        }
        else
        {
            EXPECT_EQ(0, act2_draws);
            EXPECT_FLOAT_EQ(400.0f, foe->stats()->hitpoints()) << "foe hp 381 != 400 before the fix";
            EXPECT_FLOAT_EQ(500.0f, w->stats()->magicpoints()) << "no MP spent";
            EXPECT_EQ(0, count_sounds()) << "no sound";
            EXPECT_FLOAT_EQ(0.0f, w->busy());
        }
    }
}

// DISARM's success adds its own 5 busy. With the setting off the probe's
// bump adds the fire delay on top: 6 + 5 = 11 for a bot, and for a level-10
// hero (whose dexterity takes its delay to about 4.72) that delay plus 5.
// With the setting on the cast adds only its own 5.
//
// RED on the unfixed game: with the setting on the bot ends at 11, the
// hero at its delay plus 5.
TEST_F(GameplayFixesSoldier, disarm_adds_only_its_own_busy_with_the_setting_on)
{
    Ground ground;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes ? "setting on" : "setting off");
        walker* bot = front_soldier(fixes, 4);
        ASSERT_NE(nullptr, bot);
        walker* foe = body_ahead(bot, 2);
        ASSERT_NE(nullptr, foe);
        const Cast c = cast_now(bot);
        EXPECT_TRUE(c.cast) << c.reason;
        EXPECT_FLOAT_EQ(60.0f, foe->busy()) << "6 * (10 - 1 + 1): the foe is staggered";
        EXPECT_FLOAT_EQ(fixes ? 5.0f : 11.0f, bot->busy());

        // The hero: the same scene with a seat's guy behind the walker.
        world().delete_objects();
        current_game->sim_events->clear();
        walker* hero = make_soldier(10);
        ASSERT_NE(nullptr, hero);
        hero->setxy(96, 96);
        hero->set_act_type(ACT_RANDOM);
        hero->set_curdir(static_cast<signed char>(FACE_RIGHT));
        hero->set_enddir(static_cast<char>(FACE_RIGHT));
        hero->set_lastx(hero->stepsize());
        hero->set_lasty(0);
        hero->set_busy(0);
        hero->set_ani_type(ANI_WALK);
        hero->set_current_special(4);
        const float delay = hero->fire_frequency();
        EXPECT_NEAR(4.723f, delay, 0.001f) << "a level-10 hero's fire delay";
        walker* hero_foe = body_ahead(hero, 2);
        ASSERT_NE(nullptr, hero_foe);
        const Cast h = cast_now(hero);
        EXPECT_TRUE(h.cast) << h.reason;
        EXPECT_FLOAT_EQ(fixes ? 5.0f : delay + 5.0f, hero->busy());
        delete hero;
    }
}

// What the probe never cost: a wall in front stops the grid half of the
// probe before any body is asked, so CHARGE's PATH BLOCKED was always free.
// An ally in front blocks too; the loud probe's bump against it swings at
// nothing (no busy) but writes collide_ob, which the quiet one leaves alone.
//
// RED on the unfixed game: with the setting on the ally is the soldier's
// collide_ob.
TEST_F(GameplayFixesSoldier, a_wall_in_front_was_always_free_and_an_ally_is_left_alone)
{
    Ground ground;
    ground.paint(7, 6, PIX_WALL2);  // x 112..127: the cell one pixel ahead
    ground.paint(7, 7, PIX_WALL2);
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes ? "setting on" : "setting off");
        walker* w = front_soldier(fixes, 1);
        ASSERT_NE(nullptr, w);
        const Cast c = cast_now(w);
        EXPECT_FALSE(c.cast);
        EXPECT_EQ(std::string("PATH BLOCKED"), c.reason);
        EXPECT_EQ(0, c.draws);
        EXPECT_FLOAT_EQ(0.0f, w->busy());
        EXPECT_EQ(ANI_WALK, w->ani_type());
        EXPECT_FLOAT_EQ(500.0f, w->stats()->magicpoints());
        EXPECT_EQ(nullptr, w->collide_ob());
        EXPECT_EQ(0, count_sounds());
    }
    ground.paint(7, 6, PIX_GRASS1);
    ground.paint(7, 7, PIX_GRASS1);
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes ? "ally, setting on" : "ally, setting off");
        walker* w = front_soldier(fixes, 1);
        ASSERT_NE(nullptr, w);
        walker* ally = body_ahead(w, 1);
        ASSERT_NE(nullptr, ally);
        const Cast c = cast_now(w);
        EXPECT_FALSE(c.cast);
        EXPECT_EQ(std::string("PATH BLOCKED"), c.reason);
        EXPECT_FLOAT_EQ(0.0f, w->busy()) << "a friend is never swung at";
        EXPECT_EQ(ANI_WALK, w->ani_type());
        EXPECT_EQ(fixes ? nullptr : ally, w->collide_ob());
    }
}

// A bot whose enddir has not caught up with its heading: the loud probe's
// bump, wanting to swing along facing(lastx, lasty), sets enddir and turns
// the bot one notch (curdir RIGHT -> UP_RIGHT, lasty 0 -> -4) instead. With
// the setting on the bot is not turned.
//
// RED on the unfixed game: with the setting on curdir is UP_RIGHT.
TEST_F(GameplayFixesSoldier, the_probe_no_longer_turns_a_bot_with_a_stale_heading)
{
    Ground ground;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes ? "setting on" : "setting off");
        walker* w = front_soldier(fixes, 1, /*stale=*/true);
        ASSERT_NE(nullptr, w);
        ASSERT_NE(nullptr, body_ahead(w, 2));
        const Cast c = cast_now(w);
        EXPECT_FALSE(c.cast);
        EXPECT_EQ(std::string("PATH BLOCKED"), c.reason);
        EXPECT_FLOAT_EQ(0.0f, w->busy()) << "a turn, not a swing";
        if (fixes == 0)
        {
            EXPECT_EQ(FACE_UP_RIGHT, w->curdir()) << "turned one notch toward UP";
            EXPECT_FLOAT_EQ(-4.0f, w->lasty());
        }
        else
        {
            EXPECT_EQ(FACE_RIGHT, w->curdir()) << "not turned";
            EXPECT_FLOAT_EQ(0.0f, w->lasty());
            EXPECT_EQ(FACE_UP, w->enddir()) << "the stale enddir is the soldier's own";
        }
    }
}

// A drumstick one pixel ahead does not block, so CHARGE goes on to rush; the
// loud probe has eaten it first. A door the soldier holds the key to does
// block, and the loud probe has opened it. With the setting on neither is
// touched.
//
// RED on the unfixed game: with the setting on the drumstick and the door
// are dead.
TEST_F(GameplayFixesSoldier, the_probe_eats_nothing_and_opens_no_door_with_the_setting_on)
{
    Ground ground;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes ? "drumstick, setting on" : "drumstick, setting off");
        walker* w = front_soldier(fixes, 1);
        ASSERT_NE(nullptr, w);
        w->stats()->set_hitpoints(w->stats()->max_hitpoints() / 2);  // hungry
        walker* food = world().add_fx_ob(Order::Treasure, FAMILY_DRUMSTICK);
        ASSERT_NE(nullptr, food);
        food->set_sizex(8);
        food->set_sizey(8);
        food->setxy(111, 100);  // one pixel into the box one step ahead
        const Cast c = cast_now(w);
        EXPECT_TRUE(c.cast) << "a treasure never blocks: " << c.reason;
        EXPECT_EQ(fixes ? 0 : 1, food->dead()) << "eaten only by the loud probe";
    }
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes ? "door, setting on" : "door, setting off");
        walker* w = front_soldier(fixes, 1);
        ASSERT_NE(nullptr, w);
        walker* door = world().add_weap_ob(Order::Weapon, FAMILY_DOOR);
        ASSERT_NE(nullptr, door);
        door->set_team_num(3);
        door->stats()->set_level(2);
        door->set_sizex(16);
        door->set_sizey(16);
        door->setxy(111, 96);
        w->set_keys(static_cast<std::uint32_t>(1u << 2));
        const Cast c = cast_now(w);
        EXPECT_FALSE(c.cast);
        EXPECT_EQ(std::string("PATH BLOCKED"), c.reason) << "the door blocks this round either way";
        EXPECT_EQ(fixes ? 0 : 1, door->dead()) << "opened only by the loud probe";
    }
}
