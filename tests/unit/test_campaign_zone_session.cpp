/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// CampaignZoneSession (docs/basecamp-zones-design.md): the SDL-free Base
// Camp gameplay-zone state machine — default-composition equivalence (no
// hook / erroring hook / over-budget layout all land on the same built-in
// full-capability roster), the whole-row-unit layout arithmetic, action-row
// decoration through the shared helpers, the debit-then-dispatch act()
// path over the REAL provider glue, the assign/lock helpers, and the
// applied-settings fingerprint trigger.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/campaign_state_providers.h>
#include <openglad/resources/save_data.h>

#include <memory>
#include <string>
#include <vector>

using namespace og::script;
using og::ui::CampaignPickerSession;
using og::ui::CampaignZoneSession;

namespace {

// Chunk names deliberately do NOT start with `packs/` (the pack-Lua
// coverage inventory rule; these throwaway chunks exist nowhere on disk).
constexpr const char* kPack = "test.zonesession";

class CampaignZoneSessionTest : public ::testing::Test {
protected:
    CampaignZoneSessionTest()
    {
        previous_ = current_game;
        current_game = nullptr;  // dispatch resolves the shared UI VM
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_pack_lib_modules();
        hooks::clear_campaign_providers();
        save_.current_campaign = "testcamp";
        save_.my_team = 0;
        save_.m_totalcash[0] = 1000;
        save_.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
        save_.team_list[0]->name = "Alpha";
        save_.team_list[0]->teamnum = 0;
        save_.team_size = 1;
        hooks::install_campaign_providers(
            og::data::make_campaign_providers(save_));
    }

    ~CampaignZoneSessionTest() override
    {
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_pack_lib_modules();
        hooks::clear_campaign_providers();
        current_game = previous_;
    }

    static void register_script(const std::string& source,
                                const char* chunk = "zonesess/scripts/c.lua")
    {
        register_pack_script({kPack, chunk, source});
    }

    SaveData save_;
    GameplayContext* previous_ = nullptr;
};

// The default composition every fallback must land on: one roster widget,
// full capability, the whole 8-unit band, header at the classic spot.
void expect_default_composition(const CampaignZoneSession& zone)
{
    EXPECT_FALSE(zone.scripted());
    EXPECT_FALSE(zone.composed()) << "the bare default has nothing to open";
    const CampaignZoneSession::RosterLayout& roster = zone.roster();
    EXPECT_TRUE(roster.can_deploy);
    EXPECT_TRUE(roster.can_train);
    EXPECT_TRUE(roster.can_reorder);
    EXPECT_TRUE(roster.can_team);
    EXPECT_TRUE(roster.can_hire);
    EXPECT_TRUE(roster.locks.empty());
    EXPECT_FALSE(roster.assign.active);
    EXPECT_EQ(0, roster.start_unit);
    EXPECT_EQ(0, roster.row_start_unit);
    EXPECT_TRUE(roster.header_at_top);
    EXPECT_EQ(CampaignZoneSession::kZoneRowUnits, roster.rows_per_page);
    EXPECT_TRUE(zone.texts().empty());
    EXPECT_TRUE(zone.actions().empty());
    EXPECT_EQ(nullptr, zone.readout());
}

}  // namespace

// ---------------------------------------------------------------------------
// Default-composition equivalence: every "no scripted zone" answer renders
// the SAME built-in composition (one renderer).
// ---------------------------------------------------------------------------

