/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// MULTIPLAYER ARENAS (issues #206/#304/#305/#306): the campaign's Base
// Camp composition and the pages the SETUP wizard hosts —
// campaigns/modes/packs/modes.core/scripts/campaign_picker.lua — driven
// through the real CampaignZoneSession and CampaignPickerSession over
// og::data::make_campaign_providers, with the SHIPPED campaign archive
// mounted (the test_imaginations_dream_log pattern). Expectations derive
// at runtime from the campaign's own data — the scen titles in the mounted
// archive and the generated lib/mode_levels.lua manifest — never from a
// pinned list of 40 strings, so a modes_mapgen regeneration moves both
// sides together.
//
// The camp is ONE docket row — SETUP, noted with the scenario the campaign
// cursor sits on — under a roster that LEADS, and it is this client's only
// door into the wizard. The ROOT page is the GAMES index the wizard's GAME
// step hosts, and the seven arena pages are what its ARENA step shows;
// each carries a host-only RANDOM row appended LAST, served by the
// script's ONE roll helper. match_knobs tells the wizard which knobs this
// game uses, which root row lists the cursor's arena, and what a fresh
// arena deals to its authored teams (#305). No progress vocabulary
// survives anywhere in this campaign any more — no tally, no readout, no
// call line (feedback item 4). The retired camp setup page, its TEAMS/FILL
// macros and the signature are gone: the rules have one home in
// picker_common now (tests/unit/test_match_setup_session.cpp).

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
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

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <charconv>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

std::string get_asset_path();

namespace {

namespace hooks = og::script::hooks;
using og::ui::CampaignPickerSession;
using og::ui::CampaignZoneSession;

// The seven games in campaign.yaml description order — the one ordering
// pin this campaign carries (mode tags, page titles and rule lines; never
// the 40 arena strings).
struct BookMode {
    const char* tag;
    const char* prefix;  // the scen-title prefix that marks the band
    const char* title;
    const char* flavor;
};

constexpr BookMode kBookModes[] = {
    {"tdm", "Team Deathmatch: ", "TEAM DEATHMATCH",
     "First team to the kill target wins."},
    {"ctf", "CTF: ", "CAPTURE THE FLAG", "Take their flag home. Keep yours."},
    {"onslaught", "Onslaught: ", "ONSLAUGHT",
     "Last team with engines standing wins."},
    {"mutant", "Mutant: ", "MUTANT", "Every kill changes your form."},
    {"soccer", "Soccer: ", "SOCCER", "Kick the ball into their goal."},
    {"basketball", "Basketball: ", "BASKETBALL",
     "Shoot or dunk. Most points wins."},
    {"ffa", "FFA: ", "FREE FOR ALL", "Every fighter for themselves."},
};
constexpr std::size_t kModeCount = sizeof(kBookModes) / sizeof(kBookModes[0]);

// Camp docket geometry: ONE row, SETUP, which states the match the campaign
// is set to and opens the wizard on its GAME step. The same row on a joiner
// — it browses the wizard, so there is nothing to cut — which is why the
// host and joiner counts are equal now.
constexpr std::size_t kCampSetupRow = 0;
constexpr std::size_t kCampHostRows = 1;
constexpr std::size_t kCampJoinerRows = 1;
// The roster's share of the camp band: the roster LEADS the composition, so
// its column heading sits outside the grid at the classic y=33 and costs no
// unit. 8 units - 1 docket unit - 0 heading = 7 hero rows, on both faces.
constexpr int kCampRosterRows = 7;
constexpr int kCampJoinerRosterRows = 7;

// The campaign's arena census (the generator's own hard count — the old
// DECK_SIZE, which the roll inherits as og.campaign_random(#rows)).
constexpr int kArenaCount = 40;

// Display budgets (the imaginations pins).
constexpr std::size_t kLabelBudget = 24;
constexpr std::size_t kNoteBudget = 20;
constexpr std::size_t kLineBudget = 38;
constexpr std::size_t kSdlRowFaceChars = 42;

// ---------------------------------------------------------------------------
// Runtime-derived expectations
// ---------------------------------------------------------------------------

// The campaign's arenas, derived from the MOUNTED archive's scen titles:
// every id in 0..1023 that answers a title belongs to exactly one band
// (its prefix). This is the same source the script's stripped labels
// read, minus the script itself.
struct DerivedBook {
    std::vector<int> ordered;                       // every arena id, ascending
    std::map<std::string, std::vector<int>> bands;  // tag -> ids ascending
    std::map<int, std::string> stripped;            // id -> prefix-cut title
    std::map<int, std::string> tag_of;              // id -> mode tag
};

DerivedBook derive_book()
{
    DerivedBook book;
    for (int id = 0; id <= 1023; id++)
    {
        std::string title;
        if (og::data::load_scenario_title_with_error(
                ("scen" + std::to_string(id)).c_str(), title) !=
            og::data::LevelFileIoError::None)
            continue;
        bool matched = false;
        for (const BookMode& mode : kBookModes)
        {
            const std::string prefix = mode.prefix;
            if (title.rfind(prefix, 0) == 0)
            {
                book.ordered.push_back(id);
                book.bands[mode.tag].push_back(id);
                book.stripped[id] = title.substr(prefix.size());
                book.tag_of[id] = mode.tag;
                matched = true;
                break;
            }
        }
        EXPECT_TRUE(matched) << "scen" << id << " title '" << title
                             << "' carries no known mode prefix";
    }
    return book;
}

// The manifest facts each row's note carries, parsed from the generated
// lib/mode_levels.lua the script itself binds via og.use("mode_levels").
struct ManifestRow {
    std::string mode;
    int teams = 0;
    int fighters = 0;
    int time_limit = 0;
    int score_limit = 0;
    int cap_team0 = -1;  // spawn_caps[0], -1 = absent
};

std::optional<int> parse_decimal(std::string_view text)
{
    int value = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::optional<int> parse_wrapped_decimal(std::string_view line,
                                         std::string_view prefix,
                                         std::string_view suffix)
{
    if (!line.starts_with(prefix) || !line.ends_with(suffix) ||
        line.size() < prefix.size() + suffix.size())
    {
        return std::nullopt;
    }
    return parse_decimal(line.substr(
        prefix.size(), line.size() - prefix.size() - suffix.size()));
}

std::map<int, ManifestRow> parse_manifest()
{
    std::map<int, ManifestRow> rows;
    const std::string path = std::string(OG_CAMPAIGNS_SOURCE_DIR) +
                             "/modes/packs/modes.core/lib/mode_levels.lua";
    std::ifstream in(path);
    EXPECT_TRUE(in.is_open()) << "cannot open " << path;
    int current = -1;
    bool in_caps = false;
    std::string line;
    while (std::getline(in, line))
    {
        const std::string_view view = line;
        if (const auto row = parse_wrapped_decimal(view, "  [", "] = {");
            row.has_value())
        {
            current = *row;
            in_caps = false;
            continue;
        }
        if (current < 0)
            continue;
        if (line == "    spawn_caps = {")
        {
            in_caps = true;
            continue;
        }
        if (in_caps)
        {
            constexpr std::string_view kCapPrefix = "      [";
            const std::size_t split = view.find("] = ");
            if (view.starts_with(kCapPrefix) && split != view.npos &&
                view.ends_with(','))
            {
                const auto team = parse_decimal(view.substr(
                    kCapPrefix.size(), split - kCapPrefix.size()));
                const auto value = parse_decimal(view.substr(
                    split + 4, view.size() - (split + 4) - 1));
                if (team == 0 && value.has_value())
                    rows[current].cap_team0 = *value;
            }
            else if (line == "    },")
                in_caps = false;
            continue;
        }
        constexpr std::string_view kModePrefix = "    mode = \"";
        constexpr std::string_view kModeSuffix = "\",";
        if (view.starts_with(kModePrefix) && view.ends_with(kModeSuffix))
        {
            rows[current].mode = view.substr(
                kModePrefix.size(),
                view.size() - kModePrefix.size() - kModeSuffix.size());
        }
        else if (const auto teams_value =
                     parse_wrapped_decimal(view, "    teams = ", ",");
                 teams_value.has_value())
        {
            rows[current].teams = *teams_value;
        }
        else if (const auto fighters_value =
                     parse_wrapped_decimal(view, "    fighters = ", ",");
                 fighters_value.has_value())
        {
            rows[current].fighters = *fighters_value;
        }
        else if (const auto time_value = parse_wrapped_decimal(
                     view, "    time_limit = ", ",");
                 time_value.has_value())
        {
            rows[current].time_limit = *time_value;
        }
        else if (const auto score_value = parse_wrapped_decimal(
                     view, "    score_limit = ", ",");
                 score_value.has_value())
        {
            rows[current].score_limit = *score_value;
        }
    }
    return rows;
}

// The note the script must post for one arena — an independent C++ twin
// of the script's mode_note, over the same manifest facts. CTF's clock is
// the one fact a knob can override, so this twin holds while TIME LIMIT is
// MAP; ModesBookTest.ctf_note_follows_the_time_limit_knob owns the override.
std::string expected_note(const std::string& tag, const ManifestRow& row)
{
    if (tag == "tdm")
        return std::format("{} teams, to {}", row.teams, row.score_limit);
    if (tag == "ctf")
        return std::format("{} sides, {}m", row.teams, row.time_limit / 720);
    if (tag == "onslaught")
        return std::format("{} sides, {} lives", row.teams, row.cap_team0);
    if (tag == "mutant")
        return std::format("{} shifters, to {}", row.fighters,
                           row.score_limit);
    if (tag == "soccer")
        return std::format("{} sides, {} goals", row.teams, row.score_limit);
    if (tag == "basketball")
        return std::format("{} sides, to {}", row.teams, row.score_limit);
    return std::format("{} heads, to {}", row.fighters, row.score_limit);
}

// The camp row's note twin: the scenario's OWN title, upper-cased — the
// same bytes the Base Camp's second header line and the wizard's MATCH step
// already print. Read through the MOUNTED archive's loader, never a pinned
// list of 40 strings, so a regeneration moves both sides together.
std::string expected_camp_note(int id)
{
    std::string title;
    EXPECT_EQ(og::data::LevelFileIoError::None,
              og::data::load_scenario_title_with_error(
                  ("scen" + std::to_string(id)).c_str(), title))
        << "scen" << id;
    for (char& c : title)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return title;
}

// The band's own note: what the GAME step's rows say instead of a tally.
// Branchless, like the Lua it pins: every shipped band holds four arenas or
// more, so a singular arm would be dead on both sides of the pin.
std::string expected_arenas_note(std::size_t band_size)
{
    return std::format("{} arenas", band_size);
}

// The roll twin: the 1-based ordered-manifest index the deterministic test
// provider answers, stepped one row on (wrapping) when it lands on the
// arena the camp is ALREADY set to — a roll that deals the current pairing
// is a button that changes nothing.
int expected_roll(const DerivedBook& book, int pick, int pair_id)
{
    const int count = static_cast<int>(book.ordered.size());
    int index = pick - 1;
    if (book.ordered[static_cast<std::size_t>(index)] == pair_id &&
        count > 1)
        index = (index + 1) % count;
    return book.ordered[static_cast<std::size_t>(index)];
}

// ---------------------------------------------------------------------------
// Fixture: the shipped pack over the real provider glue
// ---------------------------------------------------------------------------

class ModesBookTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        previous_game_ = current_game;
        current_game = nullptr;  // dispatch resolves the shared UI VM
        restore_default_campaigns();
        (void)og::resources::mount((get_asset_path() + "packs/").c_str(),
                                   "packs/", 1);
        previous_mount_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("modes"))
            << "builtin/modes.glad should restore and mount";
        og::resources::refresh_pack_scripts();
        save_.current_campaign = "modes";
        save_.my_team = 0;
        save_.scen_num = 300;
        save_.m_totalcash[0] = 250;
        (void)og::data::consume_match_settings_dirty();
        install_providers();
    }

    void TearDown() override
    {
        hooks::clear_campaign_providers();
        (void)og::data::consume_match_settings_dirty();
        (void)unmount_campaign_package_with_error("modes");
        if (!previous_mount_.empty() && previous_mount_ != "modes")
            (void)mount_campaign_package_with_error(previous_mount_);
        og::resources::refresh_pack_scripts();
        current_game = previous_game_;
    }

    void install_providers(std::function<bool()> is_host = {})
    {
        hooks::install_campaign_providers(
            og::data::make_campaign_providers(save_, std::move(is_host)));
    }

    // The real provider glue with a deterministic roll: og.campaign_random
    // answers `pick` (tests choose picks inside 1..n; an out-of-range pick
    // clamps to n so a stale test fails loudly on the value, not UB).
    void install_providers_with_pick(int pick,
                                     std::function<bool()> is_host = {})
    {
        hooks::CampaignProviders providers =
            og::data::make_campaign_providers(save_, std::move(is_host));
        providers.random_pick = [pick](int n) {
            return pick <= n ? pick : n;
        };
        hooks::install_campaign_providers(std::move(providers));
    }

    void complete_all(const DerivedBook& book)
    {
        for (const int id : book.ordered)
            save_.add_level_completed("modes", id);
    }

    // Every page opens on its own id; the ROOT ("") is the GAMES index,
    // and open_at("") is what CampaignPickerSession::open() does.
    CampaignPickerSession::DecoratedPage open_page(const std::string& page_id)
    {
        CampaignPickerSession session(save_);
        EXPECT_TRUE(session.open_at(page_id)) << "page: " << page_id;
        return session.page();
    }

    SaveData save_;
    std::string previous_mount_;
    GameplayContext* previous_game_ = nullptr;
};

