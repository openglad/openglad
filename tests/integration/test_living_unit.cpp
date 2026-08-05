#include <openglad/gameplay/living.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/core/irandom.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/constants.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>
#include <gtest/gtest.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/interface/game_context.h>
#include <array>
#include "test_gameplay_context_scope.h"

// --- From test_living_r11.cpp ---
namespace detail_living_r11 {
namespace {

struct LivingFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    LivingFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

living* add_living(LivingFixture& fx, char family, unsigned char team)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(96, 96);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_normal_stepsize(1.0f);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

} // namespace

TEST(LivingUnit, living_r11_act_owner_dead_and_action_follow)
{
    LivingFixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0);
    living* owner = add_living(fx, FAMILY_SOLDIER, 0);
    ASSERT_TRUE(self != nullptr && owner != nullptr);

    self->set_dead(1);
    ASSERT_TRUE(!self->act());
    self->set_dead(0);

    self->set_owner(owner);
    self->set_lifetime(5);
    owner->set_dead(1);
    ASSERT_TRUE(!self->act());

    // ACTION_FOLLOW path in do_action
    self->set_dead(0);
    self->set_owner(nullptr);
    self->set_lifetime(0);
    self->set_action(ACTION_FOLLOW);
    self->set_foe(nullptr);
    owner->set_dead(0);
    owner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    owner->set_team_num(0);
    owner->set_user(0);
    ASSERT_TRUE(self->do_action());
    ASSERT_EQ(owner, self->leader());
    ASSERT_TRUE(self->stats()->has_commands());
}

TEST(LivingUnit, living_r11_facing_thresholds)
{
    living l;
    ASSERT_TRUE(l.facing(0, 1) == FACE_DOWN);
    ASSERT_TRUE(l.facing(0, -1) == FACE_UP);

    // x > 0 branch thresholds
    ASSERT_TRUE(l.facing(1, 3) == FACE_DOWN);
    ASSERT_TRUE(l.facing(2, 1) == FACE_DOWN_RIGHT);
    ASSERT_TRUE(l.facing(3, 0) == FACE_RIGHT);
    ASSERT_TRUE(l.facing(2, -1) == FACE_UP_RIGHT);
    ASSERT_TRUE(l.facing(1, -3) == FACE_UP);

    // x < 0 branch thresholds
    ASSERT_TRUE(l.facing(-1, -3) == FACE_UP);
    ASSERT_TRUE(l.facing(-2, -1) == FACE_UP_LEFT);
    ASSERT_TRUE(l.facing(-3, 0) == FACE_LEFT);
    ASSERT_TRUE(l.facing(-2, 1) == FACE_DOWN_LEFT);
    ASSERT_TRUE(l.facing(-1, 3) == FACE_DOWN);
}

TEST(LivingUnit, living_r11_collide_and_act_type_switches)
{
    LivingFixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0);
    living* foe = add_living(fx, FAMILY_ORC, 1);
    ASSERT_TRUE(self != nullptr && foe != nullptr);

    // collide() auto-attackable path
    self->collide(foe);

    self->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(self->act());

    self->set_act_type(ACT_DIE);
    self->stats()->clear_command();
    self->set_action(0);
    self->set_user(-1);
    self->set_curdir(FACE_UP);
    self->set_enddir(FACE_UP);
    ASSERT_TRUE(self->act());
    ASSERT_TRUE(self->dead());
}

TEST(LivingUnit, living_r11_summon_difficulty_checkspecial_and_walk_paths)
{
    LivingFixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0);
    living* foe = add_living(fx, FAMILY_ORC, 1);
    ASSERT_TRUE(self && foe);

    // do_summon path.
    walker* summoned = self->do_summon(FAMILY_SKELETON, 25);
    ASSERT_TRUE(summoned != nullptr);
    ASSERT_TRUE(summoned->owner() == self);
    ASSERT_TRUE(summoned->lifetime() == 25);

    // Default set_difficulty fallback path on unknown family id.
    self->set_order_family(Order::Living, static_cast<char>(127));
    self->set_difficulty(2);
    ASSERT_TRUE(self->stats()->max_hitpoints() >= self->stats()->hitpoints());
    self->set_order_family(Order::Living, FAMILY_SOLDIER);

    // check_special path when not enough magic resets special to 1.
    self->set_current_special(4);
    self->stats()->set_special_cost(4, 200);
    self->stats()->set_magicpoints(0);
    (void)self->check_special();
    ASSERT_TRUE(self->current_special() == 1);

    // living::walk bounds fail + direction-turn path.
    self->setxy(0, 0);
    self->set_curdir(FACE_LEFT);
    ASSERT_TRUE(!self->walk(-1.0f, 0.0f));
    self->set_curdir(FACE_UP);
    ASSERT_TRUE(self->walk(1.0f, 0.0f));

    // ACT_RANDOM path with foe present/no fire then search.
    self->set_foe(foe);
    self->set_lineofsight(1);
    self->set_act_type(ACT_RANDOM);
    (void)self->act();
}
} // namespace detail_living_r11

// --- From test_living_r14.cpp ---
namespace detail_living_r14 {
namespace {

struct LivingR14Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;
    GameContext gc;

    LivingR14Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
        push_test_context(&gc);
    }

