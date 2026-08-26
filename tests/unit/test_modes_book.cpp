/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// THE GAMESMASTER'S TABLE (issues #206/#212): the Multiplayer Modes
// campaign's Base Camp composition and the rooms it opens —
// campaigns/modes/packs/modes.core/scripts/campaign_picker.lua — driven
// through the real CampaignZoneSession and CampaignPickerSession over
// og::data::make_campaign_providers, with the SHIPPED campaign archive
// mounted (the test_imaginations_dream_log pattern). Expectations derive
// at runtime from the campaign's own data — the scen titles in the mounted
// archive and the generated lib/mode_levels.lua manifest — never from a
// pinned list of 39 strings, so a modes_mapgen regeneration moves both
// sides together.
//
// The camp replaced the book's root and card pages: the tallies live in the
// header readout and the GAME row, and the RANDOM SCENARIO roll (D3, which
// replaced TONIGHT'S CARD) is a camp action whose result carries the level.
// The seven field pages, the games index (which keeps the signature, since
// the cover is what it changes) and MATCH SETUP — three knobs the host
// turns one click at a time (D4, which retired the presets) — are the
// rooms, reached by page rows.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
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
#include <cstdint>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

std::string get_asset_path();

namespace {

namespace hooks = og::script::hooks;
using og::ui::CampaignPickerSession;
using og::ui::CampaignZoneSession;

// The seven games in campaign.yaml description order — the one ordering
// pin the book carries (mode tags, page titles and flavor lines; never the
// 39 arena strings).
struct BookMode {
    const char* tag;
    const char* prefix;  // the scen-title prefix that marks the band
    const char* title;
    const char* flavor;
};

constexpr BookMode kBookModes[] = {
    {"tdm", "Team Deathmatch: ", "TEAM DEATHMATCH",
     "Four colors go in. One walks out."},
    {"ctf", "CTF: ", "CAPTURE THE FLAG", "Steal theirs. Keep yours. Simple."},
    {"onslaught", "Onslaught: ", "ONSLAUGHT",
     "Hold the line until there is no line."},
    {"mutant", "Mutant: ", "MUTANT", "New shape every kill. Keep count."},
    {"soccer", "Soccer: ", "SOCCER", "No hands. Blades are fine."},
    {"basketball", "Basketball: ", "BASKETBALL",
     "The arc is chalk. The rim is law."},
    {"ffa", "FFA: ", "FREE FOR ALL", "No teams. No excuses. No refunds."},
};
constexpr std::size_t kModeCount = sizeof(kBookModes) / sizeof(kBookModes[0]);

// Camp docket geometry: GAME, FIELD, then (host only) the RANDOM SCENARIO
// roll, then MATCH SETUP. Four rows on the host in EVERY state and three
// on a joiner — the camp's whole grid is 8 units and the roster floor
// takes three, so an over-band row would not append, it would hide one
// behind the pager (the signature therefore lives on the games index).
constexpr std::size_t kCampGameRow = 0;
constexpr std::size_t kCampFieldRow = 1;
constexpr std::size_t kCampRandomRow = 2;
constexpr std::size_t kCampSetupRow = 3;
constexpr std::size_t kCampHostRows = 4;
constexpr std::size_t kCampJoinerRows = 3;
constexpr std::size_t kCampJoinerSetupRow = 2;
// The roster's share of the camp band: four docket rows + the roster
// heading leave three hero rows of the 8-unit band (the readout hoists
// into the panel heading for free).
constexpr int kCampRosterRows = 3;

// The campaign's arena census (the generator's own hard count — the old
// DECK_SIZE, which the roll inherits as og.campaign_random(#rows)).
constexpr int kArenaCount = 39;

// MATCH SETUP's knobs (D4 — the presets retired): one row each, in the
// order the page composes them, each labelled with the value it holds.
// TIME LIMIT joined in #241 — the summaries below (the rules line and the
// camp's digest) deliberately stay at the score, because the row itself
// already wears its value where the host turns it. TEAMS left in the
// 2026-08-26 lineup amendment (A1/A3) and TROOPS in amendment B5: the
// sides are the LINEUP page's band by band, and so is whether each team's
// map-shipped units are fielded, so the table is two rows and the
// summaries lost both their heads.
constexpr std::size_t kScoreRow = 0;
constexpr std::size_t kTimeRow = 1;
constexpr const char* kKnobIds[] = {"score", "time"};
constexpr std::size_t kKnobCount = sizeof(kKnobIds) / sizeof(kKnobIds[0]);

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

// The mode table row for a tag (the title the camp's GAME row spells).
const BookMode& book_mode(const std::string& tag)
{
    for (const BookMode& mode : kBookModes)
    {
        if (tag == mode.tag)
            return mode;
    }
    ADD_FAILURE() << "no such mode tag: " << tag;
    return kBookModes[0];
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

std::map<int, ManifestRow> parse_manifest()
{
    std::map<int, ManifestRow> rows;
    const std::string path = std::string(OG_CAMPAIGNS_SOURCE_DIR) +
                             "/modes/packs/modes.core/lib/mode_levels.lua";
    std::ifstream in(path);
    EXPECT_TRUE(in.is_open()) << "cannot open " << path;
    static const std::regex kRowStart(R"(^  \[(\d+)\] = \{$)");
    static const std::regex kIntField(
        R"(^    (teams|fighters|time_limit|score_limit) = (\d+),$)");
    static const std::regex kModeField(R"re(^    mode = "(\w+)",$)re");
    static const std::regex kCapEntry(R"(^      \[(\d+)\] = (\d+),$)");
    int current = -1;
    bool in_caps = false;
    std::string line;
    while (std::getline(in, line))
    {
        std::smatch m;
        if (std::regex_match(line, m, kRowStart))
        {
            current = std::stoi(m[1]);
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
            if (std::regex_match(line, m, kCapEntry))
            {
                if (std::stoi(m[1]) == 0)
                    rows[current].cap_team0 = std::stoi(m[2]);
            }
            else if (line == "    },")
                in_caps = false;
            continue;
        }
        if (std::regex_match(line, m, kModeField))
        {
            rows[current].mode = m[1];
        }
        else if (std::regex_match(line, m, kIntField))
        {
            const int value = std::stoi(m[2]);
            if (m[1] == "teams")
                rows[current].teams = value;
            else if (m[1] == "fighters")
                rows[current].fighters = value;
            else if (m[1] == "time_limit")
                rows[current].time_limit = value;
            else
                rows[current].score_limit = value;
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

// The call-line twin: first unstamped band id scanning forward from the
// cursor, wrapping past the band end; -1 when the band is fully stamped.
int expected_call(const std::vector<int>& band, const SaveData& save)
{
    for (int id : band)
    {
        if (id >= save.scen_num && !save.is_level_completed(id))
            return id;
    }
    for (int id : band)
    {
        if (id < save.scen_num && !save.is_level_completed(id))
            return id;
    }
    return -1;
}

// The stamp-tally twin: the book's ONE word for a cleared field.
std::string expected_stamp_note(const std::vector<int>& band,
                                const SaveData& save)
{
    int stamped = 0;
    for (int id : band)
    {
        if (save.is_level_completed(id))
            stamped++;
    }
    return std::format("{}/{} stamped", stamped, band.size());
}

// The roll twin: the 1-based ordered-manifest index the deterministic test
// provider answers, stepped one row on (wrapping) when it lands on the
// field the table is ALREADY set to — a roll that deals the current
// pairing is a button that changes nothing.
int expected_roll(const DerivedBook& book, int pick, int pair_id)
{
    const int count = static_cast<int>(book.ordered.size());
    int index = pick - 1;
    if (book.ordered[static_cast<std::size_t>(index)] == pair_id &&
        count > 1)
        index = (index + 1) % count;
    return book.ordered[static_cast<std::size_t>(index)];
}

std::unique_ptr<guy> make_member(short teamnum, const std::string& name,
                                 short level, std::uint32_t exp)
{
    auto member = std::make_unique<guy>(static_cast<char>(FAMILY_SOLDIER));
    member->teamnum = teamnum;
    member->name = name;
    member->level = level;
    member->exp = exp;
    return member;
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

    std::int32_t state(const std::string& key) const
    {
        return save_.campaign_state_get("modes", key);
    }

    void complete_all(const DerivedBook& book)
    {
        for (const int id : book.ordered)
            save_.add_level_completed("modes", id);
    }

    // The book's rooms open on their own page ids now — the root page
    // retired into the camp.
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
// WHOLE composition (no pager, no hover), so it is where a docket row and
// its confirmation can be read as a player meets them.
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
        << "modes.core must register the book";
    EXPECT_TRUE(hooks::campaign_zone_registered())
        << "modes.core must compose the camp";
    EXPECT_TRUE(hooks::campaign_registered_vars().empty())
        << "the table registers no campaign vars";

    // The camp and every room it opens parse clean.
    hooks::CampaignZone zone;
    EXPECT_TRUE(hooks::campaign_zone(zone));
    hooks::CampaignPage page;
    EXPECT_TRUE(hooks::campaign_picker_page("games", page));
    for (const BookMode& mode : kBookModes)
        EXPECT_TRUE(hooks::campaign_picker_page(mode.tag, page)) << mode.tag;
    EXPECT_TRUE(hooks::campaign_picker_page("setup", page));

    const std::vector<og::script::ScriptError>& errors =
        og::script::active_world_scripts().host().errors();
    for (const og::script::ScriptError& error : errors)
        ADD_FAILURE() << "script error at " << error.where << ": "
                      << error.message;
}

// A page id the book never lists answers "no page" (the engine's
// malformed-page guard keeps the stock UI reachable); an entry id no row
// carries is a served no-op with no toast. The v1 root and card pages are
// retired into the camp, so they are two more ids the book does not serve.
TEST_F(ModesBookTest, unknown_and_retired_pages_are_guarded)
{
    hooks::CampaignPage page;
    EXPECT_FALSE(hooks::campaign_picker_page("neverwhere", page));
    EXPECT_FALSE(hooks::campaign_picker_page("", page))
        << "the root page retired into the camp";
    EXPECT_FALSE(hooks::campaign_picker_page("card", page))
        << "the v1 card page stays retired (the deck itself retired with "
           "D3's RANDOM SCENARIO roll)";
    hooks::CampaignActionResult result;
    EXPECT_TRUE(hooks::campaign_picker_action("neverwhere", result))
        << "the book serves actions, so the hook dispatches";
    EXPECT_TRUE(result.ok);
    EXPECT_EQ("", result.message);
}

// ---------------------------------------------------------------------------
// The camp: the Gamesmaster's table
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, base_camp_composes_the_table)
{
    const DerivedBook book = derive_book();
    ASSERT_EQ(static_cast<std::size_t>(kArenaCount), book.ordered.size())
        << "the campaign ships 39 arenas";
    ASSERT_EQ(kModeCount, book.bands.size());
    const std::map<int, ManifestRow> manifest = parse_manifest();
    const int cursor = save_.scen_num;
    ASSERT_TRUE(book.tag_of.contains(cursor)) << "the fixture cursor is a "
                                                 "field of the manifest";
    const std::string tag = book.tag_of.at(cursor);

    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted()) << "the camp composition must adopt";
    EXPECT_TRUE(zone.composed());

    // The header readout: your stamps, and ONLY your stamps. The purse is
    // already inked in the C++ header cell above this band on every
    // surface — a composed GOLD cell would print the same wallet twice,
    // and disagree with it outright under infinite gold.
    ASSERT_NE(nullptr, zone.readout());
    EXPECT_TRUE(zone.readout()->in_header_band)
        << "the roster does not lead, so the readout heads the panel";
    ASSERT_EQ(1u, zone.readout()->items.size());
    EXPECT_EQ("BOOK", zone.readout()->items[0].label);
    EXPECT_EQ("0/39", zone.readout()->items[0].value);
    for (const hooks::CampaignZoneWidget::ReadoutItem& item :
         zone.readout()->items)
    {
        EXPECT_NE("GOLD", item.label)
            << "the header cell owns the purse (docs/basecamp-zones-design"
               ".md: the strip may never depend on a campaign composing "
               "gold into its own readout)";
    }

    // No text line on the host: the five docket rows take the whole band
    // the roster floor leaves, and the match rules live on the row that
    // changes them.
    EXPECT_TRUE(zone.texts().empty())
        << "a host line would cost the fifth docket row";

    // The docket: the pairing, the roll and the rules.
    ASSERT_EQ(1u, zone.actions().size());
    const std::vector<CampaignZoneSession::Row>& rows = camp_rows(zone);
    ASSERT_EQ(kCampHostRows, rows.size())
        << "the docket is four rows in every host state";

    const CampaignZoneSession::Row& game = rows[kCampGameRow];
    EXPECT_EQ(CampaignPickerSession::Kind::Page, game.kind);
    EXPECT_EQ("games", game.id);
    EXPECT_EQ(std::string("GAME: ") + book_mode(tag).title, game.label);
    EXPECT_EQ(expected_stamp_note(book.bands.at(tag), save_), game.note);

    const CampaignZoneSession::Row& field = rows[kCampFieldRow];
    EXPECT_EQ(CampaignPickerSession::Kind::Page, field.kind);
    EXPECT_EQ(tag, field.id) << "the FIELD row is one hop into its game";
    EXPECT_EQ("FIELD: " + book.stripped.at(cursor), field.label);
    EXPECT_EQ(expected_note(tag, manifest.at(cursor)), field.note);

    // The roll is an ACTION wearing its own name — the arena is only known
    // after the click, and the "level row wears the arena" rule is honored
    // by the engine's confirmation toast ("Level set to <arena>.", pinned
    // by the terminal camp test below), not by this label.
    const CampaignZoneSession::Row& roll = rows[kCampRandomRow];
    EXPECT_EQ(CampaignPickerSession::Kind::Action, roll.kind);
    EXPECT_EQ("random_scenario", roll.id);
    EXPECT_EQ("RANDOM SCENARIO", roll.label);
    EXPECT_EQ("any game, any field", roll.note)
        << "the roll crosses games, so the note must not read as the "
           "current game's fields only";
    EXPECT_EQ(0, roll.cost) << "the roll is free";
    EXPECT_TRUE(roll.affordable);

    const CampaignZoneSession::Row& setup = rows[kCampSetupRow];
    EXPECT_EQ(CampaignPickerSession::Kind::Page, setup.kind);
    EXPECT_EQ("setup", setup.id);
    EXPECT_EQ("MATCH SETUP", setup.label);
    EXPECT_EQ("map", setup.note) << "the rules digest, at note "
                                               "length";

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
    EXPECT_EQ(kCampRosterRows, roster.rows_per_page)
        << "4 docket units + 1 roster heading leaves 3 roster rows of the "
           "8-unit band (the readout hoists into the panel heading for "
           "free)";
}

// The docket the SDL panel actually SHOWS. A composition whose rows spill
// past their band does not append — it hides the tail behind two bare
// arrows at the end of the first row, and MATCH SETUP living on page 2 of
// an uncounted pager is not a camp that "has" a MATCH SETUP row.
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
        check(zone, "the fresh host camp");
    }

    // The states that used to grow an extra row: a full book (the
    // signature moved to the index it retitles) and a signed one (no cover
    // line at the table).
    complete_all(book);
    {
        CampaignZoneSession zone(save_);
        zone.fetch();
        ASSERT_TRUE(zone.scripted());
        ASSERT_EQ(kCampHostRows, camp_rows(zone).size());
        EXPECT_EQ("sign the book", camp_rows(zone)[kCampGameRow].note)
            << "a full book asks for the signature through its own door";
        check(zone, "39/39, unsigned");
    }
    ASSERT_TRUE(save_.campaign_state_set("modes", "book_signed", 1));
    {
        CampaignZoneSession zone(save_);
        zone.fetch();
        ASSERT_TRUE(zone.scripted());
        ASSERT_EQ(kCampHostRows, camp_rows(zone).size());
        check(zone, "39/39, signed");
    }

    // And the joiner, whose one line costs a row unit rather than a row.
    install_providers([] { return false; });
    {
        CampaignZoneSession zone(save_);
        zone.fetch();
        ASSERT_TRUE(zone.scripted());
        ASSERT_EQ(kCampJoinerRows, camp_rows(zone).size());
        check(zone, "the joiner camp");
    }
}

// The whole camp, driven on a real client: the terminal camp loop renders
// the composition and dispatches one row. The roll is the row this test is
// here for — a one-click level set whose CONFIRMATION has to name the
// arena it just set (the action row itself wears the ceremony's name, so
// the engine toast is where the "level row wears the arena" rule now
// lives), routed Acted-level through the driver's own gated tail.
TEST_F(ModesBookTest, terminal_camp_rolls_the_scenario_and_names_what_it_set)
{
    const DerivedBook book = derive_book();
    // A deterministic mid-manifest pick, off the current field: no skip
    // arm.
    const int pick = 7;
    const int rolled = expected_roll(book, pick, save_.scen_num);
    ASSERT_NE(save_.scen_num, rolled);
    install_providers_with_pick(pick);

    ScriptedCampIo io;
    io.save = &save_;
    io.answers = {std::to_string(kCampRandomRow + 1), "0"};
    og::ui::run_terminal_campaign_camp(save_, io.io());

    EXPECT_EQ(rolled, io.applied_level) << "the docket row set the level";
    EXPECT_EQ(rolled, save_.scen_num);
    ASSERT_EQ(1u, io.notices.size());
    // The ENGINE names what it set, in its own voice: the scenario's full
    // title (the SDL tail toasts world().title, and the routed set speaks
    // identically here). The stripped-arena spelling was the card ROW's
    // label affair; the rule that survives the deck is that the
    // confirmation names something playable, never the ceremony.
    std::string raw_title;
    ASSERT_EQ(og::data::LevelFileIoError::None,
              og::data::load_scenario_title_with_error(
                  ("scen" + std::to_string(rolled)).c_str(), raw_title));
    EXPECT_EQ(std::format("Level set to {}.", raw_title), io.notices[0])
        << "the confirmation names the arena, never the row's ceremony";

    // The docket the prompt actually listed: four rows, numbered, in the
    // camp's own order — the pager is a panel constraint, and a terminal
    // that hid rows behind one would be inventing a limit.
    ASSERT_GE(io.pages.size(), 2u);
    EXPECT_NE(std::string::npos, io.pages[0].find("Camp # [1-4] (0 = back): "))
        << io.pages[0];
    EXPECT_NE(std::string::npos, io.pages[0].find("BOOK 0/39"));
    EXPECT_NE(std::string::npos,
              io.pages[0].find("   3. RANDOM SCENARIO - any game, any field\n"))
        << io.pages[0];
    EXPECT_NE(std::string::npos,
              io.pages[0].find("   4. MATCH SETUP - map  >\n"))
        << "the fourth row is on the first face of every client";

    // And the click was not a no-op: the refetched camp is set to the
    // arena it named, and the roll row still stands for the next night.
    EXPECT_NE(std::string::npos,
              io.pages[1].find("   2. FIELD: " + book.stripped.at(rolled)))
        << io.pages[1];
    EXPECT_NE(std::string::npos,
              io.pages[1].find("   3. RANDOM SCENARIO - any game, any field\n"))
        << io.pages[1];
}

// The driver-level host gate on the Acted-carried level: the terminal
// providers have no host predicate (og.campaign_is_host is true on a
// terminal), so the row COMPOSES — and the driver's own is_host, the SET
// LEVEL predicate, still refuses the set. One refusal, no cursor motion,
// and no second answer behind it (the roll carries no message).
TEST_F(ModesBookTest, terminal_roll_refuses_for_a_non_host_driver)
{
    install_providers_with_pick(7);

    ScriptedCampIo io;
    io.save = &save_;
    io.answers = {std::to_string(kCampRandomRow + 1), "0"};
    og::ui::TerminalCampaignPickerIo tio = io.io();
    tio.is_host = [] { return false; };
    og::ui::run_terminal_campaign_camp(save_, tio);

    EXPECT_EQ(-1, io.applied_level) << "the refused set never applies";
    EXPECT_EQ(300, save_.scen_num);
    ASSERT_EQ(1u, io.notices.size());
    EXPECT_EQ(std::string(og::ui::kCampaignPickerHostGuardMessage),
              io.notices[0]);
}

TEST_F(ModesBookTest, base_camp_tallies_recount_from_the_save)
{
    const DerivedBook book = derive_book();
    const std::vector<int>& ctf = book.bands.at("ctf");
    ASSERT_GE(ctf.size(), 3u);
    for (int i = 0; i < 3; i++)
        save_.add_level_completed("modes", ctf[static_cast<std::size_t>(i)]);
    save_.scen_num = static_cast<short>(ctf[0]);

    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    EXPECT_EQ("3/39", zone.readout()->items[0].value)
        << "the header counts the whole book";
    const std::vector<CampaignZoneSession::Row>& rows = camp_rows(zone);
    EXPECT_EQ("GAME: CAPTURE THE FLAG", rows[kCampGameRow].label);
    EXPECT_EQ(std::format("3/{} stamped", ctf.size()),
              rows[kCampGameRow].note)
        << "the GAME row tallies the band the cursor sits in";
    EXPECT_EQ("FIELD: " + book.stripped.at(ctf[0]),
              rows[kCampFieldRow].label);
}

// The pairing derivation when the cursor is NOT a field of the manifest —
// winning a band's last arena parks it one past the band. The table falls
// back to the first game still holding an unstamped field, and says so
// without claiming to have fixed progression.
TEST_F(ModesBookTest, dangling_cursor_falls_back_to_the_first_open_game)
{
    const DerivedBook book = derive_book();
    const std::vector<int>& ctf = book.bands.at("ctf");
    const std::vector<int>& tdm = book.bands.at("tdm");
    save_.scen_num = static_cast<short>(ctf.back() + 1);
    ASSERT_FALSE(book.tag_of.contains(save_.scen_num)) << "cursor dangles";

    {
        CampaignZoneSession zone(save_);
        zone.fetch();
        ASSERT_TRUE(zone.scripted());
        const std::vector<CampaignZoneSession::Row>& rows = camp_rows(zone);
        EXPECT_EQ("GAME: TEAM DEATHMATCH", rows[kCampGameRow].label)
            << "the first game with an unstamped field";
        const int call = expected_call(tdm, save_);
        ASSERT_GE(call, 0);
        EXPECT_EQ("FIELD: " + book.stripped.at(call),
                  rows[kCampFieldRow].label)
            << "its own next unstamped field, wrapped like the call line";
    }

    // Every field stamped AND the cursor dangling: the table still names a
    // pairing rather than composing a broken row.
    complete_all(book);
    {
        CampaignZoneSession zone(save_);
        zone.fetch();
        ASSERT_TRUE(zone.scripted());
        const std::vector<CampaignZoneSession::Row>& rows = camp_rows(zone);
        EXPECT_EQ("GAME: TEAM DEATHMATCH", rows[kCampGameRow].label);
        EXPECT_EQ("FIELD: " + book.stripped.at(tdm[0]),
                  rows[kCampFieldRow].label);
        EXPECT_EQ("sign the book", rows[kCampGameRow].note)
            << "a book with nothing left to tally asks for the signature "
               "waiting behind this door";
    }
}

// A joiner gets the pairing, the rules and its own book — and loses the
// rows it could never play. The cut is at FETCH, not at the click.
TEST_F(ModesBookTest, joiner_camp_cuts_the_roll_and_the_sign)
{
    const DerivedBook book = derive_book();
    complete_all(book);  // even at 39/39 a joiner is offered no signature
    install_providers([] { return false; });

    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    const std::vector<CampaignZoneSession::Row>& rows = camp_rows(zone);
    ASSERT_EQ(kCampJoinerRows, rows.size());
    EXPECT_EQ("games", rows[kCampGameRow].id);
    EXPECT_EQ(book.tag_of.at(save_.scen_num), rows[kCampFieldRow].id);
    const CampaignZoneSession::Row& setup = rows[kCampJoinerSetupRow];
    EXPECT_EQ("setup", setup.id);
    EXPECT_EQ("MATCH SETUP", setup.label);
    EXPECT_EQ("map", setup.note)
        << "the match rules are SYNCED: a joiner reads the same digest it "
           "cannot write, and the page behind the row says whose call it "
           "is";
    for (const CampaignZoneSession::Row& row : rows)
    {
        EXPECT_NE("random_scenario", row.id)
            << "a joiner cannot play the roll — cut at fetch";
        EXPECT_NE("sign", row.id);
    }
    EXPECT_EQ(expected_stamp_note(book.bands.at(book.tag_of.at(save_.scen_num)),
                                  save_),
              rows[kCampGameRow].note)
        << "a joiner is never sent to a signature it will not be offered";

    // One line, spending the row unit the host leaves free: who calls the
    // game.
    ASSERT_EQ(1u, zone.texts().size());
    ASSERT_EQ(1u, zone.texts()[0].lines.size());
    EXPECT_EQ("The host calls the game.", zone.texts()[0].lines[0]);

    // Its own book, and a roster it may still shape.
    EXPECT_EQ("39/39", zone.readout()->items[0].value);
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
    ASSERT_EQ(first.readout()->items.size(), second.readout()->items.size());
    for (std::size_t i = 0; i < first.readout()->items.size(); i++)
    {
        EXPECT_EQ(first.readout()->items[i].label,
                  second.readout()->items[i].label);
        EXPECT_EQ(first.readout()->items[i].value,
                  second.readout()->items[i].value);
    }
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
    EXPECT_EQ(0, state("book_signed")) << "a fetch never writes";
    EXPECT_EQ(0, rolls) << "a fetch never rolls — the roll lives in the "
                           "action, where the click is";
}

// ---------------------------------------------------------------------------
// The games index
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, games_index_lists_the_seven_games_with_stamp_tallies)
{
    const DerivedBook book = derive_book();
    const std::vector<int>& ctf = book.bands.at("ctf");
    ASSERT_GE(ctf.size(), 3u);
    for (int i = 0; i < 3; i++)
        save_.add_level_completed("modes", ctf[static_cast<std::size_t>(i)]);

    const CampaignPickerSession::DecoratedPage page = open_page("games");
    EXPECT_EQ("SEVEN GAMES", page.title);
    ASSERT_EQ(2u, page.lines.size());
    EXPECT_EQ("Every game keeps its own page.", page.lines[0]);
    EXPECT_EQ("Stamped: 3 of 39.", page.lines[1]);

    ASSERT_EQ(kModeCount, page.rows.size());
    for (std::size_t i = 0; i < kModeCount; i++)
    {
        const CampaignPickerSession::Row& row = page.rows[i];
        EXPECT_EQ(CampaignPickerSession::Kind::Page, row.kind);
        EXPECT_EQ(kBookModes[i].tag, row.id);
        EXPECT_EQ(kBookModes[i].title, row.label);
        EXPECT_EQ(expected_stamp_note(book.bands.at(kBookModes[i].tag), save_),
                  row.note);
    }
}