// The camp's docket rows, fetched through the real zone session.
const std::vector<CampaignZoneSession::Row>& camp_rows(
    const CampaignZoneSession& zone)
{
    return zone.actions()[0].rows;
}

// A scripted terminal client: answers in, the composed page text and the
// notice lines out. The terminal camp is the surface that renders the
// WHOLE composition (no pager, no hover), so it is where the docket row
// and its confirmation can be read as a player meets them.
struct ScriptedCampIo {
    SaveData* save = nullptr;
    std::vector<std::string> answers;
    std::size_t next = 0;
    std::vector<std::string> pages;    // one composed screen per prompt
    std::vector<std::string> notices;  // confirmations, refusals, toasts
    int applied_level = -1;

    og::ui::TerminalCampaignPickerIo io()
    {
        og::ui::TerminalCampaignPickerIo out;
        out.prompt = [this](const std::string& title,
                            const std::vector<std::string>& lines,
                            const std::string& label)
            -> std::optional<std::string> {
            std::string page = "--- " + title + " ---\n";
            for (const std::string& line : lines)
                page += line + "\n";
            page += label;
            pages.push_back(page);
            if (next >= answers.size())
                return std::nullopt;
            return answers[next++];
        };
        out.notice = [this](const std::string& line) {
            notices.push_back(line);
        };
        out.is_host = [] { return true; };
        out.apply_level = [this](int level, bool /*replay_arm*/) {
            applied_level = level;
            if (save != nullptr)
                save->scen_num = static_cast<short>(level);
        };
        return out;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Registration and clean load
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, pack_registers_one_camp_and_book_with_no_vars)
{
    EXPECT_TRUE(hooks::campaign_picker_registered())
        << "modes.core must register its pages";
    EXPECT_TRUE(hooks::campaign_zone_registered())
        << "modes.core must compose the camp";
    EXPECT_TRUE(hooks::campaign_match_knobs_registered())
        << "modes.core must tell the wizard which knobs its games use";
    EXPECT_TRUE(hooks::campaign_registered_vars().empty())
        << "the campaign registers no campaign vars";

    // The camp, every page the wizard hosts and the knobs parse clean.
    hooks::CampaignZone zone;
    EXPECT_TRUE(hooks::campaign_zone(zone));
    hooks::CampaignPage page;
    EXPECT_TRUE(hooks::campaign_picker_page("", page))
        << "the root IS the GAMES index the wizard's GAME step hosts";
    EXPECT_TRUE(hooks::campaign_picker_page("games", page));
    for (const BookMode& mode : kBookModes)
        EXPECT_TRUE(hooks::campaign_picker_page(mode.tag, page)) << mode.tag;
    hooks::CampaignMatchKnobs k;
    EXPECT_TRUE(hooks::campaign_match_knobs(k));

    const std::vector<og::script::ScriptError>& errors =
        og::script::active_world_scripts().host().errors();
    for (const og::script::ScriptError& error : errors)
        ADD_FAILURE() << "script error at " << error.where << ": "
                      << error.message;
}

// A page id the campaign never lists answers "no page" (the engine's
// malformed-page guard keeps the stock UI reachable); an entry id no row
// carries is a served no-op with no toast. The ROOT is the one id that
// changed sides: it used to be retired and now answers the GAMES index.
TEST_F(ModesBookTest, unknown_and_retired_pages_are_guarded)
{
    hooks::CampaignPage page;
    EXPECT_FALSE(hooks::campaign_picker_page("neverwhere", page));
    EXPECT_TRUE(hooks::campaign_picker_page("", page))
        << "the root is the GAMES index the wizard hosts";
    EXPECT_FALSE(hooks::campaign_picker_page("setup", page))
        << "the camp setup page retired into the wizard";
    EXPECT_FALSE(hooks::campaign_picker_page("card", page))
        << "the v1 card page stays retired (the deck itself retired with "
           "D3's RANDOM ARENA roll)";
    hooks::CampaignActionResult result;
    EXPECT_TRUE(hooks::campaign_picker_action("neverwhere", result))
        << "the campaign serves actions, so the hook dispatches";
    EXPECT_TRUE(result.ok);
    EXPECT_EQ("", result.message);

    // The retired roll id: the docket's RANDOM ARENA action became the two
    // RANDOM rows the wizard's own pages carry, so "random_scenario" is a
    // served no-op like any id no row spells — never a second roll.
    hooks::CampaignActionResult retired;
    EXPECT_TRUE(hooks::campaign_picker_action("random_scenario", retired));
    EXPECT_TRUE(retired.ok);
    EXPECT_EQ(-1, retired.level) << "the retired roll answers no level";
    EXPECT_EQ("", retired.message);
}

// ---------------------------------------------------------------------------
// The camp: one SETUP row under a roster that leads
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, base_camp_composes_the_table)
{
    const DerivedBook book = derive_book();
    ASSERT_EQ(static_cast<std::size_t>(kArenaCount), book.ordered.size())
        << "the campaign ships 40 arenas";
    ASSERT_EQ(kModeCount, book.bands.size());
    const int cursor = save_.scen_num;
    ASSERT_TRUE(book.tag_of.contains(cursor)) << "the fixture cursor is an "
                                                 "arena of the manifest";

    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted()) << "the camp composition must adopt";
    EXPECT_TRUE(zone.composed());

    // No readout at all. The tally it carried was the progress vocabulary
    // feedback item 4 retired, and the purse is already inked in the C++
    // header cell above this band on every surface.
    EXPECT_EQ(nullptr, zone.readout())
        << "the camp composes no readout: nothing here counts anything";

    // No text line either: the one row already states the match, and the
    // rules are the wizard's own steps.
    EXPECT_TRUE(zone.texts().empty())
        << "a line here would restate the row and cost a roster unit";

    // The docket: ONE row, the wizard's only door on this client.
    ASSERT_EQ(1u, zone.actions().size());
    const std::vector<CampaignZoneSession::Row>& rows = camp_rows(zone);
    ASSERT_EQ(kCampHostRows, rows.size())
        << "the docket is ONE row in every host state";

    const CampaignZoneSession::Row& setup = rows[kCampSetupRow];
    EXPECT_EQ(CampaignPickerSession::Kind::Page, setup.kind);
    EXPECT_EQ("games", setup.id)
        << "the id names no ROOT page row, so the wizard opens on GAME";
    EXPECT_EQ("SETUP", setup.label);
    EXPECT_EQ(expected_camp_note(cursor), setup.note)
        << "the note is the scenario's OWN title, the same bytes the header "
           "line and the wizard's MATCH step print";
    EXPECT_EQ(0, setup.cost);
    EXPECT_TRUE(setup.affordable);

    for (const CampaignZoneSession::Row& row : rows)
    {
        EXPECT_NE("setup", row.id) << "the camp setup page left for the wizard";
        EXPECT_NE("random_scenario", row.id)
            << "the docket roll moved onto the wizard's own pages";
        EXPECT_EQ(std::string::npos, row.label.find("GAME:"))
            << "the GAME:/ARENA: pair collapsed into one row";
        EXPECT_EQ(std::string::npos, row.label.find("ARENA:"));
    }

    // The roster keeps every capability and no oath column: this campaign
    // has no story reason for locks or assignment.
    const CampaignZoneSession::RosterLayout& roster = zone.roster();
    EXPECT_TRUE(roster.can_deploy);
    EXPECT_TRUE(roster.can_train);
    EXPECT_TRUE(roster.can_reorder);
    EXPECT_TRUE(roster.can_team);
    EXPECT_TRUE(roster.can_hire);
    EXPECT_TRUE(roster.locks.empty());
    EXPECT_FALSE(roster.assign.active);
    EXPECT_TRUE(roster.header_at_top)
        << "the roster leads the composition, so its column heading keeps "
           "the classic y=33 band instead of leaving it blank";
    EXPECT_EQ(kCampRosterRows, roster.rows_per_page)
        << "1 docket unit + a roster that leads (header outside the grid) "
           "leave 7 of 8";
}

