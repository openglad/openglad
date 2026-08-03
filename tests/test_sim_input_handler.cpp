/* Tests for sim-layer input handler (G11).
 * These are integration tests (require SDL for walker creation).
 */
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/interface/input_action.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_context.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> make_living(unsigned char team, signed char user = -1)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l) return nullptr;
    auto w = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    if (!w) return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    w->set_user(user);
    w->setxy(80, 80);
    return w;
}

static void teardown()
{
    if (og::runtime::current_session->myscreen_)
        og::runtime::current_session->myscreen_->world().delete_objects();
}

// sim_find_next_control: finds player character first
TEST(SimInputHandler, sim_find_next_control_player_first)
{
    auto npc = make_living(0);
    auto player = make_living(0);
    ASSERT_TRUE(npc != nullptr) << "npc should be created";
    ASSERT_TRUE(player != nullptr) << "player should be created";

    // Give the player a myguy so it's recognized as a player character
    player->myguy = reinterpret_cast<guy*>(1); // non-null sentinel

    // Transfer ownership to oblist
    walker* player_ptr = player.get();
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(npc));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(player));

    walker* found = sim_find_next_control(og::runtime::current_session->myscreen_->world(), 0);
    ASSERT_TRUE(found == player_ptr) << "should prefer player character over NPC";

    teardown();
}


// sim_find_next_control: falls back to team member if no player char
TEST(SimInputHandler, sim_find_next_control_team_fallback)
{
    auto npc = make_living(0);
    ASSERT_TRUE(npc != nullptr) << "npc should be created";
    npc->myguy = nullptr;

    walker* npc_ptr = npc.get();
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(npc));

    walker* found = sim_find_next_control(og::runtime::current_session->myscreen_->world(), 0);
    ASSERT_TRUE(found == npc_ptr) << "should find team member as fallback";

    teardown();
}


// sim_find_next_control: returns nullptr when no one alive
TEST(SimInputHandler, sim_find_next_control_none)
{
    teardown(); // ensure empty oblist
    walker* found = sim_find_next_control(og::runtime::current_session->myscreen_->world(), 0);
    ASSERT_TRUE(found == nullptr) << "should return nullptr when oblist empty";
}


// sim_process_player_input: requests endgame when no control available
TEST(SimInputHandler, sim_input_endgame_when_no_control)
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

    ASSERT_TRUE(result.endgame_requested) << "should request endgame with no walkers";
    ASSERT_EQ(1, result.endgame_type) << "endgame type should be 1";
}


// sim_process_player_input: assigns control and updates HP
TEST(SimInputHandler, sim_input_assigns_control)
{
    auto w = make_living(0);
    ASSERT_TRUE(w != nullptr) << "walker should be created";
    w->stats()->set_hitpoints(42.0f);
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w));

    InputState input;
    input.clear();
    walker* control = nullptr;
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS];

    og::sim::SimEventLog log;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    ASSERT_TRUE(!result.endgame_requested) << "should not endgame";
    ASSERT_TRUE(control != nullptr) << "control should be assigned";
    ASSERT_TRUE(result.control_hp_changed) << "HP should be updated";
    ASSERT_TRUE(result.control_hp == 42.0f) << "HP should match";

    teardown();
}

