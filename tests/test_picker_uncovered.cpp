#include "SDL.h"
#include <openglad/interface/input/button.h>
#include <openglad/interface/screen.h>
#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/ui/picker_common.h>
#include "test_framework.h"
#include "test_input_helpers.h"

#include <string>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)
#include <openglad/platform/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


Sint32 name_guy(Sint32 arg);
Sint32 do_pick_campaign(Sint32 arg1);
Sint32 do_set_scen_level(Sint32 arg1);
Sint32 change_teamnum(Sint32 arg);
Sint32 change_hire_teamnum(Sint32 arg);

namespace
{
struct PickerStateGuard
{
    std::unique_ptr<guy> saved_current;
    guy* saved_old = nullptr;
	Sint32 saved_editguy = 0;
	char saved_end = 0;
	unsigned char saved_team_size = 0;
	short saved_scen_num = 0;

    PickerStateGuard()
    {
        saved_current = std::move(og::runtime::current_session->current_guy_);
        saved_old = pks().old_guy;
        saved_editguy = og::runtime::current_session->editguy_;
        saved_end = og::runtime::current_session->myscreen_->world().end;
        saved_team_size = og::runtime::current_session->myscreen_->save_data.team_size;
        saved_scen_num = og::runtime::current_session->myscreen_->save_data.scen_num;
    }

    ~PickerStateGuard()
    {
        og::runtime::current_session->current_guy_ = std::move(saved_current);
        pks().old_guy = saved_old;
        og::runtime::current_session->editguy_ = saved_editguy;
        og::runtime::current_session->myscreen_->world().end = saved_end;
        og::runtime::current_session->myscreen_->save_data.team_size = saved_team_size;
        og::runtime::current_session->myscreen_->save_data.scen_num = saved_scen_num;
    }
};

struct TeamSlotGuard
{
	int slot;
	guy* saved;
	TeamSlotGuard(int slot_) : slot(slot_), saved(og::runtime::current_session->myscreen_->save_data.team_list[slot_].release()) {}
	~TeamSlotGuard() { og::runtime::current_session->myscreen_->save_data.team_list[slot].reset(saved); }
};

struct ButtonSlotGuard
{
	int slot;
	vbutton* saved;
	ButtonSlotGuard(int slot_) : slot(slot_), saved(og::runtime::current_session->allbuttons_[slot_]) {}
	~ButtonSlotGuard() { og::runtime::current_session->allbuttons_[slot] = saved; }
};

struct OwnedButtonReplacementGuard
{
    int slot;
    vbutton* saved;

    OwnedButtonReplacementGuard(int slot_, const char* name)
        : slot(slot_), saved(og::runtime::current_session->allbuttons_[slot_])
    {
        og::runtime::current_session->allbuttons_[slot] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, name, KEYSTATE_UNKNOWN);
    }

    ~OwnedButtonReplacementGuard()
    {
        delete og::runtime::current_session->allbuttons_[slot];
        og::runtime::current_session->allbuttons_[slot] = saved;
    }
};

int name_guy_injector(void*)
{
    og::runtime::ensure_thread_session();
    SDL_Delay(40);
    inject_key_press(SDLK_RETURN, 10);
    return 0;
}
} // namespace

void test_picker_name_guy_paths()
{
    PickerStateGuard guard;

    std::unique_ptr<guy> original_current = std::move(og::runtime::current_session->current_guy_);
    og::runtime::current_session->current_guy_ = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->current_guy_->name = "CURSORIG";

    SDL_Thread* rename_current_thread = SDL_CreateThread(name_guy_injector, "picker_name_current", nullptr);
    TEST_ASSERT(rename_current_thread != nullptr, "rename-current injector should be created");
    TEST_ASSERT_EQ(2, (int)name_guy(0), "name_guy(0) should return REDRAW");
    int thread_result = 0;
    SDL_WaitThread(rename_current_thread, &thread_result);
    TEST_ASSERT(og::runtime::current_session->current_guy_->name == "CURSORIG", "return without text should preserve current guy name");

    TeamSlotGuard slot_guard(0);
    og::runtime::current_session->editguy_ = 0;
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(new guy(FAMILY_MAGE));
    og::runtime::current_session->myscreen_->save_data.team_list[0]->name = "TEAMORIG";

    SDL_Thread* rename_team_thread = SDL_CreateThread(name_guy_injector, "picker_name_team", nullptr);
    TEST_ASSERT(rename_team_thread != nullptr, "rename-team injector should be created");
    TEST_ASSERT_EQ(2, (int)name_guy(1), "name_guy(1) should return REDRAW");
    SDL_WaitThread(rename_team_thread, &thread_result);
    TEST_ASSERT(og::runtime::current_session->myscreen_->save_data.team_list[0]->name == "TEAMORIG", "return without text should preserve team guy name");

    og::runtime::current_session->myscreen_->save_data.team_list[0].reset();
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(nullptr);
    og::runtime::current_session->current_guy_ = std::move(original_current);
}
REGISTER_TEST(test_picker_name_guy_paths);

