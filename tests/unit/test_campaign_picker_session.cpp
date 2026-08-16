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
#include <optional>
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

    // Clip at the budget: an over-long compose never escapes the face, and
    // it says it was cut instead of stopping mid-word.
    EXPECT_EQ("FIELD KI..", og::ui::campaign_picker_row_text(action, 10));

    // A retired purchase stops quoting its price and says so.
    action.done = true;
    EXPECT_EQ("FIELD KIT  [DONE]",
              og::ui::campaign_picker_row_text(action, 42));

    // Page rows wear the door marker; a level row the engine could not find
    // wears CLOSED.
    CampaignPickerSession::Row page;
    page.label = "STORES";
    page.kind = CampaignPickerSession::Kind::Page;
    EXPECT_EQ("STORES  >", og::ui::campaign_picker_row_text(page, 42));

    CampaignPickerSession::Row closed;
    closed.label = "SCEN 9999";
    closed.kind = CampaignPickerSession::Kind::Level;
    closed.available = false;
    EXPECT_EQ("SCEN 9999  [CLOSED]",
              og::ui::campaign_picker_row_text(closed, 42));

    // The unaffordable mark is the terminals' spelling of the SDL dimmed
    // face: the SDL faces stay unmarked (they dim), the prompts say it.
    CampaignPickerSession::Row poor;
    poor.label = "TOO RICH";
    poor.kind = CampaignPickerSession::Kind::Action;
    poor.cost = 999999;
    poor.affordable = false;
    EXPECT_EQ("TOO RICH  999999g",
              og::ui::campaign_picker_row_text(poor, 42));
    EXPECT_EQ("TOO RICH  999999g  [NEED GOLD]",
              og::ui::campaign_picker_row_text(poor, 42, true));
}

// One camp roster row: the padlock letter, the oath cell and the reason, as
// three space-separated COLUMNS. The reason used to arrive behind a " - "
// separator that collided with the unsworn oath cell's own "-" and printed
// "- - " mid-row — a stutter that reads as a typo on both terminals.
TEST_F(CampaignPickerSessionTest, camp_roster_row_never_stutters_the_dash)
{
    guy member(FAMILY_SOLDIER);
    member.name = "Tom";
    member.level = 1;
    member.exp = 0;
    member.deployed = false;
    member.campaign_tag = 0;

    og::script::hooks::CampaignAssignSpec assign;
    assign.active = true;
    assign.key = "muster";
    assign.labels = {"WAR", "BURDEN"};

    og::script::hooks::CampaignRosterLock lock;
    lock.unset = true;
    lock.reason = "Swear at the Falls first.";

    const std::string row = og::ui::format_campaign_camp_roster_row(
        member, assign, &lock, og::ui::kCampaignPickerTerminalRowBudget);
    EXPECT_EQ(std::string::npos, row.find("- - "))
        << "the oath placeholder and the reason separator collided: " << row;
    EXPECT_EQ("[L] Tom          SOLDIER   L= 1 XP=     0   -  "
              "Swear at the Falls first.",
              row);
    EXPECT_GE(og::ui::kCampaignPickerTerminalRowBudget, row.size())
        << "the whole row still fits a stock 80-column terminal";

    // A sworn hero keeps the same columns with the word in the oath cell.
    member.campaign_tag = 1;
    const std::string sworn = og::ui::format_campaign_camp_roster_row(
        member, assign, nullptr, og::ui::kCampaignPickerTerminalRowBudget);
    EXPECT_EQ("[ ] Tom          SOLDIER   L= 1 XP=     0   WAR", sworn);

    // The level field is two characters up to 99 and three beyond, so the
    // stat block is padded to its widest shape: the oath cell has to start
    // at the SAME offset for a level-1 recruit and a level-100 veteran, or
    // the heading the header strip pads out to heads nothing.
    member.level = 100;
    const std::string veteran = og::ui::format_campaign_camp_roster_row(
        member, assign, nullptr, og::ui::kCampaignPickerTerminalRowBudget);
    EXPECT_EQ(sworn.find("WAR"), veteran.find("WAR"))
        << "the oath column moved when the hero passed level 99:\n"
        << sworn << "\n"
        << veteran;
    EXPECT_EQ("[ ] Tom          SOLDIER   L=100 XP=     0  WAR", veteran);
}

// ---------------------------------------------------------------------------
// The shared terminal driver (the camp + book prompt loop)
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