// A respawning death must not run the normal auto-claim scan even when
// another same-team hero is available. Keeping the dead entity pointer is
// what preserves the authoritative control mapping through the countdown.
TEST(SimInputHandler, respawning_dead_hero_does_not_switch_to_teammate)
{
    auto dead_up = make_living(0, 0);
    auto teammate_up = make_living(0, -1);
    ASSERT_NE(nullptr, dead_up);
    ASSERT_NE(nullptr, teammate_up);

    dead_up->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    teammate_up->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    dead_up->set_act_type(ACT_CONTROL);
    dead_up->set_dead(1);
    walker* const dead_hero = dead_up.get();
    walker* const teammate = teammate_up.get();

    GameWorld& world =
        og::runtime::current_session->myscreen_->world();
    const short saved_respawn_mode = world.respawn_mode;
    const char saved_type = world.type;
    world.respawn_mode = 1;
    world.type = 0;
    world.oblist.push_back(std::move(dead_up));
    world.oblist.push_back(std::move(teammate_up));

    InputState input;
    input.clear();
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;
    walker* control = dead_hero;

    const SimInputResult retained = sim_process_player_input(
        input.players[0], control, world, 0, 0, debounce, special_names, &log);
    EXPECT_EQ(dead_hero, control);
    EXPECT_EQ(dead_hero, retained.new_control);
    EXPECT_EQ(-1, teammate->user());
    EXPECT_FALSE(retained.endgame_requested);

    // The same shape with respawns off retains legacy auto-switch behavior.
    world.respawn_mode = 0;
    const SimInputResult switched = sim_process_player_input(
        input.players[0], control, world, 0, 0, debounce, special_names, &log);
    EXPECT_EQ(teammate, control);
    EXPECT_TRUE(switched.control_hp_changed);

    world.respawn_mode = saved_respawn_mode;
    world.type = saved_type;
    teardown();
}


// sim_process_player_input: movement via held keys
TEST(SimInputHandler, sim_input_movement)
{
    auto w = make_living(0);
    ASSERT_TRUE(w != nullptr) << "walker should be created";
    w->setxy(100, 100);
    walker* control = w.get();
    control->set_user(0);
    control->set_act_type(ACT_CONTROL);
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w));

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
    ASSERT_TRUE(control != nullptr) << "control should still be set";

    teardown();
}


TEST(SimInputHandler, sim_input_switch_special_wraps_and_debounces)
{
    auto w = make_living(0);
    ASSERT_TRUE(w != nullptr) << "walker should be created";
    walker* control = w.get();
    control->set_user(0);
    control->set_current_special(1);
    control->stats()->set_level(1);
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w));

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

    ASSERT_EQ(1, debounce.changedspec) << "switch special should set debounce latch";
    ASSERT_EQ(1, control->current_special()) << "invalid/locked special should wrap to 1";

    input.clear();
    sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    ASSERT_EQ(0, debounce.changedspec) << "no press frame should clear special debounce";

    teardown();
}


