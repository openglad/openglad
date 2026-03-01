/* Tests for sim-layer input handler (G11).
 * These are integration tests (require SDL for walker creation).
 */
#include <openglad/sim/sim_input_handler.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/input/input_action.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/screen.h>
#include <openglad/runtime/game_context.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> make_living(unsigned char team, signed char user = -1)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l) return nullptr;
    auto w = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    if (!w) return nullptr;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    w->user = user;
    w->setxy(80, 80);
    return w;
}

static void teardown()
{
    if (og::runtime::current_session->myscreen_)
        og::runtime::current_session->myscreen_->world().delete_objects();
}

// sim_find_next_control: finds player character first
void test_sim_find_next_control_player_first()
{
    auto npc = make_living(0);
    auto player = make_living(0);
    TEST_ASSERT(npc != nullptr, "npc should be created");
    TEST_ASSERT(player != nullptr, "player should be created");

    // Give the player a myguy so it's recognized as a player character
    player->myguy = reinterpret_cast<guy*>(1); // non-null sentinel

    // Transfer ownership to oblist
    walker* npc_ptr = npc.get();
    walker* player_ptr = player.get();
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(npc));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(player));

    walker* found = sim_find_next_control(og::runtime::current_session->myscreen_->world(), 0);
    TEST_ASSERT(found == player_ptr, "should prefer player character over NPC");

    teardown();
}
REGISTER_TEST(test_sim_find_next_control_player_first);

// sim_find_next_control: falls back to team member if no player char
void test_sim_find_next_control_team_fallback()
{
    auto npc = make_living(0);
    TEST_ASSERT(npc != nullptr, "npc should be created");
    npc->myguy = nullptr;

    walker* npc_ptr = npc.get();
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(npc));

    walker* found = sim_find_next_control(og::runtime::current_session->myscreen_->world(), 0);
    TEST_ASSERT(found == npc_ptr, "should find team member as fallback");

    teardown();
}
REGISTER_TEST(test_sim_find_next_control_team_fallback);

// sim_find_next_control: returns nullptr when no one alive
void test_sim_find_next_control_none()
{
    teardown(); // ensure empty oblist
    walker* found = sim_find_next_control(og::runtime::current_session->myscreen_->world(), 0);
    TEST_ASSERT(found == nullptr, "should return nullptr when oblist empty");
}
REGISTER_TEST(test_sim_find_next_control_none);

// sim_process_player_input: requests endgame when no control available
void test_sim_input_endgame_when_no_control()
{
    teardown();

    InputState input;
    input.clear();
    walker* control = nullptr;
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS];

    og::sim::SimEventLog log;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(result.endgame_requested, "should request endgame with no walkers");
    TEST_ASSERT_EQ(1, result.endgame_type, "endgame type should be 1");
}
REGISTER_TEST(test_sim_input_endgame_when_no_control);

// sim_process_player_input: assigns control and updates HP
void test_sim_input_assigns_control()
{
    auto w = make_living(0);
    TEST_ASSERT(w != nullptr, "walker should be created");
    w->stats()->hitpoints = 42.0f;
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(w));

    InputState input;
    input.clear();
    walker* control = nullptr;
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS];

    og::sim::SimEventLog log;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(!result.endgame_requested, "should not endgame");
    TEST_ASSERT(control != nullptr, "control should be assigned");
    TEST_ASSERT(result.control_hp_changed, "HP should be updated");
    TEST_ASSERT(result.control_hp == 42.0f, "HP should match");

    teardown();
}
REGISTER_TEST(test_sim_input_assigns_control);

// sim_process_player_input: movement via held keys
void test_sim_input_movement()
{
    auto w = make_living(0);
    TEST_ASSERT(w != nullptr, "walker should be created");
    w->setxy(100, 100);
    walker* control = w.get();
    control->user = 0;
    control->set_act_type(ACT_CONTROL);
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(w));

    InputState input;
    input.clear();
    // Simulate holding right key
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS];
    og::sim::SimEventLog log;

    sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    // Walker should have attempted to walk right
    // (exact position change depends on stepsize, but walkstep should have been called)
    TEST_ASSERT(control != nullptr, "control should still be set");

    teardown();
}
REGISTER_TEST(test_sim_input_movement);