// The obligation the camp migration must not drop: a page FETCH renders the
// same bytes twice and writes nothing. The pages read state (the tallies,
// the signature, the match rules); only actions may write it.
TEST_F(ModesBookTest, picker_menu_is_pure_and_render_stable)
{
    std::vector<std::string> pages = {"games", "setup"};
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
    EXPECT_EQ(0, state("book_signed")) << "a page fetch never writes";
}

// ---------------------------------------------------------------------------
// The field pages
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, field_pages_match_the_manifest_bands)
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
        ASSERT_EQ(band.size(), page.rows.size()) << kBookModes[i].tag;
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

        ASSERT_EQ(2u, page.lines.size());
        EXPECT_EQ(kBookModes[i].flavor, page.lines[0]);
        const int call = expected_call(band, save_);
        ASSERT_GE(call, 0) << "nothing completed: every band has a call";
        EXPECT_EQ("The book calls: " + book.stripped.at(call) + ".",
                  page.lines[1]);

        // A row select carries the SET LEVEL consequence, save untouched.
        const CampaignPickerSession::Outcome pick = session.choose(0);
        EXPECT_EQ(CampaignPickerSession::OutcomeKind::SetLevel, pick.kind);
        EXPECT_EQ(band[0], pick.level);
        EXPECT_EQ(300, save_.scen_num);
    }
}