TEST_F(CampaignZoneSessionTest, no_registration_fetches_the_default)
{
    register_script(R"LUA(og.log("no campaign registration"))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    expect_default_composition(zone);
}

// A registration with a BOOK but no base_camp hook gets the transitional
// book-door composition instead of the bare default: every client enters a
// book through a camp page row, so the default roster alone would strand the
// book. The door is one page row onto "" (the book's root), named by the
// book's own root title, and the roster keeps every capability.
TEST_F(CampaignZoneSessionTest, registration_without_base_camp_opens_the_book)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return { title = "KETTLE'S BOOK" }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();

    EXPECT_FALSE(zone.scripted()) << "the campaign composed no camp";
    EXPECT_TRUE(zone.composed()) << "but it does have a door to show";
    ASSERT_EQ(1u, zone.actions().size());
    ASSERT_EQ(1u, zone.actions()[0].rows.size());
    const CampaignZoneSession::Row& door = zone.actions()[0].rows[0];
    EXPECT_EQ(CampaignPickerSession::Kind::Page, door.kind);
    EXPECT_EQ("", door.id) << "the book's root page id";
    EXPECT_EQ("KETTLE'S BOOK", door.label);
    // The roster below it keeps full capability and the remaining band.
    const CampaignZoneSession::RosterLayout& roster = zone.roster();
    EXPECT_TRUE(roster.can_deploy);
    EXPECT_TRUE(roster.can_hire);
    EXPECT_FALSE(roster.header_at_top) << "a roster that does not lead pays "
                                          "one unit for its header";
    EXPECT_TRUE(zone.texts().empty());
    EXPECT_EQ(nullptr, zone.readout());
}

// A campaign with neither hook keeps the bare default — no door, nothing to
// open.
TEST_F(CampaignZoneSessionTest, registration_without_either_hook_is_default)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_action = function(entry_id)
    return { message = "no book here" }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    expect_default_composition(zone);
}

TEST_F(CampaignZoneSessionTest, erroring_hook_falls_to_the_default)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    error("the fire is out")
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    expect_default_composition(zone);
}

TEST_F(CampaignZoneSessionTest, malformed_zone_falls_to_the_default)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = { { kind = "text", lines = { "no roster" } } } }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    expect_default_composition(zone);
}

// The over-budget LAYOUT rejection (post-parse): a legal-by-parse
// composition whose fixed shares leave the roster under 2 rows.
TEST_F(CampaignZoneSessionTest, over_budget_layout_falls_to_the_default)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "text", weight = 4, lines = { "a" } },
        { kind = "actions", weight = 3,
          entries = { { id = "x", label = "X", kind = "action" } } },
        { kind = "roster" },
      },
    }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    // 4 + 3 + roster header 1 = 8 => zero roster rows: over-budget.
    expect_default_composition(zone);
}

TEST_F(CampaignZoneSessionTest, explicit_roster_weight_past_the_band_rejects)
{
    hooks::CampaignZone raw;
    hooks::CampaignZoneWidget text;
    text.kind = hooks::CampaignZoneWidget::Kind::Text;
    text.lines = {"a"};
    raw.widgets.push_back(text);
    hooks::CampaignZoneWidget roster;
    roster.kind = hooks::CampaignZoneWidget::Kind::Roster;
    roster.weight = 7;  // 1 text + 1 header + 7 rows = 9 > 8
    raw.widgets.push_back(roster);
    CampaignZoneSession zone(save_);
    EXPECT_FALSE(zone.adopt(raw));
}

// ---------------------------------------------------------------------------
// Layout arithmetic: integer row units on the 8-unit band.
// ---------------------------------------------------------------------------

