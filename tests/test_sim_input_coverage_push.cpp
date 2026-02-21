#include <openglad/sim/sim_input_handler.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/input/input_state.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>

#include <memory>
#include <string>

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
    auto w = std::make_unique<walker>();
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

} // namespace

OG_UNIT_TEST(test_sim_input_find_next_control_priorities)
{
    SimInputFixture fx;
    walker* team_npc = add_living(fx, 0, -1);
    walker* player_like = add_living(fx, 0, -1);
    OG_ASSERT(team_npc != nullptr);
    OG_ASSERT(player_like != nullptr);

    walker* found = sim_find_next_control(fx.level, 0);
    OG_ASSERT(found == team_npc);

    team_npc->user = 0;
    found = sim_find_next_control(fx.level, 0);
    OG_ASSERT(found == player_like);
}

OG_UNIT_TEST(test_sim_input_endgame_and_control_assignment_paths)
{
    SimInputFixture fx;
    walker* control = nullptr;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};

    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(result.endgame_requested);
    OG_ASSERT(result.endgame_type == 1);

    walker* w = add_living(fx, 0, -1);
    w->stats()->hitpoints = 37.0f;
    result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(!result.endgame_requested);
    OG_ASSERT(control == w);
    OG_ASSERT(result.control_hp_changed);
    OG_ASSERT(result.control_hp == 37.0f);
}

OG_UNIT_TEST(test_sim_input_switch_special_yell_and_mismatch_paths)
{
    SimInputFixture fx;
    walker* control = add_living(fx, 0, 0);
    walker* ally = add_living(fx, 0, -1);
    OG_ASSERT(control != nullptr);
    OG_ASSERT(ally != nullptr);
    control->set_act_type(ACT_CONTROL);
    ally->set_act_type(ACT_RANDOM);

    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    special_names[FAMILY_SOLDIER][2] = "NONE";

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchSpecial)] = true;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(debounce.changedspec == 1);
    OG_ASSERT(control->current_special == 1);
    OG_ASSERT(result.new_control == control);

    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    control->action = 0;
    result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(result.notify_text == "SUMMONING DEFENSE!");
    OG_ASSERT(ally->action == ACTION_FOLLOW);

    control->action = ACTION_FOLLOW;
    result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(result.notify_text == "RELEASING MEN!");
    OG_ASSERT(ally->action == 0);

    control->user = 1;
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(result.new_control == control);

    control->user = 0;
    control->stats()->frozen_delay = 1;
    result = sim_process_player_input(
        input.players[0], control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(control->stats()->frozen_delay == 0);
}
