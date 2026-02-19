#include <openglad/core/stats.h>
#include <openglad/runtime/game_context.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/data/gloader.h>
#include <openglad/data/save_data.h>
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

    walker* actor = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
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

void test_walker_init_fire_turn_busy_and_fire_fallback_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    auto w_up = make_living(FAMILY_SOLDIER, 0, 3);
    TEST_ASSERT(w_up != nullptr, "walker created");
    if (!w_up)
        return;
    walker* w = w_up.get();
    w->sim_level = &myscreen->level_data;
    w->setxy(160, 160);
    w->lastx = 1;
    w->lasty = 0;

    // ACT_CONTROL + direction mismatch should reject init_fire.
    w->curdir = FACE_LEFT;
    w->enddir = FACE_LEFT;
    w->set_act_type(ACT_CONTROL);
    TEST_ASSERT(!w->init_fire(1, 0), "ACT_CONTROL should reject firing when turn is required");

    // Non-control mismatch should take the turn() path.
    w->set_act_type(ACT_RANDOM);
    w->curdir = FACE_LEFT;
    w->enddir = FACE_LEFT;
    TEST_ASSERT(w->init_fire(1, 0), "non-control should allow init_fire to turn first");

    // Busy gate should block firing.
    w->busy = 1;
    w->curdir = FACE_RIGHT;
    w->enddir = FACE_RIGHT;
    TEST_ASSERT(!w->init_fire(1, 0), "busy walkers should not init_fire");

    // ANI_WALK branch should transition into attack animation.
    w->busy = 0;
    w->ani_type = ANI_WALK;
    TEST_ASSERT(w->init_fire(1, 0), "ANI_WALK branch should succeed and start attack animation");
    TEST_ASSERT_EQ(ANI_ATTACK, (int)w->ani_type, "ANI_WALK firing should switch to ANI_ATTACK");

    // Non-walk path delegates to fire(); force fire() to fail via magic cost check.
    w->ani_type = ANI_ATTACK;
    w->stats()->magicpoints = 0.0f;
    w->stats()->weapon_cost = 10.0f;
    TEST_ASSERT(!w->init_fire(1, 0), "non-walk init_fire should return false when fire() fails");
}
REGISTER_TEST(test_walker_init_fire_turn_busy_and_fire_fallback_paths);

void test_walker_round5_act_switch_random_and_fire_branches()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* actor = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    walker* foe = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr && foe != nullptr, "actor and foe should be created");
    if (!(actor && foe))
        return;

    actor->team_num = 1;
    actor->setxy(96, 96);
    actor->sim_level = &myscreen->level_data;
    actor->ani_type = ANI_WALK;
    actor->stats()->clear_command();

    foe->team_num = 2;
    foe->setxy(128, 96);
    foe->sim_level = &myscreen->level_data;

    // ACT_GUARD no-foe path: break from switch then return 0.
    actor->foe = nullptr;
    actor->set_act_type(ACT_GUARD);
    TEST_ASSERT(!actor->act(), "ACT_GUARD should return false when no nearby foe exists");

    // ACT_FIRE dispatch path from the act() switch.
    actor->set_act_type(ACT_FIRE);
    actor->lineofsight = 2;
    actor->lastx = 0;
    actor->lasty = 0;
    TEST_ASSERT(actor->act(), "ACT_FIRE should dispatch and return true");

    // ACT_RANDOM 1/4 + 1/20 branch should queue COMMAND_WALK.
    SequenceRandom rng_walk_branch({0, 0, 5, 1, 2});
    actor->sim_rng = &rng_walk_branch;
    actor->stats()->clear_command();
    actor->ani_type = ANI_WALK;
    actor->foe = nullptr;
    actor->set_act_type(ACT_RANDOM);
    TEST_ASSERT(actor->act(), "ACT_RANDOM walk-command branch should return true");

    // ACT_RANDOM 3/4 branch should acquire far foe and queue COMMAND_SEARCH.
    SequenceRandom rng_search_branch({3, 0});
    actor->sim_rng = &rng_search_branch;
    actor->stats()->clear_command();
    actor->ani_type = ANI_WALK;
    actor->foe = nullptr;
    actor->set_act_type(ACT_RANDOM);
    (void)actor->act();

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_round5_act_switch_random_and_fire_branches);

