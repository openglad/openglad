#include <openglad/core/stats.h>
#include <openglad/runtime/game_context.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/render/view.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"

#include <memory>
#include <vector>

extern screen* myscreen;

namespace
{
struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* ctx) { set_global_context(ctx); }
    ~GlobalContextGuard() { set_global_context(nullptr); }
    GlobalContextGuard(const GlobalContextGuard&) = delete;
    GlobalContextGuard& operator=(const GlobalContextGuard&) = delete;
};

static std::unique_ptr<walker> make_living(char family, unsigned char team = 0, short level = 3)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, myscreen);
    if (w)
        w->setxy(100, 100);
    return w;
}

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
} // namespace

void test_walker_compute_outline_state_transitions()
{
    auto viewer = make_living(FAMILY_SOLDIER, 1);
    auto subject = make_living(FAMILY_SOLDIER, 2);
    TEST_ASSERT(viewer != nullptr && subject != nullptr, "walkers created");
    if (!(viewer && subject))
        return;

    // Drive the outline state machine through multiple branches.
    subject->outline = OUTLINE_INVULNERABLE;
    subject->flight_left = 5;
    subject->invisibility_left = 0;
    subject->invulnerable_left = 5;
    subject->stats()->set_bit_flags(BIT_NAMED, 1);

    subject->compute_outline(viewer.get());
    TEST_ASSERT(subject->outline != 0, "outline should remain non-zero with active flags");

    subject->outline = subject->query_team_color(); // OUTLINE_INVISIBLE expands to query_team_color()
    subject->flight_left = 0;
    subject->invulnerable_left = 5;
    subject->compute_outline(viewer.get());
    TEST_ASSERT(subject->outline == OUTLINE_INVULNERABLE, "invisible should transition to invulnerable when invulnerable_left set");

    subject->outline = OUTLINE_FLYING;
    subject->invulnerable_left = 0;
    subject->invisibility_left = 5;
    // If BIT_NAMED is set and the viewer is on another team, compute_outline()
    // prioritizes OUTLINE_NAMED over invisibility. Clear it to exercise the
    // OUTLINE_FLYING -> OUTLINE_INVISIBLE transition.
    subject->stats()->set_bit_flags(BIT_NAMED, 0);
    subject->compute_outline(viewer.get());
    TEST_ASSERT(subject->outline == subject->query_team_color(), "flying should transition to invisible when invisibility_left set");
}
REGISTER_TEST(test_walker_compute_outline_state_transitions);

void test_walker_generator_fire_sets_weapon_lifetime_or_owner_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.game_screen = myscreen;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    // NOTE: walker::fire() has additional state/animation dependencies, so this test
    // sticks to the generator-specific weapon creation path plus create_weapon().

    // Generator: mage tower (generator-only create_weapon path).
    walker* gen_tower = myscreen->level_data.add_ob(Order::Generator, FAMILY_TOWER);
    TEST_ASSERT(gen_tower != nullptr, "generator tower created");
    if (gen_tower) {
        gen_tower->team_num = 2;
        gen_tower->stats()->level = 5;
        gen_tower->setxy(128, 128);
        gen_tower->lastx = 1;
        gen_tower->lasty = 0;
        gen_tower->stats()->magicpoints = 9999.0f;
        walker* weapon = gen_tower->fire();
        TEST_ASSERT(weapon != nullptr, "tower fire should create a living projectile/spawn");
        if (weapon)
        {
            TEST_ASSERT_EQ(ANI_TELE_IN, (int)weapon->ani_type, "tower spawn should set tele-in animation");
            TEST_ASSERT(weapon->owner == nullptr, "tower spawn should clear owner");
        }
    }

    // Generator: tent (default generator branch).
    walker* gen_tent = myscreen->level_data.add_ob(Order::Generator, FAMILY_TENT);
    TEST_ASSERT(gen_tent != nullptr, "generator tent created");
    if (gen_tent) {
        gen_tent->team_num = 3;
        gen_tent->stats()->level = 4;
        gen_tent->setxy(160, 128);
        gen_tent->lastx = 1;
        gen_tent->lasty = 0;
        gen_tent->stats()->magicpoints = 9999.0f;
        walker* weapon = gen_tent->fire();
        TEST_ASSERT(weapon != nullptr, "tent fire should create a living projectile/spawn");
        if (weapon)
        {
            TEST_ASSERT(weapon->lifetime >= 800, "tent spawn should assign lifetime");
            TEST_ASSERT(weapon->owner == gen_tent, "tent spawn should keep owner");
        }
    }

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_generator_fire_sets_weapon_lifetime_or_owner_paths);

void test_walker_generator_create_weapon_special_case()
{
    myscreen->level_data.delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.game_screen = myscreen;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    walker* gen = myscreen->level_data.add_ob(Order::Generator, FAMILY_TREEHOUSE);
    TEST_ASSERT(gen != nullptr, "generator created");
    if (gen) {
        gen->team_num = 1;
        gen->stats()->level = 3;
        gen->default_weapon = FAMILY_ELF;
        gen->current_weapon = gen->default_weapon;
        walker* weapon = gen->create_weapon();
        TEST_ASSERT(weapon != nullptr, "create_weapon should return a spawned living for generators");
    }

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_generator_create_weapon_special_case);

