#include <openglad/legacy/graph.h>
#include <openglad/runtime/game_context.h>
#include <openglad/entities/guy.h>
#include <openglad/data/gloader.h>
#include "test_framework.h"

#include <memory>

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
    auto w = g.create_walker_owned(myscreen);
    if (w)
        w->setxy(100, 100);
    return w;
}
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
        gen_tower->lastx = gen_tower->stepsize;
        gen_tower->lasty = 0;
        walker* weapon = gen_tower->create_weapon();
        TEST_ASSERT(weapon != nullptr, "tower create_weapon should create a living");
    }

    // Generator: tent (default generator branch).
    walker* gen_tent = myscreen->level_data.add_ob(Order::Generator, FAMILY_TENT);
    TEST_ASSERT(gen_tent != nullptr, "generator tent created");
    if (gen_tent) {
        gen_tent->team_num = 3;
        gen_tent->stats()->level = 4;
        gen_tent->lastx = gen_tent->stepsize;
        gen_tent->lasty = 0;
        walker* weapon = gen_tent->create_weapon();
        TEST_ASSERT(weapon != nullptr, "tent create_weapon should create a living");
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