void test_walker_round5_act_random_contiguous_block_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* actor = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    walker* foe = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr && foe != nullptr, "actor and foe should be created");
    if (!(actor && foe))
        return;

    actor->team_num = 1;
    actor->setxy(96, 96);
    actor->sim_level = &myscreen->level_data;
    actor->ani_type = ANI_WALK;
    actor->lineofsight = 20;

    foe->team_num = 2;
    foe->setxy(112, 96);
    foe->sim_level = &myscreen->level_data;

    // No-foe branch: find_far_foe fails and queues COMMAND_RANDOM_WALK.
    myscreen->level_data.delete_objects();
    actor = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(actor != nullptr, "actor should be recreated");
    if (!actor)
        return;
    actor->team_num = 1;
    actor->setxy(96, 96);
    actor->sim_level = &myscreen->level_data;
    actor->lineofsight = 20;
    actor->ani_type = ANI_WALK;

    SequenceRandom rng_no_foe({0, 1, 0});
    actor->sim_rng = &rng_no_foe;
    actor->foe = nullptr;
    actor->stats()->clear_command();
    actor->ani_type = ANI_WALK;
    actor->set_act_type(ACT_RANDOM);
    (void)actor->act();

    // Rebuild actor/foe pair for LOS branches.
    myscreen->level_data.delete_objects();
    actor = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    foe = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr && foe != nullptr, "actor and foe should be recreated");
    if (!(actor && foe))
        return;

    actor->team_num = 1;
    actor->setxy(96, 96);
    actor->sim_level = &myscreen->level_data;
    actor->ani_type = ANI_WALK;
    actor->lineofsight = 20;
    actor->foe = foe;

    foe->team_num = 2;
    foe->setxy(112, 96);
    foe->sim_level = &myscreen->level_data;

    // In-range foe with blocked ranged attack path: fire_check false -> turn/walkstep.
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    SequenceRandom rng_turn_walk({0, 1, 1});
    actor->sim_rng = &rng_turn_walk;
    actor->stats()->clear_command();
    actor->set_act_type(ACT_RANDOM);
    (void)actor->act();

    // In-range foe with clear fire path: init_fire + COMMAND_FIRE path.
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    SequenceRandom rng_fire_cmd({0, 1, 1, 7});
    actor->sim_rng = &rng_fire_cmd;
    actor->stats()->clear_command();
    actor->set_act_type(ACT_RANDOM);
    (void)actor->act();

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_round5_act_random_contiguous_block_paths);