TEST_F(CampaignZoneSessionTest, four_widget_layout_assigns_whole_row_units)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "readout",
          items = { { label = "COIN", value = "1000" } } },
        { kind = "text", lines = { "one", "two" } },
        { kind = "actions",
          entries = {
            { id = "a", label = "A", kind = "action" },
            { id = "b", label = "B", kind = "action" },
          } },
        { kind = "roster" },
      },
    }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());

    // The roster does not lead, so it moves its column headings into the
    // grid and abandons the classic header slot — the readout takes that
    // band as the panel's heading and costs NO row unit. 2 text lines =
    // 16px -> 2 units at 0; actions default share = min(2, 3) = 2 units at
    // 2; roster: header unit at 4, rows 5..7 (the remainder).
    ASSERT_NE(nullptr, zone.readout());
    EXPECT_TRUE(zone.readout()->in_header_band);
    ASSERT_EQ(1u, zone.texts().size());
    EXPECT_EQ(0, zone.texts()[0].start_unit);
    EXPECT_EQ(2, zone.texts()[0].units);
    ASSERT_EQ(1u, zone.actions().size());
    EXPECT_EQ(2, zone.actions()[0].start_unit);
    EXPECT_EQ(2, zone.actions()[0].units);
    EXPECT_EQ(2, zone.actions()[0].page.rows_per_page)
        << "two rows in a two-slot band need no pager row";
    EXPECT_FALSE(zone.actions()[0].more_row);
    const CampaignZoneSession::RosterLayout& roster = zone.roster();
    EXPECT_EQ(4, roster.start_unit);
    EXPECT_FALSE(roster.header_at_top);
    EXPECT_EQ(5, roster.row_start_unit);
    EXPECT_EQ(3, roster.rows_per_page);
}

// A roster that DOES lead keeps the classic header slot for its column
// headings, so a readout in that composition stays a grid row.
TEST_F(CampaignZoneSessionTest, roster_first_keeps_the_readout_in_the_grid)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "roster", weight = 6 },
        { kind = "readout",
          items = { { label = "COIN", value = "1000" } } },
      },
    }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_NE(nullptr, zone.readout());
    EXPECT_FALSE(zone.readout()->in_header_band);
    EXPECT_EQ(6, zone.readout()->start_unit);
    EXPECT_TRUE(zone.roster().header_at_top);
    EXPECT_EQ(6, zone.roster().rows_per_page);
}

TEST_F(CampaignZoneSessionTest, roster_first_keeps_the_classic_header)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "roster" },
        { kind = "actions",
          entries = { { id = "a", label = "A", kind = "action" } } },
      },
    }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    const CampaignZoneSession::RosterLayout& roster = zone.roster();
    EXPECT_EQ(0, roster.start_unit);
    EXPECT_EQ(0, roster.row_start_unit);
    EXPECT_TRUE(roster.header_at_top)
        << "a leading roster keeps the y=33 header outside the grid";
    EXPECT_EQ(7, roster.rows_per_page) << "8 units minus the 1-unit action";
    ASSERT_EQ(1u, zone.actions().size());
    EXPECT_EQ(7, zone.actions()[0].start_unit)
        << "the actions band follows the roster in script order";
}

TEST_F(CampaignZoneSessionTest, actions_overflow_pages_in_place)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    local entries = {}
    for i = 1, 8 do
      entries[i] = { id = "e" .. i, label = "E" .. i, kind = "action" }
    end
    return {
      widgets = {
        { kind = "actions", weight = 3, entries = entries },
        { kind = "roster" },
      },
    }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(1u, zone.actions().size());
    const CampaignZoneSession::ActionsLayout& actions = zone.actions()[0];
    EXPECT_EQ(8, actions.page.item_count);
    // Round 4: the window's last slot is the MORE pager ROW, so a 3-slot
    // band windows TWO authored rows at a time.
    EXPECT_EQ(2, actions.page.rows_per_page);
    EXPECT_TRUE(actions.page.multi_page())
        << "8 entries over a 3-slot band page in place";
    ASSERT_TRUE(actions.more_row);
    EXPECT_EQ("MORE", actions.more.label);
    EXPECT_EQ("1/4", actions.more.note);
    // The refetch preserves the widget's window (the after-own-mutation
    // trigger must not yank the page under the pointer), pager row and
    // all.
    ASSERT_TRUE(zone.step_actions_window(0));
    EXPECT_EQ("2/4", zone.actions()[0].more.note);
    zone.refetch();
    EXPECT_EQ(1, zone.actions()[0].page.page);
    EXPECT_EQ("2/4", zone.actions()[0].more.note);
}