// The docket the SDL panel actually SHOWS. A composition whose rows spill
// past their band does not append — it hides the tail behind two bare
// arrows at the end of the first row, and a door living on page 2 of an
// uncounted pager is not a camp that "has" that door.
TEST_F(ModesBookTest, every_camp_row_renders_without_a_pager)
{
    const DerivedBook book = derive_book();
    const auto check = [](const CampaignZoneSession& zone, const char* what) {
        ASSERT_EQ(1u, zone.actions().size()) << what;
        const CampaignZoneSession::ActionsLayout& docket = zone.actions()[0];
        EXPECT_EQ(static_cast<int>(docket.rows.size()),
                  docket.page.end_index() - docket.page.first_index())
            << what << ": every docket row must be in the first window";
        EXPECT_FALSE(docket.page.multi_page())
            << what << ": the camp must not page its own docket";
    };

    {
        CampaignZoneSession zone(save_);
        zone.fetch();
        ASSERT_TRUE(zone.scripted());
        ASSERT_EQ(kCampHostRows, camp_rows(zone).size());
        check(zone, "the fresh host camp");
    }

    // A campaign played to the end composes the identical camp: there is
    // nothing here for completion to move.
    complete_all(book);
    {
        CampaignZoneSession zone(save_);
        zone.fetch();
        ASSERT_TRUE(zone.scripted());
        ASSERT_EQ(kCampHostRows, camp_rows(zone).size());
        check(zone, "every arena played");
    }

    // And the joiner, which browses the same one row.
    install_providers([] { return false; });
    {
        CampaignZoneSession zone(save_);
        zone.fetch();
        ASSERT_TRUE(zone.scripted());
        ASSERT_EQ(kCampJoinerRows, camp_rows(zone).size());
        check(zone, "the joiner camp");
    }
}

// The host gate on the two RANDOM rows is the SCRIPT's, at fetch: a joiner
// cannot set the level, so the row is never composed for it rather than
// left to refuse at the click (the SDL "(HOST)" face is level-rows-only, so
// an ungated action row would be a dead button).
TEST_F(ModesBookTest, joiner_pages_compose_no_random_rows)
{
    const DerivedBook book = derive_book();
    install_providers([] { return false; });

    hooks::CampaignPage root;
    ASSERT_TRUE(hooks::campaign_picker_page("", root));
    ASSERT_EQ(kModeCount, root.entries.size())
        << "seven games and nothing else on a joiner";
    for (const hooks::CampaignPageEntry& entry : root.entries)
    {
        EXPECT_EQ(hooks::CampaignPageEntry::Kind::Page, entry.kind)
            << entry.id;
        EXPECT_NE("random", entry.id);
    }

    for (const BookMode& mode : kBookModes)
    {
        hooks::CampaignPage page;
        ASSERT_TRUE(hooks::campaign_picker_page(mode.tag, page)) << mode.tag;
        EXPECT_EQ(book.bands.at(mode.tag).size(), page.entries.size())
            << mode.tag << ": the band's arenas, no RANDOM ARENA";
        for (const hooks::CampaignPageEntry& entry : page.entries)
        {
            EXPECT_EQ(hooks::CampaignPageEntry::Kind::Level, entry.kind)
                << mode.tag << " " << entry.id;
            EXPECT_NE(std::string("random_") + mode.tag, entry.id);
        }
    }

    // And the host, for the same fetch: the rows ARE there, so the pin
    // above is a gate and not a page that never grew a row.
    install_providers();
    hooks::CampaignPage host_root;
    ASSERT_TRUE(hooks::campaign_picker_page("", host_root));
    EXPECT_EQ(kModeCount + 1u, host_root.entries.size());
}

