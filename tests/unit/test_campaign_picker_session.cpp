/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// CampaignPickerSession (issue #206): the SDL-free scripted-picker state
// machine — open/decoration over a scripted SaveData, paging + the depth
// cap, back() semantics, the debit-then-dispatch action order over the
// REAL og::data::make_campaign_providers glue, refusals, infinite gold,
// toasts, and the policy-free SetLevel outcome.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/campaign_state_providers.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace og::script;
using og::ui::CampaignPickerSession;

namespace {

// Chunk names deliberately do NOT start with `packs/`: that prefix declares
// the bytes to the pack-Lua coverage inventory, and these throwaway chunks
// exist nowhere in the repository (the test_classpack_lua_decl discipline).
constexpr const char* kPack = "test.pickersession";

class CampaignPickerSessionTest : public ::testing::Test {
protected:
    CampaignPickerSessionTest()
    {
        previous_ = current_game;
        current_game = nullptr;  // dispatch resolves the shared UI VM
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_pack_lib_modules();
        hooks::clear_campaign_providers();
        save_.current_campaign = "testcamp";
        save_.my_team = 0;
        // Install the REAL provider glue over the fixture save — the same
        // wiring every surface installs, so og.campaign_* in the scripts
        // below reads/writes this exact SaveData.
        hooks::install_campaign_providers(
            og::data::make_campaign_providers(save_));
    }

    ~CampaignPickerSessionTest() override
    {
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_pack_lib_modules();
        hooks::clear_campaign_providers();
        current_game = previous_;
    }

    static void register_script(const std::string& source,
                                const char* chunk = "pickertest/scripts/c.lua")
    {
        register_pack_script({kPack, chunk, source});
    }

    static const std::vector<std::string>& vm_log()
    {
        return active_world_scripts().host().log();
    }

    static bool log_contains(const std::string& needle)
    {
        for (const std::string& line : vm_log()) {
            if (line.find(needle) != std::string::npos)
                return true;
        }
        return false;
    }

    SaveData save_;
    GameplayContext* previous_ = nullptr;
};

}  // namespace

// ---------------------------------------------------------------------------
// open()
// ---------------------------------------------------------------------------

TEST_F(CampaignPickerSessionTest, open_answers_false_without_registration)
{
    register_script(R"LUA(og.log("no campaign registration"))LUA");
    CampaignPickerSession session(save_);
    EXPECT_FALSE(session.open());
    EXPECT_FALSE(session.is_open());
}

TEST_F(CampaignPickerSessionTest, open_answers_false_on_malformed_root)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return { entries = {} }  -- no title: malformed
  end,
}))LUA");
    CampaignPickerSession session(save_);
    EXPECT_FALSE(session.open());
    EXPECT_FALSE(session.is_open());
}

// ---------------------------------------------------------------------------
// Root fetch + decoration
// ---------------------------------------------------------------------------

TEST_F(CampaignPickerSessionTest, root_fetch_decorates_rows_from_the_save)
{
    save_.scen_num = 300;
    save_.add_level_completed("testcamp", 301);
    save_.m_totalcash[0] = 50;
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return {
      title = "CHOOSE A GAME",
      lines = { "The Gamesmaster opens the book." },
      entries = {
        { id = "300", label = "THE CIRCLE", kind = "level", level = 300, note = "4 teams" },
        { id = "301", label = "THE PIT", kind = "level", level = 301 },
        { id = "302", label = "THE WALL", kind = "level", level = 302 },
        { id = "cheap", label = "RATION", kind = "action", cost = 50 },
        { id = "dear", label = "FIELD KIT", kind = "action", cost = 60 },
        { id = "free", label = "PRAY", kind = "action" },
        { id = "tdm", label = "DEATHMATCH", kind = "page" },
      },
    }
  end,
}))LUA");

    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    EXPECT_EQ(1, session.depth());
    const CampaignPickerSession::DecoratedPage& page = session.page();
    EXPECT_EQ("CHOOSE A GAME", page.title);
    ASSERT_EQ(1u, page.lines.size());
    ASSERT_EQ(7u, page.rows.size());

    // Level decoration: CURRENT from scen_num, CLEARED from the ledger.
    EXPECT_TRUE(page.rows[0].is_level());
    EXPECT_TRUE(page.rows[0].current);
    EXPECT_FALSE(page.rows[0].cleared);
    EXPECT_EQ("4 teams", page.rows[0].note);
    EXPECT_TRUE(page.rows[1].cleared);
    EXPECT_FALSE(page.rows[1].current);
    EXPECT_FALSE(page.rows[2].cleared);
    EXPECT_FALSE(page.rows[2].current);

    // Affordability over the acting wallet (team 0 holds 50).
    EXPECT_TRUE(page.rows[3].affordable) << "cost == wallet is affordable";
    EXPECT_FALSE(page.rows[4].affordable) << "cost > wallet is not";
    EXPECT_TRUE(page.rows[5].affordable) << "cost 0 is always affordable";
    EXPECT_EQ(60, page.rows[4].cost);
    EXPECT_FALSE(page.rows[6].is_level());
}