TEST(SimInputHandler, sim_input_yell_sets_follow_and_notification)
{
    auto control_up = make_living(0, 0);
    auto ally_up = make_living(0, -1);
    ASSERT_TRUE(control_up != nullptr) << "control should be created";
    ASSERT_TRUE(ally_up != nullptr) << "ally should be created";

    walker* control = control_up.get();
    walker* ally = ally_up.get();
    control->set_act_type(ACT_CONTROL);
    control->set_yo_delay(0);
    ally->set_leader(nullptr);
    ally->set_action(0);

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(ally_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    const SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    ASSERT_EQ(SOUND_YO, result.play_sound) << "plain yell should request yo sound";
    ASSERT_TRUE(result.notify_text == "Yo!") << "plain yell should emit Yo notification";
    ASSERT_TRUE(result.notify_source == control) << "notification source should be control";
    ASSERT_TRUE(ally->leader() == control) << "plain yell should assign nearby ally leader";
    ASSERT_EQ(30, control->yo_delay()) << "plain yell should set yell cooldown";

    // The authoritative server drops the result cosmetics; the sound and the
    // HUD text only reach the client mirrors as sim events (issue #145).
    ASSERT_EQ(2u, log.size()) << "plain yell should emit exactly sound + notification";
    ASSERT_TRUE(log.events()[0].kind == og::sim::EventKind::PlaySound)
        << "first yell event should be the sound";
    ASSERT_EQ(static_cast<std::uint32_t>(SOUND_YO), log.events()[0].a)
        << "yell sound event should carry SOUND_YO";
    ASSERT_TRUE(log.events()[1].kind == og::sim::EventKind::Notification)
        << "second yell event should be the notification";
    ASSERT_TRUE(log.events()[1].text == "Yo!")
        << "yell notification event should carry the Yo text";

    teardown();
}


TEST(SimInputHandler, sim_input_yell_within_cooldown_emits_nothing)
{
    auto control_up = make_living(0, 0);
    ASSERT_TRUE(control_up != nullptr) << "control should be created";

    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    control->set_yo_delay(0);

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    ASSERT_EQ(2u, log.size()) << "the first yell should emit sound + notification";

    // Second yell inside the cooldown window: yo_delay ticks 30 -> 29 and the
    // yell branch is skipped, so nothing new reaches the mirrors.
    log.clear();
    const SimInputResult repeat = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    ASSERT_EQ(-1, repeat.play_sound) << "yell inside the cooldown should request no sound";
    ASSERT_TRUE(repeat.notify_text.empty()) << "yell inside the cooldown should not notify";
    ASSERT_EQ(0u, log.size()) << "yell inside the cooldown should emit no sim events";
    ASSERT_EQ(29, control->yo_delay()) << "the cooldown should still be ticking down";

    teardown();
}


TEST(SimInputHandler, sim_input_shift_yell_summon_and_release)
{
    auto control_up = make_living(0, 0);
    auto ally_up = make_living(0, -1);
    ASSERT_TRUE(control_up != nullptr) << "control should be created";
    ASSERT_TRUE(ally_up != nullptr) << "ally should be created";

    walker* control = control_up.get();
    walker* ally = ally_up.get();
    control->set_act_type(ACT_CONTROL);
    control->set_action(0);
    ally->set_action(0);

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(ally_up));

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

    ASSERT_TRUE(result.notify_text == "SUMMONING DEFENSE!") << "shift+yell should enter summon mode";
    ASSERT_EQ(ACTION_FOLLOW, ally->action()) << "summon mode should set ally action to follow";
    ASSERT_EQ(1u, log.size()) << "summon should emit one event and no sound";
    ASSERT_TRUE(log.events()[0].kind == og::sim::EventKind::Notification)
        << "summon event should be a notification";
    ASSERT_TRUE(log.events()[0].text == "SUMMONING DEFENSE!")
        << "summon notification should carry the summon text";

    log.clear();
    control->set_action(ACTION_FOLLOW);
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    ASSERT_TRUE(result.notify_text == "RELEASING MEN!") << "shift+yell in follow mode should release";
    ASSERT_EQ(1u, log.size()) << "release should emit one event and no sound";
    ASSERT_TRUE(log.events()[0].kind == og::sim::EventKind::Notification)
        << "release event should be a notification";
    ASSERT_TRUE(log.events()[0].text == "RELEASING MEN!")
        << "release notification should carry the release text";
    ASSERT_EQ(0, ally->action()) << "release should reset ally action";
    ASSERT_EQ(0, control->action()) << "release should reset control action";

    teardown();
}


TEST(SimInputHandler, sim_input_frozen_and_user_mismatch_early_returns)
{
    auto w = make_living(0);
    ASSERT_TRUE(w != nullptr) << "walker should be created";
    walker* control = w.get();
    control->set_act_type(ACT_CONTROL);
    control->set_user(1);
    control->stats()->set_frozen_delay(2);
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w));

    InputState input;
    input.clear();
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    ASSERT_TRUE(result.new_control == control) << "user mismatch should return current control early";

    control->set_user(0);
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    ASSERT_TRUE(result.new_control == control) << "frozen control should still be returned";
    ASSERT_EQ(1, control->stats()->frozen_delay()) << "frozen delay should decrement";

    teardown();
}