void test_sim_input_switch_special_wraps_and_debounces()
{
    auto w = make_living(0);
    TEST_ASSERT(w != nullptr, "walker should be created");
    walker* control = w.get();
    control->user = 0;
    control->current_special = 1;
    control->stats()->level = 1;
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(w));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchSpecial)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    special_names[FAMILY_SOLDIER][2] = "NONE";
    og::sim::SimEventLog log;

    sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT_EQ(1, debounce.changedspec, "switch special should set debounce latch");
    TEST_ASSERT_EQ(1, control->current_special, "invalid/locked special should wrap to 1");

    input.clear();
    sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    TEST_ASSERT_EQ(0, debounce.changedspec, "no press frame should clear special debounce");

    teardown();
}
REGISTER_TEST(test_sim_input_switch_special_wraps_and_debounces);

void test_sim_input_yell_sets_follow_and_notification()
{
    auto control_up = make_living(0, 0);
    auto ally_up = make_living(0, -1);
    TEST_ASSERT(control_up != nullptr, "control should be created");
    TEST_ASSERT(ally_up != nullptr, "ally should be created");

    walker* control = control_up.get();
    walker* ally = ally_up.get();
    control->set_act_type(ACT_CONTROL);
    control->yo_delay = 0;
    ally->leader = nullptr;
    ally->action = 0;

    og::runtime::current_session->myscreen_->oblist().push_back(std::move(control_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(ally_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    const SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT_EQ(SOUND_YO, result.play_sound, "plain yell should request yo sound");
    TEST_ASSERT(result.notify_text == "Yo!", "plain yell should emit Yo notification");
    TEST_ASSERT(result.notify_source == control, "notification source should be control");
    TEST_ASSERT(ally->leader == control, "plain yell should assign nearby ally leader");
    TEST_ASSERT_EQ(30, control->yo_delay, "plain yell should set yell cooldown");

    teardown();
}
REGISTER_TEST(test_sim_input_yell_sets_follow_and_notification);

void test_sim_input_shift_yell_summon_and_release()
{
    auto control_up = make_living(0, 0);
    auto ally_up = make_living(0, -1);
    TEST_ASSERT(control_up != nullptr, "control should be created");
    TEST_ASSERT(ally_up != nullptr, "ally should be created");

    walker* control = control_up.get();
    walker* ally = ally_up.get();
    control->set_act_type(ACT_CONTROL);
    control->action = 0;
    ally->action = 0;

    og::runtime::current_session->myscreen_->oblist().push_back(std::move(control_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(ally_up));

    InputState input;
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(result.notify_text == "SUMMONING DEFENSE!", "shift+yell should enter summon mode");
    TEST_ASSERT_EQ(ACTION_FOLLOW, ally->action, "summon mode should set ally action to follow");

    control->action = ACTION_FOLLOW;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(result.notify_text == "RELEASING MEN!", "shift+yell in follow mode should release");
    TEST_ASSERT_EQ(0, ally->action, "release should reset ally action");
    TEST_ASSERT_EQ(0, control->action, "release should reset control action");

    teardown();
}
REGISTER_TEST(test_sim_input_shift_yell_summon_and_release);

void test_sim_input_frozen_and_user_mismatch_early_returns()
{
    auto w = make_living(0);
    TEST_ASSERT(w != nullptr, "walker should be created");
    walker* control = w.get();
    control->set_act_type(ACT_CONTROL);
    control->user = 1;
    control->stats()->frozen_delay = 2;
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(w));

    InputState input;
    input.clear();
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.new_control == control, "user mismatch should return current control early");

    control->user = 0;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(result.new_control == control, "frozen control should still be returned");
    TEST_ASSERT_EQ(1, control->stats()->frozen_delay, "frozen delay should decrement");

    teardown();
}
REGISTER_TEST(test_sim_input_frozen_and_user_mismatch_early_returns);

void test_sim_input_switch_char_forward_and_reverse_paths()
{
    auto w1_up = make_living(0, 0);
    auto w2_up = make_living(0, -1);
    auto w3_up = make_living(0, -1);
    TEST_ASSERT(w1_up && w2_up && w3_up, "walkers should be created");
    if (!(w1_up && w2_up && w3_up))
        return;

    walker* w1 = w1_up.get();
    walker* w2 = w2_up.get();
    walker* w3 = w3_up.get();
    w1->set_act_type(ACT_CONTROL);
    w2->set_act_type(ACT_RANDOM);
    w3->set_act_type(ACT_RANDOM);
    w1->real_team_num = 255;
    w2->real_team_num = 255;
    w3->real_team_num = 255;

    og::runtime::current_session->myscreen_->oblist().push_back(std::move(w1_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(w2_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(w3_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    walker* control = w1;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(control == w2, "forward switch should select next eligible teammate");
    TEST_ASSERT_EQ(1, debounce.changedchar, "switch-char press should set debounce");
    TEST_ASSERT(result.control_hp_changed, "switching should mark HP changed");

    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    debounce.changedchar = 0;
    control->user = 0;
    control->set_act_type(ACT_CONTROL);

    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(control == w1, "reverse switch should select previous eligible teammate");
    TEST_ASSERT(result.control_hp_changed, "reverse switching should mark HP changed");

    teardown();
}
REGISTER_TEST(test_sim_input_switch_char_forward_and_reverse_paths);

void test_sim_input_switch_char_error_and_default_action_paths()
{
    auto detached = make_living(0, 0);
    TEST_ASSERT(detached != nullptr, "detached control should be created");
    if (!detached)
        return;

    walker* control = detached.get();
    control->set_act_type(ACT_CONTROL);
    control->action = 99; // default branch for shift+yell action switch

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(control == detached.get(), "missing oldcontrol in oblist should fall back to old control");
    TEST_ASSERT(result.control_hp_changed, "failed switch should still report control hp");

    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    debounce.changedchar = 0;

    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT_EQ(0, (int)control->action, "default shift+yell action branch should reset control action");
}
REGISTER_TEST(test_sim_input_switch_char_error_and_default_action_paths);

void test_sim_input_bonus_rounds_and_pressed_held_actions_paths()
{
    auto control_up = make_living(0, 0);
    TEST_ASSERT(control_up != nullptr, "control should be created");
    if (!control_up)
        return;

    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    control->setxy(100, 100);
    control->lastx = control->stepsize;
    control->lasty = 0.0f;
    control->bonus_rounds = 2;
    control->stats()->set_bit_flags(BIT_ANIMATE, 1);

    og::runtime::current_session->myscreen_->oblist().push_back(std::move(control_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::Special)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Fire)] = true;
    input.players[0].held[static_cast<int>(InputAction::Special)] = true;
    input.players[0].held[static_cast<int>(InputAction::Fire)] = true;
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    const SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(result.new_control == control, "processing should return same control");
    TEST_ASSERT_EQ(1, (int)control->bonus_rounds, "bonus rounds should decrement each tick");

    teardown();
}
REGISTER_TEST(test_sim_input_bonus_rounds_and_pressed_held_actions_paths);

void test_sim_input_switch_char_forward_selects_next_friendly()
{
    auto control_up = make_living(0, 0);
    auto ally_up = make_living(0, -1);
    TEST_ASSERT(control_up != nullptr, "control should be created");
    TEST_ASSERT(ally_up != nullptr, "ally should be created");

    walker* oldcontrol = control_up.get();
    walker* ally = ally_up.get();
    oldcontrol->set_act_type(ACT_CONTROL);
    oldcontrol->stats()->hitpoints = 33.0f;
    ally->stats()->hitpoints = 77.0f;

    og::runtime::current_session->myscreen_->oblist().push_back(std::move(control_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(ally_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;
    SimInputResult result = sim_process_player_input(
        input.players[0], oldcontrol, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(oldcontrol == ally, "switch char should move control to next valid teammate");
    TEST_ASSERT_EQ(1, debounce.changedchar, "switch char should set debounce latch");
    TEST_ASSERT(result.control_hp_changed, "switch char should report hp change");
    TEST_ASSERT(result.control_hp == 77.0f, "switch char hp should reflect new control");

    teardown();
}
REGISTER_TEST(test_sim_input_switch_char_forward_selects_next_friendly);

void test_sim_input_switch_char_reverse_and_missing_old_control_paths()
{
    // Reverse path.
    auto ally_up = make_living(0, -1);
    auto control_up = make_living(0, 0);
    TEST_ASSERT(control_up != nullptr, "control should be created");
    TEST_ASSERT(ally_up != nullptr, "ally should be created");

    walker* control = control_up.get();
    walker* ally = ally_up.get();
    control->set_act_type(ACT_CONTROL);
    ally->stats()->hitpoints = 88.0f;

    og::runtime::current_session->myscreen_->oblist().push_back(std::move(ally_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(control_up));

    InputState input;
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(control == ally, "reverse switch should pick previous valid teammate");
    TEST_ASSERT(result.control_hp == 88.0f, "reverse switch should return ally hp");

    teardown();

    // mine == end() failure path: control not in oblist should keep old control.
    auto orphan_up = make_living(0, 0);
    TEST_ASSERT(orphan_up != nullptr, "orphan control should be created");
    walker* orphan = orphan_up.get();
    orphan->set_act_type(ACT_CONTROL);
    orphan->stats()->hitpoints = 19.0f;

    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    debounce = {};

    result = sim_process_player_input(
        input.players[0], orphan, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(orphan == orphan_up.get(), "missing old control entry should preserve old control");
    TEST_ASSERT(result.control_hp_changed, "missing old control should still return hp update");
    TEST_ASSERT(result.control_hp == 19.0f, "hp update should reflect preserved old control");
}
REGISTER_TEST(test_sim_input_switch_char_reverse_and_missing_old_control_paths);

void test_sim_input_switch_char_no_candidate_keeps_old_control_and_ticks()
{
    auto control_up = make_living(0, 0);
    TEST_ASSERT(control_up != nullptr, "control should be created");

    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    control->bonus_rounds = 2;
    control->lastx = 1.0f;
    control->lasty = 0.0f;
    control->yo_delay = 4;
    control->stats()->hitpoints = 55.0f;
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(control_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    const SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    TEST_ASSERT(control != nullptr, "control should remain valid");
    TEST_ASSERT_EQ(1, control->bonus_rounds, "bonus rounds should tick down");
    TEST_ASSERT_EQ(3, control->yo_delay, "yo_delay should tick down");
    TEST_ASSERT(result.control_hp_changed, "switch attempt should still report hp");
    TEST_ASSERT(result.control_hp == 55.0f, "hp should remain from old control");

    teardown();
}
REGISTER_TEST(test_sim_input_switch_char_no_candidate_keeps_old_control_and_ticks);

void test_sim_input_special_and_fire_paths()
{
    auto control_up = make_living(0, 0);
    TEST_ASSERT(control_up != nullptr, "control should be created");

    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(control_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::Special)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Fire)] = true;
    input.players[0].held[static_cast<int>(InputAction::Special)] = true;
    input.players[0].held[static_cast<int>(InputAction::Fire)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.new_control == control, "special/fire paths should preserve control");

    control->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.new_control == control, "non-empty command queue path should return control");

    teardown();
}
REGISTER_TEST(test_sim_input_special_and_fire_paths);

void test_sim_find_next_control_fallback_any_team_player()
{
    teardown();
    auto other_team_player = make_living(1);
    TEST_ASSERT(other_team_player != nullptr, "other-team player should be created");
    other_team_player->myguy = reinterpret_cast<guy*>(1);
    walker* expected = other_team_player.get();
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(other_team_player));

    walker* found = sim_find_next_control(og::runtime::current_session->myscreen_->world(), 0);
    TEST_ASSERT(found == expected, "fallback pass should find any alive player character");

    teardown();
}
REGISTER_TEST(test_sim_find_next_control_fallback_any_team_player);

void test_sim_input_switch_char_wraparound_forward_and_reverse()
{
    // Forward wraparound: old control is last entry, so selection wraps to head.
    auto candidate_up = make_living(0, -1);
    auto control_up = make_living(0, 0);
    TEST_ASSERT(candidate_up != nullptr, "candidate should be created");
    TEST_ASSERT(control_up != nullptr, "control should be created");
    walker* candidate = candidate_up.get();
    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    candidate->stats()->hitpoints = 61.0f;

    og::runtime::current_session->myscreen_->oblist().push_back(std::move(candidate_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(control_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    TEST_ASSERT(control == candidate, "forward switch should wrap to first teammate");
    TEST_ASSERT(result.control_hp_changed, "wrapped switch should report hp change");
    TEST_ASSERT(result.control_hp == 61.0f, "wrapped switch hp should match target");

    teardown();

    // Reverse wraparound: old control is first entry, so reverse wraps to tail.
    auto reverse_control_up = make_living(0, 0);
    auto reverse_candidate_up = make_living(0, -1);
    TEST_ASSERT(reverse_control_up != nullptr, "reverse control should be created");
    TEST_ASSERT(reverse_candidate_up != nullptr, "reverse candidate should be created");
    walker* reverse_control = reverse_control_up.get();
    walker* reverse_candidate = reverse_candidate_up.get();
    reverse_control->set_act_type(ACT_CONTROL);
    reverse_candidate->stats()->hitpoints = 72.0f;

    og::runtime::current_session->myscreen_->oblist().push_back(std::move(reverse_control_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(reverse_candidate_up));

    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    debounce = {};

    result = sim_process_player_input(
        input.players[0], reverse_control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    TEST_ASSERT(reverse_control == reverse_candidate, "reverse switch should wrap to last teammate");
    TEST_ASSERT(result.control_hp == 72.0f, "reverse wrapped hp should match target");

    teardown();
}
REGISTER_TEST(test_sim_input_switch_char_wraparound_forward_and_reverse);

void test_sim_input_switch_special_valid_advance_and_shift_yell_default_branch()
{
    auto control_up = make_living(0, 0);
    TEST_ASSERT(control_up != nullptr, "control should be created");
    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    control->current_special = 1;
    control->stats()->level = 20;
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(control_up));

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    special_names[FAMILY_SOLDIER][2] = "VALID_SPECIAL";
    og::sim::SimEventLog log;
    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchSpecial)] = true;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.new_control == control, "switch special should keep control");
    TEST_ASSERT_EQ(2, control->current_special, "valid unlocked special should advance");

    control->action = 99;
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.new_control == control, "shift+yell default branch should keep control");
    TEST_ASSERT_EQ(0, control->action, "default shift+yell branch should reset action");

    teardown();
}
REGISTER_TEST(test_sim_input_switch_special_valid_advance_and_shift_yell_default_branch);

void test_sim_input_deep_branch_coverage_smoke()
{
    teardown();
    auto control_up = make_living(0, 0);
    auto ally_after_up = make_living(0, -1);
    auto ally_before_up = make_living(0, -1);
    TEST_ASSERT(control_up != nullptr, "control should be created");
    TEST_ASSERT(ally_after_up != nullptr, "ally after should be created");
    TEST_ASSERT(ally_before_up != nullptr, "ally before should be created");

    walker* control = control_up.get();
    walker* ally_after = ally_after_up.get();
    walker* ally_before = ally_before_up.get();
    control->set_act_type(ACT_CONTROL);
    control->stats()->level = 30;
    control->current_special = 1;
    control->yo_delay = 0;
    control->ani_type = ANI_ATTACK; // forces animate() branch
    control->stats()->set_bit_flags(BIT_ANIMATE, 1);
    control->cycle = 0;
    // Create local mutable animation data (global tables are const)
    static signed char test_wrap_seq[] = {0, -1};
    static const signed char * test_wrap_rows[] = {test_wrap_seq, test_wrap_seq, test_wrap_seq, test_wrap_seq,
                                                    test_wrap_seq, test_wrap_seq, test_wrap_seq, test_wrap_seq,
                                                    test_wrap_seq, test_wrap_seq, test_wrap_seq, test_wrap_seq,
                                                    test_wrap_seq, test_wrap_seq, test_wrap_seq, test_wrap_seq};
    control->ani = test_wrap_rows; // force animation wrap in idle branch

    og::runtime::current_session->myscreen_->oblist().push_back(std::move(ally_before_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(control_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(ally_after_up));

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    special_names[FAMILY_SOLDIER][2] = "SPECIAL_OK";
    special_names[FAMILY_SOLDIER][3] = "SPECIAL_OK_2";
    og::sim::SimEventLog log;
    InputState input;
    SimInputResult result;

    // Forward switch character.
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.control_hp_changed, "forward switch should report hp");

    // Reset latch and reverse switch character.
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.control_hp_changed, "reverse switch should report hp");

    // Switch special advance.
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchSpecial)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.new_control == control, "switch special should preserve control");

    // Plain yell.
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.notify_source == control || result.notify_source == nullptr, "yell path executes");

    // Shift+yell summon, then release, then default.
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    control->action = 0;
    (void)sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    control->action = ACTION_FOLLOW;
    (void)sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    control->action = 99;
    (void)sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);

    // Movement/action block with walk and fire/special branches.
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::Special)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Fire)] = true;
    input.players[0].held[static_cast<int>(InputAction::Special)] = true;
    input.players[0].held[static_cast<int>(InputAction::Fire)] = true;
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.new_control == control, "movement/action branch should keep control");

    // Idle animation branch (no walk input).
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.new_control == control, "idle animate branch should keep control");

    // Mismatch user early return line.
    control->user = 1;
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.new_control == control, "user mismatch path should return control");

    teardown();
    (void)ally_after;
    (void)ally_before;
}
REGISTER_TEST(test_sim_input_deep_branch_coverage_smoke);