// The window a paged docket OPENS on is the one holding the [CURRENT] row
// (og::ui::open_row_window, 2026-09-22, PR #307): the row the camp is
// pointing at — the one the command strip's GO is about — is never the row
// behind the pager. Browsing is the player's after that: the pager row
// moves the window and an own-mutation refetch keeps it, and only a NEW
// [CURRENT] row (a level set through the docket or the wizard, under the
// open camp) re-opens the band.
TEST_F(CampaignZoneSessionTest, a_paged_docket_opens_on_the_current_row)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "actions", weight = 3,
          entries = {
            { id = "e1", label = "E1", kind = "action" },
            { id = "L6", label = "THE LATE ROAD", kind = "level", level = 6 },
            { id = "e3", label = "E3", kind = "action" },
            { id = "L4", label = "THE ROAD", kind = "level", level = 4 },
            { id = "e5", label = "E5", kind = "action" },
            { id = "e6", label = "E6", kind = "action" },
          } },
        { kind = "roster" },
      },
    }
  end,
  picker_action = function(entry_id)
    return { message = "Noted." }
  end,
}))LUA");
    save_.scen_num = 4;
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(1u, zone.actions().size());
    EXPECT_EQ(6, zone.actions()[0].page.item_count);
    EXPECT_EQ(2, zone.actions()[0].page.rows_per_page)
        << "a 3-slot band spends one slot on the pager row";
    EXPECT_EQ(3, og::ui::current_row_index(zone.actions()[0].rows))
        << "the fourth row is the one the cursor sits on";
    EXPECT_EQ(1, zone.actions()[0].page.page)
        << "so the camp opens on the SECOND window, not at home";
    EXPECT_EQ(2, zone.actions()[0].page.first_index());
    EXPECT_EQ(4, zone.actions()[0].page.end_index());
    EXPECT_EQ("2/3", zone.actions()[0].more.note);
    EXPECT_EQ("L4", zone.actions()[0].current_id);

    // Browsing: the pager row moves the window, and a refetch (the
    // after-own-mutation trigger — a purchase, a state write) leaves the
    // player where they were reading.
    ASSERT_TRUE(zone.step_actions_window(0));
    EXPECT_EQ(2, zone.actions()[0].page.page);
    zone.refetch();
    EXPECT_EQ(2, zone.actions()[0].page.page)
        << "a refetch with the same [CURRENT] row keeps the browsed window";
    EXPECT_EQ("3/3", zone.actions()[0].more.note);
    const CampaignZoneSession::Outcome acted = zone.act(0, 4);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Acted, acted.kind);
    EXPECT_EQ(2, zone.actions()[0].page.page)
        << "and so does the refetch an ACTED row fires";

    // A level set under the open camp IS the camp pointing somewhere
    // else: the next refetch re-opens the band on the new [CURRENT] row.
    save_.scen_num = 6;
    zone.refetch();
    EXPECT_EQ(1, og::ui::current_row_index(zone.actions()[0].rows));
    EXPECT_EQ(0, zone.actions()[0].page.page)
        << "a NEW current row re-opens the window that holds it";
    EXPECT_EQ("1/3", zone.actions()[0].more.note);
    EXPECT_EQ("L6", zone.actions()[0].current_id);

    // And a docket that marks no row at all opens at home.
    save_.scen_num = 99;
    zone.refetch();
    EXPECT_EQ(-1, og::ui::current_row_index(zone.actions()[0].rows));
    EXPECT_EQ(0, zone.actions()[0].page.page);
    EXPECT_EQ("", zone.actions()[0].current_id);
}

// ---------------------------------------------------------------------------
// Decoration: the shared row helpers behind both sessions.
// ---------------------------------------------------------------------------