// The full loop over the REAL provider glue, rooted at the book's own root
// page (""), the page id the camp's book door names: invalid rows, the action
// arm (debit + toast + §3.8 autosave), an unaffordable row (marked BEFORE the
// click, then refused), a lines-only subpage (and its rowless prompt label),
// blank-line back, a LABELLED road the campaign does not carry (marked CLOSED
// and refused in the campaign's voice), the level arm (apply tail + CURRENT
// re-derive), and 0-at-root close. The gold strip rides every page: a shop
// quotes prices, so the purse has to be on the same screen.
TEST_F(CampaignPickerSessionTest, terminal_driver_runs_the_whole_book)
{
    const std::string previous_mount = get_mounted_campaign();
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    save_.m_totalcash[0] = 100;
    save_.scen_num = 7;  // never the road under test: CURRENT is earned here
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
               { id = "1", label = "THE ARENA", kind = "level", level = 1 },
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
        "9",         // out of range (4 rows) -> invalid
        "3",         // FIELD KIT -> Acted: debit 60, toast, autosave
        "3",         // now unaffordable (40 < 60) -> Refused
        "4",         // LORE -> OpenedPage (lines only, no rows)
        "1",         // no rows on LORE -> invalid
        "",          // blank -> back to ROOT
        "1",         // THE CIRCLE -> a road not in the campaign: refused
        "2",         // THE ARENA -> SetLevel: tail + CURRENT re-derive
        "0",         // 0 at the root closes the book
    };
    og::ui::run_terminal_campaign_page(save_, scripted.io(), "");

    const std::vector<std::string> expected_notices = {
        "Invalid camp row.",
        "Invalid camp row.",
        "Kit stowed for the road.",
        "Not enough gold.",
        "Invalid camp row.",
        std::string(og::ui::kCampaignLevelClosedMessage),
        "Level set to THE ARENA.",
    };
    EXPECT_EQ(expected_notices, scripted.notices);

    EXPECT_EQ(1, scripted.applied_level);
    EXPECT_EQ(1, save_.scen_num);
    EXPECT_EQ(40u, save_.m_totalcash[0]) << "the 60g action debit must land";
    EXPECT_EQ(1, save_.campaign_state_get("testcamp", "kit"));
    EXPECT_TRUE(user_file_exists("save/pickerdrv.gtl"))
        << "the Acted arm must run the §3.8 company-autosave tail";

    // Prompt 0: the root page in the shared composed shape.
    ASSERT_GE(scripted.prompts.size(), 10u);
    EXPECT_EQ("ROOT", scripted.prompts[0].title);
    EXPECT_EQ("Camp # [1-4] (0 = back): ", scripted.prompts[0].label)
        << "a book page is a room inside the camp: no surface says 'mission'";
    // The blocking regression: a row the BOOK labelled still has to declare
    // that the campaign does not carry the road. Availability is a fact
    // about the road, not about whether the engine had to name the row.
    EXPECT_NE(std::string::npos,
              scripted.page_text(0).find("   1. THE CIRCLE  [CLOSED]\n"));
    EXPECT_NE(std::string::npos,
              scripted.page_text(0).find("   2. THE ARENA\n"));
    EXPECT_NE(std::string::npos,
              scripted.page_text(0).find("   3. FIELD KIT  60g\n"));
    // The purse is on the page that quotes the price, and it tracks the
    // debit.
    EXPECT_NE(std::string::npos,
              scripted.page_text(0).find("COMPANY  DEP 0/0  GOLD 100\n"));
    // After the action, the refetched page re-derived the scripted label —
    // and the row the player can no longer buy says so BEFORE the click.
    EXPECT_NE(std::string::npos,
              scripted.page_text(3).find("   3. KIT OWNED  60g  [NEED GOLD]\n"));
    EXPECT_NE(std::string::npos,
              scripted.page_text(3).find("COMPANY  DEP 0/0  GOLD 40\n"));
    // The lines-only subpage prompts with the rowless label (prompt 5 —
    // prompts 0-4 are the ROOT page's invalid/action/refusal/page steps).
    EXPECT_EQ("LORE", scripted.prompts[5].title);
    EXPECT_EQ("Camp # (0 = back): ", scripted.prompts[5].label);
    EXPECT_NE(std::string::npos,
              scripted.page_text(5).find("Only words here.\n"));
    // The final prompt shows the applied cursor's CURRENT marker — and the
    // CLOSED road never wears it.
    EXPECT_NE(std::string::npos,
              scripted.page_text(9).find("   2. THE ARENA  [CURRENT]\n"));
    EXPECT_NE(std::string::npos,
              scripted.page_text(9).find("   1. THE CIRCLE  [CLOSED]\n"));

    (void)remove_user_file("save/pickerdrv.gtl");
    (void)og::data::set_active_company_slot("save0");
    if (previous_mount != "gladiator")
    {
        (void)unmount_campaign_package_with_error("gladiator");
        if (!previous_mount.empty())
            (void)mount_campaign_package_with_error(previous_mount);
    }
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
    og::ui::run_terminal_campaign_page(save_, scripted.io(), "");

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

