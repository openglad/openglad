#include <openglad/gameplay/statistics.h>
#include <openglad/interface/game_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include "test_sim_random_scope.h"

#include <algorithm>
#include <list>
#include <memory>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* ctx) { push_test_context(ctx); }
    ~GlobalContextGuard() { pop_test_context(); }
    GlobalContextGuard(const GlobalContextGuard&) = delete;
    GlobalContextGuard& operator=(const GlobalContextGuard&) = delete;
};

static std::unique_ptr<walker> make_living(char family, unsigned char team = 0, short level = 3)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
        w->setxy(100, 100);
    return w;
}

// A written RNG script for the world's SimRandom -- the stream living::act,
// act_random, act_guard, death() and statistics::try_command actually draw
// from, reached with ScopedSimRandom. A GameContext rng reaches only walker
// construction and combat math (walker_rng/combat_rng), so pushing a context
// RNG leaves the AI branch picks to whatever a shuffled predecessor left in
// the LCG.
class SequenceRandom : public IRandom
{
public:
    explicit SequenceRandom(std::initializer_list<std::uint32_t> values)
        : values_(values), index_(0) {}

    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        const std::uint32_t raw = (index_ < values_.size()) ? values_[index_++] : values_.back();
        return raw % max_exclusive;
    }

private:
    std::vector<std::uint32_t> values_;
    std::size_t index_;
};

// The head of a walker's command queue: which AI arm just ran, and with what
// tick budget. COMMAND_WALK/40 is act_random's no-foe arm, COMMAND_SEARCH/200
// its blocked-shot arm, COMMAND_FIRE its clear-shot arm, COMMAND_SEARCH/300
// living::act's own "4 of 5 times" arm.
struct QueuedCommand
{
    int type = -1;
    int count = -1;
    int com1 = -9999;
    int com2 = -9999;
};

static QueuedCommand queued_command(walker* w)
{
    const auto& q = w->stats()->commands;
    if (q.empty())
        return {};
    return {static_cast<int>(q.front().commandtype),
            static_cast<int>(q.front().commandcount),
            static_cast<int>(q.front().com1),
            static_cast<int>(q.front().com2)};
}

// Puts a walker into the exact pre-switch state act() needs to reach its
// act_type arm: nothing animating, nothing queued, no pending turn, not busy,
// not frozen. Without this, act() returns early from animate()/turn()/
// do_command() and never touches the branch under test.
static void ready_to_act(walker* w, short act_type, int dir = FACE_UP)
{
    w->stats()->clear_command();
    w->stats()->set_frozen_delay(0);
    w->set_ani_type(ANI_WALK);
    w->set_curdir(static_cast<signed char>(dir));
    w->set_enddir(static_cast<char>(dir));
    // set_weapon_heading() and create_weapon() read lastx/lasty -- the firing
    // heading -- not curdir, so keep the two consistent or every shot launches
    // due north off the map and fire_check returns WallBlocked.
    const float step = w->stepsize();
    switch (dir)
    {
        case FACE_UP:    w->set_lastx(0.0f);   w->set_lasty(-step); break;
        case FACE_RIGHT: w->set_lastx(step);   w->set_lasty(0.0f);  break;
        case FACE_DOWN:  w->set_lastx(0.0f);   w->set_lasty(step);  break;
        case FACE_LEFT:  w->set_lastx(-step);  w->set_lasty(0.0f);  break;
        default: break;
    }
    w->set_busy(0.0f);
    w->set_act_type(act_type);
}

// Live entries of one order+family in the world object list. Both
// create_weapon()'s generator arm and death()'s generator arm add there
// (death()'s explosions are add_ob(Order::FX, ...), which routes to oblist --
// NOT to fxlist).
static int count_obs(Order order, int family)
{
    int n = 0;
    for (const auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
        if (uptr && uptr->order() == order && static_cast<int>(uptr->family()) == family)
            ++n;
    return n;
}
} // namespace

TEST(WalkerCoreMore, walker_compute_outline_state_transitions)
{
    auto viewer = make_living(FAMILY_SOLDIER, 1);
    auto subject = make_living(FAMILY_SOLDIER, 2);
    ASSERT_TRUE(viewer != nullptr && subject != nullptr) << "walkers created";
    if (!(viewer && subject))
        return;

    // Drive the outline state machine through multiple branches.
    subject->set_outline(OUTLINE_INVULNERABLE);
    subject->set_flight_left(5);
    subject->set_invisibility_left(0);
    subject->set_invulnerable_left(5);
    subject->stats()->set_bit_flags(BIT_NAMED, 1);

    subject->compute_outline(viewer.get());
    ASSERT_EQ(OUTLINE_FLYING, (int)subject->outline())
        << "invulnerable + flying: flight wins the invulnerable branch";

    subject->set_outline(subject->query_team_color()); // OUTLINE_INVISIBLE expands to query_team_color()
    subject->set_flight_left(0);
    subject->set_invulnerable_left(5);
    subject->compute_outline(viewer.get());
    ASSERT_TRUE(subject->outline() == OUTLINE_INVULNERABLE) << "invisible should transition to invulnerable when invulnerable_left set";

    subject->set_outline(OUTLINE_FLYING);
    subject->set_invulnerable_left(0);
    subject->set_invisibility_left(5);
    // If BIT_NAMED is set and the viewer is on another team, compute_outline()
    // prioritizes OUTLINE_NAMED over invisibility. Clear it to exercise the
    // OUTLINE_FLYING -> OUTLINE_INVISIBLE transition.
    subject->stats()->set_bit_flags(BIT_NAMED, 0);
    subject->compute_outline(viewer.get());
    ASSERT_TRUE(subject->outline() == subject->query_team_color()) << "flying should transition to invisible when invisibility_left set";

    // Flying, no longer invisible, but under an invulnerability potion: the
    // potion is the outline a player must be able to read at a glance.
    subject->set_outline(OUTLINE_FLYING);
    subject->set_invisibility_left(0);
    subject->set_invulnerable_left(5);
    subject->compute_outline(viewer.get());
    ASSERT_EQ(OUTLINE_INVULNERABLE, (int)subject->outline())
        << "flying + invulnerable, not invisible: the potion outline shows";

    // Plain team colour, no potion of any kind, but a NAMED enemy: a boss on
    // the other team is outlined for the viewer who has to fight it.
    subject->set_outline(subject->query_team_color());
    subject->set_invulnerable_left(0);
    subject->set_flight_left(0);
    subject->set_invisibility_left(0);
    subject->stats()->set_bit_flags(BIT_NAMED, 1);
    ASSERT_NE(subject->team_num(), viewer->team_num());
    subject->compute_outline(viewer.get());
    ASSERT_EQ(OUTLINE_NAMED, (int)subject->outline())
        << "a named enemy is outlined as named for an enemy viewer";

    // The same walker seen by one of its OWN team is not outlined as named.
    subject->set_outline(subject->query_team_color());
    viewer->set_team_num(subject->team_num());
    subject->compute_outline(viewer.get());
    ASSERT_EQ((int)subject->query_team_color(), (int)subject->outline())
        << "a named ally keeps its team colour";
}


TEST(WalkerCoreMore, walker_generator_fire_sets_weapon_lifetime_or_owner_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    // NOTE: walker::fire() has additional state/animation dependencies, so this test
    // sticks to the generator-specific weapon creation path plus create_weapon().

    // Generator: mage tower (generator-only create_weapon path).
    walker* gen_tower = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_TRUE(gen_tower != nullptr) << "generator tower created";
    if (gen_tower) {
        gen_tower->set_team_num(2);
        gen_tower->stats()->set_level(5);
        gen_tower->setxy(128, 128);
        gen_tower->set_lastx(1);
        gen_tower->set_lasty(0);
        gen_tower->stats()->set_magicpoints(9999.0f);
        walker* weapon = gen_tower->fire();
        ASSERT_TRUE(weapon != nullptr) << "tower fire should create a living projectile/spawn";
        if (weapon)
        {
            ASSERT_EQ(ANI_TELE_IN, (int)weapon->ani_type()) << "tower spawn should set tele-in animation";
            ASSERT_TRUE(weapon->owner() == nullptr) << "tower spawn should clear owner";
        }
    }

    // Generator: tent (default generator branch).
    walker* gen_tent = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(gen_tent != nullptr) << "generator tent created";
    if (gen_tent) {
        gen_tent->set_team_num(3);
        gen_tent->stats()->set_level(4);
        gen_tent->setxy(160, 128);
        gen_tent->set_lastx(1);
        gen_tent->set_lasty(0);
        gen_tent->stats()->set_magicpoints(9999.0f);
        walker* weapon = gen_tent->fire();
        ASSERT_TRUE(weapon != nullptr) << "tent fire should create a living projectile/spawn";
        if (weapon)
        {
            ASSERT_TRUE(weapon->lifetime() >= 800) << "tent spawn should assign lifetime";
            ASSERT_TRUE(weapon->owner() == gen_tent) << "tent spawn should keep owner";
        }
    }

    og::runtime::current_session->myscreen_->world().delete_objects();
}