// The one thing the camp row says is WHICH match is set — never how much of
// the campaign is behind you. Playing every arena must move nothing on it.
TEST_F(ModesBookTest, camp_row_note_is_the_scenario_title_never_a_tally)
{
    const DerivedBook book = derive_book();
    const std::vector<int>& ctf = book.bands.at("ctf");
    ASSERT_GE(ctf.size(), 3u);
    save_.scen_num = static_cast<short>(ctf[0]);

    const auto read_row = [this]() {
        CampaignZoneSession zone(save_);
        zone.fetch();
        EXPECT_TRUE(zone.scripted());
        EXPECT_EQ(kCampHostRows, camp_rows(zone).size());
        return camp_rows(zone)[kCampSetupRow];
    };

    const CampaignZoneSession::Row before = read_row();
    EXPECT_EQ("SETUP", before.label);
    EXPECT_EQ(expected_camp_note(ctf[0]), before.note);

    for (int i = 0; i < 3; i++)
        save_.add_level_completed("modes", ctf[static_cast<std::size_t>(i)]);
    const CampaignZoneSession::Row partway = read_row();
    EXPECT_EQ(before.note, partway.note) << "three arenas played: nothing moved";

    complete_all(book);
    const CampaignZoneSession::Row done = read_row();
    EXPECT_EQ(before.id, done.id);
    EXPECT_EQ(before.label, done.label);
    EXPECT_EQ(before.note, done.note)
        << "a finished campaign composes the identical row";
    EXPECT_EQ(std::string::npos, done.note.find('/'))
        << "no n/m tally: " << done.note;
    EXPECT_EQ(std::string::npos, done.note.find("CLEARED")) << done.note;
    EXPECT_EQ(std::string::npos, done.note.find("cleared")) << done.note;
}

// The pairing derivation when the cursor is NOT an arena of the manifest —
// winning a band's last arena parks it one past the band. The fallback is
// pure ORDERING now: the band holding the greatest id BELOW the cursor, so
// a dangling cursor stays on the game just played. Nothing here reads
// completion, which is the whole point of the rewrite (feedback item 4).
TEST_F(ModesBookTest, dangling_cursor_falls_back_to_the_band_below_it)
{
    const DerivedBook book = derive_book();
    const std::vector<int>& ctf = book.bands.at("ctf");
    const std::vector<int>& tdm = book.bands.at("tdm");
    ASSERT_EQ(book.ordered.front(), tdm.front())
        << "tdm leads the manifest, so scen 0 falls through to it";

    const auto camp_note = [this]() {
        CampaignZoneSession zone(save_);
        zone.fetch();
        EXPECT_TRUE(zone.scripted());
        EXPECT_EQ(kCampHostRows, camp_rows(zone).size());
        return camp_rows(zone)[kCampSetupRow].note;
    };

    // One past the last CTF arena: the greatest id below the cursor IS that
    // arena, so the camp still names the game just played.
    save_.scen_num = static_cast<short>(ctf.back() + 1);
    ASSERT_FALSE(book.tag_of.contains(save_.scen_num)) << "cursor dangles";
    EXPECT_EQ(expected_camp_note(ctf.back()), camp_note())
        << "the band holding the greatest id below the cursor";

    // And it says the same with every arena played: the fallback is an
    // ordering, not a search for something unplayed.
    complete_all(book);
    EXPECT_EQ(expected_camp_note(ctf.back()), camp_note())
        << "a finished campaign moves the fallback nowhere";
    save_.completed_levels.clear();

    // Below EVERY arena: nothing sits under the cursor, so the first game's
    // first arena answers.
    save_.scen_num = 0;
    ASSERT_FALSE(book.tag_of.contains(save_.scen_num));
    EXPECT_EQ(expected_camp_note(tdm.front()), camp_note())
        << "nothing below the cursor: MODES[1]'s first arena";
}

// A joiner meets the same one row — there is nothing on this docket a
// machine that does not set the level cannot browse.
TEST_F(ModesBookTest, joiner_camp_is_the_one_setup_row)
{
    const DerivedBook book = derive_book();
    complete_all(book);
    install_providers([] { return false; });

    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    const std::vector<CampaignZoneSession::Row>& rows = camp_rows(zone);
    ASSERT_EQ(kCampJoinerRows, rows.size());
    EXPECT_EQ("games", rows[kCampSetupRow].id);
    EXPECT_EQ("SETUP", rows[kCampSetupRow].label);
    EXPECT_EQ(expected_camp_note(save_.scen_num), rows[kCampSetupRow].note);
    for (const CampaignZoneSession::Row& row : rows)
    {
        EXPECT_NE("random_scenario", row.id)
            << "the docket roll retired onto the wizard's pages";
        EXPECT_NE("setup", row.id) << "the camp setup page left for the wizard";
    }

    // No readout, no line, and the roster keeps the whole rest of the band.
    EXPECT_EQ(nullptr, zone.readout());
    EXPECT_TRUE(zone.texts().empty());
    EXPECT_EQ(kCampJoinerRosterRows, zone.roster().rows_per_page)
        << "1 docket unit + a roster that leads leave 7 of 8";
    EXPECT_TRUE(zone.roster().header_at_top);
    EXPECT_TRUE(zone.roster().can_deploy);
    EXPECT_TRUE(zone.roster().can_hire);
}

TEST_F(ModesBookTest, base_camp_is_pure_and_render_stable)
{
    // A counting roll provider: a random pick computed at fetch time would
    // be the deck re-labelled, so the fetch-purity pin counts the rolls
    // beside the state writes.
    int rolls = 0;
    {
        hooks::CampaignProviders providers =
            og::data::make_campaign_providers(save_);
        providers.random_pick = [&rolls](int n) {
            rolls++;
            return n;
        };
        hooks::install_campaign_providers(std::move(providers));
    }
    CampaignZoneSession first(save_);
    first.fetch();
    CampaignZoneSession second(save_);
    second.fetch();
    ASSERT_TRUE(first.scripted());
    ASSERT_TRUE(second.scripted());
    EXPECT_EQ(nullptr, first.readout());
    EXPECT_EQ(nullptr, second.readout());
    ASSERT_EQ(first.texts().size(), second.texts().size());
    for (std::size_t w = 0; w < first.texts().size(); w++)
    {
        ASSERT_EQ(first.texts()[w].lines.size(),
                  second.texts()[w].lines.size());
        for (std::size_t i = 0; i < first.texts()[w].lines.size(); i++)
            EXPECT_EQ(first.texts()[w].lines[i], second.texts()[w].lines[i]);
    }
    ASSERT_EQ(camp_rows(first).size(), camp_rows(second).size());
    for (std::size_t i = 0; i < camp_rows(first).size(); i++)
    {
        EXPECT_EQ(camp_rows(first)[i].id, camp_rows(second)[i].id);
        EXPECT_EQ(camp_rows(first)[i].label, camp_rows(second)[i].label);
        EXPECT_EQ(camp_rows(first)[i].note, camp_rows(second)[i].note);
    }
    EXPECT_FALSE(og::data::consume_match_settings_dirty())
        << "a fetch never writes a knob";
    EXPECT_EQ(0, rolls) << "a fetch never rolls — the roll lives in the "
                           "action, where the click is";
}

// ---------------------------------------------------------------------------
// The games index
// ---------------------------------------------------------------------------

// The GAME step's own page: seven games noted with the SIZE of their band —
// a fact, not a score — and then, host only and LAST so no arena ordinal
// moves, the RANDOM row. No tally line: with zero lines the step fits nine
// rows, which is what lets the eighth row land without a pager.
TEST_F(ModesBookTest, games_index_lists_the_seven_games_then_random)
{
    const DerivedBook book = derive_book();
    const std::vector<int>& ctf = book.bands.at("ctf");
    ASSERT_GE(ctf.size(), 3u);
    // Progress in the save must change nothing on this page.
    for (int i = 0; i < 3; i++)
        save_.add_level_completed("modes", ctf[static_cast<std::size_t>(i)]);

    const CampaignPickerSession::DecoratedPage page = open_page("games");
    EXPECT_EQ("GAMES", page.title);
    EXPECT_TRUE(page.lines.empty())
        << "the index states the games; it counts nothing — "
        << (page.lines.empty() ? std::string() : page.lines[0]);

    ASSERT_EQ(kModeCount + 1u, page.rows.size())
        << "seven games, then the host's RANDOM";
    for (std::size_t i = 0; i < kModeCount; i++)
    {
        const CampaignPickerSession::Row& row = page.rows[i];
        EXPECT_EQ(CampaignPickerSession::Kind::Page, row.kind);
        EXPECT_EQ(kBookModes[i].tag, row.id);
        EXPECT_EQ(kBookModes[i].title, row.label);
        EXPECT_EQ(expected_arenas_note(book.bands.at(kBookModes[i].tag).size()),
                  row.note);
    }

    const CampaignPickerSession::Row& random = page.rows[kModeCount];
    EXPECT_EQ(CampaignPickerSession::Kind::Action, random.kind)
        << "the roll is an action: the arena is only known after the click";
    EXPECT_EQ("random", random.id);
    EXPECT_EQ("RANDOM", random.label);
    EXPECT_EQ("any game, any arena", random.note);
    EXPECT_EQ(0, random.cost) << "the roll is free";
    EXPECT_TRUE(random.affordable);
}