TEST_F(CampaignZoneSessionTest, action_rows_decorate_cost_and_affordability)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "actions",
          entries = {
            { id = "cheap", label = "CHEAP", kind = "action", cost = 100 },
            { id = "dear", label = "DEAR", kind = "action", cost = 5000 },
            { id = "lv", label = "", kind = "level", level = 31999 },
          } },
        { kind = "roster" },
      },
    }
  end,
}))LUA");
    save_.add_level_completed("testcamp", 31999);
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(1u, zone.actions().size());
    const std::vector<CampaignZoneSession::Row>& rows =
        zone.actions()[0].rows;
    ASSERT_EQ(3u, rows.size());
    EXPECT_TRUE(rows[0].affordable) << "1000g wallet covers 100g";
    EXPECT_FALSE(rows[1].affordable) << "1000g wallet refuses 5000g";
    EXPECT_TRUE(rows[2].is_level());
    EXPECT_TRUE(rows[2].cleared) << "level rows decorate CLEARED";
    EXPECT_EQ("SCEN 31999", rows[2].label)
        << "empty level labels take the save-side fill (no such scenario "
           "file whatever campaign an earlier test left mounted)";
}

// ---------------------------------------------------------------------------
// act(): the shared debit-then-dispatch machinery.
// ---------------------------------------------------------------------------

TEST_F(CampaignZoneSessionTest, act_debits_dispatches_toasts_and_refetches)
{
    register_script(R"LUA(og.register_campaign_hooks({
  vars = { "kit" },
  base_camp = function()
    local label = "FIELD KIT"
    if og.campaign_state_get("kit") == 1 then
      label = "KIT OWNED"
    end
    return {
      widgets = {
        { kind = "actions",
          entries = {
            { id = "buy", label = label, kind = "action", cost = 60 },
          } },
        { kind = "roster" },
      },
    }
  end,
  picker_action = function(entry_id)
    og.campaign_state_set("kit", 1)
    return { message = "Kit stowed." }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ("FIELD KIT", zone.actions()[0].rows[0].label);

    const CampaignZoneSession::Outcome outcome = zone.act(0, 0);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Acted, outcome.kind);
    EXPECT_EQ(940u, save_.m_totalcash[0]) << "debit BEFORE dispatch";
    EXPECT_EQ(1, save_.campaign_state_get("testcamp", "kit"));
    EXPECT_EQ("Kit stowed.", zone.take_message());
    EXPECT_EQ("", zone.take_message()) << "the read clears the toast";
    EXPECT_EQ("KIT OWNED", zone.actions()[0].rows[0].label)
        << "an Acted outcome refetches the composition";
}

TEST_F(CampaignZoneSessionTest, act_refuses_unaffordable_without_dispatch)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "actions",
          entries = {
            { id = "dear", label = "DEAR", kind = "action", cost = 5000 },
          } },
        { kind = "roster" },
      },
    }
  end,
  picker_action = function(entry_id)
    return {}
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    const CampaignZoneSession::Outcome outcome = zone.act(0, 0);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Refused, outcome.kind);
    EXPECT_EQ("Not enough gold.", outcome.reason);
    EXPECT_EQ(1000u, save_.m_totalcash[0]) << "no debit on refusal";
}

TEST_F(CampaignZoneSessionTest, act_refunds_when_no_action_hook_serves)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "actions",
          entries = {
            { id = "x", label = "X", kind = "action", cost = 60 },
          } },
        { kind = "roster" },
      },
    }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    const CampaignZoneSession::Outcome outcome = zone.act(0, 0);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Refused, outcome.kind);
    EXPECT_EQ("This book takes no orders.", outcome.reason);
    EXPECT_EQ(1000u, save_.m_totalcash[0])
        << "a book with no picker_action must not charge";
}

TEST_F(CampaignZoneSessionTest, act_ignores_level_page_and_stale_indices)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "actions",
          entries = {
            { id = "p", label = "P", kind = "page" },
            { id = "l", label = "L", kind = "level", level = 2 },
          } },
        { kind = "roster" },
      },
    }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::None, zone.act(0, 0).kind)
        << "page rows belong to the surface (the zone submenu)";
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::None, zone.act(0, 1).kind)
        << "level rows belong to the surface (the set tail)";
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::None, zone.act(0, 9).kind);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::None, zone.act(3, 0).kind);
}