// walker::create_weapon()'s Order::Generator arm: the summon is a LIVING of the
// generator's default_weapon family, on the generator's team, owned by it, on
// its floor -- and deliberately NOT difficulty-scaled here (A12b: fire() rolls
// the spawn's real level and applies set_difficulty exactly once).
TEST(WalkerCoreMore, walker_generator_create_weapon_spawns_owned_living_unscaled)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    // A hard difficulty is live while the summon is created, so a reinstated
    // set_difficulty() call here would be visible in the spawn's hitpoints.
    const short old_difficulty = world.difficulty;
    world.difficulty = 150;

    walker* gen = world.add_ob(Order::Generator, FAMILY_TREEHOUSE);
    ASSERT_NE(nullptr, gen) << "generator created";
    gen->set_team_num(1);
    gen->stats()->set_level(3);
    gen->set_floor(3);  // a summon must land on the summoner's floor, not floor 0
    gen->set_default_weapon(FAMILY_ELF);
    gen->set_current_weapon(gen->default_weapon());

    // Reference spawn straight from the loader: the unscaled baseline.
    walker* reference = world.add_ob(Order::Living, FAMILY_ELF);
    ASSERT_NE(nullptr, reference) << "reference elf created";
    const float unscaled_hp = reference->stats()->max_hitpoints();

    const int elves_before = count_obs(Order::Living, FAMILY_ELF);
    walker* weapon = gen->create_weapon();
    ASSERT_NE(nullptr, weapon) << "create_weapon spawns a living for generators";
    ASSERT_EQ(elves_before + 1, count_obs(Order::Living, FAMILY_ELF))
        << "the summon is added to the world's object list";
    ASSERT_EQ(Order::Living, weapon->query_order())
        << "a generator's create_weapon spawns a LIVING, not an Order::Weapon";
    ASSERT_EQ(static_cast<int>(FAMILY_ELF), static_cast<int>(weapon->family()))
        << "the summon is the generator's default_weapon family";
    ASSERT_EQ(1, static_cast<int>(weapon->team_num()))
        << "the summon joins the generator's team";
    ASSERT_EQ(gen, weapon->owner()) << "the generator owns its summon";
    ASSERT_EQ(3, static_cast<int>(weapon->floor()))
        << "the summon spawns on the summoner's floor";
    ASSERT_FLOAT_EQ(unscaled_hp, weapon->stats()->max_hitpoints())
        << "A12b: create_weapon must not difficulty-scale the summon";

    world.difficulty = old_difficulty;
    world.delete_objects();
}


TEST(WalkerCoreMore, walker_act_guard_and_random_branch_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    auto actor = make_living(FAMILY_ORC, 1, 4);
    ASSERT_TRUE(actor != nullptr) << "actor created";
    if (!actor)
        return;

    actor->setxy(96, 96);

    {
        FixedRandom rng1(1);
        GameContext c;
        c.rng = &rng1;
        GlobalContextGuard guard(&c);

        actor->set_act_type(ACT_GUARD);
        actor->set_foe(nullptr);
        const bool acted = actor->act();
        ASSERT_TRUE(!acted) << "ACT_GUARD with no nearby foe should return false";
    }

    {
        // 1-in-5 special roll misses, 1-in-5 act_random() roll hits: act()
        // delegates to living::act_random(), which finds no foe in an empty
        // level and queues its wander (COMMAND_RANDOM_WALK expands to
        // COMMAND_WALK) for exactly 40 ticks.
        SequenceRandom rng_seq_values({1, 0, 0});
        ScopedSimRandom rng_seq(&rng_seq_values);

        ready_to_act(actor.get(), ACT_RANDOM);
        actor->set_foe(nullptr);
        ASSERT_FALSE(actor->act())
            << "living::act's act_random() arm breaks out of the switch and returns 0";
        ASSERT_EQ(nullptr, actor->foe())
            << "find_near_foe has nothing to acquire in an emptied level";
        const QueuedCommand wander = queued_command(actor.get());
        ASSERT_EQ(COMMAND_WALK, wander.type)
            << "act_random's no-foe arm queues COMMAND_RANDOM_WALK";
        ASSERT_EQ(40, wander.count)
            << "with act_random's 40-tick wander budget, not living::act's own 20";
    }

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, foe) << "foe created";
    foe->set_team_num(2);
    foe->setxy(128, 96);
    actor->set_team_num(1);
    actor->set_lineofsight(50);

    {
        // Same two rolls, but now a hostile soldier stands 32px to the east and
        // the orc carries BIT_NO_RANGED: act_random acquires it, fire_check
        // denies the shot, and the orc turns one clockwise step toward it and
        // queues its 200-tick search.
        SequenceRandom rng_seq_values({1, 0, 0});
        ScopedSimRandom rng_seq(&rng_seq_values);

        ready_to_act(actor.get(), ACT_RANDOM, FACE_UP);
        actor->set_foe(nullptr);
        ASSERT_FALSE(actor->act())
            << "living::act's act_random() arm breaks out of the switch and returns 0";
        ASSERT_EQ(foe, actor->foe())
            << "act_random acquires the only near foe in the level";
        ASSERT_EQ(FACE_UP_RIGHT, static_cast<int>(actor->curdir()))
            << "the blocked shot turns one clockwise step from FACE_UP toward the foe at +x";
        const QueuedCommand search = queued_command(actor.get());
        ASSERT_EQ(COMMAND_SEARCH, search.type)
            << "the blocked-shot arm falls through to act_random's COMMAND_SEARCH";
        ASSERT_EQ(200, search.count)
            << "act_random's search budget is 200 ticks";
        ASSERT_EQ(ANI_WALK, static_cast<int>(actor->ani_type()))
            << "a denied shot must not start the attack animation";
    }

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCoreMore, walker_act_generate_zero_vector_and_hp_cap_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* gen = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(gen != nullptr) << "generator created";
    if (!gen)
        return;

    gen->stats()->set_level(20);
    gen->stats()->set_max_hitpoints(10);
    gen->stats()->set_hitpoints(10);
    gen->set_default_weapon(FAMILY_ELF);
    gen->set_current_weapon(gen->default_weapon());

    ASSERT_TRUE(current_game != nullptr && current_game->world != nullptr) << "current_game world context must be active";
    if (!(current_game && current_game->world))
        return;
    // Seed chosen so act_generate() deterministic SimRandom hits:
    // next(60) > next(300), then next(3)==1 and next(3)==1 (zero vector fallback).
    current_game->world->rng_.state_ = 18;

    gen->set_act_type(ACT_GENERATE);
    const float busy_before = gen->busy();
    ASSERT_FALSE(gen->act())
        << "walker::act's ACT_GENERATE arm breaks out of the switch and returns 0";
    ASSERT_FLOAT_EQ(1.0f, gen->lastx())
        << "act_generate forces lastx=1 when the random step vector is zero";
    ASSERT_FLOAT_EQ(0.0f, gen->lasty())
        << "and leaves lasty at the zero the RNG rolled";
    // The spawn consequence: init_fire(1, 0) aims the post east and starts its
    // attack animation, which is what releases the spawn on a later tick.
    ASSERT_EQ(FACE_RIGHT, static_cast<int>(gen->enddir()))
        << "init_fire turns the generator onto the heading act_generate rolled";
    ASSERT_EQ(ANI_ATTACK, static_cast<int>(gen->ani_type()))
        << "init_fire starts the generator's attack animation";
    ASSERT_FLOAT_EQ(busy_before + gen->fire_frequency(), gen->busy())
        << "init_fire charges the full fire_frequency cooldown";
    ASSERT_FLOAT_EQ(10.0f, gen->stats()->hitpoints())
        << "the per-spawn +1 regen is clamped back to max_hitpoints (10)";
    ASSERT_FLOAT_EQ(10.0f, gen->stats()->max_hitpoints())
        << "and the cap itself is untouched";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCoreMore, walker_act_guard_else_and_act_random_turn_walk_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    auto actor = make_living(FAMILY_ORC, 1, 4);
    auto foe = make_living(FAMILY_SOLDIER, 2, 4);
    ASSERT_TRUE(actor != nullptr && foe != nullptr) << "walkers created";
    if (!(actor && foe))
        return;

    actor->setxy(96, 96);
    foe->setxy(128, 96);

    // No nearby foe case: hit act_guard() else return path.
    actor->set_foe(nullptr);
    og::runtime::current_session->myscreen_->world().delete_objects();
    actor->set_act_type(ACT_GUARD);
    ASSERT_TRUE(!actor->act()) << "ACT_GUARD should return false when no foe is found";

    // Recreate context and drive act_random() through fire_check-false turn + walkstep path.
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();
    actor = make_living(FAMILY_ORC, 1, 4);
    foe = make_living(FAMILY_SOLDIER, 2, 4);
    ASSERT_TRUE(actor != nullptr && foe != nullptr) << "walkers recreated";
    if (!(actor && foe))
        return;

    actor->setxy(96, 96);
    foe->setxy(128, 96);
    actor->set_foe(foe.get());
    actor->set_lineofsight(30);
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1); // forces fire_check() false branch

    // 1-in-5 special roll misses, 1-in-5 act_random() roll hits, then
    // act_random's next(80) is non-zero so the preset foe is kept.
    SequenceRandom rng_seq_values({1, 0, 7});
    ScopedSimRandom rng_seq(&rng_seq_values);

    ready_to_act(actor.get(), ACT_RANDOM, FACE_UP);
    actor->set_foe(foe.get());
    ASSERT_FALSE(actor->act())
        << "living::act's act_random() arm breaks out of the switch and returns 0";
    ASSERT_EQ(foe.get(), actor->foe()) << "the in-range foe is kept, not dropped";
    ASSERT_EQ(FACE_UP_RIGHT, static_cast<int>(actor->curdir()))
        << "BIT_NO_RANGED denies the shot, so act_random turns one clockwise "
           "step from FACE_UP toward the foe at +x";
    const QueuedCommand search = queued_command(actor.get());
    ASSERT_EQ(COMMAND_SEARCH, search.type)
        << "the blocked-shot arm falls through to act_random's COMMAND_SEARCH";
    ASSERT_EQ(200, search.count) << "act_random's search budget is 200 ticks";
    ASSERT_EQ(ANI_WALK, static_cast<int>(actor->ani_type()))
        << "a denied shot must not start the attack animation";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCoreMore, walker_query_next_to_and_generator_fire_check_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* blocker = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(actor != nullptr && blocker != nullptr) << "walkers created";
    if (!(actor && blocker))
        return;

    actor->setxy(100, 100);
    actor->set_sizex(12);
    actor->set_sizey(12);
    actor->set_lastx(1);
    actor->set_lasty(0);
    blocker->setxy(static_cast<short>(actor->xpos() + actor->sizex() - 1),
                   static_cast<short>(actor->ypos() - actor->sizey()));
    blocker->set_sizex(12);
    blocker->set_sizey(12);
    ASSERT_TRUE(actor->query_next_to()) << "query_next_to should detect nearby blocking object to the right";

    actor->set_lastx(-1);
    actor->set_lasty(-1);
    blocker->setxy(10, 10); // clear proximity
    ASSERT_TRUE(!actor->query_next_to()) << "query_next_to should return false when next tile is passable";

    walker* gen = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(gen != nullptr) << "generator created";
    if (gen)
    {
        ASSERT_TRUE(gen->fire_check(1, 0)) << "generator fire_check should always succeed";
    }

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCoreMore, walker_init_fire_turn_busy_and_fire_fallback_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    auto w_up = make_living(FAMILY_SOLDIER, 0, 3);
    ASSERT_TRUE(w_up != nullptr) << "walker created";
    if (!w_up)
        return;
    walker* w = w_up.get();
    w->setxy(160, 160);
    w->set_lastx(1);
    w->set_lasty(0);

    // ACT_CONTROL + direction mismatch should reject init_fire.
    w->set_curdir(FACE_LEFT);
    w->set_enddir(FACE_LEFT);
    w->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(!w->init_fire(1, 0)) << "ACT_CONTROL should reject firing when turn is required";

    // Non-control mismatch should take the turn() path.
    w->set_act_type(ACT_RANDOM);
    w->set_curdir(FACE_LEFT);
    w->set_enddir(FACE_LEFT);
    ASSERT_TRUE(w->init_fire(1, 0)) << "non-control should allow init_fire to turn first";

    // Busy gate should block firing.
    w->set_busy(1);
    w->set_curdir(FACE_RIGHT);
    w->set_enddir(FACE_RIGHT);
    ASSERT_TRUE(!w->init_fire(1, 0)) << "busy walkers should not init_fire";

    // ANI_WALK branch should transition into attack animation.
    w->set_busy(0);
    w->set_ani_type(ANI_WALK);
    ASSERT_TRUE(w->init_fire(1, 0)) << "ANI_WALK branch should succeed and start attack animation";
    ASSERT_EQ(ANI_ATTACK, (int)w->ani_type()) << "ANI_WALK firing should switch to ANI_ATTACK";

    // Non-walk path delegates to fire(); force fire() to fail via magic cost check.
    w->set_ani_type(ANI_ATTACK);
    w->stats()->set_magicpoints(0.0f);
    w->stats()->set_weapon_cost(10.0f);
    ASSERT_TRUE(!w->init_fire(1, 0)) << "non-walk init_fire should return false when fire() fails";
}