TEST(SimInputHandler, sim_input_switch_char_forward_and_reverse_paths)
{
    auto w1_up = make_living(0, 0);
    auto w2_up = make_living(0, -1);
    auto w3_up = make_living(0, -1);
    ASSERT_TRUE(w1_up && w2_up && w3_up) << "walkers should be created";
    if (!(w1_up && w2_up && w3_up))
        return;

    walker* w1 = w1_up.get();
    walker* w2 = w2_up.get();
    walker* w3 = w3_up.get();
    w1->set_act_type(ACT_CONTROL);
    w2->set_act_type(ACT_RANDOM);
    w3->set_act_type(ACT_RANDOM);
    w1->set_real_team_num(255);
    w2->set_real_team_num(255);
    w3->set_real_team_num(255);

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w1_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w2_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w3_up));

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

    ASSERT_TRUE(control == w2) << "forward switch should select next eligible teammate";
    ASSERT_EQ(1, debounce.changedchar) << "switch-char press should set debounce";
    ASSERT_TRUE(result.control_hp_changed) << "switching should mark HP changed";

    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    debounce.changedchar = 0;
    control->set_user(0);
    control->set_act_type(ACT_CONTROL);

    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    ASSERT_TRUE(control == w1) << "reverse switch should select previous eligible teammate";
    ASSERT_TRUE(result.control_hp_changed) << "reverse switching should mark HP changed";

    teardown();
}


TEST(SimInputHandler, sim_input_switch_char_error_and_default_action_paths)
{
    auto detached = make_living(0, 0);
    ASSERT_TRUE(detached != nullptr) << "detached control should be created";
    if (!detached)
        return;

    walker* control = detached.get();
    control->set_act_type(ACT_CONTROL);
    control->set_action(99); // default branch for shift+yell action switch

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    ASSERT_TRUE(control == detached.get()) << "missing oldcontrol in oblist should fall back to old control";
    ASSERT_TRUE(result.control_hp_changed) << "failed switch should still report control hp";

    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    debounce.changedchar = 0;

    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    ASSERT_EQ(0, (int)control->action()) << "default shift+yell action branch should reset control action";
}


TEST(SimInputHandler, sim_input_bonus_rounds_and_pressed_held_actions_paths)
{
    auto control_up = make_living(0, 0);
    ASSERT_TRUE(control_up != nullptr) << "control should be created";
    if (!control_up)
        return;

    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    control->setxy(100, 100);
    control->set_lastx(control->stepsize());
    control->set_lasty(0.0f);
    control->set_bonus_rounds(2);
    control->stats()->set_bit_flags(BIT_ANIMATE, 1);

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));

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

    ASSERT_TRUE(result.new_control == control) << "processing should return same control";
    ASSERT_EQ(1, (int)control->bonus_rounds()) << "bonus rounds should decrement each tick";

    teardown();
}