// The obligation the wizard migration must not drop: a page FETCH renders
// the same bytes twice and writes nothing. The pages read state (the
// tallies, the clock); only actions may write it.
TEST_F(ModesBookTest, picker_menu_is_pure_and_render_stable)
{
    std::vector<std::string> pages = {"", "games"};
    for (const BookMode& mode : kBookModes)
        pages.push_back(mode.tag);

    for (const std::string& id : pages)
    {
        const CampaignPickerSession::DecoratedPage first = open_page(id);
        const CampaignPickerSession::DecoratedPage second = open_page(id);
        EXPECT_EQ(first.title, second.title) << id;
        EXPECT_EQ(first.lines, second.lines) << id;
        ASSERT_EQ(first.rows.size(), second.rows.size()) << id;
        for (std::size_t i = 0; i < first.rows.size(); i++)
        {
            EXPECT_EQ(first.rows[i].id, second.rows[i].id) << id;
            EXPECT_EQ(first.rows[i].label, second.rows[i].label) << id;
            EXPECT_EQ(first.rows[i].note, second.rows[i].note) << id;
        }
    }
    EXPECT_FALSE(og::data::consume_match_settings_dirty())
        << "a page fetch never writes a knob";
}

// ---------------------------------------------------------------------------
// The arena pages (the wizard's ARENA step)
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, arena_pages_match_the_manifest_bands)
{
    const DerivedBook book = derive_book();
    const std::map<int, ManifestRow> manifest = parse_manifest();
    ASSERT_EQ(static_cast<std::size_t>(kArenaCount), manifest.size())
        << "the generated manifest carries one row per arena";
    // The two runtime sources agree: every titled arena has a manifest
    // row of the same mode (the band-coupling tripwire).
    for (const int id : book.ordered)
    {
        ASSERT_TRUE(manifest.contains(id)) << "scen" << id;
        EXPECT_EQ(book.tag_of.at(id), manifest.at(id).mode) << "scen" << id;
    }

    for (std::size_t i = 0; i < kModeCount; i++)
    {
        // The two-hop route: the games index, then the game's own page.
        CampaignPickerSession session(save_);
        ASSERT_TRUE(session.open_at("games"));
        const CampaignPickerSession::Outcome outcome = session.choose(i);
        ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
                  outcome.kind)
            << kBookModes[i].tag;
        const CampaignPickerSession::DecoratedPage& page = session.page();
        EXPECT_EQ(kBookModes[i].title, page.title);

        const std::vector<int>& band = book.bands.at(kBookModes[i].tag);
        ASSERT_EQ(band.size() + 1u, page.rows.size())
            << kBookModes[i].tag << ": the band's arenas, then RANDOM ARENA";
        for (std::size_t r = 0; r < band.size(); r++)
        {
            const CampaignPickerSession::Row& row = page.rows[r];
            const int id = band[r];
            EXPECT_EQ(CampaignPickerSession::Kind::Level, row.kind);
            EXPECT_EQ(id, row.level);
            EXPECT_EQ(std::to_string(id), row.id);
            EXPECT_EQ(book.stripped.at(id), row.label);
            EXPECT_FALSE(row.label.empty());
            EXPECT_LE(row.label.size(), kLabelBudget) << row.label;
            EXPECT_EQ(expected_note(kBookModes[i].tag, manifest.at(id)),
                      row.note)
                << "scen" << id;
        }

        // The roll comes LAST, so every arena above it keeps the ordinal a
        // player (and a terminal drive) already learned.
        const CampaignPickerSession::Row& random = page.rows[band.size()];
        EXPECT_EQ(CampaignPickerSession::Kind::Action, random.kind);
        EXPECT_EQ(std::string("random_") + kBookModes[i].tag, random.id)
            << "the row id carries the band the roll covers";
        EXPECT_EQ("RANDOM ARENA", random.label);
        EXPECT_EQ("any arena of this game", random.note);
        EXPECT_EQ(0, random.cost);

        ASSERT_EQ(1u, page.lines.size())
            << kBookModes[i].tag << ": the rule line alone";
        EXPECT_EQ(kBookModes[i].flavor, page.lines[0]);

        // A row select carries the SET LEVEL consequence, save untouched.
        const CampaignPickerSession::Outcome pick = session.choose(0);
        EXPECT_EQ(CampaignPickerSession::OutcomeKind::SetLevel, pick.kind);
        EXPECT_EQ(band[0], pick.level);
        EXPECT_EQ(300, save_.scen_num);
    }
}

// CTF is the one game whose note states a clock, and the clock is the one
// fact the TIME LIMIT knob overrides (#241). The note must promise what the
// arena will actually run: the row's own value while the knob is MAP, and
// the knob's minutes the moment it is turned. A note still advertising the
// manifest after an override is the same dishonesty this issue set out to
// close. The wizard's RULES row and this note are now the two places the
// clock shows before the match. (The camp docket used to carry a third
// copy on its ARENA row; that row collapsed into the one SETUP row, whose
// note is the scenario title.)
TEST_F(ModesBookTest, ctf_note_follows_the_time_limit_knob)
{
    const DerivedBook book = derive_book();
    const std::map<int, ManifestRow> manifest = parse_manifest();
    const std::vector<int>& ctf = book.bands.at("ctf");
    ASSERT_FALSE(ctf.empty());
    const int id = ctf[0];
    const int authored = manifest.at(id).time_limit;
    ASSERT_GT(authored, 0) << "the shipped CTF rows author a clock";
    save_.scen_num = static_cast<short>(id);

    // Knob at MAP: the manifest's own minutes, exactly as before.
    ASSERT_EQ(0, save_.time_limit);
    EXPECT_EQ(std::format("{} sides, {}m", manifest.at(id).teams,
                          authored / 720),
              open_page("ctf").rows[0].note);

    // Knob turned: both surfaces that carry the note follow it.
    save_.time_limit = 3600; // 5 minutes, off every shipped row's value
    ASSERT_NE(authored, save_.time_limit);
    const std::string overridden =
        std::format("{} sides, 5m", manifest.at(id).teams);
    EXPECT_EQ(overridden, open_page("ctf").rows[0].note)
        << "the arena page's row promises the resolved clock";

    // And the arenas of every OTHER game are untouched — the knob names a
    // clock, and only CTF's note states one.
    save_.scen_num = 300;
    const std::vector<int>& tdm = book.bands.at("tdm");
    EXPECT_EQ(expected_note("tdm", manifest.at(tdm[0])),
              open_page("tdm").rows[0].note);

    save_.time_limit = 0;
}

// An arena page carries the game's rule line and NOTHING else — no "Next
// uncleared:", no "Every arena here is cleared." The line count is the pin
// that matters: with one line the window holds eight rows, which is the
// arithmetic the wizard's pager depends on.
TEST_F(ModesBookTest, arena_pages_carry_no_progress_line)
{
    const DerivedBook book = derive_book();
    for (const bool played : {false, true})
    {
        if (played)
            complete_all(book);
        for (std::size_t i = 0; i < kModeCount; i++)
        {
            hooks::CampaignPage page;
            ASSERT_TRUE(hooks::campaign_picker_page(kBookModes[i].tag, page))
                << kBookModes[i].tag;
            ASSERT_EQ(1u, page.lines.size())
                << kBookModes[i].tag << (played ? " (played out)" : "")
                << ": the rule line alone";
            EXPECT_EQ(kBookModes[i].flavor, page.lines[0]);
            EXPECT_EQ(std::string::npos, page.lines[0].find("cleared"));
        }
    }

    // A played-out band still lists live level rows: #207 keeps every
    // arena replayable, and nothing on the page says otherwise.
    hooks::CampaignPage ctf;
    ASSERT_TRUE(hooks::campaign_picker_page("ctf", ctf));
    ASSERT_EQ(book.bands.at("ctf").size() + 1u, ctf.entries.size());
    for (std::size_t i = 0; i < book.bands.at("ctf").size(); i++)
    {
        EXPECT_EQ(hooks::CampaignPageEntry::Kind::Level, ctf.entries[i].kind)
            << "a played arena still plays";
    }
}