TEST_F(CampaignPickerSessionTest,
       level_label_fill_reads_the_mounted_campaign_title)
{
    const std::string previous_mount = get_mounted_campaign();
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return {
      title = "BOOK",
      entries = {
        { id = "1", kind = "level", level = 1 },
        { id = "gone", kind = "level", level = 654321 },
      },
    }
  end,
}))LUA");

    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(2u, session.page().rows.size());
    // The fill is the scenario title of the mounted campaign's level 1 —
    // compared against the real header read, never a hardcoded string.
    const std::string expected = og::data::load_scenario_title("scen1");
    ASSERT_NE("none", expected) << "gladiator scen1 must be readable";
    EXPECT_EQ(expected, session.page().rows[0].label);
    // A missing level falls back to the id form.
    EXPECT_EQ("SCEN 654321", session.page().rows[1].label);

    if (previous_mount != "gladiator")
    {
        (void)unmount_campaign_package_with_error("gladiator");
        if (!previous_mount.empty())
            (void)mount_campaign_package_with_error(previous_mount);
    }
}

// ---------------------------------------------------------------------------
// Paging: push/back and the depth cap
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kNestedPages = R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    if page_id == "" then
      return { title = "ROOT",
               entries = { { id = "a", kind = "page", label = "A" } } }
    end
    if page_id == "a" then
      return { title = "PAGE A",
               entries = { { id = "b", kind = "page", label = "B" } } }
    end
    if page_id == "b" then
      return { title = "PAGE B",
               entries = { { id = "c", kind = "page", label = "C" } } }
    end
    if page_id == "c" then
      return { title = "PAGE C",
               entries = { { id = "d", kind = "page", label = "D" } } }
    end
    return { title = "PAGE " .. page_id }
  end,
}))LUA";

}  // namespace

TEST_F(CampaignPickerSessionTest, page_depth_caps_at_four)
{
    register_script(kNestedPages);
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());

    using Outcome = CampaignPickerSession::OutcomeKind;
    EXPECT_EQ(Outcome::OpenedPage, session.choose(0).kind);
    EXPECT_EQ("PAGE A", session.page().title);
    EXPECT_EQ(Outcome::OpenedPage, session.choose(0).kind);
    EXPECT_EQ(Outcome::OpenedPage, session.choose(0).kind);
    EXPECT_EQ("PAGE C", session.page().title);
    EXPECT_EQ(4, session.depth());

    // Depth 4 is the floor of the book: another page refuses.
    const CampaignPickerSession::Outcome refused = session.choose(0);
    EXPECT_EQ(Outcome::Refused, refused.kind);
    EXPECT_FALSE(refused.reason.empty());
    EXPECT_EQ("PAGE C", session.page().title) << "the page must not change";
    EXPECT_EQ(4, session.depth());

    // back() pops and refetches; at the root it answers false (close).
    EXPECT_TRUE(session.back());
    EXPECT_EQ("PAGE B", session.page().title);
    EXPECT_TRUE(session.back());
    EXPECT_TRUE(session.back());
    EXPECT_EQ("ROOT", session.page().title);
    EXPECT_EQ(1, session.depth());
    EXPECT_FALSE(session.back()) << "root BACK closes the picker";
    EXPECT_TRUE(session.is_open()) << "root back() leaves the page usable";
}