TEST(SimInputHandler, sim_input_switch_char_forward_selects_next_friendly)
{
    auto control_up = make_living(0, 0);
    auto ally_up = make_living(0, -1);
    ASSERT_TRUE(control_up != nullptr) << "control should be created";
    ASSERT_TRUE(ally_up != nullptr) << "ally should be created";

    walker* oldcontrol = control_up.get();
    walker* ally = ally_up.get();
    oldcontrol->set_act_type(ACT_CONTROL);
    oldcontrol->stats()->set_hitpoints(33.0f);
    ally->stats()->set_hitpoints(77.0f);

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(ally_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;
    SimInputResult result = sim_process_player_input(
        input.players[0], oldcontrol, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    ASSERT_TRUE(oldcontrol == ally) << "switch char should move control to next valid teammate";
    ASSERT_EQ(1, debounce.changedchar) << "switch char should set debounce latch";
    ASSERT_TRUE(result.control_hp_changed) << "switch char should report hp change";
    ASSERT_TRUE(result.control_hp == 77.0f) << "switch char hp should reflect new control";

    teardown();
}


TEST(SimInputHandler, sim_input_switch_char_reverse_and_missing_old_control_paths)
{
    // Reverse path.
    auto ally_up = make_living(0, -1);
    auto control_up = make_living(0, 0);
    ASSERT_TRUE(control_up != nullptr) << "control should be created";
    ASSERT_TRUE(ally_up != nullptr) << "ally should be created";

    walker* control = control_up.get();
    walker* ally = ally_up.get();
    control->set_act_type(ACT_CONTROL);
    ally->stats()->set_hitpoints(88.0f);

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(ally_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));

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

    ASSERT_TRUE(control == ally) << "reverse switch should pick previous valid teammate";
    ASSERT_TRUE(result.control_hp == 88.0f) << "reverse switch should return ally hp";

    teardown();

    // mine == end() failure path: control not in oblist should keep old control.
    auto orphan_up = make_living(0, 0);
    ASSERT_TRUE(orphan_up != nullptr) << "orphan control should be created";
    walker* orphan = orphan_up.get();
    orphan->set_act_type(ACT_CONTROL);
    orphan->stats()->set_hitpoints(19.0f);

    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    debounce = {};

    result = sim_process_player_input(
        input.players[0], orphan, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    ASSERT_TRUE(orphan == orphan_up.get()) << "missing old control entry should preserve old control";
    ASSERT_TRUE(result.control_hp_changed) << "missing old control should still return hp update";
    ASSERT_TRUE(result.control_hp == 19.0f) << "hp update should reflect preserved old control";
}


TEST(SimInputHandler, sim_input_switch_char_no_candidate_keeps_old_control_and_ticks)
{
    auto control_up = make_living(0, 0);
    ASSERT_TRUE(control_up != nullptr) << "control should be created";

    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    control->set_bonus_rounds(2);
    control->set_lastx(1.0f);
    control->set_lasty(0.0f);
    control->set_yo_delay(4);
    control->stats()->set_hitpoints(55.0f);
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    const SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    ASSERT_TRUE(control != nullptr) << "control should remain valid";
    ASSERT_EQ(1, control->bonus_rounds()) << "bonus rounds should tick down";
    ASSERT_EQ(3, control->yo_delay()) << "yo_delay should tick down";
    ASSERT_TRUE(result.control_hp_changed) << "switch attempt should still report hp";
    ASSERT_TRUE(result.control_hp == 55.0f) << "hp should remain from old control";

    teardown();
}


TEST(SimInputHandler, sim_input_special_and_fire_paths)
{
    auto control_up = make_living(0, 0);
    ASSERT_TRUE(control_up != nullptr) << "control should be created";

    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));

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
    ASSERT_TRUE(result.new_control == control) << "special/fire paths should preserve control";

    control->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    ASSERT_TRUE(result.new_control == control) << "non-empty command queue path should return control";

    teardown();
}


TEST(SimInputHandler, sim_find_next_control_never_claims_another_team)
{
    teardown();
    auto other_team_player = make_living(1);
    ASSERT_TRUE(other_team_player != nullptr) << "other-team player should be created";
    other_team_player->myguy = reinterpret_cast<guy*>(1);
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(other_team_player));

    walker* found = sim_find_next_control(og::runtime::current_session->myscreen_->world(), 0);
    ASSERT_EQ(nullptr, found)
        << "a wiped seat must never take control of an enemy-color character";

    teardown();
}

TEST(SimInputHandler, sim_find_next_control_skips_claimed_fallback_player)
{
    teardown();
    auto claimed_other_team_player = make_living(1, 2);
    ASSERT_TRUE(claimed_other_team_player != nullptr)
        << "claimed other-team player should be created";
    claimed_other_team_player->myguy = reinterpret_cast<guy*>(1);
    og::runtime::current_session->myscreen_->world().oblist.push_back(
        std::move(claimed_other_team_player));

    walker* found = sim_find_next_control(
        og::runtime::current_session->myscreen_->world(), 0);
    ASSERT_TRUE(found == nullptr)
        << "claimed characters should not be reassigned";

    teardown();
}