// CTF is the one game whose note states a clock, and the clock is the one
// fact the TIME LIMIT knob overrides (#241). The note must promise what the
// field will actually run: the row's own value while the knob is MAP, and
// the knob's minutes the moment it is turned. A note still advertising the
// manifest after an override is the same dishonesty this issue set out to
// close — and it is the only place a player reads the clock before the
// match, since rules_line/rules_digest deliberately stop at three knobs.
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
        << "the field page's row promises the resolved clock";

    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    EXPECT_EQ(overridden, camp_rows(zone)[kCampFieldRow].note)
        << "so does the camp docket's FIELD row";

    // And the fields of every OTHER game are untouched — the knob names a
    // clock, and only CTF's note states one.
    save_.scen_num = 300;
    const std::vector<int>& tdm = book.bands.at("tdm");
    EXPECT_EQ(expected_note("tdm", manifest.at(tdm[0])),
              open_page("tdm").rows[0].note);

    save_.time_limit = 0;
}

TEST_F(ModesBookTest, call_line_scans_forward_and_wraps)
{
    const DerivedBook book = derive_book();
    const std::vector<int>& ctf = book.bands.at("ctf");
    ASSERT_GE(ctf.size(), 3u);
    const int last = ctf.back();

    // The post-band dangling cursor (winning the last CTF arena parks
    // scen_num one past it): the call wraps to the band start.
    save_.scen_num = static_cast<short>(last + 1);
    {
        hooks::CampaignPage page;
        ASSERT_TRUE(hooks::campaign_picker_page("ctf", page));
        const int call = expected_call(ctf, save_);
        EXPECT_EQ(ctf.front(), call) << "the wraparound arm";
        ASSERT_EQ(2u, page.lines.size());
        EXPECT_EQ("The book calls: " + book.stripped.at(call) + ".",
                  page.lines[1]);
    }

    // Mid-band cursor with stamped fields ahead: the scan skips them.
    save_.scen_num = static_cast<short>(ctf[1]);
    save_.add_level_completed("modes", ctf[1]);
    {
        hooks::CampaignPage page;
        ASSERT_TRUE(hooks::campaign_picker_page("ctf", page));
        const int call = expected_call(ctf, save_);
        EXPECT_EQ(ctf[2], call) << "the forward-scan arm skips the stamp";
        EXPECT_EQ("The book calls: " + book.stripped.at(call) + ".",
                  page.lines[1]);
    }

    // Every field stamped: the line says exactly that, and NOT that the
    // page is shut — #207 keeps every cleared field replayable, and all
    // ten rows under this line are still live level rows.
    for (const int id : ctf)
        save_.add_level_completed("modes", id);
    {
        hooks::CampaignPage page;
        ASSERT_TRUE(hooks::campaign_picker_page("ctf", page));
        EXPECT_EQ("Every field here is stamped.", page.lines[1]);
        ASSERT_EQ(ctf.size(), page.entries.size());
        for (const hooks::CampaignPageEntry& entry : page.entries)
        {
            EXPECT_EQ(hooks::CampaignPageEntry::Kind::Level, entry.kind)
                << "a stamped field still plays";
        }
    }
}