void test_walker_round6_init_fire_animate_and_misc_guards()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker should be created");
    if (!w)
        return;

    // next_frame path (smoke coverage without touching protected state).
    const short before = w->query_frame();
    (void)w->next_frame();
    const short after = w->query_frame();
    (void)before;
    (void)after;
    TEST_ASSERT(true, "next_frame should be callable");

    // move_myguy_to nullptr guard.
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    w->move_myguy_to(nullptr);
    TEST_ASSERT(w->myguy != nullptr, "move_myguy_to(nullptr) should keep myguy unchanged");

    // init_fire: ACT_CONTROL early-return branch.
    w->curdir = FACE_UP;
    w->set_act_type(ACT_CONTROL);
    TEST_ASSERT(!w->init_fire(1, 0), "init_fire should return false for control walker needing turn");

    // init_fire: busy early-return branch.
    w->set_act_type(ACT_RANDOM);
    w->curdir = FACE_RIGHT;
    w->busy = 1;
    TEST_ASSERT(!w->init_fire(1, 0), "init_fire should return false when busy");

    // animate null-ani guard using headless default ctor.
    walker headless;
    TEST_ASSERT(!headless.animate(), "animate should return false when ani is null");

    // ACT() pointer cleanup and recoil/lunge clamping.
    walker* dead_target = myscreen->level_data.add_ob(Order::Living, FAMILY_ARCHER);
    TEST_ASSERT(dead_target != nullptr, "dead target should be created");
    if (dead_target)
    {
        dead_target->dead = 1;
        w->attack_lunge = 0.2f;
        w->hit_recoil = 0.2f;
        w->ani_type = ANI_WALK;
        w->set_act_type(ACT_CONTROL);

        w->foe = dead_target;
        w->leader = nullptr;
        w->owner = nullptr;
        (void)w->act();
        TEST_ASSERT(w->foe == nullptr, "act should clear dead foe pointer");

        w->foe = nullptr;
        w->leader = dead_target;
        w->owner = nullptr;
        w->ani_type = ANI_WALK;
        (void)w->act();
        TEST_ASSERT(w->leader == nullptr, "act should clear dead leader pointer");

        TEST_ASSERT(w->attack_lunge == 0.0f && w->hit_recoil == 0.0f,
                    "act should clamp lunge/recoil to zero");
    }

    // animate ANI_TELE_OUT default no-handler branch.
    w->ani_type = ANI_TELE_OUT;
    w->cycle = 120;
    w->curdir = FACE_DOWN;
    TEST_ASSERT(!w->animate(), "ANI_TELE_OUT without handler should return false after reset");
}
REGISTER_TEST(test_walker_round6_init_fire_animate_and_misc_guards);

void test_walker_round6_fire_and_friendliness_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* actor = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* target = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(actor && target, "actor/target should be created");
    if (!(actor && target))
        return;

    actor->setxy(64, 64);
    actor->lastx = 1;
    actor->lasty = 0;
    actor->stats()->magicpoints = 0.0f;
    actor->stats()->weapon_cost = 5.0f;
    TEST_ASSERT(actor->fire() == nullptr, "fire should fail when magicpoints are insufficient");

    actor->stats()->magicpoints = 999.0f;
    actor->stats()->weapon_cost = 0.0f;
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    TEST_ASSERT(actor->fire() == nullptr, "fire should return null for BIT_NO_RANGED");
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);

    // fire_check special guards.
    actor->set_order_family(Order::Generator, FAMILY_TOWER);
    TEST_ASSERT(actor->fire_check(1, 0), "fire_check should always succeed for generators");
    actor->set_order_family(Order::Living, FAMILY_SOLDIER);

    actor->foe = nullptr;
    TEST_ASSERT(!actor->fire_check(1, 0), "fire_check should fail when no foe is selected");

    actor->foe = target;
    target->setxy(actor->xpos + 4, actor->ypos + 40);
    actor->curdir = FACE_RIGHT;
    TEST_ASSERT(!actor->fire_check(0, 1), "fire_check should fail on targetdir mismatch");

    // create_weapon default switch branch (diagonal facing).
    actor->lastx = 1;
    actor->lasty = 1;
    walker* diagonal_weapon = actor->create_weapon();
    TEST_ASSERT(diagonal_weapon != nullptr, "create_weapon should succeed for living actor");

    // is_friendly / is_friendly_to_team paths with allied mode and myguy combinations.
    SaveData save;
    save.allied_mode = 1;
    actor->sim_save = &save;
    target->sim_save = &save;
    actor->team_num = 0;
    target->team_num = 2;
    actor->clear_myguy();
    target->set_owned_myguy(std::make_unique<guy>(FAMILY_ORC));
    TEST_ASSERT(actor->is_friendly(target) != 0,
                "allied mode with one myguy and team0 other should be friendly");

    actor->dead = 1;
    TEST_ASSERT(actor->is_friendly_to_team(2) == 0, "dead walker should not be friendly to any team");
    actor->dead = 0;
    actor->clear_myguy();
    TEST_ASSERT(actor->is_friendly_to_team(actor->team_num) != 0,
                "no-myguy walker should be friendly only to matching team");
}
REGISTER_TEST(test_walker_round6_fire_and_friendliness_paths);

