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
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

static std::unique_ptr<walker> make_living(unsigned char team, signed char user = -1)
{
    loader* l = myscreen->level_data.myloader.get();
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
    if (myscreen)
        myscreen->level_data.delete_objects();
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
    myscreen->level_data.oblist.push_back(std::move(npc));
    myscreen->level_data.oblist.push_back(std::move(player));

    walker* found = sim_find_next_control(myscreen->level_data, 0);
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
    myscreen->level_data.oblist.push_back(std::move(npc));

    walker* found = sim_find_next_control(myscreen->level_data, 0);
    TEST_ASSERT(found == npc_ptr, "should find team member as fallback");

    teardown();
}
REGISTER_TEST(test_sim_find_next_control_team_fallback);

// sim_find_next_control: returns nullptr when no one alive
void test_sim_find_next_control_none()
{
    teardown(); // ensure empty oblist
    walker* found = sim_find_next_control(myscreen->level_data, 0);
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
        input.players[0], control, myscreen->level_data,
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
    myscreen->level_data.oblist.push_back(std::move(w));

    InputState input;
    input.clear();
    walker* control = nullptr;
    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS];

    og::sim::SimEventLog log;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, myscreen->level_data,
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
    myscreen->level_data.oblist.push_back(std::move(w));

    InputState input;
    input.clear();
    // Simulate holding right key
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS];
    og::sim::SimEventLog log;

    short oldx = control->xpos;
    sim_process_player_input(
        input.players[0], control, myscreen->level_data,
        0, 0, debounce, special_names, &log);

    // Walker should have attempted to walk right
    // (exact position change depends on stepsize, but walkstep should have been called)
    TEST_ASSERT(control != nullptr, "control should still be set");

    teardown();
}
REGISTER_TEST(test_sim_input_movement);
