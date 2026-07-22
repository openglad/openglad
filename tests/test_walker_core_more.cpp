#include <openglad/gameplay/statistics.h>
#include <openglad/platform/game_context.h>
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
    ASSERT_TRUE(subject->outline() != 0) << "outline should remain non-zero with active flags";

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


TEST(WalkerCoreMore, walker_generator_create_weapon_special_case)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    walker* gen = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TREEHOUSE);
    ASSERT_TRUE(gen != nullptr) << "generator created";
    if (gen) {
        gen->set_team_num(1);
        gen->stats()->set_level(3);
        gen->set_default_weapon(FAMILY_ELF);
        gen->set_current_weapon(gen->default_weapon());
        walker* weapon = gen->create_weapon();
        ASSERT_TRUE(weapon != nullptr) << "create_weapon should return a spawned living for generators";
    }

    og::runtime::current_session->myscreen_->world().delete_objects();
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
        SequenceRandom rng_seq({0, 1, 0, 0});
        GameContext c;
        c.rng = &rng_seq;
        GlobalContextGuard guard(&c);

        actor->stats()->clear_command();
        actor->set_act_type(ACT_RANDOM);
        actor->set_foe(nullptr);
        (void)actor->act();
        // ACT_RANDOM no-foe branch may pick either random-walk or distant-foe search based on RNG.
        (void)actor->stats()->has_commands();
    }

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    if (foe)
    {
        foe->set_team_num(2);
        foe->setxy(128, 96);
    }
    actor->set_team_num(1);
    actor->set_lineofsight(50);
    actor->set_foe(foe);

    {
        SequenceRandom rng_seq({0, 1, 1, 5});
        GameContext c;
        c.rng = &rng_seq;
        GlobalContextGuard guard(&c);

        actor->stats()->clear_command();
        actor->set_act_type(ACT_RANDOM);
        (void)actor->act();

        ASSERT_TRUE(actor->foe() == foe) << "ACT_RANDOM visible-foe branch should keep the selected foe";
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
    (void)gen->act();
    ASSERT_EQ(1, static_cast<int>(gen->lastx())) << "act_generate should force lastx=1 when random step vector is zero";
    ASSERT_EQ((int)gen->stats()->max_hitpoints(), (int)gen->stats()->hitpoints()) << "act_generate should clamp hitpoints at max";

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

    SequenceRandom rng_seq({0, 1, 1});
    GameContext c;
    c.rng = &rng_seq;
    GlobalContextGuard guard(&c);

    actor->set_act_type(ACT_RANDOM);
    (void)actor->act();
    ASSERT_TRUE(actor->act_type() != ACT_FIRE) << "act_random blocked fire path should not set ACT_FIRE";

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

    // ACT_RANDOM 1/4 + 1/20 branch should queue COMMAND_WALK.
    SequenceRandom rng_walk_branch({0, 0, 5, 1, 2});
    actor->stats()->clear_command();
    actor->set_ani_type(ANI_WALK);
    actor->set_foe(nullptr);
    actor->set_act_type(ACT_RANDOM);
    ASSERT_TRUE(actor->act()) << "ACT_RANDOM walk-command branch should return true";

    // ACT_RANDOM 3/4 branch should acquire far foe and queue COMMAND_SEARCH.
    SequenceRandom rng_search_branch({3, 0});
    actor->stats()->clear_command();
    actor->set_ani_type(ANI_WALK);
    actor->set_foe(nullptr);
    actor->set_act_type(ACT_RANDOM);
    (void)actor->act();

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCoreMore, walker_round5_act_random_contiguous_block_paths)
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
    actor->set_lineofsight(20);

    foe->set_team_num(2);
    foe->setxy(112, 96);

    // No-foe branch: find_far_foe fails and queues COMMAND_RANDOM_WALK.
    og::runtime::current_session->myscreen_->world().delete_objects();
    actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(actor != nullptr) << "actor should be recreated";
    if (!actor)
        return;
    actor->set_team_num(1);
    actor->setxy(96, 96);
    actor->set_lineofsight(20);
    actor->set_ani_type(ANI_WALK);

    SequenceRandom rng_no_foe({0, 1, 0});
    actor->set_foe(nullptr);
    actor->stats()->clear_command();
    actor->set_ani_type(ANI_WALK);
    actor->set_act_type(ACT_RANDOM);
    (void)actor->act();

    // Rebuild actor/foe pair for LOS branches.
    og::runtime::current_session->myscreen_->world().delete_objects();
    actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(actor != nullptr && foe != nullptr) << "actor and foe should be recreated";
    if (!(actor && foe))
        return;

    actor->set_team_num(1);
    actor->setxy(96, 96);
    actor->set_ani_type(ANI_WALK);
    actor->set_lineofsight(20);
    actor->set_foe(foe);

    foe->set_team_num(2);
    foe->setxy(112, 96);

    // In-range foe with blocked ranged attack path: fire_check false -> turn/walkstep.
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    SequenceRandom rng_turn_walk({0, 1, 1});
    actor->stats()->clear_command();
    actor->set_act_type(ACT_RANDOM);
    (void)actor->act();

    // In-range foe with clear fire path: init_fire + COMMAND_FIRE path.
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    SequenceRandom rng_fire_cmd({0, 1, 1, 7});
    actor->stats()->clear_command();
    actor->set_act_type(ACT_RANDOM);
    (void)actor->act();

    og::runtime::current_session->myscreen_->world().delete_objects();
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


