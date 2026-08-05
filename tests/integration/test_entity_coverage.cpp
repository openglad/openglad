#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/input_action.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/gameplay/sim_event_log.h>
#include <gtest/gtest.h>

#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace {

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

static void clear_level()
{
    og::runtime::current_session->myscreen_->world().delete_objects();
}

} // namespace

TEST(EntityCoverage, sim_input_reverse_switch_missing_old_control_keeps_old)
{
    auto orphan_up = make_living(0, 0);
    ASSERT_TRUE(orphan_up != nullptr) << "orphan control should exist";

    walker* control = orphan_up.get();
    control->set_act_type(ACT_CONTROL);
    control->stats()->set_hitpoints(21.0f);

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

    ASSERT_TRUE(control == orphan_up.get()) << "missing reverse entry should keep old control";
    ASSERT_TRUE(result.control_hp_changed) << "missing reverse entry should report hp";
    ASSERT_TRUE(result.control_hp == 21.0f) << "hp should match preserved control";
}


TEST(EntityCoverage, sim_input_shift_yell_default_action_branch)
{
    clear_level();

    auto control_up = make_living(0, 0);
    auto ally_up = make_living(0, -1);
    ASSERT_TRUE(control_up != nullptr && ally_up != nullptr) << "walkers should be created";

    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    control->set_action(99);

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

    ASSERT_EQ(0, control->action()) << "default shift+yell branch should reset action to 0";
    ASSERT_TRUE(result.new_control == control) << "control should be preserved";

    clear_level();
}


TEST(EntityCoverage, sim_input_switch_char_wraps_to_prior_candidate)
{
    clear_level();

    auto candidate_up = make_living(0, -1);
    auto control_up = make_living(0, 0);
    ASSERT_TRUE(candidate_up != nullptr && control_up != nullptr) << "walkers should be created";

    walker* candidate = candidate_up.get();
    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    candidate->stats()->set_hitpoints(77.0f);

    // candidate is before control; forward scan should use wrap-around loop.
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

    ASSERT_TRUE(control == candidate) << "switch should wrap to prior candidate";
    ASSERT_TRUE(result.control_hp == 77.0f) << "reported hp should match wrapped candidate";

    clear_level();
}


TEST(EntityCoverage, sim_input_idle_animation_cycle_wraps)
{
    clear_level();

    auto control_up = make_living(0, 0);
    ASSERT_TRUE(control_up != nullptr) << "control should exist";

    walker* control = control_up.get();
    control->set_act_type(ACT_CONTROL);
    control->stats()->set_bit_flags(BIT_ANIMATE, 1);
    control->set_curdir(0);
    control->set_cycle(0);
    // Create local mutable animation data (global tables are const)
    static signed char test_seq[] = {9, -1};
    static const signed char * test_ani_rows[] = {test_seq, test_seq, test_seq, test_seq,
                                                   test_seq, test_seq, test_seq, test_seq,
                                                   test_seq, test_seq, test_seq, test_seq,
                                                   test_seq, test_seq, test_seq, test_seq};
    control->ani = test_ani_rows;

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control_up));

    InputState input;
    input.clear();

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    og::sim::SimEventLog log;

    SimInputResult result = sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);

    ASSERT_EQ(0, (int)control->cycle()) << "idle animation should wrap cycle at -1 sentinel";
    ASSERT_EQ(9, (int)control->frame()) << "idle animation should set frame from ani table";
    ASSERT_TRUE(result.new_control == control) << "idle path should keep control";

    clear_level();
}