void test_picker_edit_guy_paths()
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);

    // Empty session path: no team members to train.
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(nullptr);
    og::ui::TrainSession empty_session(og::runtime::current_session->myscreen_->save_data);
    TEST_ASSERT(empty_session.empty(), "session should be empty with no team");

    // Successful accept path.
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(new guy(FAMILY_SOLDIER));
    og::runtime::current_session->myscreen_->save_data.team_list[0]->teamnum = 0;
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
    og::runtime::current_session->myscreen_->save_data.m_totalcash[0] = 100000;

    og::ui::TrainSession session(og::runtime::current_session->myscreen_->save_data);
    TEST_ASSERT(!session.empty(), "session should not be empty");

    session.increase_stat(og::ui::TrainSession::Stat::Strength, 1);
    TEST_ASSERT(session.current_cost() > 0, "cost should be positive after stat increase");
    TEST_ASSERT(session.accept(), "accept should succeed with enough gold");

    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(nullptr);
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
}
REGISTER_TEST(test_picker_edit_guy_paths);

void test_picker_campaign_and_level_wrappers_cancel_fast()
{
    PickerStateGuard guard;

    og::runtime::current_session->myscreen_->world().end = 1;
    int scen_before = og::runtime::current_session->myscreen_->save_data.scen_num;

    TEST_ASSERT_EQ(2, (int)do_pick_campaign(0), "do_pick_campaign should return REDRAW");
    TEST_ASSERT_EQ(2, (int)do_set_scen_level(0), "do_set_scen_level should return REDRAW");
    TEST_ASSERT_EQ(scen_before, (int)og::runtime::current_session->myscreen_->save_data.scen_num, "cancel paths should preserve selected scenario");
}
REGISTER_TEST(test_picker_campaign_and_level_wrappers_cancel_fast);

void test_picker_team_wraps_on_negative_step()
{
    PickerStateGuard guard;
    OwnedButtonReplacementGuard button2_guard(2, "b2");
    OwnedButtonReplacementGuard button18_guard(18, "b18");
    const short saved_team_num = og::runtime::current_session->current_team_num_;

    og::runtime::current_session->current_guy_ = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->current_guy_->teamnum = static_cast<short>(0);
    og::runtime::current_session->current_team_num_ = static_cast<short>(0);

    TEST_ASSERT_EQ(4, (int)change_teamnum(-1), "change_teamnum should return OK");
    TEST_ASSERT_EQ(3, (int)og::runtime::current_session->current_guy_->teamnum, "change_teamnum should wrap 0 -> 3 for arg -1");
    TEST_ASSERT_STR_EQ("Playing on Team 4", og::runtime::current_session->allbuttons_[18]->label.c_str(), "team label should wrap to Team 4");

    og::runtime::current_session->current_guy_->teamnum = static_cast<short>(0);
    og::runtime::current_session->current_team_num_ = static_cast<short>(0);
    TEST_ASSERT_EQ(4, (int)change_hire_teamnum(-1), "change_hire_teamnum should return OK");
    TEST_ASSERT_EQ(3, (int)og::runtime::current_session->current_team_num_, "change_hire_teamnum should wrap 0 -> 3 for arg -1");
    TEST_ASSERT_EQ(3, (int)og::runtime::current_session->current_guy_->teamnum, "change_hire_teamnum should mirror to current_guy");
    TEST_ASSERT_STR_EQ("Hiring for Team 4", og::runtime::current_session->allbuttons_[2]->label.c_str(), "hire label should wrap to Team 4");

    og::runtime::current_session->current_team_num_ = saved_team_num;
}
REGISTER_TEST(test_picker_team_wraps_on_negative_step);
