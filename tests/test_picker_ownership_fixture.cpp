#include <openglad/gameplay/guy.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_common.h>
#include "test_framework.h"

#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace {
struct OwnershipFixtureState {
    int saved_team_size = 0;
    std::unique_ptr<guy> saved_slot0;
};

OwnershipFixtureState g_fixture;

void setup_picker_ownership_fixture()
{
    g_fixture.saved_team_size = og::runtime::current_session->myscreen_->save_data.team_size;
    g_fixture.saved_slot0.reset(og::runtime::current_session->myscreen_->save_data.team_list[0].release());

    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(new guy(FAMILY_SOLDIER));
    og::runtime::current_session->myscreen_->save_data.team_list[0]->name = "FIXTURE_SOLDIER";
    og::runtime::current_session->myscreen_->save_data.team_list[0]->teamnum = 2;
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
}

void teardown_picker_ownership_fixture()
{
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(g_fixture.saved_slot0.release());
    og::runtime::current_session->myscreen_->save_data.team_size = static_cast<unsigned char>(g_fixture.saved_team_size);
}
} // namespace

class PickerOwnershipFixture {
public:
    void SetUp()
    {
        setup_picker_ownership_fixture();
    }

    void TearDown()
    {
        teardown_picker_ownership_fixture();
    }
};

TEST_F(PickerOwnershipFixture, picker_cycle_team_guy_ownership_copy_and_alias)
{
    const int original_strength = og::runtime::current_session->myscreen_->save_data.team_list[0]->strength;

    // Create a TrainSession to cycle through team members
    og::ui::TrainSession session(og::runtime::current_session->myscreen_->save_data);
    ASSERT_TRUE(!session.empty()) << "session should not be empty";

    // working_copy() should be a distinct copy from original()
    const guy& working = session.working_copy();
    const guy& original = session.original();

    ASSERT_TRUE(&working != &original) << "working copy should be distinct from original";
    ASSERT_EQ(original_strength, (int)working.strength) << "working copy should have same strength as original";
    ASSERT_EQ(original_strength, (int)original.strength) << "original should match team list slot";
}

