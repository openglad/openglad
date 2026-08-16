/* The Company Fire (campaigns/westlands/packs/westlands.fire): the scripted
 * Base Camp of War of the Westlands. Covers the whole pack against the
 * SHIPPED builtin/westlands.glad:
 *   - the camp zone: the stanza band and its priority ladder, the docket
 *     mirroring the authored exit graph level by level (and `escort`
 *     mirroring each level's SAVE_ALL bit), the hot one-shot offers at the
 *     fire, and the layout that always leaves the roster rows;
 *   - SPLIT THE PARTY: the swearing window, the freeze predicate matrix, the
 *     muster counts (waiters included), the lock table per (oath tag,
 *     tonight's road), the unsworn bypass, the wiped-front collapse, the
 *     one-front-done and early-summit states, the reunion gate and the loop;
 *   - the zone submenus: the QUARTERMASTER's windows/`done` retirement and
 *     THE LEDGER's line permutations, plus actions through both sessions over
 *     the real provider glue — debits, the delve grant, one-shot
 *     disappearance, cap refusals, and every defensive refusal arm via direct
 *     hook dispatch;
 *   - a prose-budget sweep over every camp state (38-char lines, 24-char
 *     labels, 20-char notes, three text lines, the row cap);
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
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

using og::ui::CampaignPickerSession;
using og::ui::CampaignZoneSession;
using westlands_fixture::LoadedWestlandsLevel;
using westlands_fixture::MountedCampaignTest;
namespace hooks = og::script::hooks;

constexpr const char* kFirePackId = "westlands.fire";
constexpr const char* kCampaign = "westlands";

// The prose budgets the camp composes to (docs/basecamp-zones-design.md and
// the generator's own self-check): three text lines of 38 glyphs, 24-glyph
// row labels, 20-glyph notes.
constexpr std::size_t kLineBudget = 38;
constexpr std::size_t kLabelBudget = 24;
constexpr std::size_t kNoteBudget = 20;
constexpr std::size_t kCampTextLines = 3;

// The shipped act-3 roads and the summit that joins them.
constexpr int kWarRoad[] = {13, 14, 15, 16, 17};
constexpr int kBurdenRoad[] = {19, 20, 21, 22, 23};
constexpr int kSummit = 24;

// ---------------------------------------------------------------------------
// Camp-side fixture: the mounted shipped package, the REAL provider glue over
// a scripted SaveData, dispatch in the shared UI VM.
// ---------------------------------------------------------------------------

class WestlandsFireCamp : public MountedCampaignTest
{
protected:
    WestlandsFireCamp()
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

    // --- the company file -------------------------------------------------

    void set_cursor(int level)
    {
        save_.scen_num = static_cast<short>(level);
    }

    void clear_completions()
    {
        save_.completed_levels.clear();
    }

    void complete(std::initializer_list<int> levels)
    {
        for (const int level : levels)
            save_.add_level_completed(kCampaign, level);
    }

    void complete_road(const int (&road)[5])
    {
        for (const int level : road)
            save_.add_level_completed(kCampaign, level);
    }

    // Swear `war` swords to the war road and `burden` to the Bearer, and
    // leave `unsworn` blades unsworn — the roster the muster derivations
    // census through og.campaign_team().
    void muster_company(int war, int burden, int unsworn)
    {
        for (auto& slot : save_.team_list)
            slot.reset();
        int filled = 0;
        const auto enlist = [this, &filled](int tag) {
            save_.team_list[static_cast<std::size_t>(filled)] =
                std::make_unique<guy>(FAMILY_SOLDIER);
            guy& member =
                *save_.team_list[static_cast<std::size_t>(filled)];
            member.name = "Sword" + std::to_string(filled);
            member.teamnum = 0;
            member.campaign_tag = static_cast<std::uint8_t>(tag);
            filled++;
        };
        for (int i = 0; i < war; i++)
            enlist(1);
        for (int i = 0; i < burden; i++)
            enlist(2);
        for (int i = 0; i < unsworn; i++)
            enlist(0);
        save_.team_size = static_cast<unsigned char>(filled);
    }

    // Stand the whole company down — the state the oath ceremony itself
    // leaves behind (every assign cycle un-deploys the hero it swears).
    void bench_company()
    {
        for (auto& slot : save_.team_list)
        {
            if (slot)
                slot->deployed = false;
        }
    }

    // Which save slots are standing in tonight's sortie.
    std::vector<int> standing() const
    {
        std::vector<int> slots;
        for (std::size_t i = 0; i < save_.team_list.size(); i++)
        {
            if (save_.team_list[i] && save_.team_list[i]->deployed)
                slots.push_back(static_cast<int>(i));
        }
        return slots;
    }

    // --- the camp ---------------------------------------------------------

    // Fetch the camp. Every expectation below is written against a COMPOSED
    // fire, so a composition that fell back to the default zone fails here
    // instead of three assertions later as a missing row.
    void open_camp(CampaignZoneSession& zone)
    {
        zone.fetch();
        ASSERT_TRUE(zone.scripted()) << "the fire must compose the zone";
        ASSERT_EQ(1u, zone.texts().size()) << "one stanza band";
        ASSERT_EQ(1u, zone.actions().size()) << "one docket";
        ASSERT_NE(nullptr, zone.readout()) << "the purse heads the panel";
    }

    static const std::vector<std::string>& camp_text(
        const CampaignZoneSession& zone)
    {
        return zone.texts().at(0).lines;
    }

    static const std::vector<CampaignZoneSession::Row>& docket(
        const CampaignZoneSession& zone)
    {
        return zone.actions().at(0).rows;
    }

    static int row_named(const std::vector<CampaignZoneSession::Row>& rows,
                         const std::string& label)
    {
        for (std::size_t i = 0; i < rows.size(); i++)
            if (rows[i].label == label)
                return static_cast<int>(i);
        return -1;
    }

    static bool text_has(const CampaignZoneSession& zone,
                         const std::string& line)
    {
        const std::vector<std::string>& lines = camp_text(zone);
        return std::find(lines.begin(), lines.end(), line) != lines.end();
    }

    // The level ids the docket offers, in row order.
    static std::vector<int> docket_levels(const CampaignZoneSession& zone)
    {
        std::vector<int> levels;
        for (const CampaignZoneSession::Row& row : docket(zone))
            if (row.is_level())
                levels.push_back(row.level);
        return levels;
    }

    // --- the zone submenus ------------------------------------------------

    hooks::CampaignPage fetch(const std::string& page_id)
    {
        hooks::CampaignPage page;
        EXPECT_TRUE(hooks::campaign_picker_page(page_id, page))
            << "page '" << page_id << "' must answer";
        return page;
    }

    static int line_index(const hooks::CampaignPage& page,
                          const std::string& line)
    {
        for (std::size_t i = 0; i < page.lines.size(); i++)
            if (page.lines[i] == line)
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

    static int page_row_named(const CampaignPickerSession::DecoratedPage& page,
                              const std::string& label)
    {
        for (std::size_t i = 0; i < page.rows.size(); i++)
            if (page.rows[i].label == label)
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
// The open road: the stanza band, the docket, the layout
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireCamp, camp_opens_the_fire)
{
    save_.m_totalcash[0] = 250;
    CampaignZoneSession zone(save_);
    open_camp(zone);

    // The pack train heads the panel (hoisted out of the row grid). The purse
    // is NOT here: the GOLD cell is C++-owned on every surface, and a camp
    // that composed its own would print the same number twice.
    ASSERT_EQ(1u, zone.readout()->items.size());
    EXPECT_EQ("PACKS", zone.readout()->items[0].label);
    EXPECT_EQ("none", zone.readout()->items[0].value);
    EXPECT_TRUE(zone.readout()->in_header_band);

    ASSERT_EQ(1u, camp_text(zone).size());
    EXPECT_EQ("Cold camp. No fire past the ford.", camp_text(zone)[0]);

    // One road ahead, then the two doors. No back row, ever.
    const std::vector<CampaignZoneSession::Row>& rows = docket(zone);
    ASSERT_EQ(3u, rows.size());
    EXPECT_EQ("THE FOREST ROAD", rows[0].label);
    EXPECT_EQ("the road out", rows[0].note);
    EXPECT_TRUE(rows[0].is_level());
    EXPECT_EQ(2, rows[0].level);
    EXPECT_EQ("QUARTERMASTER", rows[1].label);
    EXPECT_EQ(CampaignPickerSession::Kind::Page, rows[1].kind);
    EXPECT_EQ("stores", rows[1].id);
    EXPECT_EQ("packs and wages", rows[1].note);
    EXPECT_EQ("THE LEDGER", rows[2].label);
    EXPECT_EQ("ledger", rows[2].id);
    EXPECT_EQ("what the road cost", rows[2].note);

    // Full company capabilities, no oath column, no locks — and the roster
    // still keeps rows under the stanza and the docket.
    const CampaignZoneSession::RosterLayout& roster = zone.roster();
    EXPECT_TRUE(roster.can_deploy);
    EXPECT_TRUE(roster.can_train);
    EXPECT_TRUE(roster.can_reorder);
    EXPECT_TRUE(roster.can_team);
    EXPECT_TRUE(roster.can_hire);
    EXPECT_FALSE(roster.assign.active);
    EXPECT_TRUE(roster.locks.empty());
    EXPECT_EQ(3, roster.rows_per_page);
}

TEST_F(WestlandsFireCamp, camp_stanza_follows_the_cursor)
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
        set_cursor(s.cursor);
        CampaignZoneSession zone(save_);
        open_camp(zone);
        ASSERT_FALSE(camp_text(zone).empty()) << "cursor " << s.cursor;
        EXPECT_EQ(s.line, camp_text(zone)[0]) << "cursor " << s.cursor;
    }
}

// The camp with no forward exit at all (18 is the campaign's act gap, 99 is
// nowhere): the empty docket says why, and the two doors stay.
TEST_F(WestlandsFireCamp, a_camp_off_the_map_says_the_road_is_dark)
{
    set_cursor(18);
    CampaignZoneSession zone(save_);
    open_camp(zone);
    EXPECT_TRUE(text_has(zone, "The road is dark."));
    EXPECT_TRUE(docket_levels(zone).empty());
    ASSERT_EQ(2u, docket(zone).size());
    EXPECT_EQ("QUARTERMASTER", docket(zone)[0].label);
    EXPECT_EQ("THE LEDGER", docket(zone)[1].label);
}

TEST_F(WestlandsFireCamp, milestones_and_the_loop_line)
{
    // The ford, then the mountain gate: the later milestone wins.
    complete({3});
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        EXPECT_TRUE(text_has(zone, "The ford is held and crossed."));
    }
    complete({6});
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        EXPECT_TRUE(text_has(zone, "The mountain gate shut behind you."));
        EXPECT_FALSE(text_has(zone, "The ford is held and crossed."));
    }
    // The whole road walked: the summit milestone, and the loop line leads.
    complete({1, 2, 12, 24});
    complete_road(kWarRoad);
    complete_road(kBurdenRoad);
    complete({26});
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        ASSERT_FALSE(camp_text(zone).empty());
        EXPECT_EQ("The vale again. The fire remembers.", camp_text(zone)[0]);
        EXPECT_TRUE(text_has(zone, "The mountain is behind you."));
        // With every road walked the front derivations yield nothing, so the
        // docket falls back to the graph: replay tourism, forward only.
        const std::vector<CampaignZoneSession::Row>& rows = docket(zone);
        ASSERT_EQ(3u, rows.size());
        EXPECT_EQ("THE FOREST ROAD", rows[0].label);
        EXPECT_EQ(2, rows[0].level);
        EXPECT_TRUE(rows[0].cleared) << "a walked road decorates CLEARED";
        EXPECT_FALSE(zone.roster().assign.active);
    }
}

// ---------------------------------------------------------------------------
// The docket vs the SHIPPED map, level by level
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireCamp, docket_mirrors_the_shipped_exit_graph)
{
    // Every level's forward rows against the level's own authored exits, in
    // BOTH directions: the camp may not offer a road the map does not carry,
    // and every shipped exit the camp does not offer must be the way back to
    // a camp that offers this one (back rows never render, so a backtrack
    // exit is legal only as the reverse of a forward edge).
    std::map<int, std::vector<int>> forward;
    std::map<int, std::set<int>> shipped;
    std::map<int, bool> escort;
    for (int id = 1; id <= 26; id++)
    {
        if (id == 18)
            continue;  // the campaign's deliberate act gap
        set_cursor(id);
        CampaignZoneSession zone(save_);
        open_camp(zone);
        forward[id] = docket_levels(zone);
        // `escort` is authored beside the graph; the Bearer line is how a
        // player sees it, so that is what the mirror asserts.
        escort[id] = text_has(zone, "The Bearer walks with you.");
        for (const CampaignZoneSession::Row& row : docket(zone))
        {
            EXPECT_LE(row.label.size(), kLabelBudget) << row.label;
            EXPECT_LE(row.note.size(), kNoteBudget) << row.note;
        }
        LoadedWestlandsLevel lv(id);
        ASSERT_TRUE(lv.loaded) << "scen" << id;
        for (const auto& uptr : lv.world().fxlist)
        {
            const walker* fx = uptr.get();
            if (fx != nullptr && fx->query_order() == Order::Treasure &&
                fx->family() == FAMILY_EXIT)
                shipped[id].insert(fx->stats()->level());
        }
        const bool save_all =
            (lv.world().type & SCEN_TYPE_SAVE_ALL) != 0;
        EXPECT_EQ(save_all, escort[id])
            << "scen" << id << ": the Bearer line must mirror SAVE_ALL";
    }

    for (const auto& [id, rows] : forward)
    {
        std::set<int> offered;
        for (const int level : rows)
        {
            EXPECT_TRUE(offered.insert(level).second)
                << "scen" << id << " offers level " << level << " twice";
            EXPECT_EQ(1u, shipped[id].count(level))
                << "scen" << id << " offers level " << level
                << ", which the shipped map has no exit to";
        }
        for (const int exit : shipped[id])
        {
            if (offered.count(exit) != 0)
                continue;
            const auto back = forward.find(exit);
            ASSERT_NE(forward.end(), back)
                << "scen" << id << " exits to scen" << exit
                << ", which is not a level of this campaign";
            EXPECT_NE(back->second.end(),
                      std::find(back->second.begin(), back->second.end(), id))
                << "scen" << id << "'s exit to " << exit
                << " is neither offered nor the way back to a camp that "
                   "offers scen"
                << id;
        }
    }
    // The escort set is the Bearer's 16 SAVE_ALL levels, not a scattering.
    int escort_levels = 0;
    for (const auto& entry : escort)
        if (entry.second)
            escort_levels++;
    EXPECT_EQ(16, escort_levels) << "the Bearer walks 16 levels";
}

TEST_F(WestlandsFireCamp, forks_state_their_stake_on_the_row)
{
    set_cursor(4);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        const std::vector<CampaignZoneSession::Row>& rows = docket(zone);
        ASSERT_EQ(4u, rows.size());
        EXPECT_EQ("THE HIGH PASS", rows[0].label);
        EXPECT_EQ("south, over snow", rows[0].note);
        EXPECT_EQ(5, rows[0].level);
        EXPECT_EQ("THE FROZEN WALL", rows[1].label);
        EXPECT_EQ("a plea, a pay chest", rows[1].note);
        EXPECT_EQ(7, rows[1].level);
    }
    set_cursor(6);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        const std::vector<CampaignZoneSession::Row>& rows = docket(zone);
        EXPECT_EQ("THE BRIDGE", rows[0].label);
        EXPECT_EQ("the straight road", rows[0].note);
        EXPECT_EQ("THE LOST DELVE", rows[1].label);
        EXPECT_EQ("gold, and a price", rows[1].note);
    }
    set_cursor(24);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        const std::vector<CampaignZoneSession::Row>& rows = docket(zone);
        EXPECT_EQ("THE SCOURING", rows[0].label);
        EXPECT_EQ("home, and war", rows[0].note);
        EXPECT_EQ("THE GREY SHIPS", rows[1].label);
        EXPECT_EQ("the grey sea", rows[1].note);
    }
}

// A level row is the client's SetLevel outcome, not a save write — and the
// camp refetches CLEARED decoration from the same book.
TEST_F(WestlandsFireCamp, cleared_roads_stay_offered_and_decorated)
{
    complete({26});
    set_cursor(26);
    CampaignZoneSession zone(save_);
    open_camp(zone);
    const int vale = row_named(docket(zone), "THE VALE AGAIN");
    ASSERT_NE(-1, vale);
    EXPECT_EQ(1, docket(zone)[static_cast<std::size_t>(vale)].level);
    EXPECT_FALSE(docket(zone)[static_cast<std::size_t>(vale)].cleared)
        << "the vale itself is not cleared yet";
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::None,
              zone.act(0, static_cast<int>(vale)).kind)
        << "a level row belongs to the client's set tail, not to act()";
    EXPECT_EQ(26, save_.scen_num);
}

// ---------------------------------------------------------------------------
// The hot one-shot offers: at the fire, on the night they matter
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireCamp, the_delve_pair_burns_at_the_fire)
{
    complete({9});
    set_cursor(9);
    save_.m_totalcash[0] = 10;
    CampaignZoneSession zone(save_);
    open_camp(zone);
    EXPECT_TRUE(text_has(zone, "The hoard weighs on the wagons."));

    const std::vector<CampaignZoneSession::Row>& rows = docket(zone);
    const int count = row_named(rows, "COUNT THE DELVE GOLD");
    const int sink = row_named(rows, "SINK IT IN THE RIVER");
    ASSERT_NE(-1, count);
    ASSERT_NE(-1, sink);
    EXPECT_EQ("+800g, and a debt",
              rows[static_cast<std::size_t>(count)].note);
    EXPECT_EQ("no gold, no debt", rows[static_cast<std::size_t>(sink)].note);
    EXPECT_LT(count, row_named(rows, "QUARTERMASTER"))
        << "the night's choice sits above the standing doors";

    // The quartermaster never repeats the fire's own offers: one door, one
    // one-shot re-check.
    const hooks::CampaignPage shelf = fetch("stores");
    EXPECT_EQ(-1, entry_named(shelf, "COUNT THE DELVE GOLD"));
    EXPECT_EQ(-1, entry_named(shelf, "SINK IT IN THE RIVER"));

    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Acted,
              zone.act(0, count).kind);
    EXPECT_EQ(810u, save_.m_totalcash[0]) << "cost 0, grant 800";
    EXPECT_EQ(1, save_.campaign_state_get(kCampaign, "delve_counted"));
    EXPECT_EQ(0, save_.campaign_state_get(kCampaign, "delve_sunk"));
    EXPECT_EQ("Counted. Eight hundred, and a debt.", zone.take_message());
    EXPECT_EQ(-1, row_named(docket(zone), "COUNT THE DELVE GOLD"))
        << "the refetched docket drops both halves of the fork";
    EXPECT_EQ(-1, row_named(docket(zone), "SINK IT IN THE RIVER"));
    EXPECT_FALSE(text_has(zone, "The hoard weighs on the wagons."));
}

TEST_F(WestlandsFireCamp, sinking_the_delve_grants_nothing)
{
    complete({9});
    save_.m_totalcash[0] = 10;
    CampaignZoneSession zone(save_);
    open_camp(zone);
    const int sink = row_named(docket(zone), "SINK IT IN THE RIVER");
    ASSERT_NE(-1, sink);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Acted, zone.act(0, sink).kind);
    EXPECT_EQ(10u, save_.m_totalcash[0]) << "the river pays nobody";
    EXPECT_EQ(1, save_.campaign_state_get(kCampaign, "delve_sunk"));
    EXPECT_EQ(0, save_.campaign_state_get(kCampaign, "delve_counted"));
    EXPECT_EQ("The river takes it, and tells no one.", zone.take_message());
    EXPECT_EQ(-1, row_named(docket(zone), "COUNT THE DELVE GOLD"));
}

TEST_F(WestlandsFireCamp, the_delve_window_shuts_at_the_river)
{
    complete({9, 11});
    CampaignZoneSession zone(save_);
    open_camp(zone);
    EXPECT_EQ(-1, row_named(docket(zone), "COUNT THE DELVE GOLD"))
        << "counting after the river must be impossible";
    EXPECT_EQ(-1, row_named(docket(zone), "SINK IT IN THE RIVER"));
    EXPECT_FALSE(text_has(zone, "The hoard weighs on the wagons."));
}

TEST_F(WestlandsFireCamp, bread_for_sneak_costs_a_gesture)
{
    // The marsh nights: the window opens with 19 behind the company and
    // shuts at the Spider Pass.
    complete({12, 19});
    complete_road(kWarRoad);
    muster_company(0, 2, 0);
    set_cursor(20);
    save_.m_totalcash[0] = 60;
    CampaignZoneSession zone(save_);
    open_camp(zone);
    const int bread = row_named(docket(zone), "BREAD FOR SNEAK");
    ASSERT_NE(-1, bread) << "the fire offers the bread on the marsh road";
    EXPECT_EQ("kindness, remembered",
              docket(zone)[static_cast<std::size_t>(bread)].note);
    EXPECT_EQ(60, docket(zone)[static_cast<std::size_t>(bread)].cost);
    EXPECT_EQ(-1, entry_named(fetch("stores"), "BREAD FOR SNEAK"))
        << "the quartermaster does not repeat the fire's offer";

    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Acted,
              zone.act(0, bread).kind);
    EXPECT_EQ(0u, save_.m_totalcash[0]);
    EXPECT_EQ(1, save_.campaign_state_get(kCampaign, "sneak_bread"));
    EXPECT_EQ("He eats in silence, and says nothing.", zone.take_message());
    EXPECT_EQ(-1, row_named(docket(zone), "BREAD FOR SNEAK"));
}

TEST_F(WestlandsFireCamp, the_bread_window_shuts_at_the_spider_pass)
{
    complete({12, 19, 21});
    muster_company(1, 1, 0);
    CampaignZoneSession zone(save_);
    open_camp(zone);
    EXPECT_EQ(-1, row_named(docket(zone), "BREAD FOR SNEAK"));
}

// ---------------------------------------------------------------------------
// SPLIT THE PARTY: the swearing window
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireCamp, the_falls_open_the_oath_column)
{
    complete({12});
    set_cursor(12);
    muster_company(0, 0, 4);
    CampaignZoneSession zone(save_);
    open_camp(zone);

    const std::vector<std::string>& lines = camp_text(zone);
    ASSERT_EQ(3u, lines.size());
    EXPECT_EQ("The Falls. The company must divide.", lines[0]);
    EXPECT_EQ("Swear WAR or BURDEN. None turn back.", lines[1]);
    EXPECT_EQ("Unsworn blades wait at the Falls.", lines[2]);

    const CampaignZoneSession::RosterLayout& roster = zone.roster();
    EXPECT_TRUE(roster.assign.active);
    EXPECT_EQ("road", roster.assign.key);
    ASSERT_EQ(2u, roster.assign.labels.size());
    EXPECT_EQ("WAR", roster.assign.labels[0]);
    EXPECT_EQ("BURDEN", roster.assign.labels[1]);
    EXPECT_EQ("", roster.assign.frozen) << "nothing is sworn yet, so nothing "
                                           "is frozen";
    EXPECT_TRUE(roster.locks.empty());
    EXPECT_FALSE(roster.can_team) << "the oath column takes the chip cell";
    EXPECT_TRUE(roster.can_deploy);
    EXPECT_TRUE(roster.can_hire);

    // Both roads are live rows, and each states its own muster.
    const std::vector<CampaignZoneSession::Row>& rows = docket(zone);
    ASSERT_EQ(4u, rows.size());
    EXPECT_EQ("RIDE WEST: THE PLAINS", rows[0].label);
    EXPECT_EQ(13, rows[0].level);
    EXPECT_EQ("all ride", rows[0].note) << "a column nobody holds locks "
                                           "nobody out";
    EXPECT_EQ("GO EAST: THE MARSHES", rows[1].label);
    EXPECT_EQ(19, rows[1].level);
    EXPECT_EQ("all ride", rows[1].note);
}

TEST_F(WestlandsFireCamp, the_falls_warn_a_thin_column)
{
    complete({12});
    const struct
    {
        int war, burden, unsworn;
        const char* third;
    } cases[] = {
        {2, 2, 1, "Unsworn blades wait at the Falls."},
        {1, 3, 0, "One sword alone rides to war."},
        {3, 1, 0, "One sword alone guards the Bearer."},
        {2, 2, 0, nullptr},
    };
    for (const auto& c : cases)
    {
        muster_company(c.war, c.burden, c.unsworn);
        CampaignZoneSession zone(save_);
        open_camp(zone);
        const std::vector<std::string>& lines = camp_text(zone);
        if (c.third == nullptr)
        {
            EXPECT_EQ(2u, lines.size())
                << c.war << "/" << c.burden << "/" << c.unsworn;
            continue;
        }
        ASSERT_EQ(3u, lines.size())
            << c.war << "/" << c.burden << "/" << c.unsworn;
        EXPECT_EQ(c.third, lines[2]);
    }
}

TEST_F(WestlandsFireCamp, march_notes_count_the_column_and_the_waiters)
{
    complete({12});
    muster_company(4, 4, 2);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        EXPECT_EQ("party 4, 2 wait", docket(zone)[0].note);
        EXPECT_EQ("escort 4, 2 wait", docket(zone)[1].note);
    }
    muster_company(4, 3, 0);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        EXPECT_EQ("party 4", docket(zone)[0].note);
        EXPECT_EQ("escort 3", docket(zone)[1].note);
    }
}

// ---------------------------------------------------------------------------
// The freeze: a pure derivation over (a road level behind us, both musters)
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireCamp, the_freeze_predicate_matrix)
{
    const struct
    {
        bool started;
        int war, burden;
        bool frozen;
        const char* why;
    } cases[] = {
        {false, 2, 2, false, "the roads have not marched yet"},
        {false, 0, 0, false, "nothing sworn, nothing marched"},
        {true, 2, 2, true, "both roads sworn and marching"},
        {true, 1, 1, true, "one sword each is a company"},
        {true, 0, 0, false, "the unsworn bypass keeps v1 semantics"},
        {true, 3, 0, false, "a wiped east front collapses the split"},
        {true, 0, 3, false, "a wiped west front collapses the split"},
    };
    for (const auto& c : cases)
    {
        clear_completions();
        complete({12});
        if (c.started)
            complete({13});
        muster_company(c.war, c.burden, 1);
        set_cursor(14);
        CampaignZoneSession zone(save_);
        open_camp(zone);
        const CampaignZoneSession::RosterLayout& roster = zone.roster();
        EXPECT_TRUE(roster.assign.active) << c.why;
        if (c.frozen)
        {
            EXPECT_EQ("The Falls parted the company.", roster.assign.frozen)
                << c.why;
            EXPECT_FALSE(roster.locks.empty()) << c.why;
            continue;
        }
        EXPECT_EQ("", roster.assign.frozen) << c.why;
        EXPECT_TRUE(roster.locks.empty()) << c.why;
    }
}

TEST_F(WestlandsFireCamp, locks_follow_tonights_road)
{
    complete({12, 13});
    muster_company(2, 2, 1);
    // Tonight is a war-road level: the Bearer's column and the unsworn are
    // out; the war column deploys.
    set_cursor(14);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        ASSERT_EQ(2u, zone.roster().locks.size());
        ASSERT_NE(nullptr, zone.deploy_lock_for_tag(2));
        EXPECT_EQ("WITH THE BEARER", zone.deploy_lock_for_tag(2)->reason);
        ASSERT_NE(nullptr, zone.deploy_lock_for_tag(0));
        EXPECT_EQ("WAITS AT THE FALLS", zone.deploy_lock_for_tag(0)->reason);
        EXPECT_EQ(nullptr, zone.deploy_lock_for_tag(1))
            << "the war column rides its own road";
    }
    // Tonight is a burden-road level: the mirror image.
    set_cursor(20);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        ASSERT_NE(nullptr, zone.deploy_lock_for_tag(1));
        EXPECT_EQ("ON THE WAR ROAD", zone.deploy_lock_for_tag(1)->reason);
        EXPECT_EQ(nullptr, zone.deploy_lock_for_tag(2));
        ASSERT_NE(nullptr, zone.deploy_lock_for_tag(0));
        EXPECT_EQ("WAITS AT THE FALLS", zone.deploy_lock_for_tag(0)->reason);
    }
    // A level on neither road (the approach, the summit) carries no lock at
    // all: the whole company is free to fight it.
    set_cursor(kSummit);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        EXPECT_TRUE(zone.roster().locks.empty());
        EXPECT_EQ(nullptr, zone.deploy_lock_for_tag(0));
    }
}

TEST_F(WestlandsFireCamp, a_walked_road_sends_its_column_to_the_mountain)
{
    complete({12});
    complete_road(kWarRoad);
    muster_company(2, 2, 0);
    set_cursor(20);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        ASSERT_NE(nullptr, zone.deploy_lock_for_tag(1));
        EXPECT_EQ("WAITS AT THE MOUNTAIN",
                  zone.deploy_lock_for_tag(1)->reason)
            << "the war road is walked; its column is not on it any more";
    }
    // The mirror: the burden road walked, tonight on the war road.
    clear_completions();
    complete({12});
    complete_road(kBurdenRoad);
    set_cursor(14);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        ASSERT_NE(nullptr, zone.deploy_lock_for_tag(2));
        EXPECT_EQ("WAITS AT THE MOUNTAIN",
                  zone.deploy_lock_for_tag(2)->reason);
    }
}

TEST_F(WestlandsFireCamp, the_split_narrates_both_fronts)
{
    complete({12, 13, 19});
    muster_company(3, 3, 0);
    set_cursor(20);
    CampaignZoneSession zone(save_);
    open_camp(zone);
    const std::vector<std::string>& lines = camp_text(zone);
    ASSERT_EQ(3u, lines.size());
    EXPECT_EQ("War camps at the Wizard's Vale.", lines[0]);
    EXPECT_EQ("The Bearer rests at the Crossroads.", lines[1]);
    EXPECT_EQ("The Bearer must live tomorrow.", lines[2])
        << "tonight is a burden level; the stake is on screen";

    // Both fronts, then the marsh night's own offer, then the doors: the
    // roads lead the docket and the hot offer rides above the standing trade.
    const std::vector<CampaignZoneSession::Row>& rows = docket(zone);
    ASSERT_EQ(5u, rows.size());
    EXPECT_EQ("WEST: THE WIZARD'S VALE", rows[0].label);
    EXPECT_EQ(14, rows[0].level);
    EXPECT_EQ("party 3", rows[0].note);
    EXPECT_EQ("EAST: THE CROSSROADS", rows[1].label);
    EXPECT_EQ(20, rows[1].level);
    EXPECT_EQ("escort 3", rows[1].note);
    EXPECT_EQ("BREAD FOR SNEAK", rows[2].label);
    EXPECT_EQ("QUARTERMASTER", rows[3].label);
    EXPECT_EQ("THE LEDGER", rows[4].label);
}

TEST_F(WestlandsFireCamp, the_unsworn_bypass_keeps_the_old_road)
{
    // A company that ignored the fire marches anyway: no freeze, no locks,
    // both roads live, and an honest nag instead of a trap.
    complete({12, 13});
    muster_company(0, 0, 3);
    set_cursor(14);
    CampaignZoneSession zone(save_);
    open_camp(zone);
    ASSERT_FALSE(camp_text(zone).empty());
    EXPECT_EQ("No road is sworn. The fire waits.", camp_text(zone)[0]);
    EXPECT_TRUE(zone.roster().locks.empty());
    EXPECT_EQ(nullptr, zone.deploy_lock_for_tag(0));
    EXPECT_EQ("", zone.roster().assign.frozen)
        << "an unsworn company may still swear";
    const std::vector<CampaignZoneSession::Row>& rows = docket(zone);
    ASSERT_EQ(4u, rows.size());
    EXPECT_EQ("all ride", rows[0].note);
    EXPECT_EQ("all ride", rows[1].note);
}

TEST_F(WestlandsFireCamp, a_wiped_front_collapses_the_split)
{
    complete({12, 13, 19});
    // The Bearer's column is gone (killed, or sold): the east fire is out.
    muster_company(3, 0, 0);
    set_cursor(14);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        ASSERT_FALSE(camp_text(zone).empty());
        EXPECT_EQ("The east fire is ashes. All ride.", camp_text(zone)[0]);
        EXPECT_TRUE(zone.roster().locks.empty()) << "all ride means all ride";
        EXPECT_EQ("", zone.roster().assign.frozen)
            << "the company may re-form the column";
        EXPECT_EQ(2u, docket_levels(zone).size())
            << "both roads stay offered";
        EXPECT_EQ("all ride", docket(zone)[1].note)
            << "the dead column's row must not contradict the line above it";
    }
    // The west twin.
    muster_company(0, 3, 0);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        EXPECT_EQ("The west fire is ashes. All ride.", camp_text(zone)[0]);
        EXPECT_TRUE(zone.roster().locks.empty());
    }
}

TEST_F(WestlandsFireCamp, one_front_done_offers_only_the_other)
{
    complete({12, 19});
    complete_road(kWarRoad);
    muster_company(2, 2, 0);
    set_cursor(20);
    CampaignZoneSession zone(save_);
    open_camp(zone);
    const std::vector<std::string>& lines = camp_text(zone);
    ASSERT_EQ(3u, lines.size());
    EXPECT_EQ("The war is fought. The west waits.", lines[0]);
    EXPECT_EQ("The Bearer rests at the Crossroads.", lines[1]);
    EXPECT_EQ("The Bearer must live tomorrow.", lines[2]);
    const std::vector<CampaignZoneSession::Row>& rows = docket(zone);
    ASSERT_EQ(4u, rows.size());
    EXPECT_EQ("EAST: THE CROSSROADS", rows[0].label);
    EXPECT_EQ("escort 2", rows[0].note);
    EXPECT_EQ("BREAD FOR SNEAK", rows[1].label);
    EXPECT_EQ("QUARTERMASTER", rows[2].label);
    EXPECT_EQ("THE LEDGER", rows[3].label);
    EXPECT_EQ(1u, docket_levels(zone).size())
        << "the walked road is not offered again";
}

TEST_F(WestlandsFireCamp, the_summit_early_refuses_to_celebrate)
{
    // 17's own exit carries a company to the mountain with the Bearer still
    // out east. The engine cannot block that; the fire refuses to pretend.
    complete({12, 19, kSummit});
    complete_road(kWarRoad);
    muster_company(2, 2, 0);
    set_cursor(kSummit);
    CampaignZoneSession zone(save_);
    open_camp(zone);
    ASSERT_FALSE(camp_text(zone).empty());
    EXPECT_EQ("The Bearer is not yet come.", camp_text(zone)[0]);
    EXPECT_TRUE(text_has(zone, "The war is fought. The west waits."));
    const std::vector<int> levels = docket_levels(zone);
    ASSERT_EQ(1u, levels.size());
    EXPECT_EQ(20, levels[0]) << "only the burden road is offered";
}

TEST_F(WestlandsFireCamp, the_summit_warns_before_the_company_climbs)
{
    // The war-road-first company's ORDINARY state: 17 offers the mountain
    // and nothing else, so the cursor lands on 24 with the Bearer still out
    // east. The refusal has to be readable BEFORE GO, not after the fight.
    complete({12, 19});
    complete_road(kWarRoad);
    muster_company(2, 2, 0);
    set_cursor(kSummit);
    CampaignZoneSession zone(save_);
    open_camp(zone);
    ASSERT_FALSE(camp_text(zone).empty());
    EXPECT_EQ("The Bearer is not yet come.", camp_text(zone)[0])
        << "the mountain is uncleared and the cursor rests on it";
    EXPECT_TRUE(text_has(zone, "The war is fought. The west waits."));
    EXPECT_TRUE(text_has(zone, "The Bearer rests at the Crossroads."));
    // The burden road is still the offer; the mountain is not a row, and GO
    // at 24 stays possible — knowingly.
    const std::vector<int> levels = docket_levels(zone);
    ASSERT_EQ(1u, levels.size());
    EXPECT_EQ(20, levels[0]);
}

TEST_F(WestlandsFireCamp, the_summit_says_nothing_while_the_roads_still_run)
{
    // The warning is keyed to the mountain, not to the split: an ordinary
    // road night must not carry it.
    complete({12, 13, 19});
    muster_company(2, 2, 0);
    set_cursor(20);
    CampaignZoneSession zone(save_);
    open_camp(zone);
    EXPECT_FALSE(text_has(zone, "The Bearer is not yet come."));
}

TEST_F(WestlandsFireCamp, the_fire_says_when_nobody_will_march)
{
    // Swearing stands a hero down, so "every road promises a party, nobody
    // is deployed" is the state the Falls hands back. The camp says so
    // instead of letting GO answer for it.
    const struct
    {
        const char* why;
        std::vector<int> completed;
        int war, burden, cursor;
    } states[] = {
        {"the open road", {}, 0, 0, 1},
        {"the falls", {12}, 2, 2, 12},
        {"the split", {12, 13, 19}, 2, 2, 20},
        {"the reunion", {12, 13, 14, 15, 16, 17, 19, 20, 21, 22, 23}, 2, 2,
         23},
    };
    for (const auto& state : states)
    {
        clear_completions();
        for (const int level : state.completed)
            save_.add_level_completed(kCampaign, level);
        muster_company(state.war, state.burden, state.war == 0 ? 2 : 0);
        set_cursor(state.cursor);
        {
            CampaignZoneSession zone(save_);
            open_camp(zone);
            EXPECT_FALSE(text_has(zone, "No sword is deployed. None march."))
                << state.why << ": a standing company is not nagged";
        }
        bench_company();
        CampaignZoneSession zone(save_);
        open_camp(zone);
        EXPECT_TRUE(text_has(zone, "No sword is deployed. None march."))
            << state.why;
        EXPECT_LE(camp_text(zone).size(), kCampTextLines) << state.why;
    }
}

TEST_F(WestlandsFireCamp, an_empty_company_is_not_nagged_to_deploy)
{
    // No company at all is the fresh-file state, not a mistake to warn about.
    muster_company(0, 0, 0);
    set_cursor(1);
    CampaignZoneSession zone(save_);
    open_camp(zone);
    EXPECT_FALSE(text_has(zone, "No sword is deployed. None march."));
}

TEST_F(WestlandsFireCamp, the_locks_stand_the_wrong_column_down)
{
    // The natural order: the company deploys at the Falls, THEN the freeze
    // lands. A lock that only refused the toggle would leave the whole
    // company marching east while the fire narrated a split.
    complete({12, 13, 19});
    muster_company(2, 2, 1);
    set_cursor(20);  // a burden night
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        // Slots 0,1 swore WAR; 2,3 swore BURDEN; 4 is unsworn.
        EXPECT_EQ(std::vector<int>({2, 3}), standing())
            << "only the Bearer's column walks the marsh road";
    }
    // The mirror: tonight is a war night, so the WAR column stands.
    set_cursor(14);
    for (auto& slot : save_.team_list)
    {
        if (slot)
            slot->deployed = true;
    }
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        EXPECT_EQ(std::vector<int>({0, 1}), standing());
    }
}

TEST_F(WestlandsFireCamp, an_unlocked_camp_never_stands_anyone_down)
{
    // The bypass, the collapse and every open-road night compose no lock,
    // and a camp without locks must never touch the player's sortie.
    const struct
    {
        const char* why;
        std::vector<int> completed;
        int war, burden, unsworn, cursor;
    } states[] = {
        {"the open road", {}, 0, 0, 4, 1},
        {"the falls", {12}, 2, 2, 0, 12},
        {"the bypass", {12, 13}, 0, 0, 4, 14},
        {"a collapsed front", {12, 13, 19}, 4, 0, 0, 14},
        {"the summit", {12, 13, 19}, 2, 2, 0, kSummit},
    };
    for (const auto& state : states)
    {
        clear_completions();
        for (const int level : state.completed)
            save_.add_level_completed(kCampaign, level);
        muster_company(state.war, state.burden, state.unsworn);
        set_cursor(state.cursor);
        const std::vector<int> before = standing();
        CampaignZoneSession zone(save_);
        open_camp(zone);
        EXPECT_TRUE(zone.roster().locks.empty()) << state.why;
        EXPECT_EQ(before, standing()) << state.why;
    }
}

TEST_F(WestlandsFireCamp, the_frozen_split_closes_the_hiring_board)
{
    // A blade bought after the Falls could swear no road (the column is
    // frozen) and walk none (the unsworn are locked out of both), so the
    // camp refuses the sale instead of taking the gold for a bricked hero.
    complete({12, 13, 19});
    muster_company(2, 2, 0);
    set_cursor(20);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        EXPECT_FALSE(zone.roster().can_hire);
        const hooks::CampaignPage page = fetch("stores");
        EXPECT_NE(-1, line_index(page, "No new blades until the roads meet."))
            << "the trade door explains the closed board";
    }
    // Before the freeze the board is open, and it says nothing about it.
    clear_completions();
    complete({12});
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        EXPECT_TRUE(zone.roster().can_hire);
        const hooks::CampaignPage page = fetch("stores");
        EXPECT_EQ(-1, line_index(page, "No new blades until the roads meet."));
    }
    // A wiped column lifts the freeze — and the board with it.
    complete({12, 13, 19});
    muster_company(4, 0, 0);
    set_cursor(14);
    {
        CampaignZoneSession zone(save_);
        open_camp(zone);
        EXPECT_TRUE(zone.roster().can_hire) << "all ride, so all may be hired";
        const hooks::CampaignPage page = fetch("stores");
        EXPECT_EQ(-1, line_index(page, "No new blades until the roads meet."));
    }
}

TEST_F(WestlandsFireCamp, the_reunion_gathers_the_company)
{
    complete({12});
    complete_road(kWarRoad);
    complete_road(kBurdenRoad);
    muster_company(2, 2, 1);
    set_cursor(23);
    CampaignZoneSession zone(save_);
    open_camp(zone);
    const std::vector<std::string>& lines = camp_text(zone);
    ASSERT_EQ(2u, lines.size());
    EXPECT_EQ("The fronts meet under the mountain.", lines[0]);
    EXPECT_EQ("The company is whole again.", lines[1]);

    const CampaignZoneSession::RosterLayout& roster = zone.roster();
    EXPECT_FALSE(roster.assign.active) << "the oath is spent";
    EXPECT_TRUE(roster.locks.empty());
    EXPECT_TRUE(roster.can_team) << "the team chip comes back";

    const std::vector<CampaignZoneSession::Row>& rows = docket(zone);
    ASSERT_EQ(3u, rows.size());
    EXPECT_EQ("THE MOUNTAIN OF FIRE", rows[0].label);
    EXPECT_EQ(kSummit, rows[0].level);
    EXPECT_EQ("all swords together", rows[0].note);
}

TEST_F(WestlandsFireCamp, the_last_fork_returns_after_the_summit)
{
    complete({12, kSummit});
    complete_road(kWarRoad);
    complete_road(kBurdenRoad);
    muster_company(2, 2, 0);
    set_cursor(kSummit);
    CampaignZoneSession zone(save_);
    open_camp(zone);
    const std::vector<int> levels = docket_levels(zone);
    ASSERT_EQ(2u, levels.size());
    EXPECT_EQ(25, levels[0]);
    EXPECT_EQ(26, levels[1]);
    EXPECT_FALSE(zone.roster().assign.active);
    EXPECT_TRUE(text_has(zone, "The mountain is behind you."));
}

// ---------------------------------------------------------------------------
// The prose budgets, over every camp state
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireCamp, every_camp_state_stays_in_budget)
{
    // One entry per composed state: the sweep is the promise that a camp
    // state cannot ship a line the panel would cut or a docket the ordinal
    // band cannot hold.
    struct State
    {
        const char* name;
        std::vector<int> completed;
        int war, burden, unsworn;
        int cursor;
        bool benched = false;
    };
    const std::vector<State> states = {
        {"fresh road", {}, 0, 0, 0, 1},
        {"a fork", {3}, 0, 0, 4, 4},
        {"the delve window", {9}, 0, 0, 4, 9},
        {"the falls", {12}, 0, 0, 4, 12},
        {"the falls, sworn", {12}, 3, 3, 0, 12},
        {"the split, frozen", {12, 13, 19}, 3, 3, 2, 20},
        {"the split, bread", {12, 13, 19}, 3, 3, 0, 21},
        {"the bypass", {12, 13}, 0, 0, 4, 14},
        {"a collapsed front", {12, 13, 19}, 4, 0, 0, 14},
        {"one front done",
         {12, 13, 14, 15, 16, 17, 19},
         2,
         2,
         1,
         20},
        {"the early summit",
         {12, 13, 14, 15, 16, 17, 19, 24},
         2,
         2,
         0,
         24},
        // The state BEFORE the early summit is fought: 17's only exit put
        // the cursor on the mountain with the Bearer still out east.
        {"standing at the summit",
         {12, 13, 14, 15, 16, 17, 19},
         2,
         2,
         0,
         24},
        {"the falls, nobody standing", {12}, 2, 2, 0, 12, true},
        {"the split, nobody standing", {12, 13, 19}, 2, 2, 0, 20, true},
        {"the reunion",
         {12, 13, 14, 15, 16, 17, 19, 20, 21, 22, 23},
         2,
         2,
         0,
         23},
        {"the last fork",
         {12, 13, 14, 15, 16, 17, 19, 20, 21, 22, 23, 24},
         2,
         2,
         0,
         24},
        {"the loop",
         {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20,
          21, 22, 23, 24, 25, 26},
         0,
         0,
         3,
         1},
        {"off the map", {}, 0, 0, 2, 18},
    };
    for (const State& state : states)
    {
        clear_completions();
        for (const int level : state.completed)
            save_.add_level_completed(kCampaign, level);
        muster_company(state.war, state.burden, state.unsworn);
        if (state.benched)
            bench_company();
        set_cursor(state.cursor);
        save_.m_totalcash[0] = 4321;
        CampaignZoneSession zone(save_);
        open_camp(zone);

        const std::vector<std::string>& lines = camp_text(zone);
        EXPECT_FALSE(lines.empty()) << state.name << ": the fire must speak";
        EXPECT_LE(lines.size(), kCampTextLines) << state.name;
        for (const std::string& line : lines)
            EXPECT_LE(line.size(), kLineBudget) << state.name << ": " << line;
        const std::vector<CampaignZoneSession::Row>& rows = docket(zone);
        EXPECT_LE(rows.size(),
                  static_cast<std::size_t>(hooks::kCampaignZoneMaxActionEntries))
            << state.name;
        ASSERT_GE(rows.size(), 2u) << state.name << ": the doors always stand";
        EXPECT_EQ("QUARTERMASTER", rows[rows.size() - 2].label) << state.name;
        EXPECT_EQ("THE LEDGER", rows[rows.size() - 1].label) << state.name;
        for (const CampaignZoneSession::Row& row : rows)
        {
            EXPECT_FALSE(row.label.empty()) << state.name;
            EXPECT_LE(row.label.size(), kLabelBudget)
                << state.name << ": " << row.label;
            EXPECT_LE(row.note.size(), kNoteBudget)
                << state.name << ": " << row.note;
        }
        EXPECT_GE(zone.roster().rows_per_page,
                  CampaignZoneSession::kRosterMinRows)
            << state.name << ": the company must stay visible";
        const CampaignZoneSession::ReadoutLayout* readout = zone.readout();
        ASSERT_NE(nullptr, readout) << state.name;
        EXPECT_LE(readout->items.size(),
                  static_cast<std::size_t>(hooks::kCampaignZoneMaxReadoutItems))
            << state.name;
        for (const auto& item : readout->items)
        {
            EXPECT_FALSE(item.label.empty()) << state.name;
            EXPECT_FALSE(item.value.empty()) << state.name;
        }
    }
}

TEST_F(WestlandsFireCamp, the_readout_counts_the_packs)
{
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "provisions", 2));
    CampaignZoneSession zone(save_);
    open_camp(zone);
    ASSERT_EQ(1u, zone.readout()->items.size());
    EXPECT_EQ("PACKS", zone.readout()->items[0].label);
    EXPECT_EQ("tier two", zone.readout()->items[0].value);
    // A hex-edited tier clamps to the last word the fire knows.
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "provisions", 9));
    zone.fetch();
    EXPECT_EQ("tier three", zone.readout()->items[0].value);
}

// ---------------------------------------------------------------------------
// QUARTERMASTER: the standing trade behind the camp's page row
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireCamp, the_quartermaster_opens_from_the_fire)
{
    CampaignZoneSession zone(save_);
    open_camp(zone);
    const int door = row_named(docket(zone), "QUARTERMASTER");
    ASSERT_NE(-1, door);
    const CampaignZoneSession::Row& row =
        docket(zone)[static_cast<std::size_t>(door)];
    ASSERT_EQ(CampaignPickerSession::Kind::Page, row.kind);

    CampaignPickerSession shop(save_);
    ASSERT_TRUE(shop.open_at(row.id));
    EXPECT_EQ("QUARTERMASTER", shop.page().title);
    ASSERT_EQ(3u, shop.page().lines.size());
    EXPECT_EQ("He deals in wages, bread, and weight.", shop.page().lines[0]);
    EXPECT_EQ("What is paid is repaid, in kind.", shop.page().lines[1]);
    EXPECT_EQ("Packs put bread on the hard roads.", shop.page().lines[2]);
    ASSERT_EQ(1u, shop.page().rows.size());
    EXPECT_EQ("PROVISION PACKS", shop.page().rows[0].label);
    EXPECT_EQ("tier 1 of 3", shop.page().rows[0].note);
    EXPECT_EQ(300, shop.page().rows[0].cost);
    EXPECT_FALSE(shop.page().rows[0].done);
    EXPECT_FALSE(shop.back()) << "the shop's root is the camp's page row";
}

TEST_F(WestlandsFireCamp, provisions_stack_to_the_cap_and_retire)
{
    save_.m_totalcash[0] = 1000;
    CampaignPickerSession shop(save_);
    ASSERT_TRUE(shop.open_at("stores"));
    const char* toasts[] = {
        "Packs at tier one. Heavy, and glad.",
        "Packs at tier two. Heavy, and glad.",
        "Packs at tier three. Heavy, and glad.",
    };
    const char* notes[] = {"tier 1 of 3", "tier 2 of 3", "tier 3 of 3"};
    for (int buy = 0; buy < 3; buy++)
    {
        const int row = page_row_named(shop.page(), "PROVISION PACKS");
        ASSERT_NE(-1, row) << "purchase " << buy;
        EXPECT_EQ(notes[buy],
                  shop.page().rows[static_cast<std::size_t>(row)].note);
        EXPECT_FALSE(shop.page().rows[static_cast<std::size_t>(row)].done);
        ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
                  shop.choose(static_cast<std::size_t>(row)).kind);
        EXPECT_EQ(toasts[buy], shop.take_message());
    }
    EXPECT_EQ(100u, save_.m_totalcash[0]) << "three 300g tiers";
    EXPECT_EQ(3, save_.campaign_state_get(kCampaign, "provisions"));

    // The shelf REMEMBERS the full packs instead of hiding them: a spent row
    // quotes no price and refuses the click.
    const int spent = page_row_named(shop.page(), "PROVISION PACKS");
    ASSERT_NE(-1, spent);
    EXPECT_TRUE(shop.page().rows[static_cast<std::size_t>(spent)].done);
    EXPECT_EQ("tier 3 of 3",
              shop.page().rows[static_cast<std::size_t>(spent)].note);
    const CampaignPickerSession::Outcome refused =
        shop.choose(static_cast<std::size_t>(spent));
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Refused, refused.kind);
    EXPECT_EQ(100u, save_.m_totalcash[0]) << "a spent row must not charge";
}

TEST_F(WestlandsFireCamp, watch_pay_debits_and_then_reads_as_honored)
{
    complete({7});
    save_.m_totalcash[0] = 900;
    CampaignPickerSession shop(save_);
    ASSERT_TRUE(shop.open_at("stores"));
    const int pay = page_row_named(shop.page(), "THE WATCH'S PAY");
    ASSERT_NE(-1, pay);
    EXPECT_EQ("a debt honored",
              shop.page().rows[static_cast<std::size_t>(pay)].note);
    EXPECT_TRUE(shop.page().rows[static_cast<std::size_t>(pay)].affordable);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              shop.choose(static_cast<std::size_t>(pay)).kind);
    EXPECT_EQ(0u, save_.m_totalcash[0]) << "the 900g chest must leave";
    EXPECT_EQ(1, save_.campaign_state_get(kCampaign, "watch_paid"));
    EXPECT_EQ("The chest goes north. Paid in full.", shop.take_message());
    const int honored = page_row_named(shop.page(), "THE WATCH'S PAY");
    ASSERT_NE(-1, honored) << "the refetched shelf keeps the honored debt";
    EXPECT_TRUE(shop.page().rows[static_cast<std::size_t>(honored)].done);
}

TEST_F(WestlandsFireCamp, watch_pay_waits_for_the_wall_and_a_full_purse)
{
    // No chest before the Wall detour is cleared.
    EXPECT_EQ(-1, entry_named(fetch("stores"), "THE WATCH'S PAY"));
    complete({7});
    save_.m_totalcash[0] = 899;
    CampaignPickerSession shop(save_);
    ASSERT_TRUE(shop.open_at("stores"));
    const int pay = page_row_named(shop.page(), "THE WATCH'S PAY");
    ASSERT_NE(-1, pay);
    EXPECT_FALSE(shop.page().rows[static_cast<std::size_t>(pay)].affordable);
    const CampaignPickerSession::Outcome outcome =
        shop.choose(static_cast<std::size_t>(pay));
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Refused, outcome.kind);
    EXPECT_EQ(899u, save_.m_totalcash[0]) << "a refusal must not debit";
    EXPECT_EQ(0, save_.campaign_state_get(kCampaign, "watch_paid"));
}

// Direct hook dispatch (no row, no C++ debit): every action must re-check its
// own one-shot key, refuse with its own line, and mutate nothing — the
// defense against a dispatch no surface offered.
TEST_F(WestlandsFireCamp, actions_refuse_direct_dispatch_when_spent)
{
    save_.m_totalcash[0] = 500;
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "watch_paid", 1));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "provisions", 3));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "sneak_bread", 1));
    complete({9, 11});  // the delve window is shut

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

    // An unknown entry id is a silent no-op; the retired book pages ("" and
    // the old road) answer "no page" — the camp is the road now.
    hooks::CampaignActionResult unknown;
    ASSERT_TRUE(hooks::campaign_picker_action("no_such_offer", unknown));
    EXPECT_TRUE(unknown.ok);
    EXPECT_TRUE(unknown.message.empty());
    hooks::CampaignPage page;
    EXPECT_FALSE(hooks::campaign_picker_page("no_such_page", page));
    EXPECT_FALSE(hooks::campaign_picker_page("", page))
        << "the book's root retired into the camp";
    EXPECT_FALSE(hooks::campaign_picker_page("road", page))
        << "the road page retired into the docket";
}

// ---------------------------------------------------------------------------
// THE LEDGER
// ---------------------------------------------------------------------------

TEST_F(WestlandsFireCamp, the_ledger_narrates_the_deeds_and_both_roads)
{
    // Blank company: the one blank line.
    hooks::CampaignPage ledger = fetch("ledger");
    ASSERT_EQ(1u, ledger.lines.size());
    EXPECT_EQ("The ledger lies open, and blank.", ledger.lines[0]);
    EXPECT_TRUE(ledger.entries.empty()) << "the ledger is lines-only";

    // Everything, with BOTH roads walked: the six-line full book, in order.
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "watch_paid", 1));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "delve_counted", 1));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "provisions", 2));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "sneak_bread", 1));
    complete({12, 13, 19});
    complete_road(kWarRoad);
    complete_road(kBurdenRoad);
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

    // The sunk variant swaps the delve line — and turns the counted hoard
    // into the third kindness, so the ledger hints at what waits.
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "delve_counted", 0));
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "delve_sunk", 1));
    ledger = fetch("ledger");
    ASSERT_EQ(6u, ledger.lines.size());
    EXPECT_EQ("The river keeps what you would not.", ledger.lines[1]);
    EXPECT_EQ("Grace follows the open hand.", ledger.lines[4]);
    EXPECT_EQ("West, we rode to the horns of war.", ledger.lines[5])
        << "the six-line budget drops the tail, never the hint";

    // Tier words: one, three, and the clamp on a hex-edited tier.
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "provisions", 1));
    ledger = fetch("ledger");
    EXPECT_EQ("Packs at tier one. Heavy, and glad.", ledger.lines[2]);
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "provisions", 7));
    ledger = fetch("ledger");
    EXPECT_EQ("Packs at tier three. Heavy, and glad.", ledger.lines[2]);
}

TEST_F(WestlandsFireCamp, the_ledger_never_foretells_at_the_swearing_window)
{
    // The Falls reached, nobody marched: the book holds no front positions
    // — the frozen-state stanzas must not read as prophecy (finale review).
    complete({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    muster_company(0, 0, 4);
    const hooks::CampaignPage ledger = fetch("ledger");
    for (const std::string& line : ledger.lines)
    {
        EXPECT_EQ(std::string::npos, line.find("Vale"))
            << "front positions before any march: " << line;
        EXPECT_EQ(std::string::npos, line.find("rides out"))
            << "front positions before any march: " << line;
    }
    bool unwritten = false;
    for (const std::string& line : ledger.lines)
    {
        if (line == "Nothing is written past the Falls.")
            unwritten = true;
    }
    EXPECT_TRUE(unwritten)
        << "the swearing window says the roads are unwritten";
}

TEST_F(WestlandsFireCamp, the_ledger_keeps_both_fronts_while_they_march)
{
    complete({12, 13, 19});
    muster_company(2, 2, 0);
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "watch_paid", 1));
    const hooks::CampaignPage ledger = fetch("ledger");
    ASSERT_EQ(3u, ledger.lines.size());
    EXPECT_EQ("The Watch drinks to your name.", ledger.lines[0]);
    EXPECT_EQ("War camps at the Wizard's Vale.", ledger.lines[1]);
    EXPECT_EQ("The Bearer rests at the Crossroads.", ledger.lines[2]);
}

TEST_F(WestlandsFireCamp, the_ledger_hints_at_grace_from_two_kindnesses)
{
    // One kindness is not a pattern.
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "watch_paid", 1));
    hooks::CampaignPage ledger = fetch("ledger");
    EXPECT_EQ(-1, line_index(ledger, "Grace follows the open hand."))
        << "one kindness is not a pattern";
    // Two are.
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "sneak_bread", 1));
    ledger = fetch("ledger");
    ASSERT_EQ(3u, ledger.lines.size());
    EXPECT_EQ("Grace follows the open hand.", ledger.lines[2]);
}

TEST_F(WestlandsFireCamp, the_ledger_opens_from_the_fire)
{
    ASSERT_TRUE(save_.campaign_state_set(kCampaign, "sneak_bread", 1));
    CampaignZoneSession zone(save_);
    open_camp(zone);
    const int door = row_named(docket(zone), "THE LEDGER");
    ASSERT_NE(-1, door);
    CampaignPickerSession book(save_);
    ASSERT_TRUE(
        book.open_at(docket(zone)[static_cast<std::size_t>(door)].id));
    EXPECT_EQ("THE LEDGER", book.page().title);
    ASSERT_EQ(1u, book.page().lines.size());
    EXPECT_EQ("Sneak ate at your fire.", book.page().lines[0]);
    EXPECT_TRUE(book.page().rows.empty());
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