// The scripted labels fall back honestly when a title misbehaves: an
// empty title reads as the arena number, a prefix-less one as itself.
// The providers are the sanctioned seam (the unit-fixture install site).
TEST_F(ModesBookTest, label_fallbacks_for_empty_and_prefixless_titles)
{
    const DerivedBook book = derive_book();
    const std::vector<int>& tdm = book.bands.at("tdm");
    ASSERT_GE(tdm.size(), 2u);
    const int blank_id = tdm[0];
    const int raw_id = tdm[1];

    hooks::CampaignProviders providers =
        og::data::make_campaign_providers(save_);
    auto real_title = providers.scenario_title;
    providers.scenario_title =
        [real_title, blank_id, raw_id](int id) -> std::string {
        if (id == blank_id)
            return "";
        if (id == raw_id)
            return "AN UNPREFIXED ARENA";
        return real_title(id);
    };
    hooks::install_campaign_providers(std::move(providers));

    hooks::CampaignPage page;
    ASSERT_TRUE(hooks::campaign_picker_page("tdm", page));
    ASSERT_EQ(tdm.size() + 1u, page.entries.size())
        << "the band's arenas, then the host's RANDOM ARENA";
    EXPECT_EQ("ARENA " + std::to_string(blank_id), page.entries[0].label);
    EXPECT_EQ("AN UNPREFIXED ARENA", page.entries[1].label);
}

// ---------------------------------------------------------------------------
// RANDOM / RANDOM ARENA: the roll, one Lua helper behind two rows
// ---------------------------------------------------------------------------

// Every provider answer maps to the manifest row it names — and never to
// the arena the campaign is already set to (the click that changes nothing
// steps one row on). The row is the GAME step's LAST one, driven through
// the same CampaignPickerSession the wizard drives; the outcome CARRIES the
// level and the session never writes the cursor — the routing belongs to
// each client's gated tail.
TEST_F(ModesBookTest, roll_answers_every_arena_and_never_the_current_arena)
{
    const DerivedBook book = derive_book();
    ASSERT_EQ(static_cast<std::size_t>(kArenaCount), book.ordered.size());
    const int pair = save_.scen_num;
    ASSERT_EQ(book.ordered[0], pair) << "the fixture cursor is the first "
                                        "arena, so pick 1 exercises the "
                                        "step-on arm";

    std::set<int> rolled;
    for (int pick = 1; pick <= kArenaCount; pick++)
    {
        install_providers_with_pick(pick);
        CampaignPickerSession session(save_);
        ASSERT_TRUE(session.open()) << "pick " << pick;
        ASSERT_EQ(kModeCount + 1u, session.page().rows.size());
        const CampaignPickerSession::Outcome outcome =
            session.choose(kModeCount);
        ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted, outcome.kind)
            << "pick " << pick;
        const int expected = expected_roll(book, pick, pair);
        EXPECT_EQ(expected, outcome.level) << "pick " << pick;
        EXPECT_NE(pair, outcome.level)
            << "pick " << pick << ": the roll never answers the arena the "
                                  "campaign is set to";
        EXPECT_EQ(pair, save_.scen_num)
            << "the session never writes the cursor — the caller's gated "
               "tail does";
        EXPECT_EQ("", session.take_message())
            << "the roll speaks through the engine's set toast, not a "
               "message of its own";
        rolled.insert(outcome.level);
    }
    EXPECT_EQ(static_cast<std::size_t>(kArenaCount) - 1, rolled.size())
        << "the 40 picks reach every arena but the current one";
    EXPECT_FALSE(rolled.contains(pair));
}

// The step-on wraps: a roll that lands on the LAST manifest row while the
// campaign is set to it answers the FIRST row, not one past the end.
TEST_F(ModesBookTest, roll_on_the_last_arena_wraps_to_the_first)
{
    const DerivedBook book = derive_book();
    save_.scen_num = static_cast<short>(book.ordered.back());
    install_providers_with_pick(kArenaCount);

    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    const CampaignPickerSession::Outcome outcome =
        session.choose(kModeCount);
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted, outcome.kind);
    EXPECT_EQ(book.ordered.front(), outcome.level) << "the wrap arm";
}

// The ARENA page's own roll: the SAME helper over the band the row's id
// names, so it can only ever answer an arena of the game you are looking
// at — and never the one already set.
TEST_F(ModesBookTest, page_random_arena_rolls_only_its_band)
{
    const DerivedBook book = derive_book();
    for (const BookMode& mode : kBookModes)
    {
        const std::vector<int>& band = book.bands.at(mode.tag);
        ASSERT_GE(band.size(), 2u) << mode.tag;
        const int cursor = band.front();
        save_.scen_num = static_cast<short>(cursor);

        std::set<int> rolled;
        for (std::size_t pick = 1; pick <= band.size(); pick++)
        {
            install_providers_with_pick(static_cast<int>(pick));
            CampaignPickerSession session(save_);
            ASSERT_TRUE(session.open_at(mode.tag)) << mode.tag;
            ASSERT_EQ(band.size() + 1u, session.page().rows.size())
                << mode.tag;
            const CampaignPickerSession::Outcome outcome =
                session.choose(band.size());
            ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted, outcome.kind)
                << mode.tag << " pick " << pick;
            EXPECT_TRUE(std::find(band.begin(), band.end(),
                                  outcome.level) != band.end())
                << mode.tag << " pick " << pick << ": scen" << outcome.level
                << " is not an arena of this game";
            EXPECT_NE(cursor, outcome.level)
                << mode.tag << " pick " << pick;
            EXPECT_EQ(cursor, save_.scen_num) << mode.tag;
            rolled.insert(outcome.level);
        }
        EXPECT_EQ(band.size() - 1, rolled.size())
            << mode.tag << ": the band's picks reach every arena but the "
                           "current one";

        // The wrap arm on this band: set to its LAST arena, the pick that
        // names that arena steps round to the first.
        save_.scen_num = static_cast<short>(band.back());
        install_providers_with_pick(static_cast<int>(band.size()));
        CampaignPickerSession session(save_);
        ASSERT_TRUE(session.open_at(mode.tag)) << mode.tag;
        const CampaignPickerSession::Outcome wrapped =
            session.choose(band.size());
        ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted, wrapped.kind)
            << mode.tag;
        EXPECT_EQ(band.front(), wrapped.level)
            << mode.tag << ": the wrap arm stays inside the band";
    }
    install_providers();
}

// The SHIPPED provider (make_campaign_providers' default): wall-clock
// seeded, so the tests pin its RANGE contract — 1..n for every n, and the
// defensive floor under a malformed ask — never a sequence. The range alone
// is satisfied by a roll that stopped rolling (a constant 1 always deals the
// first arena), so the distribution is pinned too.
TEST_F(ModesBookTest, default_random_pick_rolls_and_stays_inside_the_range)
{
    const hooks::CampaignProviders providers =
        og::data::make_campaign_providers(save_);
    ASSERT_TRUE(static_cast<bool>(providers.random_pick))
        << "the default providers ship a roll";
    for (const int n : {1, 2, 3, kArenaCount})
    {
        for (int reps = 0; reps < 20; reps++)
        {
            const int roll = providers.random_pick(n);
            ASSERT_GE(roll, 1) << "n " << n;
            ASSERT_LE(roll, n) << "n " << n;
        }
    }
    EXPECT_EQ(1, providers.random_pick(0))
        << "the defensive floor (the binding rejects n < 1 first)";
    EXPECT_EQ(1, providers.random_pick(-4));

    // It ROLLS. 64 draws over 40 arenas: a live generator lands on at least
    // five distinct arenas with probability 1 - ~1e-30, while a provider that
    // answers a constant (or steps a fixed cycle of one) collapses to one.
    std::set<int> distinct;
    for (int draw = 0; draw < 64; draw++)
        distinct.insert(providers.random_pick(kArenaCount));
    EXPECT_GE(distinct.size(), std::size_t{5})
        << "64 draws over " << kArenaCount
        << " arenas must not collapse onto one — the shipped roll is a "
           "seeded generator, not a constant";
}

