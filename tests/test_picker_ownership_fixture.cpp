#include "graph.h"
#include "guy.h"
#include "test_framework.h"

#include <memory>

extern screen* myscreen;
extern guy* current_guy;
extern guy* old_guy;
extern Sint32 editguy;
extern short current_team_num;

Sint32 cycle_team_guy(Sint32 whichway);

namespace {
struct PickerOwnershipFixtureState {
    guy* saved_current_guy = nullptr;
    guy* saved_old_guy = nullptr;
    Sint32 saved_editguy = 0;
    short saved_current_team_num = 0;
    int saved_team_size = 0;
    std::unique_ptr<guy> saved_slot0;
};

PickerOwnershipFixtureState g_fixture;

void setup_picker_ownership_fixture()
{
    g_fixture.saved_current_guy = current_guy;
    g_fixture.saved_old_guy = old_guy;
    g_fixture.saved_editguy = editguy;
    g_fixture.saved_current_team_num = current_team_num;
    g_fixture.saved_team_size = myscreen->save_data.team_size;
    g_fixture.saved_slot0.reset(myscreen->save_data.team_list[0].release());

    current_guy = nullptr;
    old_guy = nullptr;
    editguy = 0;
    current_team_num = 0;

    myscreen->save_data.team_list[0].reset(new guy(FAMILY_SOLDIER));
    myscreen->save_data.team_list[0]->name = "FIXTURE_SOLDIER";
    myscreen->save_data.team_list[0]->teamnum = 2;
    myscreen->save_data.team_size = 1;
}

void teardown_picker_ownership_fixture()
{
    if (current_guy && current_guy != g_fixture.saved_current_guy) {
        delete current_guy;
    }

    myscreen->save_data.team_list[0].reset(g_fixture.saved_slot0.release());

    current_guy = g_fixture.saved_current_guy;
    old_guy = g_fixture.saved_old_guy;
    editguy = g_fixture.saved_editguy;
    current_team_num = g_fixture.saved_current_team_num;
    myscreen->save_data.team_size = static_cast<unsigned char>(g_fixture.saved_team_size);
}
} // namespace

void test_picker_cycle_team_guy_ownership_copy_and_alias()
{
    const int original_strength = myscreen->save_data.team_list[0]->strength;

    const Sint32 rc = cycle_team_guy(0);
    TEST_ASSERT_EQ(4, rc, "cycle_team_guy should return OK");

    TEST_ASSERT(current_guy != nullptr, "cycle_team_guy should create current_guy copy");
    TEST_ASSERT(old_guy == myscreen->save_data.team_list[0].get(),
        "old_guy should alias team_list slot");
    TEST_ASSERT(current_guy != old_guy,
        "current_guy should be a distinct owned copy");

    current_guy->strength += 7;
    TEST_ASSERT_EQ(original_strength, myscreen->save_data.team_list[0]->strength,
        "editing current_guy must not mutate team_list until save/edit");
    TEST_ASSERT_EQ(myscreen->save_data.team_list[0]->teamnum, current_team_num,
        "current_team_num should follow selected guy team");
}
REGISTER_TEST_WITH_FIXTURE(test_picker_cycle_team_guy_ownership_copy_and_alias,
    setup_picker_ownership_fixture, teardown_picker_ownership_fixture);