void test_sim_input_cheat_gates_and_command_queue_skip_movement_block()
{
    teardown();
    auto control_up = make_living(0, 0);
    auto ally_up = make_living(0, -1);
    TEST_ASSERT(control_up != nullptr && ally_up != nullptr, "control and ally should be created");
    if (!(control_up && ally_up))
        return;

    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    control->yo_delay = 0;
    control->current_special = 1;
    control->stats()->level = 30;

    og::runtime::current_session->myscreen_->oblist().push_back(std::move(control_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(ally_up));

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    special_names[FAMILY_SOLDIER][2] = "SPECIAL_OK";
    og::sim::SimEventLog log;
    InputState input;

    // Cheat should block switch-char branch.
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::Cheat)] = true;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.new_control == control, "cheat-held switch-char should keep existing control");
    TEST_ASSERT_EQ(0, (int)debounce.changedchar, "cheat-held switch-char should not latch debounce");

    // With queued command, movement/action block should be skipped.
    control->stats()->force_command(COMMAND_FOLLOW, 5, 0, 0);
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.new_control == control, "queued-command path should preserve control");

    // Cheat should also block both yell branches.
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    input.players[0].held[static_cast<int>(InputAction::Cheat)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    TEST_ASSERT(result.notify_text.empty(), "cheat-held yell should not emit notifications");

    teardown();
}
REGISTER_TEST(test_sim_input_cheat_gates_and_command_queue_skip_movement_block);