TEST(WalkerCoreMore, walker_round5_act_switch_random_and_fire_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(actor != nullptr && foe != nullptr) << "actor and foe should be created";
    if (!(actor && foe))
        return;

    actor->set_team_num(1);
    actor->setxy(96, 96);
    actor->set_ani_type(ANI_WALK);
    actor->stats()->clear_command();

    foe->set_team_num(2);
    foe->setxy(128, 96);

    // ACT_GUARD no-foe path: break from switch then return 0.
    actor->set_foe(nullptr);
    actor->set_act_type(ACT_GUARD);
    ASSERT_TRUE(!actor->act()) << "ACT_GUARD should return false when no nearby foe exists";

    // ACT_FIRE dispatch path from the act() switch.
    actor->set_act_type(ACT_FIRE);
    actor->set_lineofsight(2);
    actor->set_lastx(0);
    actor->set_lasty(0);
    ASSERT_TRUE(actor->act()) << "ACT_FIRE should dispatch and return true";

    // The actor is an Order::Living orc, so act() dispatches to living::act(),
    // whose ACT_RANDOM arm is a 1/5 + 1/5 rule drawn straight from the WORLD
    // rng -- not walker::act()'s 1-in-4-then-1-in-20 walk rule, which this
    // orc never executes. Both arms below are pinned, because an unpinned
    // draw is whatever a shuffled predecessor left behind.
    //
    // 16-in-25: neither next(5) is zero, so the actor acquires a near foe,
    // queues COMMAND_SEARCH and returns 1.
    og::runtime::current_session->myscreen_->world().rng_.state_ = 1;  // next(5): 3 then 1
    actor->stats()->clear_command();
    actor->set_ani_type(ANI_WALK);
    actor->set_foe(nullptr);
    actor->set_act_type(ACT_RANDOM);
    ASSERT_TRUE(actor->act()) << "ACT_RANDOM acquire/search arm should return true";

    // 4-in-25: the first next(5) is non-zero and the second is zero, so
    // living::act() calls act_random() and breaks out of the switch, which
    // falls through to `return 0`.
    og::runtime::current_session->myscreen_->world().rng_.state_ = 6;  // next(5): 4 then 0
    actor->stats()->clear_command();
    actor->set_ani_type(ANI_WALK);
    actor->set_foe(nullptr);
    actor->set_act_type(ACT_RANDOM);
    ASSERT_FALSE(actor->act()) << "ACT_RANDOM act_random() arm should return false";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


// living::act_random's three arms, each reached with a written RNG script and
// each read back through the command it queues:
//   no foe          -> COMMAND_RANDOM_WALK (expanded to COMMAND_WALK), 40 ticks
//   blocked shot    -> turn one step toward the foe + COMMAND_SEARCH, 200 ticks
//   clear shot      -> init_fire + COMMAND_FIRE carrying the foe delta
TEST(WalkerCoreMore, walker_round5_act_random_arms_queue_their_own_command)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();
    world.delete_objects();

    // No-foe arm: an empty level, so find_near_foe fails.
    walker* actor = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, actor) << "actor created";
    actor->set_team_num(1);
    actor->setxy(96, 96);
    actor->set_lineofsight(20);

    {
        SequenceRandom rng_no_foe_values({1, 0, 0});
        ScopedSimRandom rng_no_foe(&rng_no_foe_values);
        ready_to_act(actor, ACT_RANDOM);
        actor->set_foe(nullptr);
        ASSERT_FALSE(actor->act())
            << "living::act's act_random() arm breaks out of the switch and returns 0";
        ASSERT_EQ(nullptr, actor->foe()) << "no foe exists to acquire";
        const QueuedCommand wander = queued_command(actor);
        ASSERT_EQ(COMMAND_WALK, wander.type)
            << "the no-foe arm queues COMMAND_RANDOM_WALK";
        ASSERT_EQ(40, wander.count) << "act_random's wander budget is 40 ticks";
    }

    // Rebuild the actor/foe pair for the in-range arms.
    world.delete_objects();
    actor = world.add_ob(Order::Living, FAMILY_ORC);
    walker* foe = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor) << "actor recreated";
    ASSERT_NE(nullptr, foe) << "foe recreated";

    actor->set_team_num(1);
    actor->setxy(96, 96);
    actor->set_lineofsight(20);
    actor->stats()->set_magicpoints(9999.0f);
    actor->stats()->set_weapon_cost(0.0f);

    foe->set_team_num(2);
    foe->setxy(112, 96);

    // Blocked-shot arm: BIT_NO_RANGED denies fire_check, so the orc only turns.
    {
        actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        SequenceRandom rng_turn_values({1, 0, 7});
        ScopedSimRandom rng_turn(&rng_turn_values);
        ready_to_act(actor, ACT_RANDOM, FACE_UP);
        actor->set_foe(foe);
        ASSERT_FALSE(actor->act())
            << "living::act's act_random() arm breaks out of the switch and returns 0";
        ASSERT_EQ(FACE_UP_RIGHT, static_cast<int>(actor->curdir()))
            << "one clockwise step from FACE_UP toward the foe at +x";
        const QueuedCommand search = queued_command(actor);
        ASSERT_EQ(COMMAND_SEARCH, search.type)
            << "a denied shot falls through to COMMAND_SEARCH";
        ASSERT_EQ(200, search.count) << "act_random's search budget is 200 ticks";
        ASSERT_EQ(ANI_WALK, static_cast<int>(actor->ani_type()))
            << "a denied shot must not start the attack animation";
    }

    // Clear-shot arm: ranged allowed, already facing the foe, mana to spare.
    {
        actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
        ready_to_act(actor, ACT_RANDOM, FACE_RIGHT);
        actor->set_foe(foe);
        // Positive control: the shot really is available from this state, so a
        // COMMAND_FIRE failure below means act_random stopped issuing it.
        walker::FireCheckDenial denial = walker::FireCheckDenial::None;
        ASSERT_TRUE(actor->fire_check(16, 0, &denial))
            << "fire_check must pass from this setup or the arm is unreachable "
               "(denial stage " << static_cast<int>(denial) << ")";

        SequenceRandom rng_fire_values({1, 0, 7});
        ScopedSimRandom rng_fire(&rng_fire_values);
        ready_to_act(actor, ACT_RANDOM, FACE_RIGHT);
        actor->set_foe(foe);
        ASSERT_FALSE(actor->act())
            << "living::act's act_random() arm breaks out of the switch and returns 0";
        const QueuedCommand fire = queued_command(actor);
        ASSERT_EQ(COMMAND_FIRE, fire.type)
            << "a clear shot queues COMMAND_FIRE at the front of the queue";
        ASSERT_EQ(16, fire.com1) << "COMMAND_FIRE carries the foe's x delta";
        ASSERT_EQ(0, fire.com2) << "COMMAND_FIRE carries the foe's y delta";
        ASSERT_EQ(ANI_ATTACK, static_cast<int>(actor->ani_type()))
            << "init_fire starts the attack animation";
    }

    world.delete_objects();
}