TEST(SimInputHandler, sim_input_switch_char_wraparound_forward_and_reverse)
{
    // Forward wraparound: old control is last entry, so selection wraps to head.
    auto candidate_up = make_living(0, -1);
    auto control_up = make_living(0, 0);
    ASSERT_TRUE(candidate_up != nullptr) << "candidate should be created";
    ASSERT_TRUE(control_up != nullptr) << "control should be created";
    walker* candidate = candidate_up.get();
    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    candidate->stats()->set_hitpoints(61.0f);

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(candidate_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    ASSERT_TRUE(control == candidate) << "forward switch should wrap to first teammate";
    ASSERT_TRUE(result.control_hp_changed) << "wrapped switch should report hp change";
    ASSERT_TRUE(result.control_hp == 61.0f) << "wrapped switch hp should match target";

    teardown();

    // Reverse wraparound: old control is first entry, so reverse wraps to tail.
    auto reverse_control_up = make_living(0, 0);
    auto reverse_candidate_up = make_living(0, -1);
    ASSERT_TRUE(reverse_control_up != nullptr) << "reverse control should be created";
    ASSERT_TRUE(reverse_candidate_up != nullptr) << "reverse candidate should be created";
    walker* reverse_control = reverse_control_up.get();
    walker* reverse_candidate = reverse_candidate_up.get();
    reverse_control->set_act_type(ACT_CONTROL);
    reverse_candidate->stats()->set_hitpoints(72.0f);

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(reverse_control_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(reverse_candidate_up));

    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    debounce = {};

    result = sim_process_player_input(
        input.players[0], reverse_control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    ASSERT_TRUE(reverse_control == reverse_candidate) << "reverse switch should wrap to last teammate";
    ASSERT_TRUE(result.control_hp == 72.0f) << "reverse wrapped hp should match target";

    teardown();
}


TEST(SimInputHandler, sim_input_switch_special_valid_advance_and_shift_yell_default_branch)
{
    auto control_up = make_living(0, 0);
    ASSERT_TRUE(control_up != nullptr) << "control should be created";
    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    control->set_current_special(1);
    control->stats()->set_level(20);
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));

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
    ASSERT_TRUE(result.new_control == control) << "switch special should keep control";
    ASSERT_EQ(2, control->current_special()) << "valid unlocked special should advance";

    control->set_action(99);
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    ASSERT_TRUE(result.new_control == control) << "shift+yell default branch should keep control";
    ASSERT_EQ(0, control->action()) << "default shift+yell branch should reset action";

    teardown();
}


TEST(SimInputHandler, sim_input_deep_branch_coverage_smoke)
{
    teardown();
    auto control_up = make_living(0, 0);
    auto ally_after_up = make_living(0, -1);
    auto ally_before_up = make_living(0, -1);
    ASSERT_TRUE(control_up != nullptr) << "control should be created";
    ASSERT_TRUE(ally_after_up != nullptr) << "ally after should be created";
    ASSERT_TRUE(ally_before_up != nullptr) << "ally before should be created";

    walker* control = control_up.get();
    walker* ally_after = ally_after_up.get();
    walker* ally_before = ally_before_up.get();
    control->set_act_type(ACT_CONTROL);
    control->stats()->set_level(30);
    control->set_current_special(1);
    control->set_yo_delay(0);
    control->set_ani_type(ANI_ATTACK); // forces animate() branch
    control->stats()->set_bit_flags(BIT_ANIMATE, 1);
    control->set_cycle(0);
    // Create local mutable animation data (global tables are const)
    static signed char test_wrap_seq[] = {0, -1};
    static const signed char * test_wrap_rows[] = {test_wrap_seq, test_wrap_seq, test_wrap_seq, test_wrap_seq,
                                                    test_wrap_seq, test_wrap_seq, test_wrap_seq, test_wrap_seq,
                                                    test_wrap_seq, test_wrap_seq, test_wrap_seq, test_wrap_seq,
                                                    test_wrap_seq, test_wrap_seq, test_wrap_seq, test_wrap_seq};
    control->ani = test_wrap_rows; // force animation wrap in idle branch

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(ally_before_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(ally_after_up));

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
    ASSERT_TRUE(result.control_hp_changed) << "forward switch should report hp";

    // Reset latch and reverse switch character.
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    ASSERT_TRUE(result.control_hp_changed) << "reverse switch should report hp";

    // Switch special advance.
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchSpecial)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    ASSERT_TRUE(result.new_control == control) << "switch special should preserve control";

    // Plain yell.
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    ASSERT_EQ(control, result.notify_source) << "plain yell should report the controlled walker as notification source";

    // Shift+yell summon, then release, then default.
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    control->set_action(0);
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    ASSERT_EQ("SUMMONING DEFENSE!", result.notify_text) << "shift+yell should summon friendly units";
    ASSERT_EQ(control, result.notify_source) << "summon notification should come from the controlled walker";
    control->set_action(ACTION_FOLLOW);
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    ASSERT_EQ("RELEASING MEN!", result.notify_text) << "shift+yell while following should release units";
    ASSERT_EQ(0, control->action()) << "release branch should clear follow action";
    control->set_action(99);
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    ASSERT_TRUE(result.notify_text.empty()) << "default shift+yell branch should not claim a notification";
    ASSERT_EQ(0, control->action()) << "default shift+yell branch should reset unknown actions";

    // Movement/action block with walk and fire/special branches.
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::Special)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Fire)] = true;
    input.players[0].held[static_cast<int>(InputAction::Special)] = true;
    input.players[0].held[static_cast<int>(InputAction::Fire)] = true;
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    ASSERT_TRUE(result.new_control == control) << "movement/action branch should keep control";

    // Idle animation branch (no walk input).
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    ASSERT_TRUE(result.new_control == control) << "idle animate branch should keep control";

    // Mismatch user early return line.
    control->set_user(1);
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    ASSERT_TRUE(result.new_control == control) << "user mismatch path should return control";

    teardown();
    (void)ally_after;
    (void)ally_before;
}