// ---------------------------------------------------------------------------
// match_knobs: what the wizard shows for this game, and what it deals
// ---------------------------------------------------------------------------

// The ROOT page is the GAMES index (SPEC §3.2 item 1). "games" stays a
// valid alias — the docket's one SETUP row keeps that id — so the two
// answers must be the same bytes, and the engine's own root fetch (open(),
// which is open_at("")) must land on it.
TEST_F(ModesBookTest, root_page_is_the_games_index)
{
    hooks::CampaignPage root;
    hooks::CampaignPage alias;
    ASSERT_TRUE(hooks::campaign_picker_page("", root));
    ASSERT_TRUE(hooks::campaign_picker_page("games", alias));
    EXPECT_EQ("GAMES", root.title);
    EXPECT_EQ(alias.title, root.title);
    EXPECT_TRUE(root.lines.empty()) << "no tally line heads the index";
    EXPECT_EQ(alias.lines, root.lines);
    ASSERT_EQ(kModeCount + 1u, root.entries.size())
        << "seven games and the host's RANDOM";
    ASSERT_EQ(alias.entries.size(), root.entries.size());
    for (std::size_t i = 0; i < kModeCount; i++)
    {
        EXPECT_EQ(hooks::CampaignPageEntry::Kind::Page, root.entries[i].kind);
        EXPECT_EQ(kBookModes[i].tag, root.entries[i].id);
        EXPECT_EQ(kBookModes[i].title, root.entries[i].label);
        EXPECT_EQ(alias.entries[i].id, root.entries[i].id);
        EXPECT_EQ(alias.entries[i].label, root.entries[i].label);
        EXPECT_EQ(alias.entries[i].note, root.entries[i].note);
    }
    EXPECT_EQ(hooks::CampaignPageEntry::Kind::Action,
              root.entries[kModeCount].kind);
    EXPECT_EQ("random", root.entries[kModeCount].id);
    EXPECT_EQ(alias.entries[kModeCount].id, root.entries[kModeCount].id);

    // And through the session the wizard's GAME step drives: open() is
    // open_at(""), which used to fail outright on this campaign.
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open())
        << "the root must open: it is the page the GAME step hosts";
    EXPECT_EQ("GAMES", session.page().title);
    EXPECT_EQ(kModeCount + 1u, session.page().rows.size());
}

// The §3.2 table, one row per game: which knobs the wizard shows, the
// campaign's own TEAMS lines, and the root row whose page lists the
// cursor's arena. The engine spells no mode fact — this test is the
// contract between MODE_KNOBS and the wizard.
TEST_F(ModesBookTest, match_knobs_answers_per_mode)
{
    const DerivedBook book = derive_book();
    struct Expected {
        const char* tag;
        bool teams;
        hooks::CampaignFillKnob fill;
        bool score;
        bool time;
        const char* line;  // nullptr = the game posts no line
    };
    const Expected table[] = {
        {"tdm", true, hooks::CampaignFillKnob::Macro, true, true, nullptr},
        {"ctf", true, hooks::CampaignFillKnob::Macro, true, true, nullptr},
        {"onslaught", false, hooks::CampaignFillKnob::Off, false, true,
         nullptr},
        {"mutant", false, hooks::CampaignFillKnob::Band, true, true,
         "FILL sets how strong the bots are."},
        {"soccer", true, hooks::CampaignFillKnob::Macro, true, true,
         "STRONG adds a fighter, BRUTAL two."},
        {"basketball", true, hooks::CampaignFillKnob::Macro, true, true,
         "STRONG adds a fighter, BRUTAL two."},
        {"ffa", false, hooks::CampaignFillKnob::Band, true, true,
         "FILL sets how strong the bots are."},
    };
    ASSERT_EQ(kModeCount, sizeof(table) / sizeof(table[0]));

    for (const Expected& want : table)
    {
        const std::vector<int>& band = book.bands.at(want.tag);
        ASSERT_FALSE(band.empty()) << want.tag;
        save_.scen_num = static_cast<short>(band.front());
        hooks::CampaignMatchKnobs k;
        ASSERT_TRUE(hooks::campaign_match_knobs(k)) << want.tag;
        EXPECT_EQ(want.teams, k.teams) << want.tag;
        EXPECT_EQ(want.fill, k.fill) << want.tag;
        EXPECT_EQ(want.score, k.score) << want.tag;
        EXPECT_EQ(want.time, k.time) << want.tag;
        EXPECT_EQ(want.tag, k.arena_page)
            << "the ARENA tab descends into the page that lists this arena";
        if (want.line == nullptr)
        {
            EXPECT_TRUE(k.lines.empty()) << want.tag << ": " << (k.lines.empty() ? "" : k.lines[0]);
            continue;
        }
        ASSERT_EQ(1u, k.lines.size()) << want.tag;
        EXPECT_EQ(want.line, k.lines[0]) << want.tag;
    }
}

// #305: the ball games buy bodies above FAIR, so their arenas DEAL STRONG
// and say why. Every other game deals FAIR, exactly as before — the memo
// (arena_lineup_dealt_campaign/scen) and every 300/500 pin are untouched.
// The two truths have ONE source, mode_shape.bodies (§3.8.2).
TEST_F(ModesBookTest, match_knobs_ball_line_and_strong_deal_only_on_ball_arenas)
{
    const DerivedBook book = derive_book();
    constexpr int kThePitch = 820;      // soccer
    constexpr int kFirstCourt = 824;    // basketball
    ASSERT_EQ("soccer", book.tag_of.at(kThePitch));
    ASSERT_EQ("basketball", book.tag_of.at(kFirstCourt));

    for (const int id : {kThePitch, kFirstCourt})
    {
        save_.scen_num = static_cast<short>(id);
        hooks::CampaignMatchKnobs k;
        ASSERT_TRUE(hooks::campaign_match_knobs(k)) << "scen" << id;
        EXPECT_EQ(og::sim::kFillStrong, k.deal_fill)
            << "scen" << id << ": a fresh ball arena deals STRONG so the "
                               "shipped default fields the extra body";
        ASSERT_EQ(1u, k.lines.size()) << "scen" << id;
        EXPECT_EQ("STRONG adds a fighter, BRUTAL two.", k.lines[0]);
    }

    // The brawl games: FAIR, and no body line. One id per band, so a
    // mode_shape row added by hand without its line fails here.
    for (const char* tag : {"tdm", "ctf", "onslaught", "mutant", "ffa"})
    {
        const std::vector<int>& band = book.bands.at(tag);
        ASSERT_FALSE(band.empty()) << tag;
        save_.scen_num = static_cast<short>(band.front());
        hooks::CampaignMatchKnobs k;
        ASSERT_TRUE(hooks::campaign_match_knobs(k)) << tag;
        EXPECT_EQ(og::sim::kFillFair, k.deal_fill)
            << tag << ": only the ball games buy bodies";
        for (const std::string& line : k.lines)
        {
            EXPECT_EQ(std::string::npos, line.find("adds a fighter"))
                << tag << ": " << line;
        }
    }
}

// The knobs follow the SAME cursor derivation the docket does — there is
// one owner of level -> game (current_pair), and a dangling cursor must
// not send the wizard's ARENA tab somewhere the docket row does not name.
TEST_F(ModesBookTest, match_knobs_follows_the_dangling_cursor_like_the_docket)
{
    const DerivedBook book = derive_book();
    const std::vector<int>& ctf = book.bands.at("ctf");
    save_.scen_num = static_cast<short>(ctf.back() + 1);
    ASSERT_FALSE(book.tag_of.contains(save_.scen_num)) << "cursor dangles";

    hooks::CampaignMatchKnobs k;
    ASSERT_TRUE(hooks::campaign_match_knobs(k));
    EXPECT_EQ("ctf", k.arena_page)
        << "the band holding the greatest id below the cursor — "
           "current_pair's own fallback, the one owner";

    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(kCampHostRows, camp_rows(zone).size());
    EXPECT_EQ(expected_camp_note(ctf.back()),
              camp_rows(zone)[kCampSetupRow].note)
        << "the docket row names the arena the ARENA tab will open on";
}