// The scripted labels fall back honestly when a title misbehaves: an
// empty title reads as the field number, a prefix-less one as itself.
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
            return "AN UNPREFIXED FIELD";
        return real_title(id);
    };
    hooks::install_campaign_providers(std::move(providers));

    hooks::CampaignPage page;
    ASSERT_TRUE(hooks::campaign_picker_page("tdm", page));
    ASSERT_EQ(tdm.size(), page.entries.size());
    EXPECT_EQ("FIELD " + std::to_string(blank_id), page.entries[0].label);
    EXPECT_EQ("AN UNPREFIXED FIELD", page.entries[1].label);
}

// ---------------------------------------------------------------------------
// RANDOM SCENARIO: the roll (D3 — TONIGHT'S CARD retired)
// ---------------------------------------------------------------------------

// Every provider answer maps to the manifest row it names — and never to
// the field the table is already set to (the click that changes nothing
// steps one row on). The outcome CARRIES the level; the session itself
// never writes the cursor — the routing belongs to each client's gated
// tail, which is pinned by the terminal tests above and the SDL zone test
// in test_campaign_zone_ui.cpp.
TEST_F(ModesBookTest, roll_answers_every_arena_and_never_the_current_field)
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
        CampaignZoneSession zone(save_);
        zone.fetch();
        ASSERT_TRUE(zone.scripted());
        const CampaignZoneSession::Outcome outcome =
            zone.act(0, static_cast<int>(kCampRandomRow));
        ASSERT_EQ(CampaignZoneSession::OutcomeKind::Acted, outcome.kind)
            << "pick " << pick;
        const int expected = expected_roll(book, pick, pair);
        EXPECT_EQ(expected, outcome.level) << "pick " << pick;
        EXPECT_NE(pair, outcome.level)
            << "pick " << pick << ": the roll never answers the field the "
                                  "table is set to";
        EXPECT_EQ(pair, save_.scen_num)
            << "the session never writes the cursor — the caller's gated "
               "tail does";
        EXPECT_EQ("", zone.take_message())
            << "the roll speaks through the engine's set toast, not a "
               "message of its own";
        rolled.insert(outcome.level);
    }
    EXPECT_EQ(static_cast<std::size_t>(kArenaCount) - 1, rolled.size())
        << "the 39 picks reach every arena but the current field";
    EXPECT_FALSE(rolled.contains(pair));
}

