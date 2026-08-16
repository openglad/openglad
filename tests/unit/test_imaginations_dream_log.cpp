/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The Imaginations dream log (issue #207): the campaign's own scripted
// mission book — campaigns/imaginations/packs/imaginations.dreams/scripts/
// dream_log.lua — driven through the real CampaignPickerSession over the
// real og::data::make_campaign_providers glue, with the SHIPPED campaign
// archive mounted. Nothing here is synthetic: the pages are the ones a
// player gets, so a book that stops listing a dream, mislabels one, or
// loses the replay affordance for a cleared dream fails here.

#include <gtest/gtest.h>

#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/resources/campaign_state_providers.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/packs.h>
#include <openglad/resources/save_data.h>

#include <cstddef>
#include <string>
#include <vector>

std::string get_asset_path();

namespace {

namespace hooks = og::script::hooks;
using og::ui::CampaignPickerSession;

// The dream-log ledger of campaigns/imaginations/README.md: the scens the
// campaign ships today, in book order. New ideas append new scens (2, 3,
// ...) and this pin grows with them — ledger_pin_is_current below goes red
// until it does, so the book can never quietly drop a dream.
constexpr int kDreamIds[] = {1};
constexpr int kFirstUnwrittenDream = 2;
constexpr const char* kFirstDreamTitle = "The Raspberry Isle";

// The three notes the script writes, pinned verbatim: the log's own words
// for a dream had, the dream up next, and one still waiting.
constexpr const char* kNoteDreamed = "dreamed";
constexpr const char* kNoteTonight = "tonight?";
constexpr const char* kNoteNotYet = "not yet";

// Display budgets the surfaces enforce. The row-face figure is the SDL
// zone submenu row's own budget (kZoneSubmenuRowLabelChars, the picker's
// private picker_sdl_defs.h — a literal here on purpose: this test is the
// pin that says the composed dream row still fits the face uncut).
constexpr std::size_t kLabelBudget = 24;
constexpr std::size_t kNoteBudget = 20;
constexpr std::size_t kLineBudget = 38;
constexpr std::size_t kSdlRowFaceChars = 42;

class ImaginationsDreamLogTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        previous_game_ = current_game;
        current_game = nullptr;  // dispatch resolves the shared UI VM
        restore_default_campaigns();
        // Unit binaries skip io_init's asset mounts; keep the shipped packs
        // tree reachable so the core pack loads beside the campaign's
        // embedded one (append mount is idempotent).
        (void)og::resources::mount((get_asset_path() + "packs/").c_str(),
                                   "packs/", 1);
        previous_mount_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("imaginations"))
            << "builtin/imaginations.glad should restore and mount";
        // The campaign's embedded pack rides the mount; the book is its
        // chunk, so the VM must see the refreshed registry.
        og::resources::refresh_pack_scripts();
        save_.current_campaign = "imaginations";
        save_.my_team = 0;
        hooks::install_campaign_providers(
            og::data::make_campaign_providers(save_));
    }

    void TearDown() override
    {
        hooks::clear_campaign_providers();
        (void)unmount_campaign_package_with_error("imaginations");
        if (!previous_mount_.empty() && previous_mount_ != "imaginations")
            (void)mount_campaign_package_with_error(previous_mount_);
        og::resources::refresh_pack_scripts();
        current_game = previous_game_;
    }

    // Opens the book over the fixture save and answers the root page. The
    // page is fetched per open, so every case below re-derives its notes
    // from whatever the save says at that moment.
    CampaignPickerSession::DecoratedPage open_root()
    {
        CampaignPickerSession session(save_);
        EXPECT_TRUE(session.open()) << "the dream log must open";
        return session.page();
    }

    SaveData save_;
    std::string previous_mount_;
    GameplayContext* previous_game_ = nullptr;
};

}  // namespace

// ---------------------------------------------------------------------------
// The registration itself
// ---------------------------------------------------------------------------