// No registration behind a book door (a stale door, or a page the book will
// not hand back): the driver refuses in the page's own words and never
// prompts. There is no separate "no book" line any more — a campaign with no
// book never grows a door to click.
TEST_F(CampaignPickerSessionTest, terminal_driver_guards_when_no_book)
{
    register_script(R"LUA(og.log("no campaign registration"))LUA");

    ScriptedTerminalIo scripted;
    scripted.save = &save_;
    og::ui::run_terminal_campaign_page(save_, scripted.io(), "");

    ASSERT_EQ(1u, scripted.notices.size());
    EXPECT_EQ(og::ui::kCampaignPageUnreadableMessage, scripted.notices[0]);
    EXPECT_TRUE(scripted.prompts.empty())
        << "the guard path must never open the book prompt";
}

// A level row the campaign does not carry is refused INSIDE the book, exactly
// as the camp's docket refuses it: the terminal set-level tail only moves the
// cursor, so a row that already reads [CLOSED] must never answer "Level set
// to" and leave a scen_num the campaign cannot load.
TEST_F(CampaignPickerSessionTest, terminal_driver_refuses_closed_level_rows)
{
    save_.scen_num = 7;
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return { title = "BOOK",
             entries = { { id = "gone", kind = "level", level = 4242 } } }
  end,
}))LUA");

    ScriptedTerminalIo scripted;
    scripted.save = &save_;
    scripted.answers = {"1", "0"};
    og::ui::run_terminal_campaign_page(save_, scripted.io(), "");

    const std::vector<std::string> expected_notices = {
        std::string(og::ui::kCampaignLevelClosedMessage),
    };
    EXPECT_EQ(expected_notices, scripted.notices);
    EXPECT_EQ(-1, scripted.applied_level)
        << "a closed road must be refused before the client tail runs";
    EXPECT_EQ(7, save_.scen_num);
    ASSERT_FALSE(scripted.prompts.empty());
    EXPECT_NE(std::string::npos,
              scripted.page_text(0).find("   1. SCEN 4242  [CLOSED]\n"))
        << "the row says so before the click; the answer must agree";
}

// ---------------------------------------------------------------------------
// The shared terminal CAMP driver (docs/basecamp-zones-design.md "Terminals")
// ---------------------------------------------------------------------------
//
// The whole composed screen is pinned by the text drive
// (tests/unit/test_platform_headless.cpp). These arms hold the paths a
// terminal client cannot reach on its own: both terminals always answer
// host, and their own campaigns always parse.

// A campaign with neither a camp nor a book has no terminal camp: the default
// composition is a full-capability roster, and the terminals already carry
// that on their own Team Build rows.
TEST_F(CampaignPickerSessionTest, terminal_camp_guards_when_no_zone)
{
    register_script(R"LUA(og.log("no campaign registration"))LUA");

    ScriptedTerminalIo scripted;
    scripted.save = &save_;
    og::ui::run_terminal_campaign_camp(save_, scripted.io());

    ASSERT_EQ(1u, scripted.notices.size());
    EXPECT_EQ(og::ui::kCampaignCampNoZoneMessage, scripted.notices[0]);
    EXPECT_TRUE(scripted.prompts.empty())
        << "the guard path must never open the camp prompt";
}