void test_walker_act_guard_and_random_branch_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    auto actor = make_living(FAMILY_ORC, 1, 4);
    TEST_ASSERT(actor != nullptr, "actor created");
    if (!actor)
        return;

    actor->sim_level = &myscreen->level_data;
    actor->setxy(96, 96);

    {
        FixedRandom rng1(1);
        GameContext c;
        c.game_screen = myscreen;
        c.rng = &rng1;
        GlobalContextGuard guard(&c);

        actor->set_act_type(ACT_GUARD);
        actor->foe = nullptr;
        const bool acted = actor->act();
        TEST_ASSERT(!acted, "ACT_GUARD with no nearby foe should return false");
    }

    {
        SequenceRandom rng_seq({0, 1, 0, 0});
        GameContext c;
        c.game_screen = myscreen;
        c.rng = &rng_seq;
        GlobalContextGuard guard(&c);

        actor->stats()->clear_command();
        actor->set_act_type(ACT_RANDOM);
        actor->foe = nullptr;
        (void)actor->act();
        // ACT_RANDOM no-foe branch may pick either random-walk or distant-foe search based on RNG.
        (void)actor->stats()->has_commands();
    }

    walker* foe = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(foe != nullptr, "foe created");
    if (foe)
    {
        foe->team_num = 2;
        foe->setxy(128, 96);
    }
    actor->team_num = 1;
    actor->lineofsight = 50;
    actor->foe = foe;

    {
        SequenceRandom rng_seq({0, 1, 1, 5});
        GameContext c;
        c.game_screen = myscreen;
        c.rng = &rng_seq;
        GlobalContextGuard guard(&c);

        actor->stats()->clear_command();
        actor->set_act_type(ACT_RANDOM);
        (void)actor->act();

        TEST_ASSERT(actor->foe == foe, "ACT_RANDOM visible-foe branch should keep the selected foe");
    }

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_act_guard_and_random_branch_paths);

void test_walker_act_generate_zero_vector_and_hp_cap_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    // next(60)=59 and next(300+numobs*8)=0 satisfy generation condition.
    // next(3)=1 for both axes gives 0,0 to trigger the fallback lastx=1 branch.
    SequenceRandom rng_seq({59, 0, 1, 1, 0, 0, 0});
    GameContext c;
    c.game_screen = myscreen;
    c.rng = &rng_seq;
    GlobalContextGuard guard(&c);

    walker* gen = myscreen->level_data.add_ob(Order::Generator, FAMILY_TENT);
    TEST_ASSERT(gen != nullptr, "generator created");
    if (!gen)
        return;

    gen->stats()->level = 20;
    gen->stats()->max_hitpoints = 10;
    gen->stats()->hitpoints = 10;
    gen->default_weapon = FAMILY_ELF;
    gen->current_weapon = gen->default_weapon;

    gen->set_act_type(ACT_GENERATE);
    (void)gen->act();
    TEST_ASSERT_EQ(1, (int)gen->lastx, "act_generate should force lastx=1 when random step vector is zero");
    TEST_ASSERT_EQ((int)gen->stats()->max_hitpoints, (int)gen->stats()->hitpoints,
                   "act_generate should clamp hitpoints at max");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_act_generate_zero_vector_and_hp_cap_paths);

void test_walker_act_guard_else_and_act_random_turn_walk_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    auto actor = make_living(FAMILY_ORC, 1, 4);
    auto foe = make_living(FAMILY_SOLDIER, 2, 4);
    TEST_ASSERT(actor != nullptr && foe != nullptr, "walkers created");
    if (!(actor && foe))
        return;

    actor->sim_level = &myscreen->level_data;
    foe->sim_level = &myscreen->level_data;
    actor->setxy(96, 96);
    foe->setxy(128, 96);

    // No nearby foe case: hit act_guard() else return path.
    actor->foe = nullptr;
    myscreen->level_data.delete_objects();
    actor->set_act_type(ACT_GUARD);
    TEST_ASSERT(!actor->act(), "ACT_GUARD should return false when no foe is found");

    // Recreate context and drive act_random() through fire_check-false turn + walkstep path.
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();
    actor = make_living(FAMILY_ORC, 1, 4);
    foe = make_living(FAMILY_SOLDIER, 2, 4);
    TEST_ASSERT(actor != nullptr && foe != nullptr, "walkers recreated");
    if (!(actor && foe))
        return;

    actor->sim_level = &myscreen->level_data;
    foe->sim_level = &myscreen->level_data;
    actor->setxy(96, 96);
    foe->setxy(128, 96);
    actor->foe = foe.get();
    actor->lineofsight = 30;
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1); // forces fire_check() false branch

    SequenceRandom rng_seq({0, 1, 1});
    GameContext c;
    c.game_screen = myscreen;
    c.rng = &rng_seq;
    GlobalContextGuard guard(&c);

    actor->set_act_type(ACT_RANDOM);
    (void)actor->act();
    TEST_ASSERT(actor->query_act_type() != ACT_FIRE, "act_random blocked fire path should not set ACT_FIRE");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_act_guard_else_and_act_random_turn_walk_paths);

void test_walker_query_next_to_and_generator_fire_check_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* actor = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* blocker = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(actor != nullptr && blocker != nullptr, "walkers created");
    if (!(actor && blocker))
        return;

    actor->setxy(200, 200);
    blocker->setxy(static_cast<short>(actor->xpos + actor->sizex), actor->ypos);

    actor->lastx = 1;
    actor->lasty = 1;
    blocker->setxy(static_cast<short>(actor->xpos + actor->sizex), static_cast<short>(actor->ypos + actor->sizey));
    TEST_ASSERT(actor->query_next_to(), "query_next_to should detect nearby blocking object to the right");

    actor->lastx = -1;
    actor->lasty = -1;
    blocker->setxy(10, 10); // clear proximity
    TEST_ASSERT(!actor->query_next_to(), "query_next_to should return false when next tile is passable");

    walker* gen = myscreen->level_data.add_ob(Order::Generator, FAMILY_TENT);
    TEST_ASSERT(gen != nullptr, "generator created");
    if (gen)
    {
        TEST_ASSERT(gen->fire_check(1, 0), "generator fire_check should always succeed");
    }

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_query_next_to_and_generator_fire_check_paths);