// The step-on wraps: a roll that lands on the LAST manifest row while the
// table is set to it answers the FIRST row, not one past the end.
TEST_F(ModesBookTest, roll_on_the_last_field_wraps_to_the_first)
{
    const DerivedBook book = derive_book();
    save_.scen_num = static_cast<short>(book.ordered.back());
    install_providers_with_pick(kArenaCount);

    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    const CampaignZoneSession::Outcome outcome =
        zone.act(0, static_cast<int>(kCampRandomRow));
    ASSERT_EQ(CampaignZoneSession::OutcomeKind::Acted, outcome.kind);
    EXPECT_EQ(book.ordered.front(), outcome.level) << "the wrap arm";
}

// The SHIPPED provider (make_campaign_providers' default): wall-clock
// seeded, so the tests pin its RANGE contract — 1..n for every n, and the
// defensive floor under a malformed ask — never a sequence.
TEST_F(ModesBookTest, default_random_pick_answers_inside_the_range)
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
}

// ---------------------------------------------------------------------------
// SIGN THE BOOK
// ---------------------------------------------------------------------------

// The signature lives on the index page whose COVER it takes, one door
// from the camp: this is the page that visibly changes when the name
// lands, and the camp's docket stays the same four rows in every state.
TEST_F(ModesBookTest, sign_the_book_materializes_latches_and_retitles)
{
    const DerivedBook book = derive_book();
    save_.team_list[0] = make_member(0, "Valkyrie", 4, 1234);
    save_.team_size = 1;
    complete_all(book);

    // The camp points at the door and grows no row of its own.
    {
        CampaignZoneSession zone(save_);
        zone.fetch();
        ASSERT_TRUE(zone.scripted());
        EXPECT_EQ("39/39", zone.readout()->items[0].value);
        EXPECT_TRUE(zone.texts().empty());
        ASSERT_EQ(kCampHostRows, camp_rows(zone).size());
        EXPECT_EQ("sign the book", camp_rows(zone)[kCampGameRow].note);
        for (const CampaignZoneSession::Row& row : camp_rows(zone))
            EXPECT_NE("sign", row.id) << "the table gives the signature no "
                                         "row of its own";
    }

    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open_at("games"));
    EXPECT_EQ("SEVEN GAMES", session.page().title);
    ASSERT_EQ(3u, session.page().lines.size());
    EXPECT_EQ("The book is full. Sign it.", session.page().lines[2]);
    ASSERT_EQ(kModeCount + 1, session.page().rows.size());
    const CampaignPickerSession::Row& sign = session.page().rows[kModeCount];
    EXPECT_EQ(CampaignPickerSession::Kind::Action, sign.kind);
    EXPECT_EQ("sign", sign.id);
    EXPECT_EQ("SIGN THE BOOK", sign.label);
    EXPECT_EQ("your name, for good", sign.note)
        << "the permanence is stated before the click";
    EXPECT_EQ(0, sign.cost) << "the signature is free";

    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(static_cast<int>(kModeCount)).kind);
    EXPECT_EQ("Your name goes in the book.", session.take_message());
    EXPECT_EQ(1, state("book_signed"));
    EXPECT_EQ("THE BOOK OF VALKYRIE", session.page().title)
        << "the cover takes the name the moment it is signed";
    EXPECT_EQ(kModeCount, session.page().rows.size())
        << "the signature row is spent";
    EXPECT_EQ(2u, session.page().lines.size());

    // And it keeps it: a signed book is titled for good, and the camp goes
    // back to tallying.
    EXPECT_EQ("THE BOOK OF VALKYRIE", open_page("games").title);
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(kCampHostRows, camp_rows(zone).size());
    EXPECT_EQ(expected_stamp_note(book.bands.at("tdm"), save_),
              camp_rows(zone)[kCampGameRow].note)
        << "nothing left to ask for";
}