// A fetch is a READING: two in a row answer the same bytes and neither
// writes a knob. The hook runs per navigation and per deal, so a write
// here would restage the match on every tab press.
TEST_F(ModesBookTest, match_knobs_is_pure_and_render_stable)
{
    (void)og::data::consume_match_settings_dirty();
    save_.scen_num = 820;
    hooks::CampaignMatchKnobs first;
    hooks::CampaignMatchKnobs second;
    ASSERT_TRUE(hooks::campaign_match_knobs(first));
    ASSERT_TRUE(hooks::campaign_match_knobs(second));
    EXPECT_EQ(first.teams, second.teams);
    EXPECT_EQ(first.fill, second.fill);
    EXPECT_EQ(first.score, second.score);
    EXPECT_EQ(first.time, second.time);
    EXPECT_EQ(first.lines, second.lines);
    EXPECT_EQ(first.arena_page, second.arena_page);
    EXPECT_EQ(first.deal_fill, second.deal_fill);
    EXPECT_FALSE(og::data::consume_match_settings_dirty())
        << "a fetch never writes a knob";
}

// The docket's one page row IS the wizard's door (D28): its id is what the
// SDL Base Camp hands to run_match_setup_screen(page_id), and "games"
// names no ROOT page row, so MatchSetupSession::open leaves the wizard on
// its GAME step. A renamed id would silently drop the host somewhere else.
TEST_F(ModesBookTest, docket_page_rows_keep_their_ids_for_the_wizard_shortcuts)
{
    const DerivedBook book = derive_book();
    for (const int id : {300, 507, 820, 824})
    {
        save_.scen_num = static_cast<short>(id);
        CampaignZoneSession zone(save_);
        zone.fetch();
        ASSERT_TRUE(zone.scripted()) << "scen" << id;
        const std::vector<CampaignZoneSession::Row>& rows = camp_rows(zone);
        ASSERT_EQ(kCampHostRows, rows.size()) << "scen" << id;
        EXPECT_EQ(CampaignPickerSession::Kind::Page, rows[kCampSetupRow].kind);
        EXPECT_EQ("games", rows[kCampSetupRow].id)
            << "scen" << id << ": the wizard opens on the GAME step";
        EXPECT_EQ(expected_camp_note(id), rows[kCampSetupRow].note)
            << "scen" << id;

        hooks::CampaignMatchKnobs k;
        ASSERT_TRUE(hooks::campaign_match_knobs(k)) << "scen" << id;
        EXPECT_EQ(book.tag_of.at(id), k.arena_page)
            << "scen" << id << ": one owner of level -> game";
    }
}

// ---------------------------------------------------------------------------
// Budgets: every face has to fit on every client
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, every_page_and_the_camp_fit_their_budgets)
{
    std::vector<std::string> pages = {"", "games"};
    for (const BookMode& mode : kBookModes)
        pages.push_back(mode.tag);

    for (const std::string& id : pages)
    {
        hooks::CampaignPage page;
        ASSERT_TRUE(hooks::campaign_picker_page(id, page)) << id;
        EXPECT_LE(page.lines.size(),
                  static_cast<std::size_t>(hooks::kCampaignPageMaxLines));
        for (const std::string& line : page.lines)
            EXPECT_LE(line.size(), kLineBudget) << id << ": " << line;
        EXPECT_LE(page.entries.size(),
                  static_cast<std::size_t>(hooks::kCampaignPageMaxEntries));
        for (const hooks::CampaignPageEntry& entry : page.entries)
        {
            EXPECT_FALSE(entry.label.empty()) << id << " row " << entry.id;
            EXPECT_LE(entry.label.size(), kLabelBudget)
                << id << ": " << entry.label;
            // The face every surface has to draw: label + " - " + note on
            // the wizard's 42-glyph row.
            EXPECT_LE(entry.label.size() + 3 + entry.note.size(),
                      kSdlRowFaceChars)
                << id << ": " << entry.label << " - " << entry.note;
            // The 20-glyph note budget guards the notes the campaign
            // GENERATES from manifest facts — the field a regeneration can
            // grow. The two RANDOM rows are hand-written constants that
            // carry no manifest data ("any arena of this game" is 22), so
            // the composed face above is their whole contract.
            if (entry.kind == hooks::CampaignPageEntry::Kind::Action)
                continue;
            EXPECT_LE(entry.note.size(), kNoteBudget)
                << id << ": " << entry.note;
        }
    }

    // The camp itself, over EVERY arena the campaign ships as the cursor,
    // on BOTH faces — no exemptions. Its note is a generated scenario
    // TITLE, which is exactly the field a regeneration can grow, and the
    // face it has to fit is the 42-glyph docket row with its door marker
    // ("SETUP - " + title + "  >"). The per-field budgets do not apply to
    // it any more (a title runs to 28); what must hold is that the composed
    // row is not clipped, so a title that no longer fits fails HERE rather
    // than quietly ellipsing on the panel.
    //
    // The campaign's match_knobs lines are swept with it: the TEAMS step
    // prints them straight, so a line the hook grew past 38 glyphs would
    // be a hard rejection at the wizard, not a clip.
    const DerivedBook book = derive_book();
    for (const bool host : {true, false})
    {
        if (host)
            install_providers();
        else
            install_providers([] { return false; });
        const std::size_t expected_rows =
            host ? kCampHostRows : kCampJoinerRows;
        for (const int id : book.ordered)
        {
            save_.scen_num = static_cast<short>(id);
            CampaignZoneSession zone(save_);
            zone.fetch();
            ASSERT_TRUE(zone.scripted());
            ASSERT_EQ(expected_rows, camp_rows(zone).size())
                << (host ? "host" : "joiner") << " scen" << id;
            EXPECT_TRUE(zone.texts().empty())
                << "neither face spends a unit on a line";
            EXPECT_EQ(nullptr, zone.readout())
                << "neither face composes a readout";
            for (const CampaignZoneSession::Row& row : camp_rows(zone))
            {
                EXPECT_FALSE(row.label.empty()) << "camp row " << row.id;
                const std::string composed =
                    og::ui::campaign_picker_row_text(row, kSdlRowFaceChars);
                EXPECT_EQ(composed,
                          og::ui::campaign_picker_row_text(
                              row, kSdlRowFaceChars * 4))
                    << "camp row clipped on the SDL face at scen" << id
                    << ": " << composed;
                EXPECT_LE(composed.size(), kSdlRowFaceChars)
                    << "camp row overruns the docket face at scen" << id
                    << ": " << composed;
            }

            hooks::CampaignMatchKnobs knobs;
            ASSERT_TRUE(hooks::campaign_match_knobs(knobs)) << "scen" << id;
            EXPECT_LE(knobs.lines.size(),
                      static_cast<std::size_t>(
                          hooks::kCampaignMatchKnobsMaxLines));
            for (const std::string& line : knobs.lines)
            {
                EXPECT_LE(line.size(),
                          static_cast<std::size_t>(
                              hooks::kCampaignMatchKnobsLineMax))
                    << "knobs line at scen" << id << ": " << line;
            }
        }
    }
    install_providers();
}

// The LINEUP hook through the REAL shipped campaign script (§4): mounting
// builtin/modes.glad registers campaign_picker.lua, whose lineup table is
// `power` alone since amendment B1 (the preset names retired with the BOTS
// wheel) — mode_match's own stat_power over the derived-stat row.
TEST_F(ModesBookTest, lineup_hook_registers_and_prices_with_stat_power)
{
    EXPECT_TRUE(hooks::campaign_lineup_registered())
        << "campaign_picker.lua must register the lineup table";

    // power(row) == stat_power(hp, mp, armor, damage, stepsize, ff,
    // level), hand computed for a fixed row:
    //   ED = (10 * (2 + 3)) / 4 = 12; RATE = 120 / 6 = 20;
    //   OFF = 12 * 20 + 5 * 3 = 255; EHP = 100 + 4 * 4 + 20 / 2 = 126;
    //   f = (126 * (255 + 60)) / 60 = 39690 / 60 = 661.
    hooks::LineupPowerRow row;
    row.family = "SOLDIER";
    row.level = 2;
    row.hp = 100;
    row.mp = 20;
    row.armor = 4;
    row.damage = 10;
    row.stepsize = 3;
    row.fire_frequency = 6;
    long long power = 0;
    ASSERT_TRUE(hooks::campaign_fighter_power(row, power));
    EXPECT_EQ(661, power) << "the campaign's power IS mode_match.stat_power";
}
