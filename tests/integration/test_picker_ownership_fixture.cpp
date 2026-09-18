#include <openglad/gameplay/guy.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_common.h>
#include <gtest/gtest.h>

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
    // Deliberately OFF the family defaults: a working copy built fresh from
    // guy(family) and never statscopy'd would carry 12/6/1 instead.
    og::runtime::current_session->myscreen_->save_data.team_list[0]->strength = 42;
    og::runtime::current_session->myscreen_->save_data.team_list[0]->dexterity = 7;
    og::runtime::current_session->myscreen_->save_data.team_list[0]->level = 3;
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
}

void teardown_picker_ownership_fixture()
{
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(g_fixture.saved_slot0.release());
    og::runtime::current_session->myscreen_->save_data.team_size = static_cast<unsigned char>(g_fixture.saved_team_size);
}
} // namespace

class PickerOwnershipFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        setup_picker_ownership_fixture();
    }

    void TearDown() override
    {
        teardown_picker_ownership_fixture();
    }
};

// TrainSession::select_current_slot() builds a DISTINCT guy from the
// original's family and then statscopy's the original onto it, and edits to
// that copy stay off the roster until accept().
TEST_F(PickerOwnershipFixture, picker_working_copy_is_a_statscopy_that_edits_do_not_leak_from)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    og::ui::TrainSession session(save);
    ASSERT_TRUE(!session.empty()) << "session should not be empty";

    const guy& working = session.working_copy();
    const guy& original = session.original();

    ASSERT_TRUE(&working != &original) << "working copy should be distinct from original";
    ASSERT_EQ(save.team_list[0].get(), &original) << "original() aliases the roster slot";

    // statscopy (picker_common.cpp:2462-2487) carries the identity and the
    // stats across, not just the family defaults.
    EXPECT_EQ(42, (int)working.strength) << "statscopy carries the roster strength";
    EXPECT_EQ(7, (int)working.dexterity) << "statscopy carries the roster dexterity";
    EXPECT_EQ(3, (int)working.level) << "statscopy carries the roster level";
    EXPECT_EQ("FIXTURE_SOLDIER", working.name) << "statscopy carries the name";
    EXPECT_EQ(2, (int)working.teamnum) << "statscopy carries the team number";

    // Ownership: an un-accepted edit never reaches the roster.
    session.increase_stat(og::ui::TrainSession::Stat::Strength, 1);
    EXPECT_EQ(43, (int)session.working_copy().strength)
        << "the edit lands on the working copy";
    EXPECT_EQ(42, (int)session.original().strength)
        << "an unaccepted edit never reaches the roster";
    EXPECT_EQ(42, (int)save.team_list[0]->strength)
        << "and the roster slot itself is untouched";
}
