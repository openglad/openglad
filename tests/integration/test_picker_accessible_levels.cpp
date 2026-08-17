#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/io_common.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

// From picker.cpp
std::vector<int> get_accessible_levels();
// From picker_team_build.cpp: the PROGRESS GO/REPLAY click-time answer, and
// whether the report offers that click at all.
bool progress_row_click_applies(int hit_id);
bool progress_rows_actionable();

static bool contains(const std::vector<int>& v, int x)
{
    return std::find(v.begin(), v.end(), x) != v.end();
}

TEST(PickerAccessibleLevels, picker_get_accessible_levels_always_has_level1_and_current)
{
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 3;

    std::vector<int> levels = get_accessible_levels();
    ASSERT_TRUE(contains(levels, 1)) << "level 1 should always be accessible";
    ASSERT_TRUE(contains(levels, 3)) << "current level should be accessible";
}


TEST(PickerAccessibleLevels, picker_get_accessible_levels_includes_exits_of_cleared_levels)
{
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;

    // Mark level 1 as cleared; get_accessible_levels should attempt to load it and add exits.
    og::runtime::current_session->myscreen_->save_data.add_level_completed(og::runtime::current_session->myscreen_->save_data.current_campaign, 1);

    std::vector<int> levels = get_accessible_levels();
    ASSERT_TRUE(contains(levels, 1)) << "level 1 should be accessible";

    // Most campaigns have at least one exit from level 1.
    bool has_exit = false;
    for (int id : levels) {
        if (id > 1)
            has_exit = true;
    }
    ASSERT_TRUE(has_exit) << "cleared level 1 should yield at least one additional accessible level via exits";
}


TEST(PickerAccessibleLevels, picker_get_accessible_levels_handles_missing_leveldata)
{
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;

    // Add a bogus "completed" level id to force LevelRuntimeData::load() failure path.
    og::runtime::current_session->myscreen_->save_data.completed_levels[og::runtime::current_session->myscreen_->save_data.current_campaign].insert(9999);

    std::vector<int> levels = get_accessible_levels();
    ASSERT_TRUE(contains(levels, 1)) << "level 1 should still be accessible";
    ASSERT_TRUE(contains(levels, 9999)) << "bogus completed level id should still be included as accessible";
}


// The PROGRESS GO/REPLAY click rides the earned-roads gate: an id outside
// the frontier refuses (the report's own rows are always inside it, so this
// guards stale clicks and future row sources) and an in-frontier id writes
// the cursor exactly as before.
TEST(PickerAccessibleLevels, progress_row_click_applies_gate)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.current_campaign = "gladiator";
    save.scen_num = 3;

    trace_clear();
    EXPECT_FALSE(progress_row_click_applies(15))
        << "an unearned forward id must refuse";
    EXPECT_EQ(3, (int)save.scen_num);
    EXPECT_TRUE(trace_contains("picker", "progress_row_denied_gate 15"));
    EXPECT_TRUE(trace_contains("popup", "That road is not open yet."));

    EXPECT_TRUE(progress_row_click_applies(1))
        << "the campaign's entry level is always in the frontier";
    EXPECT_EQ(1, (int)save.scen_num);
}

namespace {

// A joiner lobby client (host_controls_visible false): the PROGRESS screen
// had NO host gate — this pins the one progress_row_click_applies adds.
struct JoinerProgressLobbyClient final : og::ui::IPickerLobbyClient
{
    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override { return false; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override { return std::nullopt; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override { return std::nullopt; }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return false;
    }
};

} // namespace

TEST(PickerAccessibleLevels, progress_row_click_refuses_joiners)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.current_campaign = "gladiator";
    save.scen_num = 3;

    JoinerProgressLobbyClient lobby;
    og::ui::IPickerLobbyClient* const saved_client =
        og::ui::active_picker_lobby_client();
    og::ui::install_active_picker_lobby_client(&lobby);

    trace_clear();
    EXPECT_FALSE(progress_row_click_applies(1))
        << "a joiner's click must not write scen_num";
    EXPECT_EQ(3, (int)save.scen_num);
    EXPECT_TRUE(trace_contains("picker", "progress_row_denied_nonhost 1"));
    EXPECT_TRUE(trace_contains("popup", "Only the host may set the level."));

    og::ui::install_active_picker_lobby_client(saved_client);
}

// ... and the row never invites that click in the first place. The refusal
// above is a popup, and popup_dialog owns its event loop with no lobby poll
// in it: every row of this report used to offer a joiner a click whose only
// possible answer was that popup, and a joiner sitting behind its OK button
// applies no lobby messages and cannot follow the host's GO. So the
// affordance itself is host-only — the joiner reads the report, which is
// what the joiner opened it for.
TEST(PickerAccessibleLevels, progress_rows_offer_no_click_to_a_joiner)
{
    EXPECT_TRUE(progress_rows_actionable())
        << "a solo or hosting player keeps GO/REPLAY";

    JoinerProgressLobbyClient lobby;
    og::ui::IPickerLobbyClient* const saved_client =
        og::ui::active_picker_lobby_client();
    og::ui::install_active_picker_lobby_client(&lobby);
    EXPECT_FALSE(progress_rows_actionable())
        << "a joiner's rows wear (HOST), not a button";
    og::ui::install_active_picker_lobby_client(saved_client);

    EXPECT_TRUE(progress_rows_actionable()) << "and the seam restores";
}