TEST(WalkerCoreMore, walker_round6_init_fire_animate_and_misc_guards)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker should be created";
    if (!w)
        return;

    // next_frame path (smoke coverage without touching protected state).
    const short before = w->frame();
    ASSERT_EQ(1, w->next_frame()) << "next_frame should report a successful frame set";
    ASSERT_EQ(before, w->frame()) << "next_frame should keep the wrapped frame selected";

    // A default/test-built walker has frames==0; next_frame() must guard the
    // modulo and return 0 rather than dividing by zero (UB).
    walker zero_frames;
    ASSERT_EQ(0, zero_frames.next_frame()) << "frames==0 must return 0, not divide by zero";

    // move_myguy_to nullptr guard.
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    w->move_myguy_to(nullptr);
    ASSERT_TRUE(w->myguy != nullptr) << "move_myguy_to(nullptr) should keep myguy unchanged";

    // init_fire: ACT_CONTROL early-return branch.
    w->set_curdir(FACE_UP);
    w->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(!w->init_fire(1, 0)) << "init_fire should return false for control walker needing turn";

    // init_fire: busy early-return branch.
    w->set_act_type(ACT_RANDOM);
    w->set_curdir(FACE_RIGHT);
    w->set_busy(1);
    ASSERT_TRUE(!w->init_fire(1, 0)) << "init_fire should return false when busy";

    // animate null-ani guard using headless default ctor.
    walker headless;
    ASSERT_TRUE(!headless.animate()) << "animate should return false when ani is null";

    // ACT() pointer cleanup and recoil/lunge clamping.
    walker* dead_target = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_TRUE(dead_target != nullptr) << "dead target should be created";
    if (dead_target)
    {
        dead_target->set_dead(1);
        w->set_attack_lunge(0.2f);
        w->set_hit_recoil(0.2f);
        w->set_ani_type(ANI_WALK);
        w->set_act_type(ACT_CONTROL);

        w->set_foe(dead_target);
        w->set_leader(nullptr);
        w->set_owner(nullptr);
        (void)w->act();
        ASSERT_TRUE(w->foe() == nullptr) << "act should clear dead foe pointer";

        w->set_foe(nullptr);
        w->set_leader(dead_target);
        w->set_owner(nullptr);
        w->set_ani_type(ANI_WALK);
        (void)w->act();
        ASSERT_TRUE(w->leader() == nullptr) << "act should clear dead leader pointer";

        ASSERT_TRUE(w->attack_lunge() == 0.0f && w->hit_recoil() == 0.0f) << "act should clamp lunge/recoil to zero";
    }

    // animate ANI_TELE_OUT default no-handler branch.
    w->set_ani_type(ANI_TELE_OUT);
    w->set_cycle(120);
    w->set_curdir(FACE_DOWN);
    ASSERT_TRUE(!w->animate()) << "ANI_TELE_OUT without handler should return false after reset";
}


TEST(WalkerCoreMore, walker_animate_rejects_ani_type_beyond_family_table)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    // A real loader-built walker records its family's animation-table length in
    // ani_count. FAMILY_SOLDIER uses a short table; a snapshot-forced high
    // ani_type would index past it, so animate() must reject it (reset to walk,
    // return false) rather than read out of bounds.
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr);
    if (!w)
        return;
    ASSERT_TRUE(w->ani != nullptr) << "real walker has an animation table";
    ASSERT_GT(w->ani_count, 0) << "loader records the table length";

    if (FACE_DOWN + ANI_SLIME_SPLIT * NUM_FACINGS >= w->ani_count)
    {
        w->set_ani_type(static_cast<char>(ANI_SLIME_SPLIT));
        w->set_curdir(static_cast<char>(FACE_DOWN));
        w->set_cycle(0);
        ASSERT_TRUE(!w->animate()) << "ani_type beyond the family table must be rejected";
        ASSERT_EQ(ANI_WALK, (int)w->ani_type()) << "rejected animate resets to walk";
    }

    // ani_type beyond the global ANI range must be normalized to walk.
    w->set_ani_type(static_cast<char>(99));
    w->set_curdir(static_cast<char>(FACE_DOWN));
    w->set_cycle(0);
    (void)w->animate();
    ASSERT_EQ(ANI_WALK, (int)w->ani_type()) << "out-of-range ani_type normalizes to walk";

    // Out-of-range facing/cycle must also be handled without an out-of-bounds read.
    w->set_ani_type(static_cast<char>(ANI_WALK));
    w->set_curdir(static_cast<char>(100));
    w->set_cycle(static_cast<signed char>(120));
    (void)w->animate(); // must not crash / read OOB (verified under sanitizers)

    // A NEGATIVE cycle on a valid animation clamps to the start of the
    // sequence, not to the frame before it: `cycle` is a signed char that
    // walk() can wrap and that a save or a snapshot can carry, and reading
    // seq[-5] is an out-of-bounds read whose frame lands somewhere else
    // entirely on screen.
    w->set_ani_type(static_cast<char>(ANI_WALK));
    w->set_curdir(static_cast<char>(FACE_DOWN));
    w->set_cycle(0);
    (void)w->animate();
    const short frame_from_zero = w->frame();
    const int cycle_from_zero = static_cast<int>(w->cycle());

    w->set_ani_type(static_cast<char>(ANI_WALK));
    w->set_curdir(static_cast<char>(FACE_DOWN));
    w->set_cycle(static_cast<signed char>(-5));
    (void)w->animate();
    ASSERT_EQ(cycle_from_zero, static_cast<int>(w->cycle()))
        << "a negative cycle must animate exactly as cycle 0 does";
    ASSERT_EQ(frame_from_zero, w->frame())
        << "the clamp lands on the sequence's first frame";
}


TEST(WalkerCoreMore, walker_round6_fire_and_friendliness_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* target = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(actor && target) << "actor/target should be created";
    if (!(actor && target))
        return;

    actor->setxy(64, 64);
    actor->set_lastx(1);
    actor->set_lasty(0);
    actor->stats()->set_magicpoints(0.0f);
    actor->stats()->set_weapon_cost(5.0f);
    ASSERT_TRUE(actor->fire() == nullptr) << "fire should fail when magicpoints are insufficient";

    actor->stats()->set_magicpoints(999.0f);
    actor->stats()->set_weapon_cost(0.0f);
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    ASSERT_TRUE(actor->fire() == nullptr) << "fire should return null for BIT_NO_RANGED";
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);

    // fire_check special guards.
    actor->set_order_family(Order::Generator, FAMILY_TOWER);
    ASSERT_TRUE(actor->fire_check(1, 0)) << "fire_check should always succeed for generators";
    actor->set_order_family(Order::Living, FAMILY_SOLDIER);

    actor->set_foe(nullptr);
    ASSERT_TRUE(!actor->fire_check(1, 0)) << "fire_check should fail when no foe is selected";

    actor->set_foe(target);
    target->setxy(actor->xpos() + 4, actor->ypos() + 40);
    actor->set_curdir(FACE_RIGHT);
    ASSERT_TRUE(!actor->fire_check(0, 1)) << "fire_check should fail on targetdir mismatch";

    // create_weapon default switch branch (diagonal facing).
    actor->set_lastx(1);
    actor->set_lasty(1);
    walker* diagonal_weapon = actor->create_weapon();
    ASSERT_TRUE(diagonal_weapon != nullptr) << "create_weapon should succeed for living actor";

    // Company ownership must not override the team-color alliance rule.
    GameWorld& world = og::runtime::current_session->myscreen_->world_;
    world.allied_mode = 1;
    actor->set_team_num(0);
    target->set_team_num(2);
    actor->clear_myguy();
    target->set_owned_myguy(std::make_unique<guy>(FAMILY_ORC));
    ASSERT_EQ(0, actor->is_friendly(target))
        << "different teams stay hostile even when one side is company-owned";

    actor->set_dead(1);
    ASSERT_TRUE(actor->is_friendly_to_team(2) == 0) << "dead walker should not be friendly to any team";
    actor->set_dead(0);
    actor->clear_myguy();
    ASSERT_TRUE(actor->is_friendly_to_team(actor->team_num()) != 0) << "no-myguy walker should be friendly only to matching team";
}