TEST_F(CampaignPickerSessionTest, unreadable_nested_page_refuses_in_place)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    if page_id == "" then
      return { title = "ROOT",
               entries = { { id = "bad", kind = "page", label = "BAD" } } }
    end
    error("no such page")
  end,
}))LUA");
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    const CampaignPickerSession::Outcome outcome = session.choose(0);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Refused, outcome.kind);
    EXPECT_EQ("ROOT", session.page().title);
    EXPECT_EQ(1, session.depth());
}

TEST_F(CampaignPickerSessionTest, back_refetch_failure_closes_the_session)
{
    // Chunk-local state is fine in a throwaway test chunk: the page parses
    // on the way down and errors on the way back up.
    register_script(R"LUA(local visits = 0
og.register_campaign_hooks({
  picker_menu = function(page_id)
    if page_id == "" then
      return { title = "ROOT",
               entries = { { id = "flaky", kind = "page", label = "F" } } }
    end
    if page_id == "flaky" then
      visits = visits + 1
      if visits > 1 then
        error("gone")
      end
      return { title = "FLAKY",
               entries = { { id = "sub", kind = "page", label = "S" } } }
    end
    return { title = "SUB" }
  end,
}))LUA");
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(0).kind);
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(0).kind);
    EXPECT_EQ("SUB", session.page().title);
    // Popping back re-fetches "flaky", which now errors: the session
    // closes (a malformed page answers "no scripted picker").
    EXPECT_FALSE(session.back());
    EXPECT_FALSE(session.is_open());
}

// ---------------------------------------------------------------------------
// Actions: the debit-then-dispatch order, refusals, infinite gold, toasts
// ---------------------------------------------------------------------------

namespace {

// The action logs the wallet AS THE HOOK SEES IT — the VM log therefore
// proves whether the C++ debit ran before the dispatch.
constexpr const char* kShopScript = R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    local label = "FIELD KIT"
    if og.campaign_state_get("kit") == 1 then
      label = "KIT OWNED"
    end
    return { title = "SHOP",
             entries = { { id = "buy_kit", label = label, kind = "action", cost = 60 } } }
  end,
  picker_action = function(entry_id)
    og.log("gold_at_dispatch " .. og.campaign_gold())
    og.campaign_state_set("kit", 1)
    return { message = "Kit stowed for the road." }
  end,
}))LUA";

}  // namespace

TEST_F(CampaignPickerSessionTest, action_debits_before_dispatch_and_refetches)
{
    save_.m_totalcash[0] = 100;
    register_script(kShopScript);
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    EXPECT_EQ("FIELD KIT", session.page().rows[0].label);

    const CampaignPickerSession::Outcome outcome = session.choose(0);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Acted, outcome.kind);
    // The hook observed the wallet AFTER the 60g debit: debit-first.
    EXPECT_TRUE(log_contains("gold_at_dispatch 40")) << "debit must precede "
                                                        "the dispatch";
    EXPECT_EQ(40u, save_.m_totalcash[0]);
    // The write-through landed in the save's campaign state...
    EXPECT_EQ(1, save_.campaign_state_get("testcamp", "kit"));
    // ...and the page was re-requested, so the label re-derived.
    EXPECT_EQ("KIT OWNED", session.page().rows[0].label);
    // The toast waits in take_message and is cleared by the read.
    EXPECT_EQ("Kit stowed for the road.", session.take_message());
    EXPECT_EQ("", session.take_message());
}

TEST_F(CampaignPickerSessionTest, unaffordable_action_refuses_without_dispatch)
{
    save_.m_totalcash[0] = 59;
    register_script(kShopScript);
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    EXPECT_FALSE(session.page().rows[0].affordable);

    const CampaignPickerSession::Outcome outcome = session.choose(0);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Refused, outcome.kind);
    EXPECT_EQ("Not enough gold.", outcome.reason);
    EXPECT_EQ(59u, save_.m_totalcash[0]) << "a refusal must not debit";
    EXPECT_FALSE(log_contains("gold_at_dispatch"))
        << "a refusal must not dispatch the hook";
    EXPECT_EQ("", session.take_message());
}