TEST_F(ModesBookTest, signed_book_with_no_roster_keeps_the_old_title)
{
    const DerivedBook book = derive_book();
    complete_all(book);
    ASSERT_TRUE(save_.campaign_state_set("modes", "book_signed", 1));

    const CampaignPickerSession::DecoratedPage page = open_page("games");
    EXPECT_EQ("SEVEN GAMES", page.title)
        << "an empty roster leaves nobody to sign as";
    EXPECT_EQ(kModeCount, page.rows.size())
        << "a signed book offers no signature";
    ASSERT_EQ(2u, page.lines.size());

    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(kCampHostRows, camp_rows(zone).size());
    EXPECT_TRUE(zone.texts().empty())
        << "the cover line lives on the cover, not on the table";
}

// A joiner is never offered the signature — on the camp OR behind the
// door. The book is local, but the row that writes it is the host's.
TEST_F(ModesBookTest, joiner_index_offers_no_signature)
{
    const DerivedBook book = derive_book();
    complete_all(book);
    install_providers([] { return false; });

    const CampaignPickerSession::DecoratedPage page = open_page("games");
    EXPECT_EQ(kModeCount, page.rows.size());
    ASSERT_EQ(2u, page.lines.size()) << "no signature, no prompt for one";
    for (const CampaignPickerSession::Row& row : page.rows)
        EXPECT_NE("sign", row.id);
}