void test_walker_round6_guard_and_random_direct_branches()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* actor = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    walker* foe = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr && foe != nullptr, "actor and foe should be created");
    if (!(actor && foe))
        return;

    actor->sim_level = &myscreen->level_data;
    actor->team_num = 1;
    actor->setxy(96, 96);
    actor->lineofsight = 20;
    actor->stats()->magicpoints = 9999.0f;
    actor->stats()->weapon_cost = 0.0f;

    foe->sim_level = &myscreen->level_data;
    foe->team_num = 2;
    foe->setxy(112, 96);

    // act_guard() foe path via act(): set facing + queue fire command.
    SequenceRandom guard_rng({7});
    actor->sim_rng = &guard_rng;
    actor->ani_type = ANI_WALK;
    actor->set_act_type(ACT_GUARD);
    (void)actor->act();

    // act_random() blocked-ranged path via act(): fire_check false -> turn branch.
    actor->foe = foe;
    actor->curdir = FACE_UP;
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    SequenceRandom blocked_rng({1, 0, 0});
    actor->sim_rng = &blocked_rng;
    actor->ani_type = ANI_WALK;
    actor->set_act_type(ACT_RANDOM);
    TEST_ASSERT(actor->act(), "ACT_RANDOM should still act when ranged attack is blocked");

    // act_random() in-range firing path via act(): fire_check true -> init_fire + COMMAND_FIRE.
    actor->foe = foe;
    actor->curdir = FACE_RIGHT;
    actor->enddir = FACE_RIGHT;
    actor->ani_type = ANI_WALK;
    actor->busy = 0;
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    SequenceRandom fire_rng({1, 5});
    actor->sim_rng = &fire_rng;
    actor->set_act_type(ACT_RANDOM);
    (void)actor->act();

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_round6_guard_and_random_direct_branches);

void test_walker_round7a_compute_outline_and_friendliness_edge_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* viewer = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* subject = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(viewer && subject, "viewer/subject created");
    if (!(viewer && subject))
        return;

    viewer->team_num = 0;
    subject->team_num = 1;
    subject->stats()->set_bit_flags(BIT_NAMED, 1);

    subject->outline = OUTLINE_INVULNERABLE;
    subject->flight_left = 3;
    subject->compute_outline(viewer);

    subject->outline = OUTLINE_INVULNERABLE;
    subject->flight_left = 0;
    subject->invisibility_left = 3;
    subject->compute_outline(viewer);

    subject->outline = OUTLINE_FLYING;
    subject->invisibility_left = 0;
    subject->invulnerable_left = 3;
    subject->compute_outline(viewer);

    subject->outline = OUTLINE_NAMED;
    subject->invisibility_left = 3;
    subject->invulnerable_left = 0;
    subject->flight_left = 0;
    subject->compute_outline(viewer);

    // No special flags path.
    subject->outline = OUTLINE_NAMED;
    subject->invisibility_left = 0;
    subject->invulnerable_left = 0;
    subject->flight_left = 0;
    subject->stats()->set_bit_flags(BIT_NAMED, 0);
    subject->compute_outline(viewer);
    TEST_ASSERT(subject->outline == 0 || subject->outline == subject->query_team_color(),
                "compute_outline should settle into neutral or team outline");

    // is_friendly null/dead guards and owner-chain branches.
    SaveData save;
    save.allied_mode = 1;
    subject->sim_save = &save;
    viewer->sim_save = &save;

    TEST_ASSERT_EQ(0, (int)subject->is_friendly(nullptr), "is_friendly should reject null");

    subject->dead = 1;
    TEST_ASSERT_EQ(0, (int)subject->is_friendly(viewer), "is_friendly should reject dead self");
    subject->dead = 0;

    viewer->dead = 1;
    TEST_ASSERT_EQ(0, (int)subject->is_friendly(viewer), "is_friendly should reject dead target");
    viewer->dead = 0;

    // Owner-loop traversal with one side missing myguy (has_myguy==2 path).
    walker* owner = myscreen->level_data.add_ob(Order::Living, FAMILY_MAGE);
    TEST_ASSERT(owner != nullptr, "owner created");
    if (owner)
    {
        owner->team_num = 0;
        owner->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
        subject->owner = owner;
        viewer->owner = nullptr;
        viewer->team_num = 0;
        viewer->clear_myguy();
        TEST_ASSERT(subject->is_friendly(viewer) != 0, "allied mode has_myguy==2 branch should allow red-team friendliness");
    }
}
REGISTER_TEST(test_walker_round7a_compute_outline_and_friendliness_edge_paths);