TEST_F(CampaignPickerSessionTest, infinite_gold_skips_the_debit_entirely)
{
    save_.m_totalcash[0] = 10;
    save_.infinite_gold = 1;
    register_script(kShopScript);
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    EXPECT_TRUE(session.page().rows[0].affordable)
        << "infinite gold answers affordable";

    const CampaignPickerSession::Outcome outcome = session.choose(0);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Acted, outcome.kind);
    EXPECT_EQ(10u, save_.m_totalcash[0])
        << "infinite gold skips the debit — the wallet is never written";
    EXPECT_TRUE(log_contains("gold_at_dispatch 10"));
}

namespace {

// A book with picker_menu ONLY: its costed action row can never be honored
// (no picker_action is registered), so choose() must refund and refuse.
constexpr const char* kMenuOnlyShop = R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return { title = "MENU ONLY",
             entries = { { id = "buy", label = "FIELD KIT", kind = "action", cost = 60 } } }
  end,
}))LUA";

}  // namespace

TEST_F(CampaignPickerSessionTest, costed_action_with_no_hook_refunds_and_refuses)
{
    save_.m_totalcash[0] = 100;
    register_script(kMenuOnlyShop);
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_TRUE(session.page().rows[0].affordable);

    const CampaignPickerSession::Outcome outcome = session.choose(0);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Refused, outcome.kind);
    EXPECT_EQ("This book takes no orders.", outcome.reason);
    EXPECT_EQ(100u, save_.m_totalcash[0])
        << "the debit must be refunded when no picker_action serves the row";
    EXPECT_EQ("MENU ONLY", session.page().title) << "the page must not change";
    EXPECT_EQ("", session.take_message());
}

TEST_F(CampaignPickerSessionTest, no_hook_refusal_under_infinite_gold_is_a_noop)
{
    save_.m_totalcash[0] = 10;
    save_.infinite_gold = 1;
    register_script(kMenuOnlyShop);
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());

    const CampaignPickerSession::Outcome outcome = session.choose(0);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Refused, outcome.kind);
    EXPECT_EQ("This book takes no orders.", outcome.reason);
    EXPECT_EQ(10u, save_.m_totalcash[0])
        << "under infinite gold neither the debit nor the refund may write "
           "the wallet";
}

TEST_F(CampaignPickerSessionTest, action_debit_lands_on_the_acting_team)
{
    // The lowest roster team is the acting wallet (the providers' rule) —
    // shared through campaign_picker_can_afford/debit, not duplicated.
    save_.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save_.team_list[0]->teamnum = 2;
    save_.team_size = 1;
    save_.my_team = 0;  // must lose to the roster scan
    save_.m_totalcash[0] = 1000;
    save_.m_totalcash[2] = 100;
    register_script(kShopScript);
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(0).kind);
    EXPECT_EQ(1000u, save_.m_totalcash[0]) << "my_team's wallet is untouched";
    EXPECT_EQ(40u, save_.m_totalcash[2]) << "the roster team paid";
}

// ---------------------------------------------------------------------------
// SetLevel stays policy-free
// ---------------------------------------------------------------------------

TEST_F(CampaignPickerSessionTest, set_level_outcome_carries_id_writes_nothing)
{
    save_.scen_num = 7;
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return { title = "BOOK",
             entries = { { id = "300", label = "THE CIRCLE", kind = "level", level = 300 } } }
  end,
}))LUA");
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    const CampaignPickerSession::Outcome outcome = session.choose(0);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::SetLevel, outcome.kind);
    EXPECT_EQ(300, outcome.level);
    EXPECT_EQ(7, save_.scen_num) << "the session never writes scen_num";
    EXPECT_EQ("BOOK", session.page().title) << "the page does not change";
    EXPECT_EQ(1, session.depth());

    // Out-of-range chooses are inert.
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::None,
              session.choose(99).kind);
}

// ---------------------------------------------------------------------------
// Row text composition (shared by the surfaces)
// ---------------------------------------------------------------------------