// ---------------------------------------------------------------------------
// MATCH SETUP (#212)
// ---------------------------------------------------------------------------

// The page is the knob table, and every row wears the value it holds at
// FETCH: the label is the state, so a player never has to guess what the
// match is set to before touching it.
TEST_F(ModesBookTest, match_setup_labels_every_knob_with_what_it_holds)
{
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open_at("setup"));
    const CampaignPickerSession::DecoratedPage& page = session.page();
    EXPECT_EQ("MATCH SETUP", page.title);
    ASSERT_EQ(1u, page.lines.size());
    EXPECT_EQ("Map score.", page.lines[0])
        << "0 spells its MATCHUP meaning, and nothing is 'posted'; the "
           "sides are LINEUP's, so the sentence opens on the score";
    ASSERT_EQ(kKnobCount, page.rows.size());
    const char* labels[] = {"TARGET SCORE: MAP", "TIME LIMIT: MAP"};
    const char* notes[] = {"map, 1, 3, 5, 10", "map, 5, 10, 15, 20m"};
    for (std::size_t i = 0; i < kKnobCount; i++)
    {
        EXPECT_EQ(CampaignPickerSession::Kind::Action, page.rows[i].kind);
        EXPECT_EQ(kKnobIds[i], page.rows[i].id);
        EXPECT_EQ(labels[i], page.rows[i].label);
        EXPECT_EQ(notes[i], page.rows[i].note)
            << "the note is the cycle, so the next click is never a "
               "surprise";
        EXPECT_EQ(0, page.rows[i].cost) << "turning a knob is free";
        EXPECT_TRUE(page.rows[i].affordable);
    }

    // An off-menu value (a match settled from the MATCHUP screen or a
    // lobby) spells itself on the row and in the line rather than lying.
    save_.ctf_strip_scenario_troops = 1;
    save_.ctf_capture_limit = 7;
    save_.time_limit = 2160;  // 3 minutes, off the cycle
    session.refresh();
    EXPECT_EQ("To 7.", session.page().lines[0])
        << "the line speaks the score, as the camp digest that shares its "
           "words does; the clock stays on its own row";
    EXPECT_EQ("TARGET SCORE: 7", session.page().rows[kScoreRow].label);
    EXPECT_EQ("TIME LIMIT: 3M", session.page().rows[kTimeRow].label)
        << "minutes, from the ticks the modes actually run on";

    // And the fetch that reads them writes nothing: the page is a mirror
    // until a row is clicked.
    (void)og::data::consume_match_settings_dirty();
    session.refresh();
    EXPECT_EQ(0, save_.ctf_team_count);
    EXPECT_EQ(7, save_.ctf_capture_limit);
    EXPECT_EQ(1, save_.ctf_strip_scenario_troops);
    EXPECT_EQ(2160, save_.time_limit);
    EXPECT_FALSE(og::data::consume_match_settings_dirty())
        << "a fetch never writes a knob";
}

// The whole of every cycle, one click at a time and round the wrap: the
// value the save holds, the label the refetched row wears, and the plain
// sentence the click answers with.
TEST_F(ModesBookTest, every_knob_cycles_through_its_values_and_wraps)
{
    struct Step {
        int value;
        const char* label;
        const char* said;
    };
    const std::vector<Step> score = {
        {1, "TARGET SCORE: 1", "Score to 1."},
        {3, "TARGET SCORE: 3", "Score to 3."},
        {5, "TARGET SCORE: 5", "Score to 5."},
        {10, "TARGET SCORE: 10", "Score to 10."},
        {0, "TARGET SCORE: MAP", "Score: the map's own."},
    };
    // Ticks in the save (12/s, the manifest's own unit), minutes on the
    // face. Every value on the cycle survives the provider's [720, 21600]
    // clamp untouched, so what the row says is what the sim gets.
    const std::vector<Step> time = {
        {3600, "TIME LIMIT: 5M", "Clock: 5 minutes."},
        {7200, "TIME LIMIT: 10M", "Clock: 10 minutes."},
        {10800, "TIME LIMIT: 15M", "Clock: 15 minutes."},
        {14400, "TIME LIMIT: 20M", "Clock: 20 minutes."},
        {0, "TIME LIMIT: MAP", "Clock: the map's own."},
    };
    const struct {
        std::size_t row;
        const char* setting;
        const std::vector<Step>* steps;
    } knobs[] = {
        {kScoreRow, "score_limit", &score},
        {kTimeRow, "time_limit", &time},
    };

    for (const auto& knob : knobs)
    {
        CampaignPickerSession session(save_);
        ASSERT_TRUE(session.open_at("setup"));
        for (const Step& step : *knob.steps)
        {
            ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
                      session.choose(knob.row).kind)
                << knob.setting << " -> " << step.value;
            EXPECT_EQ(step.said, session.take_message()) << knob.setting;
            // The write landed on the knob the row names, read back
            // through the same seam the script reads.
            const hooks::CampaignProviders readback =
                og::data::make_campaign_providers(save_);
            EXPECT_EQ(step.value, readback.match_get(knob.setting));
            EXPECT_EQ(step.label,
                      session.page().rows[knob.row].label)
                << "the refetched row wears the new value";
        }
        // A whole lap leaves the knob exactly where it started, and the
        // other never moved (nor the two retired fields, which no row can
        // reach: TEAMS since A3, TROOPS since B5).
        EXPECT_EQ(0, save_.ctf_team_count);
        EXPECT_EQ(0, save_.ctf_capture_limit);
        EXPECT_EQ(0, save_.ctf_strip_scenario_troops);
        EXPECT_EQ(0, save_.time_limit);
    }
}