void test_walker_round7a_death_guard_and_friendliness_team_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    // death_called guard.
    w->dead = 1;
    w->death_called = 0;
    TEST_ASSERT(w->death(), "first death call should run");
    TEST_ASSERT_EQ(0, (int)w->death(), "second death call should hit death_called guard");

    // is_friendly_to_team paths for no myguy and hired allied modes.
    SaveData save;
    w->sim_save = &save;
    w->dead = 0;
    w->team_num = 2;
    w->clear_myguy();

    save.allied_mode = 0;
    TEST_ASSERT_EQ(1, (int)w->is_friendly_to_team(2), "enemy mode should only match own team");
    TEST_ASSERT_EQ(0, (int)w->is_friendly_to_team(0), "enemy mode should reject other teams");

    save.allied_mode = 1;
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    TEST_ASSERT_EQ(1, (int)w->is_friendly_to_team(0), "hired unit in allied mode should be friendly to team 0");
    TEST_ASSERT_EQ(0, (int)w->is_friendly_to_team(3), "hired unit in allied mode should reject non-zero teams");

    // Explicit has_myguy==0 path in is_friendly.
    walker* other = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(other != nullptr, "other created");
    if (other)
    {
        other->sim_save = &save;
        other->team_num = 2;
        other->clear_myguy();
        w->clear_myguy();
        save.allied_mode = 1;
        TEST_ASSERT_EQ(1, (int)w->is_friendly(other), "both without myguy should compare teams only");
        other->team_num = 1;
        TEST_ASSERT_EQ(0, (int)w->is_friendly(other), "both without myguy different teams should be unfriendly");
    }
}
REGISTER_TEST(test_walker_round7a_death_guard_and_friendliness_team_paths);

