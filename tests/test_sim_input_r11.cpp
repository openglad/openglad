#include <openglad/sim/sim_input_handler.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/input/input_state.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <memory>
#include <array>

#include "unit/unit.h"

namespace {

struct SimInputFixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    SimInputFixture()
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(SimInputFixture& fx, unsigned char team, signed char user = -1)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    fx.level.wire_entity(w.get());
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
    fx.level.oblist.push_back(std::move(w));
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

OG_UNIT_TEST(test_sim_input_r11_switch_char_error_and_wrap_paths)
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
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(result.control_hp_changed);
    OG_ASSERT(control == &orphan);

    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    debounce.changedchar = 0;
    result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(result.control_hp_changed);
    OG_ASSERT(control == &orphan);

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
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(control == b || control == c);

    input.clear();
    debounce.changedchar = 0;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    control = a;
    a->user = 0;
    b->user = -1;
    c->user = -1;
    result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(control == c || control == b);
}

OG_UNIT_TEST(test_sim_input_r11_switch_special_yell_and_action_default)
{
    SimInputFixture fx;
    walker* control = add_living(fx, 0, 0);
    walker* ally = add_living(fx, 0, -1);
    OG_ASSERT(control != nullptr && ally != nullptr);
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
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(control->current_special == 1); // wrap due to NONE

    input.clear();
    debounce.changedspec = 0;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(result.play_sound == SOUND_YO);
    OG_ASSERT(control->yo_delay == 30);
    OG_ASSERT(ally->leader == control);

    // Shift+Yell default case in switch(control->action)
    input.clear();
    control->action = 99;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(control->action == 0);
}

OG_UNIT_TEST(test_sim_input_r11_animate_movement_and_bit_animate_paths)
{
    SimInputFixture fx;
    walker* control = add_living(fx, 0, 0);
    OG_ASSERT(control != nullptr);
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
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(result.new_control == control);

    // movement branch (walkstep)
    control->ani_type = ANI_WALK;
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    const float x_before = control->xpos;
    result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(control->xpos >= x_before);

    // BIT_ANIMATE idle animation branch + frame reset path
    control->stats()->set_bit_flags(BIT_ANIMATE, 1);
    control->cycle = 0;
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(control->cycle == 0);

    // held fire path
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Fire)] = true;
    (void)sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
}
