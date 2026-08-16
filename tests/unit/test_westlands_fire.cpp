/* The Company Fire (campaigns/westlands/packs/westlands.fire): the
 * scripted camp of War of the Westlands. Covers the whole pack against
 * the SHIPPED builtin/westlands.glad:
 *   - page gates: the root fire's stanzas/milestones/full-circle shift,
 *     THE ROAD mirroring the authored 47-exit graph level by level, the
 *     QUARTERMASTER offer windows over every completed/state permutation,
 *     THE LEDGER's line permutations (incl. the 24-from-both-roads case);
 *   - actions through CampaignPickerSession over the real provider glue:
 *     debits, the delve grant, one-shot disappearance, cap refusals, and
 *     every defensive refusal arm via direct hook dispatch;
 *   - var == 0 byte-identity on every consequence level (census AND
 *     rng_.state_ pinned against a fire-pack-unregistered baseline);
 *   - taken-path sims: exact spawn names/teams/levels/tiles, the Sneak
 *     lvl-8-at-var0 / lvl-5-at-half-hp-at-var1 soften, provision drops
 *     per hard-road level, and spawn-tile footing on the shipped grids.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "westlands_sim_fixture.h"

#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/resources/campaign_state_providers.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

using og::ui::CampaignPickerSession;
using westlands_fixture::LoadedWestlandsLevel;
using westlands_fixture::MountedCampaignTest;
namespace hooks = og::script::hooks;

constexpr const char* kFirePackId = "westlands.fire";
constexpr const char* kCampaign = "westlands";

// ---------------------------------------------------------------------------
// Picker-side fixture: the mounted shipped package, the REAL provider glue
// over a scripted SaveData, dispatch in the shared UI VM.
// ---------------------------------------------------------------------------

class WestlandsFireBook : public MountedCampaignTest
{
protected:
    WestlandsFireBook()
        : MountedCampaignTest(kCampaign)
    {
    }

    void SetUp() override
    {
        MountedCampaignTest::SetUp();
        previous_ = current_game;
        current_game = nullptr;  // dispatch resolves the shared UI VM
        hooks::clear_campaign_providers();
        save_.current_campaign = kCampaign;
        save_.my_team = 0;
        save_.scen_num = 1;
        hooks::install_campaign_providers(
            og::data::make_campaign_providers(save_));
    }

    void TearDown() override
    {
        hooks::clear_campaign_providers();
        current_game = previous_;
        MountedCampaignTest::TearDown();
    }

    // Direct page fetch (the session does the same, plus decoration).
    hooks::CampaignPage fetch(const std::string& page_id)
    {
        hooks::CampaignPage page;
        EXPECT_TRUE(hooks::campaign_picker_page(page_id, page))
            << "page '" << page_id << "' must answer";
        return page;
    }

    static int row_named(const CampaignPickerSession::DecoratedPage& page,
                         const std::string& label)
    {
        for (std::size_t i = 0; i < page.rows.size(); i++)
            if (page.rows[i].label == label)
                return static_cast<int>(i);
        return -1;
    }

    static int entry_named(const hooks::CampaignPage& page,
                           const std::string& label)
    {
        for (std::size_t i = 0; i < page.entries.size(); i++)
            if (page.entries[i].label == label)
                return static_cast<int>(i);
        return -1;
    }

    SaveData save_;
    GameplayContext* previous_ = nullptr;
};

// The sim-side fixture is the plain mounted-campaign one; each test builds
// its LoadedWestlandsLevel and injects world.campaign_vars directly (the
// authoritative sync twins are pinned by HeadlessServerRuntimeTest.
// authoritative_sync_replaces_campaign_vars_with_registered_names_only and
// ScreenExtended.sync_world_from_save_data_replaces_campaign_vars; the hook
// contract is the vars list on the world).
class WestlandsFireSim : public MountedCampaignTest
{
protected:
    WestlandsFireSim()
        : MountedCampaignTest(kCampaign)
    {
    }
};

// ---------------------------------------------------------------------------
// The root page: stanzas, milestones, the full-circle shift
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireBook, root_page_opens_the_fire)
{
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    const CampaignPickerSession::DecoratedPage& page = session.page();
    EXPECT_EQ("THE COMPANY FIRE", page.title);
    ASSERT_EQ(3u, page.rows.size());
    EXPECT_EQ("TAKE THE ROAD", page.rows[0].label);
    EXPECT_EQ("QUARTERMASTER", page.rows[1].label);
    EXPECT_EQ("THE LEDGER", page.rows[2].label);
    ASSERT_EQ(1u, page.lines.size());
    EXPECT_EQ("Cold camp. No fire past the ford.", page.lines[0]);
}

TEST_F(WestlandsFireBook, root_stanza_follows_the_campaign_cursor)
{
    const struct
    {
        int cursor;
        const char* line;
    } stanzas[] = {
        {1, "Cold camp. No fire past the ford."},
        {3, "Cold camp. No fire past the ford."},
        {4, "The Bearer sleeps. The watch is set."},
        {7, "The Bearer sleeps. The watch is set."},
        {8, "The fire is low. The road is long."},
        {12, "The fire is low. The road is long."},
        {13, "War-fires answer from the hills."},
        {17, "War-fires answer from the hills."},
        {19, "A guarded flame in the wet dark."},
        {23, "A guarded flame in the wet dark."},
        {24, "The last fire before the summit."},
        {26, "The last fire before the summit."},
        {99, "The fire is banked. The road waits."},
    };
    for (const auto& s : stanzas)
    {
        save_.scen_num = static_cast<short>(s.cursor);
        const hooks::CampaignPage page = fetch("");
        ASSERT_EQ(1u, page.lines.size()) << "cursor " << s.cursor;
        EXPECT_EQ(s.line, page.lines[0]) << "cursor " << s.cursor;
    }
}

TEST_F(WestlandsFireBook, root_milestones_and_the_full_circle_shift)
{
    const struct
    {
        int completed;
        const char* line;
    } milestones[] = {
        {3, "The ford is held and crossed."},
        {6, "The mountain gate shut behind you."},
        {12, "The falls are behind; no road back."},
        {24, "The mountain is behind you."},
    };
    for (const auto& m : milestones)
    {
        SaveData save;
        save.current_campaign = kCampaign;
        save.scen_num = 1;
        save.add_level_completed(kCampaign, m.completed);
        hooks::install_campaign_providers(
            og::data::make_campaign_providers(save));
        const hooks::CampaignPage page = fetch("");
        ASSERT_EQ(2u, page.lines.size()) << "milestone " << m.completed;
        EXPECT_EQ(m.line, page.lines[1]) << "milestone " << m.completed;
    }
    // Later milestones win over earlier ones.
    save_.add_level_completed(kCampaign, 3);
    save_.add_level_completed(kCampaign, 12);
    hooks::install_campaign_providers(og::data::make_campaign_providers(save_));
    const hooks::CampaignPage later = fetch("");
    ASSERT_EQ(2u, later.lines.size());
    EXPECT_EQ("The falls are behind; no road back.", later.lines[1]);
    // The full circle: level 26 completed shifts the title and speaks the
    // loop line first.
    save_.add_level_completed(kCampaign, 26);
    const hooks::CampaignPage looped = fetch("");
    EXPECT_EQ("THE FIRE REMEMBERS", looped.title);
    ASSERT_GE(looped.lines.size(), 2u);
    EXPECT_EQ("The vale again. The fire remembers.", looped.lines[0]);
}

// ---------------------------------------------------------------------------
// THE ROAD: the pack's graph mirror vs the SHIPPED exits, level by level
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireBook, road_page_mirrors_the_shipped_exit_graph)
{
    for (int id = 1; id <= 26; id++)
    {
        if (id == 18)
            continue;  // the campaign's deliberate act gap
        save_.scen_num = static_cast<short>(id);
        const hooks::CampaignPage road = fetch("road");
        EXPECT_EQ("THE ROAD", road.title) << "scen" << id;
        ASSERT_EQ(1u, road.lines.size()) << "scen" << id;
        std::set<int> rows;
        for (const hooks::CampaignPageEntry& entry : road.entries)
        {
            EXPECT_EQ(hooks::CampaignPageEntry::Kind::Level, entry.kind)
                << "scen" << id << " row " << entry.id;
            EXPECT_FALSE(entry.label.empty()) << "scen" << id;
            EXPECT_LE(entry.label.size(), 24u) << entry.label;
            EXPECT_LE(entry.note.size(), 20u) << entry.note;
            EXPECT_TRUE(rows.insert(entry.level).second)
                << "scen" << id << " duplicates level " << entry.level;
        }
        std::set<int> shipped;
        {
            LoadedWestlandsLevel lv(id);
            ASSERT_TRUE(lv.loaded) << "scen" << id;
            for (const auto& uptr : lv.world().fxlist)
            {
                const walker* fx = uptr.get();
                if (fx != nullptr && fx->query_order() == Order::Treasure &&
                    fx->family() == FAMILY_EXIT)
                    shipped.insert(fx->stats()->level());
            }
        }
        EXPECT_EQ(shipped, rows)
            << "scen" << id << ": the road must mirror the authored exits";
    }
}

TEST_F(WestlandsFireBook, road_forks_speak_the_briefings_words)
{
    save_.scen_num = 4;
    const hooks::CampaignPage refuge = fetch("road");
    EXPECT_EQ("You stand at THE HIDDEN REFUGE.", refuge.lines[0]);
    ASSERT_EQ(3u, refuge.entries.size());
    EXPECT_EQ("THE HIGH PASS", refuge.entries[0].label);
    EXPECT_EQ("south, over snow", refuge.entries[0].note);
    EXPECT_EQ(5, refuge.entries[0].level);
    EXPECT_EQ("THE FROZEN WALL", refuge.entries[1].label);
    EXPECT_EQ("the north plea", refuge.entries[1].note);
    EXPECT_EQ(7, refuge.entries[1].level);
    EXPECT_EQ("BACK TO THE FORD", refuge.entries[2].label);
    EXPECT_EQ("", refuge.entries[2].note);
    EXPECT_EQ(3, refuge.entries[2].level);

    save_.scen_num = 12;
    const hooks::CampaignPage falls = fetch("road");
    ASSERT_EQ(2u, falls.entries.size());
    EXPECT_EQ("THE WAR ROAD", falls.entries[0].label);
    EXPECT_EQ("the horns of war", falls.entries[0].note);
    EXPECT_EQ("THE BURDEN'S ROAD", falls.entries[1].label);
    EXPECT_EQ("the marsh road", falls.entries[1].note);

    save_.scen_num = 24;
    const hooks::CampaignPage summit = fetch("road");
    ASSERT_EQ(2u, summit.entries.size());
    EXPECT_EQ("THE SCOURING", summit.entries[0].label);
    EXPECT_EQ("home, and war", summit.entries[0].note);
    EXPECT_EQ("THE GREY SHIPS", summit.entries[1].label);
    EXPECT_EQ("the grey sea", summit.entries[1].note);

    // Off the map (18 is the act gap; 99 is nowhere): the road is dark.
    save_.scen_num = 99;
    const hooks::CampaignPage dark = fetch("road");
    EXPECT_TRUE(dark.entries.empty());
    ASSERT_EQ(1u, dark.lines.size());
    EXPECT_EQ("The road is dark.", dark.lines[0]);
}

TEST_F(WestlandsFireBook, road_rows_decorate_and_set_the_level)
{
    save_.scen_num = 4;
    save_.add_level_completed(kCampaign, 3);
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(0).kind);  // TAKE THE ROAD
    const CampaignPickerSession::DecoratedPage& road = session.page();
    const int back = row_named(road, "BACK TO THE FORD");
    ASSERT_NE(-1, back);
    EXPECT_TRUE(road.rows[static_cast<std::size_t>(back)].cleared);
    const int pass = row_named(road, "THE HIGH PASS");
    ASSERT_NE(-1, pass);
    EXPECT_FALSE(road.rows[static_cast<std::size_t>(pass)].cleared);
    // Choosing a level row is the SetLevel outcome; the session itself
    // writes nothing (each client's set-level tail owns that).
    const CampaignPickerSession::Outcome outcome =
        session.choose(static_cast<std::size_t>(pass));
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::SetLevel, outcome.kind);
    EXPECT_EQ(5, outcome.level);
    EXPECT_EQ(4, save_.scen_num);
}

// ---------------------------------------------------------------------------
// QUARTERMASTER: the offer windows
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireBook, quartermaster_gates_the_shelf)
{
    // Fresh company: provisions alone, with the tier-1 note.
    hooks::CampaignPage shelf = fetch("stores");
    ASSERT_EQ(1u, shelf.entries.size());
    EXPECT_EQ("PROVISION PACKS", shelf.entries[0].label);
    EXPECT_EQ("tier 1 of 3", shelf.entries[0].note);
    EXPECT_EQ(300, shelf.entries[0].cost);
    ASSERT_EQ(1u, shelf.lines.size());
    EXPECT_EQ("He deals in wages, bread, and weight.", shelf.lines[0]);

    // The Watch's pay appears once the Wall detour is cleared...
    save_.add_level_completed(kCampaign, 7);
    shelf = fetch("stores");
    ASSERT_NE(-1, entry_named(shelf, "THE WATCH'S PAY"));
    EXPECT_EQ(900,
              shelf.entries[static_cast<std::size_t>(
                                entry_named(shelf, "THE WATCH'S PAY"))]
                  .cost);
    // ...and vanishes once paid (one-shot on the var).
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "watch_paid", 1));
    shelf = fetch("stores");
    EXPECT_EQ(-1, entry_named(shelf, "THE WATCH'S PAY"));

    // The delve pair opens after the vault and closes at the river.
    EXPECT_EQ(-1, entry_named(shelf, "COUNT THE DELVE GOLD"));
    save_.add_level_completed(kCampaign, 9);
    shelf = fetch("stores");
    ASSERT_NE(-1, entry_named(shelf, "COUNT THE DELVE GOLD"));
    ASSERT_NE(-1, entry_named(shelf, "SINK IT IN THE RIVER"));
    EXPECT_EQ("a debt, maybe",
              shelf.entries[static_cast<std::size_t>(
                                entry_named(shelf, "COUNT THE DELVE GOLD"))]
                  .note);
    save_.add_level_completed(kCampaign, 11);
    shelf = fetch("stores");
    EXPECT_EQ(-1, entry_named(shelf, "COUNT THE DELVE GOLD"))
        << "counting after the river must be impossible";
    EXPECT_EQ(-1, entry_named(shelf, "SINK IT IN THE RIVER"));

    // Bread only in the marsh window: after 19, before 21, unshared.
    EXPECT_EQ(-1, entry_named(shelf, "BREAD FOR SNEAK"));
    save_.add_level_completed(kCampaign, 19);
    shelf = fetch("stores");
    ASSERT_NE(-1, entry_named(shelf, "BREAD FOR SNEAK"));
    EXPECT_EQ("a small kindness",
              shelf.entries[static_cast<std::size_t>(
                                entry_named(shelf, "BREAD FOR SNEAK"))]
                  .note);
    save_.add_level_completed(kCampaign, 21);
    shelf = fetch("stores");
    EXPECT_EQ(-1, entry_named(shelf, "BREAD FOR SNEAK"))
        << "the window shuts at the Spider Pass";
}

TEST_F(WestlandsFireBook, quartermaster_delve_pair_is_mutually_one_shot)
{
    save_.add_level_completed(kCampaign, 9);
    // Counted: both rows gone.
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "delve_counted", 1));
    hooks::CampaignPage shelf = fetch("stores");
    EXPECT_EQ(-1, entry_named(shelf, "COUNT THE DELVE GOLD"));
    EXPECT_EQ(-1, entry_named(shelf, "SINK IT IN THE RIVER"));
    // Sunk (fresh keys): both rows gone too.
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "delve_counted", 0));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "delve_sunk", 1));
    shelf = fetch("stores");
    EXPECT_EQ(-1, entry_named(shelf, "COUNT THE DELVE GOLD"));
    EXPECT_EQ(-1, entry_named(shelf, "SINK IT IN THE RIVER"));
}

TEST_F(WestlandsFireBook, bread_window_needs_the_marsh_first)
{
    // sneak_bread already shared closes the row even inside the window.
    save_.add_level_completed(kCampaign, 19);
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "sneak_bread", 1));
    const hooks::CampaignPage shelf = fetch("stores");
    EXPECT_EQ(-1, entry_named(shelf, "BREAD FOR SNEAK"));
}

// ---------------------------------------------------------------------------
// Actions through the session: debits, grants, one-shots, refusals
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireBook, watch_pay_debits_and_is_one_shot)
{
    save_.add_level_completed(kCampaign, 7);
    save_.m_totalcash[0] = 900;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(1).kind);  // QUARTERMASTER
    const int pay = row_named(session.page(), "THE WATCH'S PAY");
    ASSERT_NE(-1, pay);
    EXPECT_TRUE(session.page().rows[static_cast<std::size_t>(pay)].affordable);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(static_cast<std::size_t>(pay)).kind);
    EXPECT_EQ(0u, save_.m_totalcash[0]) << "the 900g chest must leave";
    EXPECT_EQ(1, save_.campaign_state_get(kCampaign, "watch_paid"));
    EXPECT_EQ("The chest goes north. Paid in full.", session.take_message());
    EXPECT_EQ(-1, row_named(session.page(), "THE WATCH'S PAY"))
        << "the refetched shelf must drop the paid chest";
}

TEST_F(WestlandsFireBook, watch_pay_refuses_a_thin_wallet)
{
    save_.add_level_completed(kCampaign, 7);
    save_.m_totalcash[0] = 899;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(1).kind);
    const int pay = row_named(session.page(), "THE WATCH'S PAY");
    ASSERT_NE(-1, pay);
    EXPECT_FALSE(
        session.page().rows[static_cast<std::size_t>(pay)].affordable);
    const CampaignPickerSession::Outcome outcome = session.choose(static_cast<std::size_t>(pay));
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Refused, outcome.kind);
    EXPECT_EQ(899u, save_.m_totalcash[0]) << "a refusal must not debit";
    EXPECT_EQ(0, save_.campaign_state_get(kCampaign, "watch_paid"));
}

TEST_F(WestlandsFireBook, counting_the_delve_grants_eight_hundred)
{
    save_.add_level_completed(kCampaign, 9);
    save_.m_totalcash[0] = 10;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(1).kind);
    const int count = row_named(session.page(), "COUNT THE DELVE GOLD");
    ASSERT_NE(-1, count);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(static_cast<std::size_t>(count)).kind);
    EXPECT_EQ(810u, save_.m_totalcash[0]) << "cost 0, grant 800";
    EXPECT_EQ(1, save_.campaign_state_get(kCampaign, "delve_counted"));
    EXPECT_EQ(0, save_.campaign_state_get(kCampaign, "delve_sunk"));
    EXPECT_EQ("Counted. Eight hundred, and a debt.", session.take_message());
    EXPECT_EQ(-1, row_named(session.page(), "COUNT THE DELVE GOLD"));
    EXPECT_EQ(-1, row_named(session.page(), "SINK IT IN THE RIVER"));
}

TEST_F(WestlandsFireBook, sinking_the_delve_grants_nothing)
{
    save_.add_level_completed(kCampaign, 9);
    save_.m_totalcash[0] = 10;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(1).kind);
    const int sink = row_named(session.page(), "SINK IT IN THE RIVER");
    ASSERT_NE(-1, sink);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(static_cast<std::size_t>(sink)).kind);
    EXPECT_EQ(10u, save_.m_totalcash[0]) << "the river pays nobody";
    EXPECT_EQ(1, save_.campaign_state_get(kCampaign, "delve_sunk"));
    EXPECT_EQ(0, save_.campaign_state_get(kCampaign, "delve_counted"));
    EXPECT_EQ("The river takes it, and tells no one.", session.take_message());
    EXPECT_EQ(-1, row_named(session.page(), "COUNT THE DELVE GOLD"));
}

TEST_F(WestlandsFireBook, provisions_stack_to_the_cap_and_vanish)
{
    save_.m_totalcash[0] = 1000;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(1).kind);
    const char* toasts[] = {
        "Packs at tier one. Heavy, and glad.",
        "Packs at tier two. Heavy, and glad.",
        "Packs at tier three. Heavy, and glad.",
    };
    const char* notes[] = {"tier 1 of 3", "tier 2 of 3", "tier 3 of 3"};
    for (int buy = 0; buy < 3; buy++)
    {
        const int row = row_named(session.page(), "PROVISION PACKS");
        ASSERT_NE(-1, row) << "purchase " << buy;
        EXPECT_EQ(notes[buy],
                  session.page().rows[static_cast<std::size_t>(row)].note);
        ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
                  session.choose(static_cast<std::size_t>(row)).kind);
        EXPECT_EQ(toasts[buy], session.take_message());
    }
    EXPECT_EQ(100u, save_.m_totalcash[0]) << "three 300g tiers";
    EXPECT_EQ(3, save_.campaign_state_get(kCampaign, "provisions"));
    EXPECT_EQ(-1, row_named(session.page(), "PROVISION PACKS"))
        << "the shelf hides the packs at the cap";
}

TEST_F(WestlandsFireBook, bread_for_sneak_costs_a_gesture)
{
    save_.add_level_completed(kCampaign, 19);
    save_.m_totalcash[0] = 60;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(1).kind);
    const int bread = row_named(session.page(), "BREAD FOR SNEAK");
    ASSERT_NE(-1, bread);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(static_cast<std::size_t>(bread)).kind);
    EXPECT_EQ(0u, save_.m_totalcash[0]);
    EXPECT_EQ(1, save_.campaign_state_get(kCampaign, "sneak_bread"));
    EXPECT_EQ("He eats in silence, and says nothing.", session.take_message());
    EXPECT_EQ(-1, row_named(session.page(), "BREAD FOR SNEAK"));
}

// Direct hook dispatch (no session row, no C++ debit): every action must
// re-check its own one-shot key, refuse with its own line, and mutate
// nothing — the defense against a dispatch the shelf never offered.
TEST_F(WestlandsFireBook, actions_refuse_direct_dispatch_when_spent)
{
    save_.m_totalcash[0] = 500;
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "watch_paid", 1));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "provisions", 3));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "sneak_bread", 1));
    save_.add_level_completed(kCampaign, 9);
    save_.add_level_completed(kCampaign, 11);  // the delve window is shut

    const struct
    {
        const char* entry;
        const char* message;
    } refusals[] = {
        {"provisions", "The packs are full."},
        {"watch_pay", "The chest is gone north already."},
        {"delve_count", "The counting is done with."},
        {"delve_sink", "The river is past."},
        {"sneak_bread", "He has eaten."},
    };
    for (const auto& r : refusals)
    {
        hooks::CampaignActionResult result;
        ASSERT_TRUE(hooks::campaign_picker_action(r.entry, result)) << r.entry;
        EXPECT_TRUE(result.ok) << r.entry;
        EXPECT_EQ(r.message, result.message) << r.entry;
    }
    EXPECT_EQ(500u, save_.m_totalcash[0]) << "no refusal may grant or debit";
    EXPECT_EQ(3, save_.campaign_state_get(kCampaign, "provisions"));
    EXPECT_EQ(0, save_.campaign_state_get(kCampaign, "delve_counted"));
    EXPECT_EQ(0, save_.campaign_state_get(kCampaign, "delve_sunk"));

    // An unknown entry id is a silent no-op; an unknown page id answers
    // "no page" (the session would refuse in place).
    hooks::CampaignActionResult unknown;
    ASSERT_TRUE(hooks::campaign_picker_action("no_such_offer", unknown));
    EXPECT_TRUE(unknown.ok);
    EXPECT_TRUE(unknown.message.empty());
    hooks::CampaignPage page;
    EXPECT_FALSE(hooks::campaign_picker_page("no_such_page", page));
}

// ---------------------------------------------------------------------------
// THE LEDGER
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireBook, ledger_narrates_the_deeds_and_both_roads)
{
    // Blank company: the one blank line.
    hooks::CampaignPage ledger = fetch("ledger");
    ASSERT_EQ(1u, ledger.lines.size());
    EXPECT_EQ("The ledger lies open, and blank.", ledger.lines[0]);
    EXPECT_TRUE(ledger.entries.empty()) << "the ledger is lines-only";

    // Everything, with BOTH roads walked (24 is reachable from both; the
    // war road backtracks 13 -> 12 and takes the marshes): exactly the
    // six-line full book, in fixed order.
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "watch_paid", 1));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "delve_counted", 1));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "provisions", 2));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "sneak_bread", 1));
    save_.add_level_completed(kCampaign, 13);
    save_.add_level_completed(kCampaign, 19);
    ledger = fetch("ledger");
    const std::vector<std::string> expected = {
        "The Watch drinks to your name.",
        "The delve gold rides with us.",
        "Packs at tier two. Heavy, and glad.",
        "Sneak ate at your fire.",
        "West, we rode to the horns of war.",
        "East, we walked the drowned road.",
    };
    EXPECT_EQ(expected, ledger.lines);

    // The sunk variant swaps the delve line.
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "delve_counted", 0));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "delve_sunk", 1));
    ledger = fetch("ledger");
    ASSERT_EQ(6u, ledger.lines.size());
    EXPECT_EQ("The river keeps what you would not.", ledger.lines[1]);

    // Tier words: one, three, and the clamp on a hex-edited tier.
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "provisions", 1));
    ledger = fetch("ledger");
    EXPECT_EQ("Packs at tier one. Heavy, and glad.", ledger.lines[2]);
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "provisions", 7));
    ledger = fetch("ledger");
    EXPECT_EQ("Packs at tier three. Heavy, and glad.", ledger.lines[2]);
}

TEST_F(WestlandsFireBook, ledger_is_reachable_from_the_fire)
{
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "sneak_bread", 1));
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(2).kind);  // THE LEDGER
    const CampaignPickerSession::DecoratedPage& page = session.page();
    EXPECT_EQ("THE LEDGER", page.title);
    ASSERT_EQ(1u, page.lines.size());
    EXPECT_EQ("Sneak ate at your fire.", page.lines[0]);
    EXPECT_TRUE(page.rows.empty());
    EXPECT_TRUE(session.back()) << "Back returns to the fire";
    EXPECT_EQ("THE COMPANY FIRE", session.page().title);
}

// ---------------------------------------------------------------------------
// var == 0 byte-identity: with every campaign var unset, the pack must be
// perfectly inert on its consequence levels — same census, same entity
// state, same rng_.state_ as a run with the fire pack unregistered.
// ---------------------------------------------------------------------------

struct EntitySnap
{
    int order = 0;
    int family = 0;
    int team = 0;
    int dead = 0;
    int floor = 0;
    int x = 0;
    int y = 0;
    int level = 0;
    float hp = 0.0f;
    std::string name;

    auto tie() const
    {
        return std::tie(order, family, team, dead, floor, x, y, level, hp,
                        name);
    }
    bool operator==(const EntitySnap& other) const
    {
        return tie() == other.tie();
    }
};

struct WorldSnap
{
    std::uint32_t rng_state = 0;
    std::vector<EntitySnap> entities;
};

EntitySnap snap_entity(const walker* ob)
{
    EntitySnap snap;
    snap.order = static_cast<int>(ob->query_order());
    snap.family = ob->family();
    snap.team = ob->team_num();
    snap.dead = ob->dead() ? 1 : 0;
    snap.floor = ob->floor();
    snap.x = ob->xpos();
    snap.y = ob->ypos();
    snap.level = ob->stats()->level();
    snap.hp = ob->stats()->hitpoints();
    snap.name = ob->stats()->name;
    return snap;
}

WorldSnap run_level(int id, std::uint32_t seed, int ticks)
{
    LoadedWestlandsLevel lv(id, seed);
    EXPECT_TRUE(lv.loaded) << "scen" << id;
    for (int t = 0; t < ticks; t++)
        lv.world().tick();
    WorldSnap snap;
    snap.rng_state = lv.world().rng_.state_;
    for (const auto& uptr : lv.world().oblist)
        if (uptr != nullptr)
            snap.entities.push_back(snap_entity(uptr.get()));
    for (const auto& uptr : lv.world().fxlist)
        if (uptr != nullptr)
            snap.entities.push_back(snap_entity(uptr.get()));
    return snap;
}

TEST_F(WestlandsFireSim, var_zero_is_byte_identical_to_a_packless_run)
{
    // The pack must actually be in play for the first run to mean anything.
    bool registered = false;
    for (const og::script::PackScript& ps : og::script::pack_scripts())
        if (ps.pack_id == kFirePackId)
            registered = true;
    ASSERT_TRUE(registered) << "westlands.fire must register on mount";

    const int consequence_levels[] = {11, 15, 21, 26};
    std::vector<WorldSnap> with_pack;
    for (const int id : consequence_levels)
        with_pack.push_back(run_level(id, 42, 300));

    og::script::unregister_pack_scripts(kFirePackId);
    og::script::unregister_pack_lib_modules(kFirePackId);
    for (const og::script::PackScript& ps : og::script::pack_scripts())
        ASSERT_NE(kFirePackId, ps.pack_id) << "baseline must be pack-less";

    for (std::size_t i = 0; i < std::size(consequence_levels); i++)
    {
        const int id = consequence_levels[i];
        const WorldSnap baseline = run_level(id, 42, 300);
        EXPECT_EQ(baseline.rng_state, with_pack[i].rng_state)
            << "scen" << id << ": the var==0 hook must not touch the RNG";
        ASSERT_EQ(baseline.entities.size(), with_pack[i].entities.size())
            << "scen" << id << ": census drift at var==0";
        for (std::size_t e = 0; e < baseline.entities.size(); e++)
            EXPECT_TRUE(baseline.entities[e] == with_pack[i].entities[e])
                << "scen" << id << " entity " << e << " ("
                << baseline.entities[e].name << ") drifted at var==0";
    }
}

// ---------------------------------------------------------------------------
// Taken paths: exact spawns, softening, drops, footing
// ---------------------------------------------------------------------------

const walker* find_named(GameWorld& world, const std::string& name, int nth = 0)
{
    int seen = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->stats()->name == name)
        {
            if (seen == nth)
                return ob;
            seen++;
        }
    }
    return nullptr;
}

int count_team_livings(GameWorld& world, int team)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->team_num() == team && !ob->dead())
            count++;
    }
    return count;
}

bool notified(og::sim::SimEventLog& events, const std::string& needle)
{
    for (const og::sim::Event& ev : events.drain())
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find(needle) != std::string::npos)
            return true;
    return false;
}

void expect_no_script_errors(LoadedWestlandsLevel& lv)
{
    const auto& errors = lv.world().scripts().host().errors();
    EXPECT_TRUE(errors.empty())
        << (errors.empty() ? "" : errors.front().message);
}

TEST_F(WestlandsFireSim, watch_paid_musters_three_swords_at_the_gate)
{
    LoadedWestlandsLevel lv(15, 42);
    ASSERT_TRUE(lv.loaded);
    lv.world().campaign_vars.emplace_back("watch_paid", 1);
    const int before = count_team_livings(lv.world(), 0);
    lv.world().tick();
    EXPECT_EQ(before + 3, count_team_livings(lv.world(), 0));
    EXPECT_TRUE(notified(lv.events, "The Watch remembers its wages."));

    const walker* warden = find_named(lv.world(), "Wall-Warden");
    ASSERT_NE(nullptr, warden);
    EXPECT_EQ(0, warden->team_num());
    EXPECT_EQ(6, warden->stats()->level());
    EXPECT_EQ(0, warden->floor());
    EXPECT_EQ(39 * GRID_SIZE, warden->xpos());
    EXPECT_EQ(9 * GRID_SIZE, warden->ypos());

    const struct
    {
        int x, y;
    } posts[] = {{34, 11}, {43, 11}};
    for (int i = 0; i < 2; i++)
    {
        const walker* watchman = find_named(lv.world(), "Watchman", i);
        ASSERT_NE(nullptr, watchman) << "Watchman " << i;
        EXPECT_EQ(0, watchman->team_num());
        EXPECT_EQ(5, watchman->stats()->level());
        EXPECT_EQ(posts[i].x * GRID_SIZE, watchman->xpos());
        EXPECT_EQ(posts[i].y * GRID_SIZE, watchman->ypos());
        // Footing: the fixed tile must carry the ally's full footprint.
        EXPECT_TRUE(lv.world().query_grid_passable(
            static_cast<float>(watchman->xpos()),
            static_cast<float>(watchman->ypos()),
            const_cast<walker*>(watchman), watchman->floor()))
            << "Watchman " << i << " tile is not standable";
    }
    EXPECT_TRUE(lv.world().query_grid_passable(
        static_cast<float>(warden->xpos()), static_cast<float>(warden->ypos()),
        const_cast<walker*>(warden), warden->floor()));
    expect_no_script_errors(lv);
}

TEST_F(WestlandsFireSim, counted_gold_raises_wraiths_on_the_river)
{
    LoadedWestlandsLevel lv(11, 42);
    ASSERT_TRUE(lv.loaded);
    lv.world().campaign_vars.emplace_back("delve_counted", 1);
    const int before = count_team_livings(lv.world(), 2);
    lv.world().tick();
    EXPECT_EQ(before + 2, count_team_livings(lv.world(), 2));
    EXPECT_TRUE(notified(lv.events, "Something pale keeps pace on the water."));

    const struct
    {
        int x, y;
    } banks[] = {{40, 20}, {40, 28}};
    const walker* wraiths[2] = {};
    for (int i = 0; i < 2; i++)
    {
        const walker* wraith = find_named(lv.world(), "Gold-Wraith", i);
        ASSERT_NE(nullptr, wraith) << "wraith " << i;
        wraiths[i] = wraith;
        EXPECT_EQ(2, wraith->team_num());
        EXPECT_EQ(FAMILY_GHOST, wraith->family());
        EXPECT_EQ(6, wraith->stats()->level());
        EXPECT_EQ(banks[i].x * GRID_SIZE, wraith->xpos());
        EXPECT_EQ(banks[i].y * GRID_SIZE, wraith->ypos());
        EXPECT_FALSE(wraith->dead());
        // Ghosts fly: the open-water tile must be standable FOR A FLYER.
        EXPECT_TRUE(lv.world().query_grid_passable(
            static_cast<float>(wraith->xpos()),
            static_cast<float>(wraith->ypos()),
            const_cast<walker*>(wraiths[i]), wraith->floor()))
            << "wraith " << i << " tile refuses a flyer";
    }

    // Stationarity: the debt PURSUES. Over 300 ticks each wraith must
    // leave its spawn tile (or have died engaging) — a wraith parked on
    // its spawn forever means the tile is a trap.
    for (int t = 0; t < 300; t++)
        lv.world().tick();
    for (int i = 0; i < 2; i++)
    {
        const bool moved = wraiths[i]->xpos() != banks[i].x * GRID_SIZE ||
                           wraiths[i]->ypos() != banks[i].y * GRID_SIZE;
        EXPECT_TRUE(moved || wraiths[i]->dead())
            << "wraith " << i << " never left its spawn tile";
    }
    expect_no_script_errors(lv);
}

TEST_F(WestlandsFireSim, shared_bread_softens_the_knife)
{
    // The var==0 pin first: the placed Sneak is the authored lvl-8 thief
    // at full authored strength, and one ticked load leaves him at level 8
    // and exactly that hp (at max, the regen path never fires).
    float authored_hp = 0.0f;
    {
        LoadedWestlandsLevel lv(21, 42);
        ASSERT_TRUE(lv.loaded);
        const walker* sneak = find_named(lv.world(), "Sneak");
        ASSERT_NE(nullptr, sneak);
        authored_hp = sneak->stats()->hitpoints();
        ASSERT_GT(authored_hp, 0.0f);
        lv.world().tick();
        EXPECT_EQ(2, sneak->team_num());
        EXPECT_EQ(8, sneak->stats()->level())
            << "the lvl-8 pin must hold at var==0";
        EXPECT_EQ(authored_hp, sneak->stats()->hitpoints());
    }
    // Bread shared: same walker, lvl 5 at half the strength. The on_load
    // halve runs before the act phase; the loader seeds current_heal_delay
    // at the rollover threshold, so the now-sub-max Sneak collects
    // compute_regen_tick's +1 rollover pulse in that same first act
    // (heal_per_round itself is 0 for him).
    LoadedWestlandsLevel lv(21, 42);
    ASSERT_TRUE(lv.loaded);
    lv.world().campaign_vars.emplace_back("sneak_bread", 1);
    lv.world().tick();
    const walker* sneak = find_named(lv.world(), "Sneak");
    ASSERT_NE(nullptr, sneak);
    EXPECT_EQ(2, sneak->team_num()) << "the betrayal itself stands";
    EXPECT_EQ(5, sneak->stats()->level());
    EXPECT_EQ(authored_hp / 2.0f + 1.0f, sneak->stats()->hitpoints());
    EXPECT_TRUE(notified(lv.events, "For a breath, the knife hesitates."));
    expect_no_script_errors(lv);
}

// The defensive arm: a modded or future 21 without a placed "Sneak" must
// leave the hook a quiet no-op (found-nobody early return), not an error.
TEST_F(WestlandsFireSim, soften_survives_a_missing_sneak)
{
    LoadedWestlandsLevel lv(21, 42);
    ASSERT_TRUE(lv.loaded);
    walker* sneak = const_cast<walker*>(find_named(lv.world(), "Sneak"));
    ASSERT_NE(nullptr, sneak);
    sneak->stats()->name = "Nobody";
    lv.world().campaign_vars.emplace_back("sneak_bread", 1);
    lv.world().tick();
    EXPECT_EQ(8, sneak->stats()->level()) << "nobody to soften, nothing moves";
    EXPECT_FALSE(notified(lv.events, "the knife hesitates"));
    expect_no_script_errors(lv);
}

int treasures_at(GameWorld& world, int family, int floor, int tx, int ty)
{
    int count = 0;
    for (const auto& uptr : world.fxlist)
    {
        const walker* fx = uptr.get();
        if (fx != nullptr && fx->query_order() == Order::Treasure &&
            fx->family() == family && fx->floor() == floor &&
            fx->xpos() == tx * GRID_SIZE && fx->ypos() == ty * GRID_SIZE)
            count++;
    }
    return count;
}

TEST_F(WestlandsFireSim, provisions_drop_on_every_hard_road_level)
{
    // The fixed drop clusters (lib/spawns.lua), and the tier each level is
    // exercised at — every tier arm (1, 2, 3, and the >3 clamp) runs.
    const struct
    {
        int id;
        int tier;
        int floor;
        int tiles[4][2];
    } drops[] = {
        {13, 1, 0, {{76, 23}, {77, 23}, {76, 26}, {77, 26}}},
        {14, 2, 0, {{29, 44}, {30, 44}, {29, 45}, {30, 45}}},
        {15, 2, 1, {{36, 20}, {37, 20}, {36, 21}, {37, 21}}},
        {16, 3, 0, {{46, 24}, {47, 24}, {46, 25}, {47, 25}}},
        {17, 1, 0, {{43, 22}, {43, 23}, {43, 26}, {43, 27}}},
        {19, 5, 0, {{12, 7}, {13, 7}, {12, 8}, {13, 8}}},  // clamps to 3
        {20, 2, 0, {{16, 24}, {17, 24}, {16, 25}, {17, 25}}},
        {21, 2, 0, {{11, 20}, {12, 20}, {11, 21}, {12, 21}}},
        {22, 3, 0, {{27, 44}, {28, 44}, {31, 44}, {32, 44}}},
        {23, 2, 0, {{4, 23}, {5, 23}, {4, 24}, {5, 24}}},
    };
    for (const auto& d : drops)
    {
        LoadedWestlandsLevel lv(d.id, 42);
        ASSERT_TRUE(lv.loaded) << "scen" << d.id;
        lv.world().campaign_vars.emplace_back("provisions", d.tier);
        lv.world().tick();
        const int sticks = std::min(d.tier, 3);
        for (int i = 0; i < 3; i++)
        {
            const int expected = i < sticks ? 1 : 0;
            EXPECT_EQ(expected,
                      treasures_at(lv.world(), FAMILY_DRUMSTICK, d.floor,
                                   d.tiles[i][0], d.tiles[i][1]))
                << "scen" << d.id << " drumstick slot " << i;
        }
        EXPECT_EQ(sticks >= 2 ? 1 : 0,
                  treasures_at(lv.world(), FAMILY_MAGIC_POTION, d.floor,
                               d.tiles[3][0], d.tiles[3][1]))
            << "scen" << d.id << " potion slot";
        // Footing: every drop tile must be standable ground on the shipped
        // grid — supplies in a wall feed nobody. Probe with a spawned
        // ground walker at each tile.
        walker* probe = lv.world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, probe) << "scen" << d.id;
        probe->set_team_num(0);
        probe->set_floor(static_cast<short>(d.floor));
        for (int i = 0; i < 4; i++)
            EXPECT_TRUE(lv.world().query_grid_passable(
                static_cast<float>(d.tiles[i][0] * GRID_SIZE),
                static_cast<float>(d.tiles[i][1] * GRID_SIZE), probe,
                d.floor))
                << "scen" << d.id << " drop tile " << i << " is not standable";
        probe->set_dead(1);
        expect_no_script_errors(lv);
    }
}

TEST_F(WestlandsFireSim, the_pilgrim_waits_only_for_the_generous)
{
    // The true arm: wages paid, bread shared, hoard refused.
    {
        LoadedWestlandsLevel lv(26, 42);
        ASSERT_TRUE(lv.loaded);
        lv.world().campaign_vars.emplace_back("watch_paid", 1);
        lv.world().campaign_vars.emplace_back("sneak_bread", 1);
        const int before = count_team_livings(lv.world(), 0);
        lv.world().tick();
        EXPECT_EQ(before + 1, count_team_livings(lv.world(), 0));
        EXPECT_TRUE(notified(lv.events, "A grey figure waits upon the quay."));
        const walker* pilgrim = find_named(lv.world(), "The Pilgrim");
        ASSERT_NE(nullptr, pilgrim);
        EXPECT_EQ(0, pilgrim->team_num());
        EXPECT_EQ(FAMILY_ARCHMAGE, pilgrim->family());
        EXPECT_EQ(10, pilgrim->stats()->level());
        EXPECT_EQ(0, pilgrim->floor());
        EXPECT_EQ(10 * GRID_SIZE, pilgrim->xpos());
        EXPECT_EQ(17 * GRID_SIZE, pilgrim->ypos());
        EXPECT_TRUE(lv.world().query_grid_passable(
            static_cast<float>(pilgrim->xpos()),
            static_cast<float>(pilgrim->ypos()),
            const_cast<walker*>(pilgrim), pilgrim->floor()))
            << "the quay tile is not standable";
        expect_no_script_errors(lv);
    }
    // Every false arm of the composite: no figure, no word.
    const struct
    {
        int watch, bread, delve;
    } denials[] = {
        {0, 1, 0},  // wages unpaid
        {1, 0, 0},  // bread unshared
        {1, 1, 1},  // the hoard counted
    };
    for (const auto& d : denials)
    {
        LoadedWestlandsLevel lv(26, 42);
        ASSERT_TRUE(lv.loaded);
        lv.world().campaign_vars.emplace_back("watch_paid", d.watch);
        lv.world().campaign_vars.emplace_back("sneak_bread", d.bread);
        lv.world().campaign_vars.emplace_back("delve_counted", d.delve);
        const int before = count_team_livings(lv.world(), 0);
        lv.world().tick();
        EXPECT_EQ(before, count_team_livings(lv.world(), 0))
            << d.watch << "/" << d.bread << "/" << d.delve;
        EXPECT_EQ(nullptr, find_named(lv.world(), "The Pilgrim"))
            << d.watch << "/" << d.bread << "/" << d.delve;
        EXPECT_FALSE(notified(lv.events, "A grey figure"))
            << d.watch << "/" << d.bread << "/" << d.delve;
        expect_no_script_errors(lv);
    }
}

// The merged scen15 hook runs both arms: allies AND drops together.
TEST_F(WestlandsFireSim, deeping_wall_merges_wages_and_provisions)
{
    LoadedWestlandsLevel lv(15, 42);
    ASSERT_TRUE(lv.loaded);
    lv.world().campaign_vars.emplace_back("watch_paid", 1);
    lv.world().campaign_vars.emplace_back("provisions", 2);
    const int before = count_team_livings(lv.world(), 0);
    lv.world().tick();
    EXPECT_EQ(before + 3, count_team_livings(lv.world(), 0));
    EXPECT_NE(nullptr, find_named(lv.world(), "Wall-Warden"));
    EXPECT_EQ(1, treasures_at(lv.world(), FAMILY_DRUMSTICK, 1, 36, 20));
    EXPECT_EQ(1, treasures_at(lv.world(), FAMILY_DRUMSTICK, 1, 37, 20));
    EXPECT_EQ(0, treasures_at(lv.world(), FAMILY_DRUMSTICK, 1, 36, 21));
    EXPECT_EQ(1, treasures_at(lv.world(), FAMILY_MAGIC_POTION, 1, 37, 21));
    expect_no_script_errors(lv);
}

}  // namespace