// One campaign, one book: the dreams pack registers exactly one picker,
// with no campaign vars and no picker_action (the log spends nothing and
// remembers nothing), and both its chunks load clean.
TEST_F(ImaginationsDreamLogTest, dreams_pack_registers_one_menu_only_book)
{
    EXPECT_TRUE(hooks::campaign_picker_registered())
        << "the mounted campaign must carry exactly one scripted picker";
    EXPECT_TRUE(hooks::campaign_registered_vars().empty())
        << "the dream log declares no campaign vars";

    hooks::CampaignActionResult result;
    EXPECT_FALSE(hooks::campaign_picker_action("1", result))
        << "a menu-only registration serves no actions";

    const std::vector<og::script::ScriptError>& errors =
        og::script::active_world_scripts().host().errors();
    for (const og::script::ScriptError& error : errors)
        ADD_FAILURE() << "script error at " << error.where << ": "
                      << error.message;
}

// ---------------------------------------------------------------------------
// The root page: one row per shipped dream, in id order
// ---------------------------------------------------------------------------

TEST_F(ImaginationsDreamLogTest, root_page_lists_every_dream_in_id_order)
{
    const CampaignPickerSession::DecoratedPage page = open_root();
    EXPECT_EQ("THE DREAM LOG", page.title);

    const std::size_t expected = sizeof(kDreamIds) / sizeof(kDreamIds[0]);
    ASSERT_EQ(expected, page.rows.size())
        << "one row per dream the campaign ships";
    for (std::size_t i = 0; i < expected; i++)
    {
        const CampaignPickerSession::Row& row = page.rows[i];
        EXPECT_EQ(CampaignPickerSession::Kind::Level, row.kind)
            << "row " << i << " must be a level row (the replay affordance)";
        EXPECT_EQ(kDreamIds[i], row.level) << "row " << i << " level id";
        EXPECT_EQ(std::to_string(kDreamIds[i]), row.id)
            << "row " << i << " entry id";
        EXPECT_EQ(0, row.cost) << "the dream log costs nothing";
    }
    // The script ships no labels: C++ filled this one from the scen header.
    EXPECT_EQ(kFirstDreamTitle, page.rows[0].label);
}

// The ledger pin above is only honest while it matches the campaign. When
// the next dream lands, its scen appears and this case fails — which is the
// reminder to extend kDreamIds (and the README ledger) with it.
TEST_F(ImaginationsDreamLogTest, ledger_pin_is_current)
{
    std::string title;
    EXPECT_NE(og::data::LevelFileIoError::None,
              og::data::load_scenario_title_with_error(
                  ("scen" + std::to_string(kFirstUnwrittenDream)).c_str(),
                  title))
        << "scen" << kFirstUnwrittenDream << " ships now: add it to kDreamIds "
        << "and to the README level ledger";
}

// ---------------------------------------------------------------------------
// The notes: dreamed / tonight? / not yet
// ---------------------------------------------------------------------------

TEST_F(ImaginationsDreamLogTest, note_reads_not_yet_for_an_untouched_dream)
{
    save_.scen_num = 99;  // the cursor is parked somewhere else
    const CampaignPickerSession::DecoratedPage page = open_root();
    ASSERT_FALSE(page.rows.empty());
    EXPECT_EQ(kNoteNotYet, page.rows[0].note);
    EXPECT_FALSE(page.rows[0].cleared);
    EXPECT_FALSE(page.rows[0].current);
}

TEST_F(ImaginationsDreamLogTest, note_reads_tonight_for_the_campaign_cursor)
{
    save_.scen_num = static_cast<short>(kDreamIds[0]);
    const CampaignPickerSession::DecoratedPage page = open_root();
    ASSERT_FALSE(page.rows.empty());
    EXPECT_EQ(kNoteTonight, page.rows[0].note);
    EXPECT_FALSE(page.rows[0].cleared);
    EXPECT_TRUE(page.rows[0].current)
        << "the engine's CURRENT marker must agree with the note";
}