TEST(SimInputHandler, sim_input_cheat_gates_and_command_queue_skip_movement_block)
{
    teardown();
    auto control_up = make_living(0, 0);
    auto ally_up = make_living(0, -1);
    ASSERT_TRUE(control_up != nullptr && ally_up != nullptr) << "control and ally should be created";
    if (!(control_up && ally_up))
        return;

    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    control->set_yo_delay(0);
    control->set_current_special(1);
    control->stats()->set_level(30);

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(ally_up));

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
    ASSERT_TRUE(result.new_control == control) << "cheat-held switch-char should keep existing control";
    ASSERT_EQ(0, (int)debounce.changedchar) << "cheat-held switch-char should not latch debounce";

    // With queued command, movement/action block should be skipped.
    control->stats()->force_command(COMMAND_FOLLOW, 5, 0, 0);
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    ASSERT_TRUE(result.new_control == control) << "queued-command path should preserve control";

    // Cheat should also block both yell branches.
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    input.players[0].held[static_cast<int>(InputAction::Cheat)] = true;
    result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);
    ASSERT_TRUE(result.notify_text.empty()) << "cheat-held yell should not emit notifications";

    teardown();
}


TEST(SimInputHandler, sim_input_switch_char_reverse_missing_old_control_restores_control)
{
    teardown();
    auto orphan_up = make_living(0, 0);
    ASSERT_TRUE(orphan_up != nullptr) << "orphan control should be created";
    if (!orphan_up)
        return;

    walker* control = orphan_up.get();
    control->set_act_type(ACT_CONTROL);
    control->stats()->set_hitpoints(27.0f);

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);

    ASSERT_TRUE(control == orphan_up.get()) << "reverse switch should restore old control when not found in oblist";
    ASSERT_TRUE(result.control_hp_changed) << "reverse missing-control path should report HP changed";
    ASSERT_TRUE(result.control_hp == 27.0f) << "reverse missing-control path should preserve old-control HP";
}


TEST(SimInputHandler, sim_input_dead_control_reassigns_to_next_alive)
{
    teardown();
    auto dead_control_up = make_living(0, 0);
    auto replacement_up = make_living(0, -1);
    ASSERT_TRUE(dead_control_up != nullptr && replacement_up != nullptr) << "walkers should be created";
    if (!(dead_control_up && replacement_up))
        return;

    walker* control = dead_control_up.get();
    walker* replacement = replacement_up.get();
    control->set_dead(1);
    replacement->stats()->set_hitpoints(64.0f);

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(dead_control_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(replacement_up));

    InputState input;
    input.clear();
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);

    ASSERT_TRUE(control == replacement) << "dead control should be replaced by next available teammate";
    ASSERT_TRUE(result.control_hp_changed) << "dead-control reassignment should report HP changed";
    ASSERT_TRUE(result.control_hp == 64.0f) << "dead-control reassignment should report replacement HP";

    teardown();
}


