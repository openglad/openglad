#include "input/button.h"
#include "graph.h"
#include "test_framework.h"
#include "test_input_helpers.h"

#include <string>
#include <memory>

extern screen* myscreen;
extern std::unique_ptr<guy> current_guy;
extern guy* old_guy;
extern Sint32 editguy;

Sint32 name_guy(Sint32 arg);
Sint32 edit_guy(Sint32 arg1);
Sint32 do_pick_campaign(Sint32 arg1);
Sint32 do_set_scen_level(Sint32 arg1);

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
        saved_current = std::move(current_guy);
        saved_old = old_guy;
        saved_editguy = editguy;
        saved_end = myscreen->end;
        saved_team_size = myscreen->save_data.team_size;
        saved_scen_num = myscreen->save_data.scen_num;
    }

    ~PickerStateGuard()
    {
        current_guy = std::move(saved_current);
        old_guy = saved_old;
        editguy = saved_editguy;
        myscreen->end = saved_end;
        myscreen->save_data.team_size = saved_team_size;
        myscreen->save_data.scen_num = saved_scen_num;
    }
};

struct TeamSlotGuard
{
	int slot;
	guy* saved;
	TeamSlotGuard(int slot_) : slot(slot_), saved(myscreen->save_data.team_list[slot_].release()) {}
	~TeamSlotGuard() { myscreen->save_data.team_list[slot].reset(saved); }
};

struct ButtonSlotGuard
{
	int slot;
	vbutton* saved;
	ButtonSlotGuard(int slot_) : slot(slot_), saved(allbuttons[slot_]) {}
	~ButtonSlotGuard() { allbuttons[slot] = saved; }
};

int name_guy_injector(void*)
{
    SDL_Delay(40);
    inject_key_press(SDLK_RETURN, 10);
    return 0;
}
} // namespace

void test_picker_name_guy_paths()
{
    PickerStateGuard guard;

    std::unique_ptr<guy> original_current = std::move(current_guy);
    current_guy = std::make_unique<guy>(FAMILY_SOLDIER);
    current_guy->name = "CURSORIG";

    SDL_Thread* rename_current_thread = SDL_CreateThread(name_guy_injector, "picker_name_current", nullptr);
    TEST_ASSERT(rename_current_thread != nullptr, "rename-current injector should be created");
    TEST_ASSERT_EQ(2, (int)name_guy(0), "name_guy(0) should return REDRAW");
    int thread_result = 0;
    SDL_WaitThread(rename_current_thread, &thread_result);
    TEST_ASSERT(current_guy->name == "CURSORIG", "return without text should preserve current guy name");

    TeamSlotGuard slot_guard(0);
    editguy = 0;
    myscreen->save_data.team_list[0].reset(new guy(FAMILY_MAGE));
    myscreen->save_data.team_list[0]->name = "TEAMORIG";

    SDL_Thread* rename_team_thread = SDL_CreateThread(name_guy_injector, "picker_name_team", nullptr);
    TEST_ASSERT(rename_team_thread != nullptr, "rename-team injector should be created");
    TEST_ASSERT_EQ(2, (int)name_guy(1), "name_guy(1) should return REDRAW");
    SDL_WaitThread(rename_team_thread, &thread_result);
    TEST_ASSERT(myscreen->save_data.team_list[0]->name == "TEAMORIG", "return without text should preserve team guy name");

    myscreen->save_data.team_list[0].reset();
    myscreen->save_data.team_list[0].reset(nullptr);
    current_guy = std::move(original_current);
}
REGISTER_TEST(test_picker_name_guy_paths);

void test_picker_edit_guy_paths()
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);
    ButtonSlotGuard button_guard(18);

    // Null current_guy path.
    current_guy = nullptr;
    TEST_ASSERT_EQ(-1, (int)edit_guy(0), "edit_guy should fail when current_guy is null");

    // Missing team slot path.
    current_guy = std::make_unique<guy>(FAMILY_SOLDIER);
    auto old_guy_owned = std::make_unique<guy>(*current_guy);
    old_guy = old_guy_owned.get();
    current_guy->teamnum = 0;
    editguy = 0;
    myscreen->save_data.team_list[0].reset(nullptr);
    TEST_ASSERT_EQ(-1, (int)edit_guy(0), "edit_guy should fail when destination slot is empty");

    // Successful transfer path.
    myscreen->save_data.team_list[0].reset(new guy(FAMILY_SOLDIER));
    myscreen->save_data.team_list[0]->teamnum = 0;
    old_guy_owned = std::make_unique<guy>(*myscreen->save_data.team_list[0]);
    old_guy = old_guy_owned.get();
    current_guy->strength = myscreen->save_data.team_list[0]->strength + 1;
    myscreen->save_data.m_totalcash[0] = 100000;

    allbuttons[18] = new vbutton();
    allbuttons[18]->do_outline = 1;
    TEST_ASSERT_EQ(4, (int)edit_guy(0), "edit_guy should return OK on valid edit");
    TEST_ASSERT_EQ(0, (int)allbuttons[18]->do_outline, "edit_guy should clear team button outline");

    delete allbuttons[18];
    allbuttons[18] = nullptr;
    myscreen->save_data.team_list[0].reset();
    myscreen->save_data.team_list[0].reset(nullptr);
    old_guy = nullptr;
    current_guy.reset();
}
REGISTER_TEST(test_picker_edit_guy_paths);

void test_picker_campaign_and_level_wrappers_cancel_fast()
{
    PickerStateGuard guard;

    myscreen->end = 1;
    int scen_before = myscreen->save_data.scen_num;

    TEST_ASSERT_EQ(2, (int)do_pick_campaign(0), "do_pick_campaign should return REDRAW");
    TEST_ASSERT_EQ(2, (int)do_set_scen_level(0), "do_set_scen_level should return REDRAW");
    TEST_ASSERT_EQ(scen_before, (int)myscreen->save_data.scen_num, "cancel paths should preserve selected scenario");
}
REGISTER_TEST(test_picker_campaign_and_level_wrappers_cancel_fast);