// A campaign that registered a BOOK but composed no camp still has to be able
// to open its book: every client enters a book through a camp page row, so
// the camp door shows the transitional book-door composition — the default
// roster plus one page row named by the book's own root title.
TEST_F(CampaignPickerSessionTest, terminal_camp_opens_the_book_without_a_zone)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return { title = "KETTLE'S BOOK",
             lines = { "The ledger lies open." },
             entries = { { id = "300", label = "THE CIRCLE", kind = "level", level = 300 } } }
  end,
}))LUA");

    ScriptedTerminalIo scripted;
    scripted.save = &save_;
    scripted.answers = {
        "1",  // camp: the book door -> the book rooted at ""
        "0",  //   book: back at the door's root -> the camp
        "0",  // close the camp
    };
    og::ui::run_terminal_campaign_camp(save_, scripted.io());

    EXPECT_TRUE(scripted.notices.empty()) << "the door must not refuse";
    ASSERT_GE(scripted.prompts.size(), 2u);
    EXPECT_EQ("Camp", scripted.prompts[0].title);
    EXPECT_NE(std::string::npos,
              scripted.page_text(0).find("   1. KETTLE'S BOOK  >\n"))
        << "the door wears the book's own title and the door marker";
    EXPECT_EQ("KETTLE'S BOOK", scripted.prompts[1].title)
        << "the door opens the book at its root page";
    // Nothing is mounted in this fixture, so the labelled road reads CLOSED
    // on the page exactly as it does in the camp docket.
    EXPECT_NE(std::string::npos,
              scripted.page_text(1).find("   1. THE CIRCLE  [CLOSED]\n"));
    // The composition is still the full-capability roster underneath, and
    // both the camp and the book page carry the same header strip.
    EXPECT_NE(std::string::npos,
              scripted.page_text(0).find("COMPANY  DEP 0/0  GOLD 5000\n"));
    EXPECT_NE(std::string::npos,
              scripted.page_text(1).find("COMPANY  DEP 0/0  GOLD 5000\n"));
}

// The camp's roster rules bind the terminals' OWN Team Build commands: the
// shared refusal composer is what both clients ask before a deploy toggle, a
// train or a hire. Benching a deployed hero stays legal — the lock is a
// deploy courtesy, not a cage.
TEST_F(CampaignPickerSessionTest, terminal_roster_refusal_answers_the_camp)
{
    save_.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save_.team_list[0]->name = "ALPHA";
    save_.team_list[0]->deployed = false;  // a bench, so the ask is a deploy
    save_.team_size = 1;
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = { { kind = "roster",
      can_train = false,
      can_hire = false,
      locks = { { unset = true, reason = "Swear first." } } } } }
  end,
}))LUA");

    using og::ui::TerminalRosterCommand;
    // An unsworn hero carries the unset lock's own reason.
    const std::optional<std::string> deploy = og::ui::terminal_roster_refusal(
        save_, TerminalRosterCommand::Deploy, 0);
    ASSERT_TRUE(deploy.has_value());
    EXPECT_EQ("Swear first.", *deploy);
    // Benching is never refused.
    save_.team_list[0]->deployed = true;
    EXPECT_FALSE(og::ui::terminal_roster_refusal(
                     save_, TerminalRosterCommand::Deploy, 0)
                     .has_value());
    save_.team_list[0]->deployed = false;
    // An empty or out-of-range slot is the caller's own bounds problem, not
    // the camp's.
    EXPECT_FALSE(og::ui::terminal_roster_refusal(
                     save_, TerminalRosterCommand::Deploy, 1)
                     .has_value());
    EXPECT_FALSE(og::ui::terminal_roster_refusal(
                     save_, TerminalRosterCommand::Deploy, -1)
                     .has_value());
    // A hero the lock no longer matches deploys: the unset lock stops
    // applying the moment the oath lands.
    save_.team_list[0]->campaign_tag = 1;
    EXPECT_FALSE(og::ui::terminal_roster_refusal(
                     save_, TerminalRosterCommand::Deploy, 0)
                     .has_value());
    save_.team_list[0]->campaign_tag = 0;

    const std::optional<std::string> train = og::ui::terminal_roster_refusal(
        save_, TerminalRosterCommand::Train, -1);
    ASSERT_TRUE(train.has_value());
    EXPECT_EQ(og::ui::kCampaignRosterTrainClosedMessage, *train);
    const std::optional<std::string> hire = og::ui::terminal_roster_refusal(
        save_, TerminalRosterCommand::Hire, -1);
    ASSERT_TRUE(hire.has_value());
    EXPECT_EQ(og::ui::kCampaignRosterHireClosedMessage, *hire);
}