// A value that is not on the cycle at all — every knob can be set from the
// MATCHUP screen, a lobby or an older save — rejoins at the head instead of
// pretending to know where it was.
TEST_F(ModesBookTest, an_off_menu_value_rejoins_the_cycle_at_its_head)
{
    save_.ctf_capture_limit = 7;
    save_.ctf_strip_scenario_troops = 1;  // the retired middle state
    save_.time_limit = 5400;    // a 7:30 court, between two cycle stops

    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open_at("setup"));
    EXPECT_EQ("TARGET SCORE: 7", session.page().rows[kScoreRow].label);

    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(kScoreRow).kind);
    EXPECT_EQ("Score: the map's own.", session.take_message());
    EXPECT_EQ(0, save_.ctf_capture_limit);

    EXPECT_EQ("TIME LIMIT: 7M", session.page().rows[kTimeRow].label)
        << "5400 ticks is 7 whole minutes, said plainly";
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(kTimeRow).kind);
    EXPECT_EQ("Clock: the map's own.", session.take_message());
    EXPECT_EQ(0, save_.time_limit);
}

TEST_F(ModesBookTest, turning_a_knob_arms_the_dirty_flag_and_the_camp_follows)
{
    (void)og::data::consume_match_settings_dirty();
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open_at("setup"));

    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(kScoreRow).kind);
    EXPECT_EQ(1, save_.ctf_capture_limit);
    EXPECT_TRUE(og::data::consume_match_settings_dirty())
        << "a successful write arms the session tail";
    EXPECT_FALSE(og::data::consume_match_settings_dirty())
        << "the flag is consumed";
    EXPECT_EQ("To 1.", session.page().lines[0])
        << "the refetched page re-derives the rules";

    // Two more clicks put the score on 5: the knobs are independent.
    for (int i = 0; i < 2; i++)
    {
        ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
                  session.choose(kScoreRow).kind);
    }
    // And one puts the clock on five minutes — which the summaries
    // deliberately do NOT speak: the line and the camp digest speak the
    // score (#241), so the TIME LIMIT row is the only place its value
    // shows.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(kTimeRow).kind);
    EXPECT_EQ(5, save_.ctf_capture_limit);
    EXPECT_EQ(3600, save_.time_limit);
    EXPECT_EQ(0, save_.ctf_team_count) << "no row reaches the retired knob";
    EXPECT_EQ(0, save_.ctf_strip_scenario_troops)
        << "and none reaches the other one either (B5)";
    EXPECT_EQ("To 5.", session.page().lines[0]);
    EXPECT_EQ("TIME LIMIT: 5M", session.page().rows[kTimeRow].label);
    EXPECT_TRUE(og::data::consume_match_settings_dirty());

    // The camp's own row carries the same rules at note length — the whole
    // state, on the row that changes it, with no line of its own.
    CampaignZoneSession zone(save_);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    EXPECT_EQ("to 5", camp_rows(zone)[kCampSetupRow].note);
    EXPECT_TRUE(zone.texts().empty());
}

// A joiner's MATCH SETUP is a READING: the knob rows are cut at fetch and
// the page states whose call it is instead. The host-hand refusal stays
// behind them as the backstop — page-agnostic action dispatch means a stale
// click still cannot write.
TEST_F(ModesBookTest, joiner_setup_page_reads_and_refuses_without_writing)
{
    install_providers([] { return false; });
    (void)og::data::consume_match_settings_dirty();

    const CampaignPickerSession::DecoratedPage page = open_page("setup");
    EXPECT_TRUE(page.rows.empty()) << "no row that could only refuse";
    ASSERT_EQ(2u, page.lines.size());
    EXPECT_EQ("Map score.", page.lines[0]);
    EXPECT_EQ("The host calls the rules.", page.lines[1])
        << "who decides, stated before any click";

    for (const char* id : kKnobIds)
    {
        hooks::CampaignActionResult result;
        ASSERT_TRUE(hooks::campaign_picker_action(id, result)) << id;
        EXPECT_TRUE(result.ok);
        EXPECT_EQ("The host calls the rules.", result.message) << id;
    }
    EXPECT_EQ(0, save_.ctf_team_count) << "no knob moved";
    EXPECT_EQ(0, save_.ctf_capture_limit);
    EXPECT_EQ(0, save_.ctf_strip_scenario_troops);
    EXPECT_EQ(0, save_.time_limit);
    EXPECT_FALSE(og::data::consume_match_settings_dirty())
        << "a refusal never arms the session tail";
}

// ---------------------------------------------------------------------------
// Budgets: the table has to fit the faces on every client
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, every_page_and_the_camp_fit_their_budgets)
{
    std::vector<std::string> pages = {"games", "setup"};
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
            EXPECT_LE(entry.note.size(), kNoteBudget)
                << id << ": " << entry.note;
        }
    }

    // The camp itself, over EVERY arena the campaign ships as the pairing —
    // no exemptions. The FIELD row carries a generated arena title, and it
    // is exactly the row whose overflow eats the grammar (the door marker
    // off its tail). A regenerated title that no longer fits has to fail
    // HERE, where the note that must give way is one line above, rather
    // than quietly ellipsing on the panel.
    //
    // Both ends of the rules digest are swept with it: the defaults spell
    // the longest SENTENCE ("map score"), a set score the longest
    // NOTE ("to 50").
    const DerivedBook book = derive_book();
    save_.ctf_strip_scenario_troops = 3;
    const short limits[] = {0, 50};
    for (const short limit : limits)
    {
        save_.ctf_capture_limit = limit;
        for (const int id : book.ordered)
        {
            save_.scen_num = static_cast<short>(id);
            // Both stamp faces: an unplayed book and a fully stamped one
            // (the GAME row's tally and the sign-the-book note both move).
            for (const bool stamped : {false, true})
            {
                if (stamped)
                    complete_all(book);
                CampaignZoneSession zone(save_);
                zone.fetch();
                ASSERT_TRUE(zone.scripted());
                ASSERT_EQ(kCampHostRows, camp_rows(zone).size());
                for (const CampaignZoneSession::TextLayout& text :
                     zone.texts())
                {
                    for (const std::string& line : text.lines)
                        EXPECT_LE(line.size(), kLineBudget) << "camp: " << line;
                }
                for (const CampaignZoneSession::Row& row : camp_rows(zone))
                {
                    EXPECT_FALSE(row.label.empty()) << "camp row " << row.id;
                    EXPECT_LE(row.label.size(), kLabelBudget)
                        << "camp: " << row.label;
                    EXPECT_LE(row.note.size(), kNoteBudget)
                        << "camp: " << row.note;
                    const std::string composed =
                        og::ui::campaign_picker_row_text(row,
                                                         kSdlRowFaceChars);
                    EXPECT_EQ(composed,
                              og::ui::campaign_picker_row_text(
                                  row, kSdlRowFaceChars * 4))
                        << "camp row clipped on the SDL face at scen" << id
                        << ": " << composed;
                }
            }
            save_.completed_levels.clear();
        }
    }
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