TEST_F(ImaginationsDreamLogTest, note_reads_dreamed_for_a_cleared_dream)
{
    save_.scen_num = 99;
    save_.add_level_completed("imaginations", kDreamIds[0]);
    const CampaignPickerSession::DecoratedPage page = open_root();
    ASSERT_FALSE(page.rows.empty());
    EXPECT_EQ(kNoteDreamed, page.rows[0].note);
    EXPECT_TRUE(page.rows[0].cleared)
        << "the engine's CLEARED marker must agree with the note";
}

// A cleared dream the cursor is also parked on still reads "dreamed": that
// is the row a player is hunting for, and the engine's CURRENT marker still
// rides the row text.
TEST_F(ImaginationsDreamLogTest, cleared_wins_the_note_over_the_cursor)
{
    save_.scen_num = static_cast<short>(kDreamIds[0]);
    save_.add_level_completed("imaginations", kDreamIds[0]);
    const CampaignPickerSession::DecoratedPage page = open_root();
    ASSERT_FALSE(page.rows.empty());
    EXPECT_EQ(kNoteDreamed, page.rows[0].note);
    EXPECT_TRUE(page.rows[0].cleared);
    EXPECT_TRUE(page.rows[0].current);
}

// ---------------------------------------------------------------------------
// The replay path
// ---------------------------------------------------------------------------

// Choosing a dream already had answers SetLevel with that scen — the whole
// point of #207 — and the session writes nothing itself (each client runs
// its own level-set tail).
TEST_F(ImaginationsDreamLogTest, choosing_a_dreamed_row_replays_that_scen)
{
    save_.scen_num = 99;
    save_.add_level_completed("imaginations", kDreamIds[0]);

    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_FALSE(session.page().rows.empty());
    ASSERT_TRUE(session.page().rows[0].cleared) << "the replay row";

    const CampaignPickerSession::Outcome outcome = session.choose(0);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::SetLevel, outcome.kind);
    EXPECT_EQ(kDreamIds[0], outcome.level);
    EXPECT_EQ(99, save_.scen_num) << "the session never writes scen_num";
    EXPECT_EQ(1, session.depth()) << "the log has no subpages to open";
}

// ---------------------------------------------------------------------------
// Budgets: the book has to fit the faces on every client
// ---------------------------------------------------------------------------

TEST_F(ImaginationsDreamLogTest, page_prose_and_every_row_fit_their_budgets)
{
    save_.scen_num = static_cast<short>(kDreamIds[0]);
    const CampaignPickerSession::DecoratedPage page = open_root();

    ASSERT_FALSE(page.lines.empty()) << "the log speaks to the player";
    EXPECT_LE(page.lines.size(),
              static_cast<std::size_t>(hooks::kCampaignPageMaxLines));
    for (const std::string& line : page.lines)
        EXPECT_LE(line.size(), kLineBudget) << "page line: " << line;

    ASSERT_FALSE(page.rows.empty());
    for (const CampaignPickerSession::Row& row : page.rows)
    {
        EXPECT_FALSE(row.label.empty()) << "level " << row.level
                                        << " lost its engine-filled label";
        EXPECT_LE(row.label.size(), kLabelBudget) << "label: " << row.label;
        EXPECT_FALSE(row.note.empty()) << "every dream carries a note";
        EXPECT_LE(row.note.size(), kNoteBudget) << "note: " << row.note;
        // The composed row (label - note + the engine marker) must reach the
        // SDL face uncut: clipping at the face budget must be a no-op.
        const std::string composed =
            og::ui::campaign_picker_row_text(row, kSdlRowFaceChars);
        EXPECT_EQ(composed, og::ui::campaign_picker_row_text(
                                row, kSdlRowFaceChars * 4))
            << "the composed row is clipped on the SDL mission face";
    }
}