// ---------------------------------------------------------------------------
// Assign / lock helpers.
// ---------------------------------------------------------------------------

TEST_F(CampaignZoneSessionTest, assign_cycle_never_returns_to_unset)
{
    EXPECT_EQ(1, CampaignZoneSession::next_assign_tag(0));
    EXPECT_EQ(2, CampaignZoneSession::next_assign_tag(1));
    EXPECT_EQ(1, CampaignZoneSession::next_assign_tag(2));
    EXPECT_EQ(1, CampaignZoneSession::next_assign_tag(255)) << "junk -> 1";
}

TEST_F(CampaignZoneSessionTest, assign_glyph_takes_the_label_first_letter)
{
    hooks::CampaignAssignSpec spec;
    spec.labels = {"war", "Burden"};
    EXPECT_EQ('-', CampaignZoneSession::assign_glyph(spec, 0));
    EXPECT_EQ('W', CampaignZoneSession::assign_glyph(spec, 1));
    EXPECT_EQ('B', CampaignZoneSession::assign_glyph(spec, 2));
    EXPECT_EQ('-', CampaignZoneSession::assign_glyph(spec, 3));
}

// The roster's oath column spells the choice out and heads itself with
// the script's own channel key. A lone letter on a coloured chip is the
// TEAM widget, and the oath has to stay readable after its toast expires.
TEST_F(CampaignZoneSessionTest, assign_cell_and_header_speak_in_words)
{
    hooks::CampaignAssignSpec spec;
    spec.key = "road";
    spec.labels = {"war", "Burden"};
    EXPECT_EQ("-", CampaignZoneSession::assign_cell_text(spec, 0, 6));
    EXPECT_EQ("WAR", CampaignZoneSession::assign_cell_text(spec, 1, 6));
    EXPECT_EQ("BURDEN", CampaignZoneSession::assign_cell_text(spec, 2, 6));
    EXPECT_EQ("-", CampaignZoneSession::assign_cell_text(spec, 3, 6));
    EXPECT_EQ("ROAD", CampaignZoneSession::assign_header_text(spec, 6));

    // Over-long words are cut with the marker, never silently truncated.
    spec.labels = {"PILGRIMAGE", "war"};
    EXPECT_EQ("PILG..", CampaignZoneSession::assign_cell_text(spec, 1, 6));

    // A spec with no key still names the column something honest.
    spec.key.clear();
    EXPECT_EQ("OATH", CampaignZoneSession::assign_header_text(spec, 6));

    // An empty label falls back to the unsworn dash rather than a blank.
    spec.labels = {"", "war"};
    EXPECT_EQ("-", CampaignZoneSession::assign_cell_text(spec, 1, 6));
}

TEST_F(CampaignZoneSessionTest, deploy_lock_matches_tag_or_unset)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "roster",
          locks = {
            { unset = true, reason = "Swear first." },
            { tag = 2, reason = "The east road is closed." },
          } },
      },
    }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_NE(nullptr, zone.deploy_lock_for_tag(0));
    EXPECT_EQ("Swear first.", zone.deploy_lock_for_tag(0)->reason);
    EXPECT_EQ(nullptr, zone.deploy_lock_for_tag(1));
    ASSERT_NE(nullptr, zone.deploy_lock_for_tag(2));
    EXPECT_EQ("The east road is closed.",
              zone.deploy_lock_for_tag(2)->reason);
}