TEST(SimInputHandler, sim_input_switch_char_skips_ineligible_candidates_then_selects_valid)
{
    teardown();

    auto control_up = make_living(0, 0);
    auto enemy_up = make_living(1, -1);          // wrong team
    auto charmed_up = make_living(0, -1);        // real_team_num != 255
    auto taken_up = make_living(0, 2);           // user already taken
    auto good_up = make_living(0, -1);           // first valid candidate
    ASSERT_TRUE(control_up && enemy_up && charmed_up && taken_up && good_up) << "walkers should be created";
    if (!(control_up && enemy_up && charmed_up && taken_up && good_up))
        return;

    auto nonliving = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(nonliving != nullptr) << "nonliving candidate should be created";
    if (!nonliving)
        return;

    walker* control = control_up.get();
    walker* good = good_up.get();
    control->set_act_type(ACT_CONTROL);
    control->set_team_num(0);
    control->set_real_team_num(255);
    control->stats()->set_hitpoints(31.0f);

    enemy_up->set_team_num(1);
    charmed_up->set_team_num(0);
    charmed_up->set_real_team_num(7);
    taken_up->set_team_num(0);
    taken_up->set_real_team_num(255);
    good_up->set_team_num(0);
    good_up->set_real_team_num(255);
    good_up->stats()->set_hitpoints(77.0f);

    // Keep every candidate friendly so team/user/real-team checks determine eligibility.
    control->set_owner(control);
    enemy_up->set_owner(control);
    charmed_up->set_owner(control);
    taken_up->set_owner(control);
    good_up->set_owner(control);

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(nonliving));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(enemy_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(charmed_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(taken_up));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(good_up));

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);

    ASSERT_TRUE(control == good) << "switch-char should skip ineligible entries and select first valid candidate";
    ASSERT_TRUE(result.control_hp_changed) << "valid switch should report hp change";
    ASSERT_TRUE(result.control_hp == 77.0f) << "reported hp should come from selected candidate";

    teardown();
}


TEST(SimInputHandler, sim_input_assigns_unowned_control_and_clears_commands)
{
    teardown();
    auto w_up = make_living(0, -1);
    ASSERT_TRUE(w_up != nullptr) << "walker should be created";
    if (!w_up)
        return;

    walker* control = w_up.get();
    control->stats()->force_command(COMMAND_FOLLOW, 3, 0, 0);
    ASSERT_TRUE(control->stats()->has_commands()) << "precondition: command queue is non-empty";
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w_up));

    InputState input;
    input.clear();
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);

    ASSERT_TRUE(result.new_control == control) << "control should be preserved";
    ASSERT_EQ(0, (int)control->user()) << "unowned control should be assigned to player";
    ASSERT_TRUE(!control->stats()->has_commands()) << "assignment path should clear queued commands";

    teardown();
}


TEST(SimInputHandler, sim_input_bonus_rounds_walks_when_last_vector_nonzero)
{
    teardown();
    auto w_up = make_living(0, 0);
    ASSERT_TRUE(w_up != nullptr) << "walker should be created";
    if (!w_up)
        return;

    walker* control = w_up.get();
    control->setxy(100, 100);
    control->set_act_type(ACT_CONTROL);
    control->set_bonus_rounds(1);
    control->set_lastx(1);
    control->set_lasty(0);
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w_up));

    InputState input;
    input.clear();
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(), 0, 0, debounce, special_names, &log);

    ASSERT_TRUE(result.new_control == control) << "control should stay active";
    ASSERT_EQ(0, (int)control->bonus_rounds()) << "bonus rounds should decrement";

    teardown();
}