// walker::act_guard() with a foe in sight: face_delta pins curdir/enddir/lastx
// at the foe, the guard WAKES to ACT_RANDOM unless it holds its post, and the
// parting COMMAND_FIRE carries the foe delta (the directional-guard-fire fix).
TEST(WalkerCoreMore, walker_round6_act_guard_faces_wakes_and_fires_directionally)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();
    world.delete_objects();

    walker* actor = world.add_ob(Order::Living, FAMILY_ORC);
    walker* foe = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor) << "actor created";
    ASSERT_NE(nullptr, foe) << "foe created";

    actor->set_team_num(1);
    actor->setxy(96, 96);
    actor->set_lineofsight(20);
    actor->stats()->set_magicpoints(9999.0f);
    actor->stats()->set_weapon_cost(0.0f);

    foe->set_team_num(2);
    foe->setxy(112, 96);

    // A posted guard facing away sights the foe 16px east of it.
    {
        SequenceRandom guard_rng_values({7});
        ScopedSimRandom guard_rng(&guard_rng_values);
        ready_to_act(actor, ACT_GUARD, FACE_UP);
        actor->set_guard_hold_post(false);
        actor->set_foe(nullptr);
        ASSERT_FALSE(actor->act())
            << "living::act's ACT_GUARD arm breaks out of the switch and returns 0";
        ASSERT_EQ(foe, actor->foe()) << "act_guard acquires the near foe";
        ASSERT_EQ(FACE_RIGHT, static_cast<int>(actor->curdir()))
            << "face_delta snaps curdir at the foe";
        ASSERT_EQ(FACE_RIGHT, static_cast<int>(actor->enddir()))
            << "face_delta snaps enddir too, so the pivot is not undone next tick";
        ASSERT_FLOAT_EQ(16.0f * actor->stepsize(), actor->lastx())
            << "face_delta writes the foe's x delta SCALED by stepsize as the "
               "firing heading -- a heading at the wrong scale aims the shot wrong";
        ASSERT_FLOAT_EQ(0.0f, actor->lasty())
            << "the foe is due east, so the firing heading carries no y";
        ASSERT_EQ(ACT_RANDOM, static_cast<int>(actor->act_type()))
            << "a genuine sighting wakes the guard out of ACT_GUARD";
        const QueuedCommand fire = queued_command(actor);
        ASSERT_EQ(COMMAND_FIRE, fire.type) << "act_guard queues its parting shot";
        ASSERT_EQ(16, fire.com1) << "the parting COMMAND_FIRE carries the foe's x delta";
        ASSERT_EQ(0, fire.com2) << "the parting COMMAND_FIRE carries the foe's y delta";
    }

    // Hold-post guard: same sighting, same facing turn, but it never wakes.
    {
        SequenceRandom guard_rng_values({7});
        ScopedSimRandom guard_rng(&guard_rng_values);
        ready_to_act(actor, ACT_GUARD, FACE_UP);
        actor->set_guard_hold_post(true);
        actor->set_foe(nullptr);
        ASSERT_FALSE(actor->act())
            << "living::act's ACT_GUARD arm breaks out of the switch and returns 0";
        ASSERT_EQ(FACE_RIGHT, static_cast<int>(actor->curdir()))
            << "a hold-post guard still turns toward the foe";
        ASSERT_EQ(ACT_GUARD, static_cast<int>(actor->act_type()))
            << "npc_flags bit 1 keeps the classic stationary sentry in ACT_GUARD";
        ASSERT_EQ(COMMAND_FIRE, queued_command(actor).type)
            << "a hold-post guard still defends its post";
        actor->set_guard_hold_post(false);
    }

    // act_random() blocked-ranged arm: fire_check denied -> turn only.
    {
        actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        SequenceRandom blocked_rng_values({1, 0, 7});
        ScopedSimRandom blocked_rng(&blocked_rng_values);
        ready_to_act(actor, ACT_RANDOM, FACE_UP);
        actor->set_foe(foe);
        ASSERT_FALSE(actor->act())
            << "living::act's act_random() arm breaks out of the switch and returns 0";
        ASSERT_EQ(FACE_UP_RIGHT, static_cast<int>(actor->curdir()))
            << "one clockwise step from FACE_UP toward the foe at +x";
        ASSERT_EQ(COMMAND_SEARCH, queued_command(actor).type)
            << "a denied shot falls through to COMMAND_SEARCH";
        ASSERT_EQ(ANI_WALK, static_cast<int>(actor->ani_type()))
            << "a denied shot must not start the attack animation";
    }

    // act_random() clear-shot arm: init_fire + COMMAND_FIRE.
    {
        actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
        SequenceRandom fire_rng_values({1, 0, 7});
        ScopedSimRandom fire_rng(&fire_rng_values);
        ready_to_act(actor, ACT_RANDOM, FACE_RIGHT);
        actor->set_foe(foe);
        ASSERT_FALSE(actor->act())
            << "living::act's act_random() arm breaks out of the switch and returns 0";
        const QueuedCommand fire = queued_command(actor);
        ASSERT_EQ(COMMAND_FIRE, fire.type) << "a clear shot queues COMMAND_FIRE";
        ASSERT_EQ(16, fire.com1) << "COMMAND_FIRE carries the foe's x delta";
        ASSERT_EQ(ANI_ATTACK, static_cast<int>(actor->ani_type()))
            << "init_fire starts the attack animation";
    }

    world.delete_objects();
}


TEST(WalkerCoreMore, walker_round7a_compute_outline_and_friendliness_edge_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* viewer = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* subject = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(viewer && subject) << "viewer/subject created";
    if (!(viewer && subject))
        return;

    viewer->set_team_num(0);
    subject->set_team_num(1);
    subject->stats()->set_bit_flags(BIT_NAMED, 1);

    // Invulnerable + flying: flight wins inside the invulnerable arm.
    subject->set_outline(OUTLINE_INVULNERABLE);
    subject->set_flight_left(3);
    subject->compute_outline(viewer);
    ASSERT_EQ(static_cast<int>(OUTLINE_FLYING), static_cast<int>(subject->outline()))
        << "invulnerable + flying shows the flight outline";

    // Invulnerable + invisible, but NAMED and hostile to the viewer: the boss
    // marker beats the cloak, so the enemy viewer can still see what it fights.
    subject->set_outline(OUTLINE_INVULNERABLE);
    subject->set_flight_left(0);
    subject->set_invisibility_left(3);
    subject->compute_outline(viewer);
    ASSERT_EQ(static_cast<int>(OUTLINE_NAMED), static_cast<int>(subject->outline()))
        << "a named enemy's outline is not hidden by invisibility";

    // Flying + invulnerable, still NAMED and hostile: same rule.
    subject->set_outline(OUTLINE_FLYING);
    subject->set_invisibility_left(0);
    subject->set_invulnerable_left(3);
    subject->compute_outline(viewer);
    ASSERT_EQ(static_cast<int>(OUTLINE_NAMED), static_cast<int>(subject->outline()))
        << "the named-enemy marker wins over the potion outline too";

    // From the NAMED outline, invisibility drops it to the team colour.
    subject->set_outline(OUTLINE_NAMED);
    subject->set_invisibility_left(3);
    subject->set_invulnerable_left(0);
    subject->set_flight_left(0);
    subject->compute_outline(viewer);
    ASSERT_EQ(static_cast<int>(subject->query_team_color()),
              static_cast<int>(subject->outline()))
        << "a cloaked walker already outlined NAMED falls back to its team colour";

    // No special flags at all: the else arm clears the outline outright.
    subject->set_outline(OUTLINE_NAMED);
    subject->set_invisibility_left(0);
    subject->set_invulnerable_left(0);
    subject->set_flight_left(0);
    subject->stats()->set_bit_flags(BIT_NAMED, 0);
    subject->compute_outline(viewer);
    ASSERT_EQ(0, static_cast<int>(subject->outline()))
        << "no name, no potion, no cloak: compute_outline clears the outline to 0";

    // is_friendly null/dead guards and owner-chain branches.
    GameWorld& world = og::runtime::current_session->myscreen_->world_;
    world.allied_mode = 1;

    ASSERT_EQ(0, (int)subject->is_friendly(nullptr)) << "is_friendly should reject null";

    subject->set_dead(1);
    ASSERT_EQ(0, (int)subject->is_friendly(viewer)) << "is_friendly should reject dead self";
    subject->set_dead(0);

    viewer->set_dead(1);
    ASSERT_EQ(0, (int)subject->is_friendly(viewer)) << "is_friendly should reject dead target";
    viewer->set_dead(0);

    // Owner-loop traversal still resolves friendliness from the roots' teams.
    walker* owner = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_MAGE);
    ASSERT_TRUE(owner != nullptr) << "owner created";
    if (owner)
    {
        owner->set_team_num(0);
        owner->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
        subject->set_owner(owner);
        viewer->set_owner(nullptr);
        viewer->set_team_num(0);
        viewer->clear_myguy();
        ASSERT_TRUE(subject->is_friendly(viewer) != 0)
            << "same-team owner roots must be friendly";
    }
}

// The team-color "player control" outline marks same-team OTHER human-controlled
// peers. It is gated on compute_outline's mark_player_controls flag: networked
// play opts in; local split-screen does NOT (each co-player has their own pane,
// and leaving the marker on flickered each player's own character because the
// net layer rewrites the render-only `outline` field every sim tick).
TEST(WalkerCoreMore, compute_outline_player_control_marker_is_gated)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();
    world.delete_objects();

    walker* viewer = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* peer = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(viewer && peer) << "viewer/peer created";
    if (!(viewer && peer))
        return;

    viewer->set_team_num(0);
    peer->set_team_num(0);   // same team as the viewer
    peer->set_user(1);       // human-controlled peer (user >= 0)
    peer->stats()->set_bit_flags(BIT_NAMED, 0);
    peer->set_invisibility_left(0);
    peer->set_flight_left(0);
    peer->set_invulnerable_left(0);

    // Local split-screen: marker suppressed, so it cannot flicker.
    peer->set_outline(0);
    peer->compute_outline(viewer, /*mark_player_controls=*/false);
    EXPECT_EQ(0, static_cast<int>(peer->outline()))
        << "local split-screen must not mark same-team player-controlled peers";

    // Networked play: marker applied.
    peer->set_outline(0);
    peer->compute_outline(viewer, /*mark_player_controls=*/true);
    EXPECT_EQ(static_cast<int>(peer->query_team_color()),
              static_cast<int>(peer->outline()))
        << "networked play marks same-team player-controlled peers";

    // The viewer's own character is never self-marked, even when networked.
    viewer->set_user(0);
    viewer->set_outline(0);
    viewer->compute_outline(viewer, /*mark_player_controls=*/true);
    EXPECT_EQ(0, static_cast<int>(viewer->outline()))
        << "a player's own character is not self-marked";

    world.delete_objects();
}


TEST(WalkerCoreMore, walker_round7a_death_guard_and_friendliness_team_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    // death_called guard.
    w->set_dead(1);
    w->set_death_called(0);
    ASSERT_TRUE(w->death()) << "first death call should run";
    ASSERT_EQ(0, (int)w->death()) << "second death call should hit death_called guard";

    // is_friendly_to_team is strict team equality for every ownership/mode.
    GameWorld& world = og::runtime::current_session->myscreen_->world_;
    w->set_dead(0);
    w->set_team_num(2);
    w->clear_myguy();

    world.allied_mode = 0;
    ASSERT_EQ(1, (int)w->is_friendly_to_team(2)) << "enemy mode should only match own team";
    ASSERT_EQ(0, (int)w->is_friendly_to_team(0)) << "enemy mode should reject other teams";

    world.allied_mode = 1;
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    for (unsigned char team = 0; team < 4; ++team)
        ASSERT_EQ(team == 2, w->is_friendly_to_team(team) != 0)
            << "company ownership must not override roster color "
            << static_cast<int>(team);
    w->clear_myguy();
    w->set_team_num(0);
    for (unsigned char team = 0; team < 4; ++team)
        ASSERT_EQ(team == 0, w->is_friendly_to_team(team) != 0)
            << "authored units must use their own color against team "
            << static_cast<int>(team);

    // Two scenario-owned walkers still use strict team equality.
    walker* other = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(other != nullptr) << "other created";
    if (other)
    {
        w->set_team_num(2);
        other->set_team_num(2);
        other->clear_myguy();
        w->clear_myguy();
        world.allied_mode = 1;
        ASSERT_EQ(1, (int)w->is_friendly(other)) << "both without myguy should compare teams only";
        other->set_team_num(1);
        ASSERT_EQ(0, (int)w->is_friendly(other)) << "both without myguy different teams should be unfriendly";
    }
}