// A lock is mechanical: the fetch that declares it stands the refused own
// heroes down. Otherwise a company deployed BEFORE the composition existed
// (the normal order — deploy, fight, and only then does the camp learn who
// is away) marches on while the padlock and the prose say it does not.
TEST_F(CampaignZoneSessionTest, a_fetch_stands_the_locked_heroes_down)
{
    save_.team_list[1] = std::make_unique<guy>(FAMILY_SOLDIER);
    save_.team_list[1]->name = "Beta";
    save_.team_list[1]->campaign_tag = 1;
    save_.team_size = 2;
    save_.team_list[0]->deployed = true;  // unsworn: the unset lock refuses
    save_.team_list[1]->deployed = true;  // tag 1: free to march
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "roster",
          locks = { { unset = true, reason = "Swear first." } } },
      },
    }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    EXPECT_FALSE(save_.team_list[0]->deployed)
        << "the unsworn hero the camp refuses must not march";
    EXPECT_TRUE(save_.team_list[1]->deployed)
        << "a hero no lock matches keeps his place";

    // Enforce::None composes the same camp without touching the sortie —
    // the refusal composer's mode, so asking about a toggle never moves it.
    save_.team_list[0]->deployed = true;
    CampaignZoneSession quiet(save_);
    quiet.fetch(CampaignZoneSession::Enforce::None);
    ASSERT_NE(nullptr, quiet.deploy_lock_for_tag(0));
    EXPECT_TRUE(save_.team_list[0]->deployed);
}

// Another machine's hero is never stood down: the tags and the locks come
// from THIS company's book, so enforcement stops at the own, editable slots.
TEST_F(CampaignZoneSessionTest, a_foreign_slot_keeps_its_place)
{
    save_.team_list[0]->deployed = true;  // unsworn, and the lock refuses it
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "roster",
          locks = { { unset = true, reason = "Swear first." } } },
      },
    }
  end,
}))LUA");
    const og::ui::PickerSaveSlotEditableCallback saved =
        og::ui::g_picker_save_slot_editable_callback;
    og::ui::g_picker_save_slot_editable_callback = [](int) { return false; };
    {
        CampaignZoneSession zone(save_);
        zone.fetch();
        ASSERT_NE(nullptr, zone.deploy_lock_for_tag(0));
        EXPECT_TRUE(save_.team_list[0]->deployed)
            << "a slot this machine may not edit is not this camp's to bench";
    }
    og::ui::g_picker_save_slot_editable_callback = saved;
    CampaignZoneSession zone(save_);
    zone.fetch();
    EXPECT_FALSE(save_.team_list[0]->deployed);
}

// A composition with no lock never touches the roster: the default zone, the
// unscripted campaign and every open-camp state leave the sortie alone.
TEST_F(CampaignZoneSessionTest, an_unlocked_fetch_leaves_the_sortie_alone)
{
    save_.team_list[0]->deployed = true;
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = { { kind = "roster" } } }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    EXPECT_TRUE(zone.roster().locks.empty());
    EXPECT_TRUE(save_.team_list[0]->deployed);
}

TEST_F(CampaignZoneSessionTest, assign_spec_parses_active_with_frozen)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "roster",
          assign = { key = "road", labels = { "WAR", "BURDEN" },
                     frozen = "The Falls parted the company." } },
      },
    }
  end,
}))LUA");
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    EXPECT_TRUE(zone.roster().assign.active);
    EXPECT_EQ("road", zone.roster().assign.key);
    EXPECT_EQ("The Falls parted the company.", zone.roster().assign.frozen);
}

// ---------------------------------------------------------------------------
// The applied-settings fingerprint (fetch trigger 4).
// ---------------------------------------------------------------------------

TEST_F(CampaignZoneSessionTest, settings_fingerprint_seeds_then_detects)
{
    CampaignZoneSession zone(save_);
    zone.fetch();
    EXPECT_FALSE(zone.settings_fingerprint_changed())
        << "the first call seeds without firing";
    EXPECT_FALSE(zone.settings_fingerprint_changed());
    save_.generator_rate = 200;
    EXPECT_TRUE(zone.settings_fingerprint_changed())
        << "an applied lobby-settings change fires the trigger";
    EXPECT_FALSE(zone.settings_fingerprint_changed())
        << "the fire re-seeds";
    save_.scen_num = 99;
    EXPECT_FALSE(zone.settings_fingerprint_changed())
        << "scen_num is the reload guard's trigger, not this one";
}