// A camp that cleared can_deploy refuses every deploy, lock or no lock.
TEST_F(CampaignPickerSessionTest, terminal_roster_refusal_reads_can_deploy)
{
    save_.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save_.team_list[0]->deployed = false;
    save_.team_size = 1;
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = { { kind = "roster", can_deploy = false } } }
  end,
}))LUA");

    const std::optional<std::string> closed = og::ui::terminal_roster_refusal(
        save_, og::ui::TerminalRosterCommand::Deploy, 0);
    ASSERT_TRUE(closed.has_value());
    EXPECT_EQ(og::ui::kCampaignRosterDeployClosedMessage, *closed);
}

// A lock with no reason still has to say something: a silent refusal is a
// padlock the SDL roster can draw, and a prompt cannot.
TEST_F(CampaignPickerSessionTest, terminal_roster_refusal_speaks_when_mute)
{
    save_.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save_.team_list[0]->deployed = false;
    save_.team_size = 1;
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = { { kind = "roster",
      locks = { { unset = true } } } } }
  end,
}))LUA");

    const std::optional<std::string> mute = og::ui::terminal_roster_refusal(
        save_, og::ui::TerminalRosterCommand::Deploy, 0);
    ASSERT_TRUE(mute.has_value());
    EXPECT_EQ(og::ui::kCampaignRosterDeployLockedMessage, *mute);
}

// No camp, no rules: an unscripted campaign keeps the terminals' own roster
// commands exactly as they were.
TEST_F(CampaignPickerSessionTest, terminal_roster_refusal_is_silent_unscripted)
{
    save_.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save_.team_size = 1;
    register_script(R"LUA(og.log("no campaign registration"))LUA");

    using og::ui::TerminalRosterCommand;
    EXPECT_FALSE(og::ui::terminal_roster_refusal(
                     save_, TerminalRosterCommand::Deploy, 0)
                     .has_value());
    EXPECT_FALSE(og::ui::terminal_roster_refusal(
                     save_, TerminalRosterCommand::Train, -1)
                     .has_value());
    EXPECT_FALSE(og::ui::terminal_roster_refusal(
                     save_, TerminalRosterCommand::Hire, -1)
                     .has_value());
}

// The camp's level rows ride the same SET LEVEL host gate as the book's, and
// a page row whose page will not parse refuses in the page's own words —
// the campaign HAS a book, this door just would not open.
TEST_F(CampaignPickerSessionTest, terminal_camp_gates_levels_and_shut_pages)
{
    save_.scen_num = 7;
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = {
      { kind = "actions", entries = {
          { id = "300", label = "THE CIRCLE", kind = "level", level = 300 },
          { id = "vault", label = "THE VAULT", kind = "page" },
        } },
      { kind = "roster" },
    } }
  end,
  picker_menu = function(page_id)
    return { entries = {} }  -- no title: malformed, the page never opens
  end,
}))LUA");

    ScriptedTerminalIo scripted;
    scripted.save = &save_;
    scripted.host = false;
    scripted.answers = {
        "1",  // THE CIRCLE -> refused by the host gate
        "2",  // THE VAULT  -> the door will not open
        "0",  // close the camp
    };
    og::ui::run_terminal_campaign_camp(save_, scripted.io());

    const std::vector<std::string> expected_notices = {
        std::string(og::ui::kCampaignPickerHostGuardMessage),
        std::string(og::ui::kCampaignPageUnreadableMessage),
    };
    EXPECT_EQ(expected_notices, scripted.notices);
    EXPECT_EQ(-1, scripted.applied_level)
        << "the gate must refuse before the client tail runs";
    EXPECT_EQ(7, save_.scen_num);

    ASSERT_FALSE(scripted.prompts.empty());
    EXPECT_EQ("Camp", scripted.prompts[0].title);
    EXPECT_EQ("Camp # [1-2] (0 = back): ", scripted.prompts[0].label);
    EXPECT_NE(std::string::npos,
              scripted.page_text(0).find("   2. THE VAULT  >\n"))
        << "page rows wear the door marker before the click";
    EXPECT_NE(std::string::npos,
              scripted.page_text(0).find("COMPANY  DEP 0/0  GOLD 5000\n"))
        << "the purse rides the camp header strip, not just Team Build";
    EXPECT_NE(std::string::npos, scripted.page_text(0).find("      (empty)\n"));
}