TEST(WalkerCoreMore, walker_round7b_base_act_guard_random_and_death_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TENT);
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(actor != nullptr && foe != nullptr) << "actor and foe created";
    if (!(actor && foe))
        return;

    actor->set_team_num(1);
    actor->setxy(96, 96);
    actor->set_lineofsight(2);
    foe->set_team_num(2);
    foe->setxy(128, 128);

    // Base walker::act_guard() no-foe return branch.
    og::runtime::current_session->myscreen_->world().delete_objects();
    actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(actor != nullptr) << "actor recreated";
    if (!actor)
        return;
    actor->set_team_num(1);
    actor->setxy(96, 96);
    actor->set_act_type(ACT_GUARD);
    ASSERT_TRUE(!actor->act()) << "base ACT_GUARD should return false when no foe is found";

    // Base walker::act_random() in-range fire path. next(4)==0 then next(20)!=0
    // routes walker::act() into act_random(); next(70) is then non-zero so the
    // preset foe survives.
    foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, foe) << "foe recreated";
    foe->set_team_num(2);
    foe->setxy(112, 96);
    actor->set_lineofsight(40);
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    {
        SequenceRandom rng_fire_values({0, 1, 7});
        ScopedSimRandom rng_fire(&rng_fire_values);
        ready_to_act(actor, ACT_RANDOM, FACE_UP);
        actor->set_foe(foe);
        ASSERT_FALSE(actor->act())
            << "walker::act's act_random() arm breaks out of the switch and returns 0";
        const QueuedCommand fire = queued_command(actor);
        ASSERT_EQ(COMMAND_FIRE, fire.type)
            << "an in-range foe makes act_random queue COMMAND_FIRE";
        ASSERT_EQ(16, fire.com1) << "COMMAND_FIRE carries the foe's x delta";
        ASSERT_EQ(0, fire.com2) << "COMMAND_FIRE carries the foe's y delta";
        ASSERT_EQ(FACE_RIGHT, static_cast<int>(actor->enddir()))
            << "init_fire aims a generator by enddir (it never turns like a living)";
        ASSERT_EQ(ANI_ATTACK, static_cast<int>(actor->ani_type()))
            << "init_fire starts the attack animation";
    }

    // BIT_NO_RANGED cannot reach act_random's turn arm here: fire_check's first
    // line lets every Order::Generator through unconditionally, so a posted
    // generator always shoots. Pin that instead of the old
    // `act_type() != ACT_FIRE` read, which act_random never writes.
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    ASSERT_TRUE(actor->fire_check(16, 0))
        << "generators always pass fire_check, even with BIT_NO_RANGED set";
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);

    // Base walker::act_random()'s 3-of-4 arm: next(4) != 0 -> far-foe search.
    {
        SequenceRandom rng_search_values({1});
        ScopedSimRandom rng_search(&rng_search_values);
        ready_to_act(actor, ACT_RANDOM, FACE_UP);
        actor->set_foe(nullptr);
        ASSERT_TRUE(actor->act())
            << "walker::act's 3-of-4 ACT_RANDOM arm returns 1 directly";
        ASSERT_EQ(foe, actor->foe()) << "the 3-of-4 arm acquires a far foe";
        const QueuedCommand search = queued_command(actor);
        ASSERT_EQ(COMMAND_SEARCH, search.type) << "the 3-of-4 arm queues a search";
        ASSERT_EQ(500, search.count)
            << "walker::act's own search budget is 500 ticks, not act_random's 200";
    }

    // Base walker::death() generator explosion and death_called guard. The
    // explosions are add_ob(Order::FX, ...), which routes to OBLIST -- reading
    // fxlist here can never see them.
    og::runtime::current_session->myscreen_->world().delete_objects();
    walker* gen = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, gen) << "generator created";
    gen->setxy(96, 96);
    gen->set_team_num(4);
    gen->stats()->set_level(6);
    gen->set_floor(2);
    gen->set_dead(1);
    gen->set_death_called(0);
    ASSERT_EQ(0, count_obs(Order::FX, FAMILY_EXPLOSION))
        << "the level starts with no explosions";
    ASSERT_TRUE(gen->death()) << "first generator death call should succeed";
    ASSERT_EQ(4, count_obs(Order::FX, FAMILY_EXPLOSION))
        << "a dying generator goes up in exactly four explosions";
    int inspected = 0;
    for (const auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
    {
        if (!uptr || uptr->order() != Order::FX ||
            static_cast<int>(uptr->family()) != FAMILY_EXPLOSION)
            continue;
        ++inspected;
        EXPECT_EQ(4, static_cast<int>(uptr->team_num()))
            << "an explosion belongs to the generator's team";
        EXPECT_EQ(ANI_EXPLODE, static_cast<int>(uptr->ani_type()))
            << "an explosion runs the explode animation";
        EXPECT_EQ(6, static_cast<int>(uptr->stats()->level()))
            << "an explosion inherits the generator's level";
        EXPECT_EQ(2, static_cast<int>(uptr->floor()))
            << "an explosion burns on the generator's floor";
        EXPECT_FLOAT_EQ(12.0f, uptr->damage())
            << "explosion damage is the generator's level doubled";
    }
    ASSERT_EQ(4, inspected) << "every one of the four explosions was inspected";
    ASSERT_EQ(0, (int)gen->death()) << "second death call should hit death_called guard";

    // Save-all early-return event branch in living death path.
    const char old_type = og::runtime::current_session->myscreen_->world().type;
    og::runtime::current_session->myscreen_->world().type = static_cast<short>(SCEN_TYPE_SAVE_ALL);
    walker* named = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(named != nullptr) << "named living created";
    if (named)
    {
        named->set_team_num(0);
        named->stats()->name = "Round7B";
        named->set_dead(1);
        named->set_death_called(0);
        ASSERT_TRUE(named->death()) << "save-all named death path should return true";
    }
    og::runtime::current_session->myscreen_->world().type = old_type;

    // FX-order death branch (log-only, returns success).
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_FLASH);
    ASSERT_TRUE(fx != nullptr) << "fx created";
    if (fx)
    {
        fx->set_dead(1);
        fx->set_death_called(0);
        ASSERT_TRUE(fx->death()) << "fx death branch should return true";
    }

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCoreMore, walker_round11_friendliness_owner_chain_and_difficulty_paths_1480_1615)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* target = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* actor_owner = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_MAGE);
    walker* actor_root = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ARCHER);
    walker* target_owner = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_DRUID);
    ASSERT_TRUE(actor && target && actor_owner && actor_root && target_owner) << "fixtures created";
    if (!(actor && target && actor_owner && actor_root && target_owner))
        return;

    GameWorld& world = og::runtime::current_session->myscreen_->world_;
    world.allied_mode = 1;

    // Owner-chain traversal: actor -> actor_owner -> actor_root -> self.
    actor_owner->set_owner(actor_root);
    actor_root->set_owner(actor_root);
    actor->set_owner(actor_owner);
    target->set_owner(target_owner);
    target_owner->set_owner(target_owner);

    actor->set_dead(0);
    target->set_dead(0);
    actor_root->set_team_num(0);
    target_owner->set_team_num(0);
    actor_root->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    target_owner->clear_myguy();
    ASSERT_TRUE(actor->is_friendly(target) != 0)
        << "same-team owner roots should be friendly regardless of ownership";

    // Both roots without myguy: seat mode must not override combat teams.
    actor_root->clear_myguy();
    target_owner->clear_myguy();
    actor_root->set_team_num(2);
    target_owner->set_team_num(2);
    ASSERT_EQ(1, (int)actor->is_friendly(target)) << "both roots without myguy and same team should be friendly";
    target_owner->set_team_num(3);
    ASSERT_EQ(0, (int)actor->is_friendly(target)) << "both roots without myguy and different team should be unfriendly";

    // is_friendly_to_team follows the owner chain but still compares color.
    actor_root->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    actor_root->set_team_num(3);
    for (unsigned char team = 0; team < 4; ++team)
        ASSERT_EQ(team == 3, actor->is_friendly_to_team(team) != 0)
            << "owner-chain unit must use its root's roster color "
            << static_cast<int>(team);

    // living::set_difficulty on a hostile-team soldier: core:soldier's Lua
    // set_difficulty hook runs og.apply_difficulty_scaling(self, level, 13, 8,
    // 5, 2), so max_hp gains 13 * level^2, then the team != 0 arm scales by the
    // difficulty percent (pinned at 100 here so the two stages stay separable).
    const short old_difficulty = world.difficulty;
    world.difficulty = 100;
    actor->set_team_num(1);
    const float hp_before = actor->stats()->max_hitpoints();
    const float mp_before = actor->stats()->max_magicpoints();
    const float dmg_before = actor->damage();
    actor->set_difficulty(4);
    ASSERT_FLOAT_EQ(hp_before + 13.0f * 16.0f, actor->stats()->max_hitpoints())
        << "level 4 adds 13 * 4^2 hitpoints at 100% difficulty";
    ASSERT_FLOAT_EQ(mp_before + 8.0f * 16.0f, actor->stats()->max_magicpoints())
        << "level 4 adds 8 * 4^2 magicpoints at 100% difficulty";
    ASSERT_FLOAT_EQ(dmg_before + 5.0f * 4.0f, actor->damage())
        << "damage scales linearly in level, not quadratically";
    ASSERT_EQ(2, (int)actor->weapons_left())
        << "the soldier hook also restocks (level + 1) / 2 weapons";
    world.difficulty = old_difficulty;
}


