#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/input_state.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/legacy/base.h>
#include <memory>
#include <string>
#include <gtest/gtest.h>
#include <openglad/gameplay/living.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <array>
#include "test_gameplay_context_scope.h"

// --- From test_sim_input_coverage_push.cpp ---
namespace detail_sim_input_coverage_push {
namespace {

struct SimInputFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    SimInputFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(SimInputFixture& fx, unsigned char team, signed char user = -1)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(80, 80);
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    w->user = user;
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

} // namespace

TEST(SimInputUnit, sim_input_find_next_control_priorities)
{
    SimInputFixture fx;
    walker* team_npc = add_living(fx, 0, -1);
    walker* player_like = add_living(fx, 0, -1);
    ASSERT_TRUE(team_npc != nullptr);
    ASSERT_TRUE(player_like != nullptr);

    walker* found = sim_find_next_control(fx.level.world(), 0);
    ASSERT_TRUE(found == team_npc);

    team_npc->user = 0;
    found = sim_find_next_control(fx.level.world(), 0);
    ASSERT_TRUE(found == player_like);
}

TEST(SimInputUnit, sim_input_endgame_and_control_assignment_paths)
{
    SimInputFixture fx;
    walker* control = nullptr;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};

    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.endgame_requested);
    ASSERT_TRUE(result.endgame_type == 1);

    walker* w = add_living(fx, 0, -1);
    w->stats()->hitpoints = 37.0f;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(!result.endgame_requested);
    ASSERT_TRUE(control == w);
    ASSERT_TRUE(result.control_hp_changed);
    ASSERT_TRUE(result.control_hp == 37.0f);
}

TEST(SimInputUnit, sim_input_switch_special_yell_and_mismatch_paths)
{
    SimInputFixture fx;
    walker* control = add_living(fx, 0, 0);
    walker* ally = add_living(fx, 0, -1);
    ASSERT_TRUE(control != nullptr);
    ASSERT_TRUE(ally != nullptr);
    control->set_act_type(ACT_CONTROL);
    ally->set_act_type(ACT_RANDOM);

    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    special_names[FAMILY_SOLDIER][2] = "NONE";

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchSpecial)] = true;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(debounce.changedspec == 1);
    ASSERT_TRUE(control->current_special == 1);
    ASSERT_TRUE(result.new_control == control);

    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    control->action = 0;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.notify_text == "SUMMONING DEFENSE!");
    ASSERT_TRUE(ally->action == ACTION_FOLLOW);

    control->action = ACTION_FOLLOW;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.notify_text == "RELEASING MEN!");
    ASSERT_TRUE(ally->action == 0);

    control->user = 1;
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.new_control == control);

    control->user = 0;
    control->stats()->frozen_delay = 1;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control->stats()->frozen_delay == 0);
}
} // namespace detail_sim_input_coverage_push

// --- From test_sim_input_r11.cpp ---
namespace detail_sim_input_r11 {
namespace {

struct SimInputFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    SimInputFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(SimInputFixture& fx, unsigned char team, signed char user = -1)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(80, 80);
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    w->user = user;
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

void assign_basic_ani(walker* w)
{
    constexpr int kAniRows = NUM_FACINGS * (ANI_SLIME_SPLIT + 1);
    static std::array<std::array<signed char, 3>, kAniRows> seqs{};
    static std::array<signed char*, kAniRows> rows{};
    for (int i = 0; i < kAniRows; ++i)
    {
        seqs[i][0] = static_cast<signed char>(i % NUM_FACINGS);
        seqs[i][1] = -1;
        seqs[i][2] = -1;
        rows[i] = seqs[i].data();
    }
    w->ani = rows.data();
}

} // namespace

TEST(SimInputUnit, sim_input_r11_switch_char_error_and_wrap_paths)
{
    SimInputFixture fx;
    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    InputState input;
    input.clear();

    walker orphan;
    orphan.user = 0;
    orphan.set_order_family(Order::Living, FAMILY_SOLDIER);
    walker* control = &orphan;

    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.control_hp_changed);
    ASSERT_TRUE(control == &orphan);

    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    debounce.changedchar = 0;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.control_hp_changed);
    ASSERT_TRUE(control == &orphan);

    // Forward wrap and reverse wrap over eligible entries.
    walker* a = add_living(fx, 0, -1);
    walker* b = add_living(fx, 0, -1);
    walker* c = add_living(fx, 0, -1);
    a->user = 0;
    control = a;

    input.clear();
    debounce.changedchar = 0;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control == b || control == c);

    input.clear();
    debounce.changedchar = 0;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    control = a;
    a->user = 0;
    b->user = -1;
    c->user = -1;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control == c || control == b);
}

TEST(SimInputUnit, sim_input_r11_switch_special_yell_and_action_default)
{
    SimInputFixture fx;
    walker* control = add_living(fx, 0, 0);
    walker* ally = add_living(fx, 0, -1);
    ASSERT_TRUE(control != nullptr && ally != nullptr);
    control->set_act_type(ACT_CONTROL);
    ally->set_act_type(ACT_RANDOM);

    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    special_names[FAMILY_SOLDIER][1] = "A";
    special_names[FAMILY_SOLDIER][2] = "NONE";

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchSpecial)] = true;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control->current_special == 1); // wrap due to NONE

    input.clear();
    debounce.changedspec = 0;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.play_sound == SOUND_YO);
    ASSERT_TRUE(control->yo_delay == 30);
    ASSERT_TRUE(ally->leader == control);

    // Shift+Yell default case in switch(control->action)
    input.clear();
    control->action = 99;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control->action == 0);
}

TEST(SimInputUnit, sim_input_r11_animate_movement_and_bit_animate_paths)
{
    SimInputFixture fx;
    walker* control = add_living(fx, 0, 0);
    ASSERT_TRUE(control != nullptr);
    control->set_act_type(ACT_CONTROL);
    assign_basic_ani(control);

    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    InputState input;

    // ani_type != ANI_WALK branch
    control->ani_type = ANI_ATTACK;
    control->cycle = 0;
    input.clear();
    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.new_control == control);

    // movement branch (walkstep)
    control->ani_type = ANI_WALK;
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    const float x_before = control->xpos;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control->xpos >= x_before);

    // BIT_ANIMATE idle animation branch + frame reset path
    control->stats()->set_bit_flags(BIT_ANIMATE, 1);
    control->cycle = 0;
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control->cycle == 0);

    // held fire path
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Fire)] = true;
    (void)sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
}
} // namespace detail_sim_input_r11