TEST_F(CampaignPickerSessionTest, row_text_composes_markers_costs_and_clips)
{
    CampaignPickerSession::Row row;
    row.label = "THE CIRCLE";
    row.kind = CampaignPickerSession::Kind::Level;
    row.current = true;
    EXPECT_EQ("THE CIRCLE  [CURRENT]",
              og::ui::campaign_picker_row_text(row, 42));

    row.current = false;
    row.cleared = true;
    row.note = "4 teams";
    EXPECT_EQ("THE CIRCLE - 4 teams  [CLEARED]",
              og::ui::campaign_picker_row_text(row, 42));

    CampaignPickerSession::Row action;
    action.label = "FIELD KIT";
    action.kind = CampaignPickerSession::Kind::Action;
    action.cost = 60;
    EXPECT_EQ("FIELD KIT  60g", og::ui::campaign_picker_row_text(action, 42));

    // Clip at the budget: an over-long compose never escapes the face.
    EXPECT_EQ("FIELD KIT ", og::ui::campaign_picker_row_text(action, 10));
}

// ---------------------------------------------------------------------------
// The shared terminal driver (the text/curses MISSIONS loop)
// ---------------------------------------------------------------------------

namespace {

// A scripted TerminalCampaignPickerIo: canned prompt answers (EOF after the
// script runs dry), recorded prompts and notices.
struct ScriptedTerminalIo {
    struct Prompt {
        std::string title;
        std::vector<std::string> lines;
        std::string label;
    };

    std::vector<std::string> answers;
    std::size_t cursor = 0;
    std::vector<Prompt> prompts;
    std::vector<std::string> notices;
    bool host = true;
    int applied_level = -1;
    SaveData* save = nullptr;

    og::ui::TerminalCampaignPickerIo io()
    {
        og::ui::TerminalCampaignPickerIo io;
        io.prompt = [this](const std::string& title,
                           const std::vector<std::string>& lines,
                           const std::string& label)
            -> std::optional<std::string> {
            prompts.push_back({title, lines, label});
            if (cursor >= answers.size())
                return std::nullopt;  // scripted input exhausted = EOF
            return answers[cursor++];
        };
        io.notice = [this](const std::string& line) {
            notices.push_back(line);
        };
        io.is_host = [this] { return host; };
        io.apply_level = [this](int level) {
            applied_level = level;
            // Both clients' set-level tail writes the save cursor.
            save->scen_num = static_cast<short>(level);
        };
        return io;
    }

    // The composed page lines of prompt `index` joined for substring checks.
    std::string page_text(std::size_t index) const
    {
        std::string joined;
        for (const std::string& line : prompts[index].lines) {
            joined += line;
            joined += '\n';
        }
        return joined;
    }
};

}  // namespace