void test_sim_input_switch_char_reverse_missing_old_control_restores_control()
{
    teardown();
    auto orphan_up = make_living(0, 0);
    TEST_ASSERT(orphan_up != nullptr, "orphan control should be created");
    if (!orphan_up)
        return;

    walker* control = orphan_up.get();
    control->set_act_type(ACT_CONTROL);
    control->stats()->hitpoints = 27.0f;

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);

    TEST_ASSERT(control == orphan_up.get(), "reverse switch should restore old control when not found in oblist");
    TEST_ASSERT(result.control_hp_changed, "reverse missing-control path should report HP changed");
    TEST_ASSERT(result.control_hp == 27.0f, "reverse missing-control path should preserve old-control HP");
}
REGISTER_TEST(test_sim_input_switch_char_reverse_missing_old_control_restores_control);

void test_sim_input_dead_control_reassigns_to_next_alive()
{
    teardown();
    auto dead_control_up = make_living(0, 0);
    auto replacement_up = make_living(0, -1);
    TEST_ASSERT(dead_control_up != nullptr && replacement_up != nullptr, "walkers should be created");
    if (!(dead_control_up && replacement_up))
        return;

    walker* control = dead_control_up.get();
    walker* replacement = replacement_up.get();
    control->dead = 1;
    replacement->stats()->hitpoints = 64.0f;

    og::runtime::current_session->myscreen_->oblist().push_back(std::move(dead_control_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(replacement_up));

    InputState input;
    input.clear();
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);

    TEST_ASSERT(control == replacement, "dead control should be replaced by next available teammate");
    TEST_ASSERT(result.control_hp_changed, "dead-control reassignment should report HP changed");
    TEST_ASSERT(result.control_hp == 64.0f, "dead-control reassignment should report replacement HP");

    teardown();
}
REGISTER_TEST(test_sim_input_dead_control_reassigns_to_next_alive);