// living::act's ACT_RANDOM "4 of 5 times" arm, no-foe leg (living.cpp: the
// else branch after `if (foe())` / `else if (!rng.next(2))`): a searching
// living that finds nobody queues COMMAND_RANDOM_WALK with a 20-tick budget,
// which try_command translates into a COMMAND_WALK carrying a random unit
// step. Nothing else pinned this leg.
TEST(WalkerCoreMore, living_act_search_arm_without_a_foe_queues_a_random_walk)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();
    world.delete_objects();

    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor) << "actor created";
    actor->set_team_num(1);
    actor->setxy(104, 104);
    ASSERT_EQ(1u, world.oblist.size())
        << "the actor is alone: there is no foe for the search to find";

    // Every scripted draw is 1, so the arm is pinned without depending on how
    // many draws the pre-switch housekeeping makes: next(5) != 0 skips the
    // special roll, next(5) != 0 skips the act_random roll (so the 4-of-5
    // search arm runs), next(2) != 0 skips the find_far_foe retry, and
    // try_command's two next(3) unit-step rolls both yield 1 - 1 == 0.
    SequenceRandom search_rng_values({1, 1, 1, 1, 1, 1, 1, 1});
    ScopedSimRandom search_rng(&search_rng_values);
    ready_to_act(actor, ACT_RANDOM, FACE_UP);

    ASSERT_TRUE(actor->act())
        << "the 4-of-5 search arm returns 1 whether or not it found a foe";
    ASSERT_EQ(nullptr, actor->foe())
        << "an empty world leaves the searcher with no foe at all";

    const QueuedCommand queued = queued_command(actor);
    ASSERT_EQ(COMMAND_WALK, queued.type)
        << "COMMAND_RANDOM_WALK is translated into a COMMAND_WALK by try_command";
    ASSERT_EQ(20, queued.count)
        << "the foe-less random walk carries a 20-tick budget";
    // add_command's zero-vector rule: a COMMAND_WALK rolled as (0, 0) would
    // stand still forever, so it is rewritten to (1, 1).
    ASSERT_EQ(1, queued.com1)
        << "a (0,0) random walk is rewritten to a real step on x";
    ASSERT_EQ(1, queued.com2)
        << "a (0,0) random walk is rewritten to a real step on y";
    ASSERT_EQ(ACT_RANDOM, static_cast<int>(actor->act_type()))
        << "a fruitless search never changes the act type";

    world.delete_objects();
}


TEST(WalkerCoreMore, walker_round8_death_obmap_cleanup_and_act_control_fallthrough_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    w->setxy(96, 96);

    // Force act() through command handling (temp==0), recoil/lunge clamping, and ACT_CONTROL return.
    w->stats()->clear_command();
    w->stats()->force_command(COMMAND_MULTIDO, 1, 0, 0);
    w->set_busy(2.0f);
    w->set_attack_lunge(0.2f);
    w->set_hit_recoil(0.2f);
    w->set_ani_type(ANI_WALK);
    w->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(w->act()) << "ACT_CONTROL path should return true";
    ASSERT_FLOAT_EQ(1.0f, w->busy())
        << "act burns exactly one tick of the firing delay (2.0 - 1.0)";
    ASSERT_FLOAT_EQ(0.0f, w->attack_lunge())
        << "0.2 - 0.4 would go negative, so act clamps attack_lunge to exactly 0";
    ASSERT_FLOAT_EQ(0.0f, w->hit_recoil())
        << "0.2 - 0.6 would go negative, so act clamps hit_recoil to exactly 0";

    // death() must unregister the corpse from the collision map BEFORE anything
    // else: leaving it registered is the stale-pointer bug the branch exists
    // to prevent.
    w->setxy(96, 96);
    obmap* map = og::runtime::current_session->myscreen_->world().myobmap.get();
    ASSERT_NE(nullptr, map) << "the world has an active obmap";
    {
        std::list<walker*>& cell = map->obmap_get_list(96, 96);
        ASSERT_NE(cell.end(), std::find(cell.begin(), cell.end(), w))
            << "the walker is registered in its cell before it dies";
    }
    const size_t active_before = map->size();
    w->set_dead(1);
    w->set_death_called(0);
    ASSERT_TRUE(w->death()) << "death should succeed with alternate myobmap";
    ASSERT_EQ(active_before - 1, map->size())
        << "death() unregisters exactly the dying walker (the bloodstain is "
           "ignore()d, so it never registers)";
    {
        std::list<walker*>& cell = map->obmap_get_list(96, 96);
        ASSERT_EQ(cell.end(), std::find(cell.begin(), cell.end(), w))
            << "no stale pointer to the corpse is left in its cell";
    }

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCoreMore, walker_round13_act_command_short_circuit_and_switch_paths_625_707)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* gen = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_TRUE(actor && foe && gen) << "fixtures created";
    if (!(actor && foe && gen))
        return;

    // Command short-circuit path: do_command() returns true and act() exits at line 625.
    actor->stats()->clear_command();
    actor->stats()->force_command(COMMAND_WALK, 1, 1, 0);
    actor->set_attack_lunge(1.0f);
    actor->set_hit_recoil(1.0f);
    ASSERT_TRUE(actor->act()) << "act should return true when queued command executes";

    // ACT_DIE branch (lines 669-673).
    actor->stats()->clear_command();
    actor->set_act_type(ACT_DIE);
    actor->set_dead(0);
    ASSERT_TRUE(actor->act()) << "ACT_DIE should return true";

    // ACT_GENERATE break path should flow to the function's final return false.
    gen->set_act_type(ACT_GENERATE);
    ASSERT_TRUE(!gen->act()) << "ACT_GENERATE path should break and return false in base act()";

    // ACT_GUARD with no available foe should break and return false.
    actor->set_dead(0);
    actor->set_foe(foe);
    foe->set_dead(1);
    actor->set_act_type(ACT_GUARD);
    ASSERT_TRUE(!actor->act()) << "ACT_GUARD with dead/no foe should return false";

    // Default act_type branch should return false.
    actor->set_act_type(99);
    ASSERT_TRUE(!actor->act()) << "unknown act type should return false";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCoreMore, walker_round14_distance_color_and_friendliness_modes_1480_1615)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* a = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* b = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(a && b) << "fixtures created";
    if (!(a && b))
        return;

    // distance_to_ob and distance_to_ob_center branches.
    a->setxy(100, 100);
    b->setxy(116, 110);
    a->set_sizex(8);
    a->set_sizey(8);
    b->set_sizex(10);
    b->set_sizey(12);
    ASSERT_EQ(26, (int)a->distance_to_ob(b)) << "distance_to_ob should use manhattan distance";
    ASSERT_TRUE(a->distance_to_ob_center(b) > 0) << "distance_to_ob_center should compute squared center distance";

    // query_team_color line path.
    a->set_team_num(3);
    ASSERT_EQ(88, (int)a->query_team_color()) << "team color should map to team*16+40";

    GameWorld& world = og::runtime::current_session->myscreen_->world_;
    a->set_dead(0);
    b->set_dead(0);

    // Enemy mode (allied_mode == 0) compares team numbers.
    world.allied_mode = 0;
    a->set_team_num(1);
    b->set_team_num(1);
    a->clear_myguy();
    b->clear_myguy();
    ASSERT_EQ(1, (int)a->is_friendly(b)) << "enemy mode same-team should be friendly";
    b->set_team_num(2);
    ASSERT_EQ(0, (int)a->is_friendly(b)) << "enemy mode different-team should be unfriendly";
    ASSERT_EQ(1, (int)a->is_friendly_to_team(1)) << "enemy mode should match own team";
    ASSERT_EQ(0, (int)a->is_friendly_to_team(0)) << "enemy mode should reject other teams";

    // Seat mode and company ownership never override different team colors.
    world.allied_mode = 1;
    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    b->set_owned_myguy(std::make_unique<guy>(FAMILY_ORC));
    ASSERT_EQ(0, (int)a->is_friendly(b))
        << "different-color company heroes must be hostile";

    // Removing company ownership still leaves different colors hostile.
    b->clear_myguy();
    b->set_team_num(3);
    ASSERT_EQ(0, (int)a->is_friendly(b)) << "one-sided myguy should reject non-red team target";
}


// set_difficulty at 150 %:
//   walker::set_difficulty's Generator arm -> hp = max_hp = 100*level*pct/100
//   living::set_difficulty            team != 0 -> the multiplier applies
//                       team 0, no myguy (A12a) -> the multiplier applies too
//                       team 0 carrying a myguy -> exempt (the player crew)
TEST(WalkerCoreMore, walker_round15_set_difficulty_scales_all_but_player_crew)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();
    world.delete_objects();

    const short old_difficulty = world.difficulty;
    world.difficulty = 150;

    walker* gen = world.add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_NE(nullptr, gen) << "generator created";
    gen->stats()->set_hitpoints(1.0f);
    gen->set_difficulty(5);
    ASSERT_FLOAT_EQ(750.0f, gen->stats()->hitpoints())
        << "a generator's hitpoints are 100 * level * difficulty% (100*5*150/100)";
    ASSERT_FLOAT_EQ(750.0f, gen->stats()->max_hitpoints())
        << "a generator's fighting HP is also its denominator";

    // Three identical soldiers: only the ownership/team differs, so the
    // multiplier is the only thing the comparisons below can be reading.
    walker* enemy = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* ally_npc = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* player = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, enemy) << "enemy created";
    ASSERT_NE(nullptr, ally_npc) << "allied NPC created";
    ASSERT_NE(nullptr, player) << "player hero created";
    ASSERT_FLOAT_EQ(enemy->stats()->max_hitpoints(), player->stats()->max_hitpoints())
        << "the three fixtures start from one loader baseline";
    ASSERT_FLOAT_EQ(enemy->stats()->max_hitpoints(), ally_npc->stats()->max_hitpoints())
        << "the three fixtures start from one loader baseline";

    enemy->set_team_num(2);
    enemy->clear_myguy();
    ally_npc->set_team_num(0);
    ally_npc->clear_myguy();   // a placed team-0 NPC, not a hired hero
    player->set_team_num(0);
    player->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));

    const float base_hp = player->stats()->max_hitpoints();

    enemy->set_difficulty(4);
    ally_npc->set_difficulty(4);
    player->set_difficulty(4);

    const float player_hp = player->stats()->max_hitpoints();
    const float player_mp = player->stats()->max_magicpoints();
    const float player_dmg = player->damage();

    ASSERT_FLOAT_EQ(player_hp * 1.5f, enemy->stats()->max_hitpoints())
        << "a hostile living takes the full 150 % hitpoint multiplier";
    ASSERT_FLOAT_EQ(player_mp * 1.5f, enemy->stats()->max_magicpoints())
        << "a hostile living takes the 150 % magic multiplier";
    ASSERT_FLOAT_EQ(player_dmg * 1.5f, enemy->damage())
        << "a hostile living takes the 150 % damage multiplier";

    ASSERT_FLOAT_EQ(enemy->stats()->max_hitpoints(), ally_npc->stats()->max_hitpoints())
        << "A12a: a placed team-0 NPC scales exactly like its foes";
    ASSERT_FLOAT_EQ(enemy->damage(), ally_npc->damage())
        << "A12a: a placed team-0 NPC scales exactly like its foes";

    // The exemption is from the DIFFICULTY multiplier only -- the hero still
    // gains the family's level scaling, so this is not a no-op path.
    ASSERT_GT(player_hp, base_hp)
        << "a player hero still gains the family level scaling";
    ASSERT_FLOAT_EQ(player->stats()->max_hitpoints(), player->stats()->hitpoints())
        << "set_difficulty tops the walker up to its new maximum";

    world.difficulty = old_difficulty;
    world.delete_objects();
}