    ~LivingR14Fixture()
    {
        pop_test_context();
    }
};

living* add_living(LivingR14Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(x, y);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(3.0f);
    w->set_normal_stepsize(3.0f);
    w->set_lineofsight(6);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

void assign_basic_ani(walker* w)
{
    static std::array<std::array<signed char, 4>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
    {
        seqs[i][0] = 0;
        seqs[i][1] = 1;
        seqs[i][2] = -1;
        seqs[i][3] = -1;
        rows[i] = seqs[i].data();
    }
    w->ani = rows.data();
}

} // namespace

TEST(LivingUnit, living_r14_lines_65_73_89_95_138_175_owner_lifetime_and_counters)
{
    LivingR14Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 96, 96);
    living* owner = add_living(fx, FAMILY_SOLDIER, 0, 120, 96);
    living* foe = add_living(fx, FAMILY_ORC, 1, 128, 96);
    ASSERT_TRUE(self && owner && foe);

    self->set_bonus_rounds(1);
    self->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(self->act());

    self->set_foe(foe);
    foe->set_dead(1);
    self->set_leader(owner);
    owner->set_dead(0);
    self->set_view_all(2);
    self->set_invulnerable_left(2);
    self->set_invisibility_left(2);
    self->set_flight_left(2);
    self->set_charm_left((2));
    self->set_real_team_num(1);
    self->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(self->act());

    self->set_owner(owner);
    self->set_lifetime(2);
    owner->set_dead(1);
    ASSERT_TRUE(!self->act());

    self->set_dead(0);
    owner->set_dead(0);
    self->set_owner(owner);
    self->set_lifetime(1);
    (void)self->act();
    ASSERT_TRUE(self->dead());
}

TEST(LivingUnit, living_r14_lines_155_190_196_212_219_226_235_245_259_266_270_303_308)
{
    LivingR14Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 0, 0);
    living* ally = add_living(fx, FAMILY_SOLDIER, 0, 16, 0);
    ASSERT_TRUE(self && ally);

    self->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    self->myguy->dexterity = 10;
    self->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    self->stats()->set_magicpoints(10.0f);
    self->stats()->set_current_magic_delay(0);
    self->set_speed_bonus(1);
    self->set_speed_bonus_left(3);
    self->set_attack_lunge(1.0f);
    self->set_hit_recoil(1.0f);

    fx.level.world().grid.frames = 1;
    fx.level.world().grid.w = 1;
    fx.level.world().grid.h = 1;
    fx.level.world().grid.data = std::make_unique<unsigned char[]>(1);
    fx.level.world().grid.data[0] = PIX_TREE_M1;
    fx.level.world().pixmaxx = GRID_SIZE;
    fx.level.world().pixmaxy = GRID_SIZE;
    fx.level.world().mysmoother.set_target(fx.level.world().grid);

    cfg.apply_setting("effects", "damage_numbers", "on");

    self->stats()->set_hitpoints(1.0f);
    self->set_flight_left(0);
    self->set_ani_type(ANI_WALK);
    self->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(self->act());
    ASSERT_FALSE(self->dead());
    ASSERT_EQ(2, self->speed_bonus_left());
    ASSERT_LT(self->attack_lunge(), 1.0f);
    ASSERT_LT(self->hit_recoil(), 1.0f);

    self->set_dead(0);
    self->stats()->set_frozen_delay(1);
    ASSERT_TRUE(self->act());

    self->set_dead(0);
    self->set_busy(1);
    self->set_skip_exit(2);
    self->set_action(ACTION_FOLLOW);
    self->set_user(-1);
    self->set_act_type(ACT_GUARD);
    self->stats()->force_command(COMMAND_WALK, 1, 1, 0);
    ASSERT_TRUE(self->act());

    self->set_action(0);
    self->set_skip_exit(0);
    self->stats()->clear_command();
    self->setxy(64, 64);
    self->set_ani_type(ANI_WALK);
    self->set_busy(0);
    self->stats()->set_frozen_delay(0);
    self->set_charm_left((0));
    self->stats()->set_bit_flags(BIT_FORESTWALK, 0);
    self->set_act_type(ACT_DIE);
    (void)self->act();
    ASSERT_TRUE(self->dead() == 1);
}

TEST(LivingUnit, living_r14_lines_371_375_380_419_433_440_shove_walk_and_animate_fallback)
{
    LivingR14Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 64, 64);
    living* ally = add_living(fx, FAMILY_SOLDIER, 0, 80, 64);
    ASSERT_TRUE(self && ally);

    assign_basic_ani(self);
    assign_basic_ani(ally);

    ally->set_act_type(ACT_GUARD);
    ASSERT_EQ(1, self->shove(ally, 1, 0));
    ASSERT_TRUE(ally->stats()->has_commands());

    self->set_curdir(FACE_LEFT);
    self->setxy(0, 0);
    self->stats()->set_bit_flags(BIT_ANIMATE, 1);
    ASSERT_TRUE(!self->walk(-1.0f, 0.0f));

    self->set_curdir(FACE_RIGHT);
    self->setxy(10, 10);
    self->set_collide_ob(ally);
    self->stats()->set_bit_flags(BIT_ANIMATE, 1);
    (void)self->walk(1.0f, 0.0f);
}
} // namespace detail_living_r14