// The oath: cycling a DEPLOYED hero un-deploys first (the SDL rule), the tag
// lands through the assign provider with the full-word toast, and a freeze
// that arrives on the refetch closes the swear prompt with its reason —
// after which the camp's own oath row refuses with the same words instead of
// reopening a prompt that can only say no.
TEST_F(CampaignPickerSessionTest, terminal_camp_oath_undeploys_then_freezes)
{
    save_.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save_.team_list[0]->name = "ALPHA";
    save_.team_list[0]->teamnum = 0;
    save_.team_list[0]->deployed = true;
    save_.team_size = 1;
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    local frozen = ""
    local team = og.campaign_team()
    if team[1] and team[1].tag > 0 then
      frozen = "The Falls parted the company."
    end
    return { widgets = { { kind = "roster",
      assign = { key = "muster", labels = { "WAR", "BURDEN" },
                 frozen = frozen } } } }
  end,
}))LUA");

    // The oath tail autosaves the active company slot; isolate and reap it.
    ASSERT_TRUE(og::data::set_active_company_slot("campdrv"));

    ScriptedTerminalIo scripted;
    scripted.save = &save_;
    scripted.answers = {
        "1",  // camp: the SWEAR door -> the swear prompt
        "1",  // swear: cycle row 1 (unset -> WAR); the refetch freezes it
        "1",  // camp: the SWEAR door again -> refused with the reason
        "0",  // close the camp
    };
    og::ui::run_terminal_campaign_camp(save_, scripted.io());

    const std::vector<std::string> expected_notices = {
        // The oath's destructive side effect belongs in the one thing that
        // speaks: swearing stood ALPHA down, and a player who then presses
        // GO would otherwise launch with an empty muster and no warning.
        "Sworn to WAR. Stood down from the muster.",
        "The Falls parted the company.",
        "The Falls parted the company.",
    };
    EXPECT_EQ(expected_notices, scripted.notices);
    EXPECT_EQ(1, static_cast<int>(save_.team_list[0]->campaign_tag));
    EXPECT_FALSE(save_.team_list[0]->deployed)
        << "a sworn hero leaves the muster board before the oath lands";
    EXPECT_TRUE(user_file_exists("save/campdrv.gtl"))
        << "the oath must run the §3.8 company-autosave tail";

    ASSERT_EQ(4u, scripted.prompts.size());
    EXPECT_EQ("Swear", scripted.prompts[1].title);
    EXPECT_EQ("Swear # [1-1] (0 = done): ", scripted.prompts[1].label);
    EXPECT_NE(std::string::npos,
              scripted.page_text(0).find("   1. SWEAR MUSTER  >\n"))
        << "an open oath column wears the door marker";
    EXPECT_NE(std::string::npos,
              scripted.page_text(2).find(
                  "   1. SWEAR MUSTER - The Falls parted the company.\n"))
        << "a frozen oath states its reason on the row instead of the marker";
    // The oath cell spells the word out on the roster row itself.
    EXPECT_NE(std::string::npos, scripted.page_text(2).find("  WAR\n"));
    // The oath heading is a COLUMN HEADING: it sits directly over the cell
    // it names, not as a third fact in the summary strip. Both lines are
    // read out of the same prompt so the pin cannot drift apart.
    {
        const std::vector<std::string>& lines = scripted.prompts[1].lines;
        ASSERT_GE(lines.size(), 2u);
        const std::size_t heading = lines[0].find("MUSTER");
        const std::size_t cell = lines[1].find('-', lines[1].find("XP="));
        ASSERT_NE(std::string::npos, heading);
        ASSERT_NE(std::string::npos, cell);
        EXPECT_EQ(heading, cell)
            << "MUSTER must head the oath column:\n"
            << lines[0] << "\n"
            << lines[1];
    }
    // The swear screen names BOTH oaths before the player commits to one:
    // the cycle is the only way to reach the second word, so a prompt that
    // never prints it is not offering a choice.
    {
        const std::string swear = scripted.page_text(1);
        EXPECT_NE(std::string::npos,
                  swear.find("A row number swears that hero: "
                             "- -> WAR -> BURDEN -> WAR\n"))
            << "every legend line must fit the terminal budget uncut:\n"
            << swear;
        EXPECT_NE(std::string::npos,
                  swear.find("[X] deployed   [ ] benched   [L] locked   "
                             "- unsworn"))
            << swear;
        EXPECT_NE(std::string::npos, swear.find("stands them down"))
            << "the un-deploy is stated before the commit, too:\n" << swear;
    }

    (void)remove_user_file("save/campdrv.gtl");
    (void)og::data::set_active_company_slot("save0");
}