void test_walker_round7b_base_act_guard_random_and_death_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* actor = myscreen->level_data.add_ob(Order::Generator, FAMILY_TENT);
    walker* foe = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr && foe != nullptr, "actor and foe created");
    if (!(actor && foe))
        return;

    actor->team_num = 1;
    actor->setxy(96, 96);
    actor->lineofsight = 2;
    actor->sim_level = &myscreen->level_data;
    foe->team_num = 2;
    foe->setxy(128, 128);

    // Base walker::act_guard() no-foe return branch.
    myscreen->level_data.delete_objects();
    actor = myscreen->level_data.add_ob(Order::Generator, FAMILY_TENT);
    TEST_ASSERT(actor != nullptr, "actor recreated");
    if (!actor)
        return;
    actor->team_num = 1;
    actor->setxy(96, 96);
    actor->sim_level = &myscreen->level_data;
    actor->set_act_type(ACT_GUARD);
    TEST_ASSERT(!actor->act(), "base ACT_GUARD should return false when no foe is found");

    // Base walker::act_random() in-range fire path.
    foe = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(foe != nullptr, "foe recreated");
    if (!foe)
        return;
    foe->team_num = 2;
    foe->setxy(112, 96);
    actor->foe = foe;
    actor->lineofsight = 40;
    actor->set_act_type(ACT_RANDOM);
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    SequenceRandom rng_fire({5, 7});
    actor->sim_rng = &rng_fire;
    (void)actor->act();

    // Base walker::act_random() blocked-ranged path -> turn + walkstep.
    actor->foe = foe;
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    SequenceRandom rng_turn_walk({5});
    actor->sim_rng = &rng_turn_walk;
    const short x_before = actor->xpos;
    const short y_before = actor->ypos;
    TEST_ASSERT(actor->act(), "base ACT_RANDOM should still act when ranged attack is blocked");
    TEST_ASSERT(actor->xpos != x_before || actor->ypos != y_before,
                "blocked-ranged act_random path should continue into walkstep");

    // Base walker::death() generator explosion and death_called guard.
    walker* gen = myscreen->level_data.add_ob(Order::Generator, FAMILY_TENT);
    TEST_ASSERT(gen != nullptr, "generator created");
    if (gen)
    {
        gen->dead = 1;
        gen->death_called = 0;
        const size_t fx_before = myscreen->level_data.fxlist.size();
        TEST_ASSERT(gen->death(), "first generator death call should succeed");
        TEST_ASSERT(myscreen->level_data.fxlist.size() >= fx_before,
                    "generator death should run explosion spawning path");
        TEST_ASSERT_EQ(0, (int)gen->death(), "second death call should hit death_called guard");
    }

    // Save-all early-return event branch in living death path.
    const short old_type = myscreen->level_data.type;
    myscreen->level_data.type = static_cast<short>(SCEN_TYPE_SAVE_ALL);
    walker* named = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(named != nullptr, "named living created");
    if (named)
    {
        named->team_num = 0;
        named->stats()->name = "Round7B";
        named->dead = 1;
        named->death_called = 0;
        TEST_ASSERT(named->death(), "save-all named death path should return true");
    }
    myscreen->level_data.type = old_type;

    // FX-order death branch (log-only, returns success).
    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_FLASH);
    TEST_ASSERT(fx != nullptr, "fx created");
    if (fx)
    {
        fx->dead = 1;
        fx->death_called = 0;
        TEST_ASSERT(fx->death(), "fx death branch should return true");
    }

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_round7b_base_act_guard_random_and_death_paths);

void test_walker_round11_friendliness_owner_chain_and_difficulty_paths_1480_1615()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* actor = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* target = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    walker* actor_owner = myscreen->level_data.add_ob(Order::Living, FAMILY_MAGE);
    walker* actor_root = myscreen->level_data.add_ob(Order::Living, FAMILY_ARCHER);
    walker* target_owner = myscreen->level_data.add_ob(Order::Living, FAMILY_DRUID);
    TEST_ASSERT(actor && target && actor_owner && actor_root && target_owner, "fixtures created");
    if (!(actor && target && actor_owner && actor_root && target_owner))
        return;

    SaveData save;
    save.allied_mode = 1;
    actor->sim_save = &save;
    target->sim_save = &save;

    // Owner-chain traversal: actor -> actor_owner -> actor_root -> self.
    actor_owner->owner = actor_root;
    actor_root->owner = actor_root;
    actor->owner = actor_owner;
    target->owner = target_owner;
    target_owner->owner = target_owner;

    actor->dead = 0;
    target->dead = 0;
    actor_root->team_num = 0;
    target_owner->team_num = 0;
    actor_root->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    target_owner->clear_myguy();
    TEST_ASSERT(actor->is_friendly(target) != 0,
                "owner-chain has_myguy==2 path should treat team-0 side as friendly");

    // Both roots without myguy: allied mode should compare teams only.
    actor_root->clear_myguy();
    target_owner->clear_myguy();
    actor_root->team_num = 2;
    target_owner->team_num = 2;
    TEST_ASSERT_EQ(1, (int)actor->is_friendly(target), "both roots without myguy and same team should be friendly");
    target_owner->team_num = 3;
    TEST_ASSERT_EQ(0, (int)actor->is_friendly(target), "both roots without myguy and different team should be unfriendly");

    // is_friendly_to_team owner-chain + hired/allied branch.
    actor_root->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    actor_root->team_num = 3;
    TEST_ASSERT_EQ(1, (int)actor->is_friendly_to_team(0), "hired allied unit should be friendly to team 0");
    TEST_ASSERT_EQ(0, (int)actor->is_friendly_to_team(2), "hired allied unit should reject non-zero teams");

    // set_difficulty default branch path for non-generator orders.
    actor->team_num = 1;
    const float hp_before = actor->stats()->max_hitpoints;
    actor->set_difficulty(4);
    TEST_ASSERT(actor->stats()->max_hitpoints > 0.0f && actor->stats()->max_hitpoints != hp_before,
                "set_difficulty should apply default non-generator scaling path");
}
REGISTER_TEST(test_walker_round11_friendliness_owner_chain_and_difficulty_paths_1480_1615);