TEST(WalkerCoreMore, walker_round16_act_random_no_foe_far_search_fallback_path)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_TRUE(actor != nullptr) << "actor created";
    if (!actor)
        return;

    og::runtime::current_session->myscreen_->world().rng_.state_ = 1; // rng(4)!=0 => ACT_RANDOM else branch
    actor->set_act_type(ACT_RANDOM);
    actor->set_ani_type(ANI_WALK);
    actor->set_foe(nullptr);
    actor->stats()->clear_command();

    // With no foes in the level, find_far_foe should return nullptr and no search command is queued.
    ASSERT_TRUE(actor->act()) << "ACT_RANDOM no-foe fallback should still return true";
    ASSERT_TRUE(actor->foe() == nullptr) << "ACT_RANDOM should keep foe null when far-foe search finds nothing";
}


TEST(WalkerCoreMore, walker_round17_query_next_to_and_fire_check_early_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* blocker = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(actor && blocker) << "fixtures created";
    if (!(actor && blocker))
        return;

    actor->setxy(100, 100);
    actor->set_sizex(12);
    actor->set_sizey(12);
    actor->set_lastx(1);
    actor->set_lasty(0);
    blocker->setxy(static_cast<short>(actor->xpos() + actor->sizex() - 1),
                   static_cast<short>(actor->ypos() - actor->sizey()));
    blocker->set_sizex(12);
    blocker->set_sizey(12);

    ASSERT_TRUE(actor->query_next_to()) << "query_next_to should report blocked when adjacent tile is occupied";
    og::runtime::current_session->myscreen_->world().remove_ob(blocker);
    ASSERT_TRUE(!actor->query_next_to()) << "query_next_to should report pass when adjacent tile is clear";

    // fire_check generator early return path (walker.cpp:943-944).
    walker* gen = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_TRUE(gen != nullptr) << "generator created";
    if (gen)
    {
        ASSERT_TRUE(gen->fire_check(1, 0)) << "generator fire_check should short-circuit true";
    }

    // fire_check no-foe early return path (walker.cpp:955-959).
    actor->set_foe(nullptr);
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    actor->stats()->set_magicpoints(9999.0f);
    ASSERT_TRUE(!actor->fire_check(1, 0)) << "fire_check should fail when actor has no foe";

    // fire_check BIT_NO_RANGED early return path (walker.cpp:962-965).
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    if (foe)
    {
        foe->setxy(static_cast<short>(actor->xpos() + 40), actor->ypos());
        actor->set_foe(foe);
        actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        ASSERT_TRUE(!actor->fire_check(1, 0)) << "fire_check should fail when BIT_NO_RANGED is set";
    }
}


TEST(WalkerCoreMore, walker_round18_animate_teleport_and_skelgrow_completion_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    // ANI_TELE_OUT + family teleport handler branch (walker.cpp:817-821).
    walker* mage = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_MAGE);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    if (mage)
    {
        mage->setxy(100, 100);
        mage->set_ani_type(ANI_TELE_OUT);
        mage->set_cycle(127); // force animate() into end-of-sequence handling
        ASSERT_TRUE(mage->animate()) << "mage teleport handler should return true from animate";
        ASSERT_EQ(ANI_TELE_IN, (int)mage->ani_type()) << "teleport handler should switch mage to ANI_TELE_IN";
    }

    // ANI_SKEL_GROW completion branch (walker.cpp:807-815).
    walker* skeleton = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SKELETON);
    ASSERT_TRUE(skeleton != nullptr) << "skeleton created";
    if (skeleton)
    {
        skeleton->set_ani_type(ANI_SKEL_GROW);
        skeleton->set_cycle(127); // force completion path
        ASSERT_TRUE(skeleton->animate()) << "skeleton grow completion should return true";
        ASSERT_EQ(ANI_WALK, (int)skeleton->ani_type()) << "skeleton grow completion should reset to ANI_WALK";
    }
}


TEST(WalkerCoreMore, walker_round19_move_myguy_fire_callback_and_act_random_no_foe_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* source = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* target = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* target2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_TRUE(source && target && target2) << "fixtures created";
    if (!(source && target && target2))
        return;

    // move_myguy_to nullptr early return.
    source->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    source->move_myguy_to(nullptr);
    ASSERT_TRUE(source->myguy != nullptr) << "move_myguy_to(nullptr) should keep myguy on source";

    // Owned transfer branch.
    source->move_myguy_to(target);
    ASSERT_TRUE(source->myguy == nullptr) << "owned myguy should move off source";
    ASSERT_TRUE(target->myguy != nullptr) << "target should receive moved owned myguy";

    // View transfer branch.
    source->set_myguy_view(target->myguy);
    source->move_myguy_to(target2);
    ASSERT_TRUE(source->myguy == nullptr) << "view myguy should clear on source after transfer";
    ASSERT_TRUE(target2->myguy == target->myguy) << "target2 should receive transferred view myguy pointer";

    // Soldier fire callback returning false branch (walker.cpp on_fire_weapon gate).
    source->setxy(100, 100);
    source->stats()->set_magicpoints(200.0f);
    source->stats()->set_weapon_cost(1.0f);
    source->set_lastx(1);
    source->set_lasty(0);
    static_cast<living*>(source)->set_weapons_left(0);
    ASSERT_TRUE(source->fire() == nullptr) << "soldier fire should return nullptr when on_fire_weapon rejects";

    // Drive ACT_RANDOM -> act_random() no-foe path so it queues random walk.
    og::runtime::current_session->myscreen_->world().remove_ob(target);
    og::runtime::current_session->myscreen_->world().remove_ob(target2);
    ASSERT_TRUE(current_game != nullptr && current_game->world != nullptr) << "current_game world context must be active";
    current_game->world->rng_.state_ = 0;
    source->set_foe(nullptr);
    source->stats()->clear_command();
    source->set_act_type(ACT_RANDOM);
    source->set_ani_type(ANI_WALK);
    const bool acted = source->act();
    ASSERT_TRUE(!acted) << "ACT_RANDOM act_random no-foe subpath should hit final false return";

    og::runtime::current_session->myscreen_->world().delete_objects();
}

// ---------------------------------------------------------------------------
// Generator spawn-rate multiplier (difficulty submenu "Generators"): on a
// fixed world RNG seed, a Frenzy-rate (200%) generator must emit strictly
// more spawns than the default rate by a fixed tick horizon, and the
// explicit 100% rate must be spawn-for-spawn identical with the unset (0)
// default. The rate scales the cadence comparison (level_draw * rate >
// threshold_draw * 100), never the draw bounds. Runs the real emission
// machinery: act_generate cadence roll -> init_fire -> attack animation ->
// fire -> create_weapon.
// ---------------------------------------------------------------------------
TEST(WalkerCoreMore, generator_rate_200_spawns_more_than_rate_100_by_fixed_tick)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();
    world.delete_objects();
    ASSERT_TRUE(current_game != nullptr && current_game->world == &world)
        << "the ambient integration context must drive the screen world";

    constexpr int kTicks = 800;
    constexpr std::uint32_t kSeed = 0x5EED5EEDu;

    const auto spawned_mages_by_tick = [&](short rate) {
        world.delete_objects();
        world.generator_rate = rate;
        world.rng_.state_ = kSeed;

        walker* gen = world.add_ob(Order::Generator, FAMILY_TOWER);
        EXPECT_TRUE(gen != nullptr) << "generator created";
        if (gen == nullptr)
            return -1;
        gen->setxy(160, 160);
        gen->set_team_num(1);
        gen->stats()->set_level(5);
        gen->set_act_type(ACT_GENERATE);
        gen->set_default_weapon(FAMILY_MAGE);
        gen->set_current_weapon(FAMILY_MAGE);

        for (int tick = 0; tick < kTicks; ++tick)
            (void)gen->act();

        // Count every mage the generator created (blocked newborns die in
        // place but stay in the un-swept oblist, so this is the emission
        // count, not the survivor count).
        int count = 0;
        for (const auto& uptr : world.oblist)
        {
            const walker* w = uptr.get();
            if (w != nullptr && w->query_order() == Order::Living &&
                w->family() == FAMILY_MAGE)
            {
                ++count;
            }
        }
        world.delete_objects();
        return count;
    };

    const int spawned_default = spawned_mages_by_tick(0);
    const int spawned_100 = spawned_mages_by_tick(100);
    const int spawned_200 = spawned_mages_by_tick(200);
    world.generator_rate = 0;

    ASSERT_GT(spawned_default, 0)
        << "the level-5 tower must emit on the default rate within "
        << kTicks << " ticks";
    EXPECT_EQ(spawned_default, spawned_100)
        << "rate 100 is an exact integer identity with the default stream";
    EXPECT_GT(spawned_200, spawned_100)
        << "Frenzy (200%) must out-spawn the default rate by tick " << kTicks;
}