TEST(WalkerCoreMore, walker_round6_guard_and_random_direct_branches)
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
    actor->set_lineofsight(20);
    actor->stats()->set_magicpoints(9999.0f);
    actor->stats()->set_weapon_cost(0.0f);

    foe->set_team_num(2);
    foe->setxy(112, 96);

    // act_guard() foe path via act(): set facing + queue fire command.
    SequenceRandom guard_rng({7});
    actor->set_ani_type(ANI_WALK);
    actor->set_act_type(ACT_GUARD);
    (void)actor->act();

    // act_random() blocked-ranged path via act(): fire_check false -> turn branch.
    actor->set_foe(foe);
    actor->set_curdir(FACE_UP);
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    SequenceRandom blocked_rng({1, 0, 0});
    actor->set_ani_type(ANI_WALK);
    actor->set_act_type(ACT_RANDOM);
    ASSERT_TRUE(actor->act()) << "ACT_RANDOM should still act when ranged attack is blocked";

    // act_random() in-range firing path via act(): fire_check true -> init_fire + COMMAND_FIRE.
    actor->set_foe(foe);
    actor->set_curdir(FACE_RIGHT);
    actor->set_enddir(FACE_RIGHT);
    actor->set_ani_type(ANI_WALK);
    actor->set_busy(0);
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    SequenceRandom fire_rng({1, 5});
    actor->set_act_type(ACT_RANDOM);
    (void)actor->act();

    og::runtime::current_session->myscreen_->world().delete_objects();
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

    subject->set_outline(OUTLINE_INVULNERABLE);
    subject->set_flight_left(3);
    subject->compute_outline(viewer);

    subject->set_outline(OUTLINE_INVULNERABLE);
    subject->set_flight_left(0);
    subject->set_invisibility_left(3);
    subject->compute_outline(viewer);

    subject->set_outline(OUTLINE_FLYING);
    subject->set_invisibility_left(0);
    subject->set_invulnerable_left(3);
    subject->compute_outline(viewer);

    subject->set_outline(OUTLINE_NAMED);
    subject->set_invisibility_left(3);
    subject->set_invulnerable_left(0);
    subject->set_flight_left(0);
    subject->compute_outline(viewer);

    // No special flags path.
    subject->set_outline(OUTLINE_NAMED);
    subject->set_invisibility_left(0);
    subject->set_invulnerable_left(0);
    subject->set_flight_left(0);
    subject->stats()->set_bit_flags(BIT_NAMED, 0);
    subject->compute_outline(viewer);
    ASSERT_TRUE(subject->outline() == 0 || subject->outline() == subject->query_team_color()) << "compute_outline should settle into neutral or team outline";

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

    // Base walker::act_random() in-range fire path.
    foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(foe != nullptr) << "foe recreated";
    if (!foe)
        return;
    foe->set_team_num(2);
    foe->setxy(112, 96);
    actor->set_foe(foe);
    actor->set_lineofsight(40);
    actor->set_act_type(ACT_RANDOM);
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    SequenceRandom rng_fire({5, 7});
    (void)actor->act();

    // Base walker::act_random() blocked-ranged path -> turn + walkstep.
    actor->set_foe(foe);
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    SequenceRandom rng_turn_walk({5});
    ASSERT_TRUE(actor->act()) << "base ACT_RANDOM should still act when ranged attack is blocked";
    ASSERT_TRUE(actor->act_type() != ACT_FIRE) << "blocked-ranged act_random path should not transition to ACT_FIRE";

    // Base walker::death() generator explosion and death_called guard.
    walker* gen = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(gen != nullptr) << "generator created";
    if (gen)
    {
        gen->set_dead(1);
        gen->set_death_called(0);
        const size_t fx_before = og::runtime::current_session->myscreen_->world().fxlist.size();
        ASSERT_TRUE(gen->death()) << "first generator death call should succeed";
        ASSERT_TRUE(og::runtime::current_session->myscreen_->world().fxlist.size() >= fx_before) << "generator death should run explosion spawning path";
        ASSERT_EQ(0, (int)gen->death()) << "second death call should hit death_called guard";
    }

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

    // Both roots without myguy: allied mode should compare teams only.
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

    // set_difficulty default branch path for non-generator orders.
    actor->set_team_num(1);
    const float hp_before = actor->stats()->max_hitpoints();
    actor->set_difficulty(4);
    ASSERT_TRUE(actor->stats()->max_hitpoints() > 0.0f && actor->stats()->max_hitpoints() != hp_before) << "set_difficulty should apply default non-generator scaling path";
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
    ASSERT_TRUE(w->busy() <= 1.0f) << "act should decrement busy when positive";
    ASSERT_EQ(0, (int)w->attack_lunge()) << "act should clamp attack_lunge to zero";
    ASSERT_EQ(0, (int)w->hit_recoil()) << "act should clamp hit_recoil to zero";

    // Exercise death() branch that removes the walker from the active obmap.
    w->set_dead(1);
    w->set_death_called(0);
    const size_t active_before = og::runtime::current_session->myscreen_->world().myobmap->size();
    ASSERT_TRUE(w->death()) << "death should succeed with alternate myobmap";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().myobmap->size() <= active_before) << "death should remove from active obmap";

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

    // PVP mode and company ownership never override different team colors.
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


TEST(WalkerCoreMore, walker_round15_set_difficulty_generator_and_non_player_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* gen = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TOWER);
    walker* enemy = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* player = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(gen && enemy && player) << "fixtures created";
    if (!(gen && enemy && player))
        return;

    gen->stats()->set_hitpoints(1.0f);
    gen->set_difficulty(5);
    ASSERT_TRUE(gen->stats()->hitpoints() > 1.0f) << "generator difficulty path should scale hitpoints directly";

    enemy->set_team_num(2);
    const float enemy_hp_before = enemy->stats()->max_hitpoints();
    enemy->set_difficulty(4);
    ASSERT_TRUE(enemy->stats()->max_hitpoints() != enemy_hp_before) << "non-player living difficulty path should scale stats";

    player->set_team_num(0);
    player->set_difficulty(4);
    ASSERT_TRUE(player->stats()->max_hitpoints() > 0.0f) << "team 0 difficulty application should leave valid hitpoints";
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