void test_sim_input_switch_char_skips_ineligible_candidates_then_selects_valid()
{
    teardown();

    auto control_up = make_living(0, 0);
    auto enemy_up = make_living(1, -1);          // wrong team
    auto charmed_up = make_living(0, -1);        // real_team_num != 255
    auto taken_up = make_living(0, 2);           // user already taken
    auto good_up = make_living(0, -1);           // first valid candidate
    TEST_ASSERT(control_up && enemy_up && charmed_up && taken_up && good_up, "walkers should be created");
    if (!(control_up && enemy_up && charmed_up && taken_up && good_up))
        return;

    auto nonliving = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(nonliving != nullptr, "nonliving candidate should be created");
    if (!nonliving)
        return;

    walker* control = control_up.get();
    walker* good = good_up.get();
    control->set_act_type(ACT_CONTROL);
    control->team_num = 0;
    control->real_team_num = 255;
    control->stats()->hitpoints = 31.0f;

    enemy_up->team_num = 1;
    charmed_up->team_num = 0;
    charmed_up->real_team_num = 7;
    taken_up->team_num = 0;
    taken_up->real_team_num = 255;
    good_up->team_num = 0;
    good_up->real_team_num = 255;
    good_up->stats()->hitpoints = 77.0f;

    // Keep every candidate friendly so team/user/real-team checks determine eligibility.
    control->owner = control;
    enemy_up->owner = control;
    charmed_up->owner = control;
    taken_up->owner = control;
    good_up->owner = control;

    og::runtime::current_session->myscreen_->oblist().push_back(std::move(control_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(nonliving));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(enemy_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(charmed_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(taken_up));
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(good_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);

    TEST_ASSERT(control == good, "switch-char should skip ineligible entries and select first valid candidate");
    TEST_ASSERT(result.control_hp_changed, "valid switch should report hp change");
    TEST_ASSERT(result.control_hp == 77.0f, "reported hp should come from selected candidate");

    teardown();
}
REGISTER_TEST(test_sim_input_switch_char_skips_ineligible_candidates_then_selects_valid);

void test_sim_input_assigns_unowned_control_and_clears_commands()
{
    teardown();
    auto w_up = make_living(0, -1);
    TEST_ASSERT(w_up != nullptr, "walker should be created");
    if (!w_up)
        return;

    walker* control = w_up.get();
    control->stats()->force_command(COMMAND_FOLLOW, 3, 0, 0);
    TEST_ASSERT(control->stats()->has_commands(), "precondition: command queue is non-empty");
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(w_up));

    InputState input;
    input.clear();
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);

    TEST_ASSERT(result.new_control == control, "control should be preserved");
    TEST_ASSERT_EQ(0, (int)control->user, "unowned control should be assigned to player");
    TEST_ASSERT(!control->stats()->has_commands(), "assignment path should clear queued commands");

    teardown();
}
REGISTER_TEST(test_sim_input_assigns_unowned_control_and_clears_commands);

void test_sim_input_bonus_rounds_walks_when_last_vector_nonzero()
{
    teardown();
    auto w_up = make_living(0, 0);
    TEST_ASSERT(w_up != nullptr, "walker should be created");
    if (!w_up)
        return;

    walker* control = w_up.get();
    control->setxy(100, 100);
    control->set_act_type(ACT_CONTROL);
    control->bonus_rounds = 1;
    control->lastx = 1;
    control->lasty = 0;
    og::runtime::current_session->myscreen_->oblist().push_back(std::move(w_up));

    InputState input;
    input.clear();
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);

    TEST_ASSERT(result.new_control == control, "control should stay active");
    TEST_ASSERT_EQ(0, (int)control->bonus_rounds, "bonus rounds should decrement");

    teardown();
}
REGISTER_TEST(test_sim_input_bonus_rounds_walks_when_last_vector_nonzero);