// The full loop over the REAL provider glue: invalid rows, the action arm
// (debit + toast + §3.8 autosave), an unaffordable refusal, a lines-only
// subpage (and its rowless prompt label), blank-line back, the level arm
// (apply tail + CURRENT re-derive), and 0-at-root close.
TEST_F(CampaignPickerSessionTest, terminal_driver_runs_the_whole_book)
{
    save_.m_totalcash[0] = 100;
    save_.save_name = "DRIVER BAND";
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    if page_id == "lore" then
      return { title = "LORE", lines = { "Only words here." } }
    end
    local label = "FIELD KIT"
    if og.campaign_state_get("kit") == 1 then
      label = "KIT OWNED"
    end
    return { title = "ROOT",
             entries = {
               { id = "300", label = "THE CIRCLE", kind = "level", level = 300 },
               { id = "buy", label = label, kind = "action", cost = 60 },
               { id = "lore", label = "LORE", kind = "page" },
             } }
  end,
  picker_action = function(entry_id)
    og.campaign_state_set("kit", 1)
    return { message = "Kit stowed for the road." }
  end,
}))LUA");

    // The Acted arm autosaves the active company slot; isolate it on a
    // scratch slot and reap the file afterwards.
    ASSERT_TRUE(og::data::set_active_company_slot("pickerdrv"));

    ScriptedTerminalIo scripted;
    scripted.save = &save_;
    scripted.answers = {
        "nonsense",  // not a number -> invalid
        "9",         // out of range (3 rows) -> invalid
        "2",         // FIELD KIT -> Acted: debit 60, toast, autosave
        "2",         // now unaffordable (40 < 60) -> Refused
        "3",         // LORE -> OpenedPage (lines only, no rows)
        "1",         // no rows on LORE -> invalid
        "",          // blank -> back to ROOT
        "1",         // THE CIRCLE -> SetLevel: tail + CURRENT re-derive
        "0",         // 0 at the root closes the book
    };
    og::ui::run_terminal_campaign_picker(save_, scripted.io());

    const std::vector<std::string> expected_notices = {
        "Invalid mission row.",
        "Invalid mission row.",
        "Kit stowed for the road.",
        "Not enough gold.",
        "Invalid mission row.",
        "Level set to THE CIRCLE.",
    };
    EXPECT_EQ(expected_notices, scripted.notices);

    EXPECT_EQ(300, scripted.applied_level);
    EXPECT_EQ(300, save_.scen_num);
    EXPECT_EQ(40u, save_.m_totalcash[0]) << "the 60g action debit must land";
    EXPECT_EQ(1, save_.campaign_state_get("testcamp", "kit"));
    EXPECT_TRUE(user_file_exists("save/pickerdrv.gtl"))
        << "the Acted arm must run the §3.8 company-autosave tail";

    // Prompt 0: the root page in the shared composed shape.
    ASSERT_GE(scripted.prompts.size(), 9u);
    EXPECT_EQ("ROOT", scripted.prompts[0].title);
    EXPECT_EQ("Mission # [1-3] (0 = back): ", scripted.prompts[0].label);
    EXPECT_NE(std::string::npos,
              scripted.page_text(0).find("   1. THE CIRCLE\n"));
    EXPECT_NE(std::string::npos,
              scripted.page_text(0).find("   2. FIELD KIT  60g\n"));
    // After the action, the refetched page re-derived the scripted label.
    EXPECT_NE(std::string::npos,
              scripted.page_text(3).find("   2. KIT OWNED  60g\n"));
    // The lines-only subpage prompts with the rowless label (prompt 5 —
    // prompts 0-4 are the ROOT page's invalid/action/refusal/page steps).
    EXPECT_EQ("LORE", scripted.prompts[5].title);
    EXPECT_EQ("Mission # (0 = back): ", scripted.prompts[5].label);
    EXPECT_NE(std::string::npos,
              scripted.page_text(5).find("Only words here.\n"));
    // The final prompt shows the applied cursor's CURRENT marker.
    EXPECT_NE(std::string::npos,
              scripted.page_text(8).find("   1. THE CIRCLE  [CURRENT]\n"));

    (void)remove_user_file("save/pickerdrv.gtl");
    (void)og::data::set_active_company_slot("save0");
}

// The SET LEVEL host gate: a non-host choosing a level row is refused with
// the terminal host guard line (pinned verbatim here, exactly once) and
// nothing is applied; pages/actions stay open to every machine by design.
TEST_F(CampaignPickerSessionTest, terminal_driver_host_gate_refuses_level_rows)
{
    save_.scen_num = 7;
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return { title = "BOOK",
             entries = { { id = "300", label = "THE CIRCLE", kind = "level", level = 300 } } }
  end,
}))LUA");

    ScriptedTerminalIo scripted;
    scripted.save = &save_;
    scripted.host = false;
    scripted.answers = {"1"};  // then EOF -> back at the root -> close
    og::ui::run_terminal_campaign_picker(save_, scripted.io());

    const std::vector<std::string> expected_notices = {
        "Only the host may set the level.",
    };
    EXPECT_EQ(expected_notices, scripted.notices);
    EXPECT_EQ(-1, scripted.applied_level)
        << "the gate must refuse before the client tail runs";
    EXPECT_EQ(7, save_.scen_num);
    EXPECT_EQ(2u, scripted.prompts.size())
        << "the refusal re-presents the page; the EOF prompt closes it";
}

// No registration: the driver prints the shared guard line (the literal is
// pinned by the text drive; this arm holds the exported constant) and never
// prompts.
TEST_F(CampaignPickerSessionTest, terminal_driver_guards_when_no_book)
{
    register_script(R"LUA(og.log("no campaign registration"))LUA");

    ScriptedTerminalIo scripted;
    scripted.save = &save_;
    og::ui::run_terminal_campaign_picker(save_, scripted.io());

    ASSERT_EQ(1u, scripted.notices.size());
    EXPECT_EQ(og::ui::kCampaignPickerNoBookMessage, scripted.notices[0]);
    EXPECT_TRUE(scripted.prompts.empty())
        << "the guard path must never open the book prompt";
}