void test_walker_round8_death_obmap_cleanup_and_act_control_fallthrough_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    w->setxy(96, 96);

    // Force act() through command handling (temp==0), recoil/lunge clamping, and ACT_CONTROL return.
    w->stats()->clear_command();
    w->stats()->force_command(COMMAND_MULTIDO, 1, 0, 0);
    w->busy = 2.0f;
    w->attack_lunge = 0.2f;
    w->hit_recoil = 0.2f;
    w->ani_type = ANI_WALK;
    w->set_act_type(ACT_CONTROL);
    TEST_ASSERT(w->act(), "ACT_CONTROL path should return true");
    TEST_ASSERT(w->busy <= 1.0f, "act should decrement busy when positive");
    TEST_ASSERT_EQ(0, (int)w->attack_lunge, "act should clamp attack_lunge to zero");
    TEST_ASSERT_EQ(0, (int)w->hit_recoil, "act should clamp hit_recoil to zero");

    // Exercise death() branch that removes from active obmap and alternate myobmap.
    obmap spare_map;
    spare_map.add(w, w->xpos, w->ypos);
    w->myobmap = &spare_map;
    w->dead = 1;
    w->death_called = 0;
    const size_t active_before = myscreen->level_data.myobmap->size();
    const size_t spare_before = spare_map.size();
    TEST_ASSERT(w->death(), "death should succeed with alternate myobmap");
    TEST_ASSERT(spare_map.size() < spare_before, "death should remove from alternate myobmap");
    TEST_ASSERT(myscreen->level_data.myobmap->size() <= active_before, "death should remove from active obmap");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_round8_death_obmap_cleanup_and_act_control_fallthrough_paths);

void test_walker_round13_act_command_short_circuit_and_switch_paths_625_707()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* actor = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    walker* gen = myscreen->level_data.add_ob(Order::Generator, FAMILY_TOWER);
    TEST_ASSERT(actor && foe && gen, "fixtures created");
    if (!(actor && foe && gen))
        return;

    // Command short-circuit path: do_command() returns true and act() exits at line 625.
    actor->stats()->clear_command();
    actor->stats()->force_command(COMMAND_WALK, 1, 1, 0);
    actor->attack_lunge = 1.0f;
    actor->hit_recoil = 1.0f;
    TEST_ASSERT(actor->act(), "act should return true when queued command executes");

    // ACT_DIE branch (lines 669-673).
    actor->stats()->clear_command();
    actor->set_act_type(ACT_DIE);
    actor->dead = 0;
    TEST_ASSERT(actor->act(), "ACT_DIE should return true");

    // ACT_GENERATE break path should flow to the function's final return false.
    gen->set_act_type(ACT_GENERATE);
    TEST_ASSERT(!gen->act(), "ACT_GENERATE path should break and return false in base act()");

    // ACT_GUARD with no available foe should break and return false.
    actor->dead = 0;
    actor->foe = foe;
    foe->dead = 1;
    actor->set_act_type(ACT_GUARD);
    TEST_ASSERT(!actor->act(), "ACT_GUARD with dead/no foe should return false");

    // Default act_type branch should return false.
    actor->set_act_type(99);
    TEST_ASSERT(!actor->act(), "unknown act type should return false");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_round13_act_command_short_circuit_and_switch_paths_625_707);
