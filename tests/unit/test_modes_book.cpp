/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// THE GAMESMASTER'S BOOK (issues #206/#212): the Multiplayer Modes
// campaign's scripted picker — campaigns/modes/packs/modes.core/scripts/
// campaign_picker.lua — driven through the real CampaignPickerSession over
// og::data::make_campaign_providers, with the SHIPPED campaign archive
// mounted (the test_imaginations_dream_log pattern). Expectations derive
// at runtime from the campaign's own data — the scen titles in the mounted
// archive and the generated lib/mode_levels.lua manifest — never from a
// pinned list of 39 strings, so a modes_mapgen regeneration moves both
// sides together.

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
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

std::string get_asset_path();

namespace {

namespace hooks = og::script::hooks;
using og::ui::CampaignPickerSession;

// The seven games in campaign.yaml description order — the one ordering
// pin the book carries (mode tags, page titles, flavor lines and the
// card page's long names; never the 39 arena strings).
struct BookMode {
    const char* tag;
    const char* prefix;  // the scen-title prefix that marks the band
    const char* title;
    const char* flavor;
    const char* long_name;
};

constexpr BookMode kBookModes[] = {
    {"tdm", "Team Deathmatch: ", "TEAM DEATHMATCH",
     "Four colors go in. One walks out.", "team deathmatch"},
    {"ctf", "CTF: ", "CAPTURE THE FLAG",
     "Steal theirs. Keep yours. Simple.", "capture the flag"},
    {"onslaught", "Onslaught: ", "ONSLAUGHT",
     "Hold the line until there is no line.", "onslaught"},
    {"mutant", "Mutant: ", "MUTANT",
     "New shape every kill. Keep count.", "mutant"},
    {"soccer", "Soccer: ", "SOCCER",
     "No hands. Blades are fine.", "soccer"},
    {"basketball", "Basketball: ", "BASKETBALL",
     "The arc is chalk. The rim is law.", "basketball"},
    {"ffa", "FFA: ", "FREE FOR ALL",
     "No teams. No excuses. No refunds.", "free for all"},
};
constexpr std::size_t kModeCount = sizeof(kBookModes) / sizeof(kBookModes[0]);

// Root page geometry: 7 mode rows, then TONIGHT'S CARD, then MATCH SETUP,
// then (only at 39/39, unsigned) SIGN THE BOOK.
constexpr std::size_t kRootCardIndex = kModeCount;
constexpr std::size_t kRootSetupIndex = kModeCount + 1;
constexpr std::size_t kRootRowsUnsigned = kModeCount + 2;
constexpr std::size_t kRootSignIndex = kModeCount + 2;

// Deck constants from the accepted design: 39 cards, stride 7 (coprime).
constexpr int kDeckSize = 39;
constexpr int kDeckStride = 7;

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
    std::vector<int> ordered;                    // every arena id, ascending
    std::map<std::string, std::vector<int>> bands;  // tag -> ids ascending
    std::map<int, std::string> stripped;         // id -> prefix-cut title
    std::map<int, std::string> tag_of;           // id -> mode tag
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

// The manifest facts each row's note posts, parsed from the generated
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
// of the script's mode_note, over the same manifest facts.
std::string expected_note(const std::string& tag, const ManifestRow& row)
{
    if (tag == "tdm")
        return std::format("{} teams, to {}", row.teams, row.score_limit);
    if (tag == "ctf")
        return std::format("{} sides, {} min", row.teams,
                           row.time_limit / 720);
    if (tag == "onslaught")
        return std::format("{} sides, {} lives", row.teams, row.cap_team0);
    if (tag == "mutant")
        return std::format("{} shifters, to {}", row.fighters,
                           row.score_limit);
    if (tag == "soccer")
        return std::format("{} sides, first to {}", row.teams,
                           row.score_limit);
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

// The roster-salt twin: sum of every hero's level, exp and name bytes,
// folded once at the end into 1..39.
int expected_cut(const SaveData& save)
{
    long long salt = 0;
    for (const auto& member : save.team_list)
    {
        if (member == nullptr)
            continue;
        salt += member->level;
        salt += static_cast<int>(member->exp);
        for (const char c : member->name)
            salt += static_cast<unsigned char>(c);
    }
    return static_cast<int>(salt % kDeckSize) + 1;
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

    CampaignPickerSession::DecoratedPage open_root()
    {
        CampaignPickerSession session(save_);
        EXPECT_TRUE(session.open()) << "the book must open";
        return session.page();
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

    SaveData save_;
    std::string previous_mount_;
    GameplayContext* previous_game_ = nullptr;
};

}  // namespace

// ---------------------------------------------------------------------------
// Registration and clean load
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, pack_registers_one_book_with_no_vars)
{
    EXPECT_TRUE(hooks::campaign_picker_registered())
        << "modes.core must register the book";
    EXPECT_TRUE(hooks::campaign_registered_vars().empty())
        << "the book registers no campaign vars";

    // Every page the book serves parses clean.
    hooks::CampaignPage page;
    EXPECT_TRUE(hooks::campaign_picker_page("", page));
    for (const BookMode& mode : kBookModes)
        EXPECT_TRUE(hooks::campaign_picker_page(mode.tag, page))
            << mode.tag;
    EXPECT_TRUE(hooks::campaign_picker_page("card", page));
    EXPECT_TRUE(hooks::campaign_picker_page("setup", page));

    const std::vector<og::script::ScriptError>& errors =
        og::script::active_world_scripts().host().errors();
    for (const og::script::ScriptError& error : errors)
        ADD_FAILURE() << "script error at " << error.where << ": "
                      << error.message;
}

// A page id the book never lists answers "no page" (the engine's
// malformed-page guard keeps the stock UI reachable); an entry id no page
// carries is a served no-op with no toast.
TEST_F(ModesBookTest, unknown_page_and_action_are_guarded)
{
    hooks::CampaignPage page;
    EXPECT_FALSE(hooks::campaign_picker_page("neverwhere", page));
    hooks::CampaignActionResult result;
    EXPECT_TRUE(hooks::campaign_picker_action("neverwhere", result))
        << "the book serves actions, so the hook dispatches";
    EXPECT_TRUE(result.ok);
    EXPECT_EQ("", result.message);
}

// ---------------------------------------------------------------------------
// The root page
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, root_lists_the_seven_games_with_live_tallies)
{
    const DerivedBook book = derive_book();
    ASSERT_EQ(static_cast<std::size_t>(kDeckSize), book.ordered.size())
        << "the campaign ships 39 arenas";
    ASSERT_EQ(kModeCount, book.bands.size());

    const CampaignPickerSession::DecoratedPage page = open_root();
    EXPECT_EQ("THE GAMESMASTER'S BOOK", page.title);
    ASSERT_EQ(3u, page.lines.size());
    EXPECT_EQ("The Gamesmaster opens the book.", page.lines[0]);
    EXPECT_EQ("Seven games. Thirty-nine fields.", page.lines[1]);
    EXPECT_EQ("Stamped: 0 of 39.", page.lines[2]);

    ASSERT_EQ(kRootRowsUnsigned, page.rows.size())
        << "seven games + the card + the posted rules; no signature yet";
    for (std::size_t i = 0; i < kModeCount; i++)
    {
        const CampaignPickerSession::Row& row = page.rows[i];
        EXPECT_EQ(CampaignPickerSession::Kind::Page, row.kind);
        EXPECT_EQ(kBookModes[i].tag, row.id);
        EXPECT_EQ(kBookModes[i].title, row.label);
        const std::vector<int>& band = book.bands.at(kBookModes[i].tag);
        EXPECT_EQ(std::format("{} arenas, 0 won", band.size()), row.note);
    }
    const CampaignPickerSession::Row& card = page.rows[kRootCardIndex];
    EXPECT_EQ(CampaignPickerSession::Kind::Page, card.kind);
    EXPECT_EQ("card", card.id);
    EXPECT_EQ("TONIGHT'S CARD", card.label);
    // Pre-latch draw: cut 1, seq 0 -> the first ordered arena.
    EXPECT_EQ(book.stripped.at(book.ordered[0]), card.note);
    const CampaignPickerSession::Row& setup = page.rows[kRootSetupIndex];
    EXPECT_EQ(CampaignPickerSession::Kind::Page, setup.kind);
    EXPECT_EQ("setup", setup.id);
    EXPECT_EQ("MATCH SETUP", setup.label);
    EXPECT_EQ("the posted rules", setup.note);
}

TEST_F(ModesBookTest, root_tallies_recount_from_the_save)
{
    const DerivedBook book = derive_book();
    const std::vector<int>& ctf = book.bands.at("ctf");
    ASSERT_GE(ctf.size(), 3u);
    for (int i = 0; i < 3; i++)
        save_.add_level_completed("modes", ctf[static_cast<std::size_t>(i)]);

    const CampaignPickerSession::DecoratedPage page = open_root();
    EXPECT_EQ("Stamped: 3 of 39.", page.lines[2]);
    EXPECT_EQ(std::format("{} arenas, 3 won", ctf.size()),
              page.rows[1].note)
        << "the CTF row tallies its own band";
    EXPECT_EQ(std::format("{} arenas, 0 won",
                          book.bands.at("tdm").size()),
              page.rows[0].note)
        << "other bands stay at zero";
}

// ---------------------------------------------------------------------------
// The mode pages
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, mode_pages_match_the_manifest_bands)
{
    const DerivedBook book = derive_book();
    const std::map<int, ManifestRow> manifest = parse_manifest();
    ASSERT_EQ(static_cast<std::size_t>(kDeckSize), manifest.size())
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
        CampaignPickerSession session(save_);
        ASSERT_TRUE(session.open());
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

    // Every field stamped: the page is shut.
    for (const int id : ctf)
        save_.add_level_completed("modes", id);
    {
        hooks::CampaignPage page;
        ASSERT_TRUE(hooks::campaign_picker_page("ctf", page));
        EXPECT_EQ("This page is stamped shut.", page.lines[1]);
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
// TONIGHT'S CARD: the deck
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, deck_deals_every_card_before_any_repeat)
{
    const DerivedBook book = derive_book();
    ASSERT_EQ(static_cast<std::size_t>(kDeckSize), book.ordered.size());

    std::set<int> dealt;
    for (int seq = 0; seq < kDeckSize; seq++)
    {
        ASSERT_TRUE(
            save_.campaign_state_set("modes", "card_seq", seq));
        hooks::CampaignPage page;
        ASSERT_TRUE(hooks::campaign_picker_page("card", page));
        EXPECT_EQ("TONIGHT'S CARD", page.title);
        ASSERT_EQ(2u, page.entries.size());
        const hooks::CampaignPageEntry& draw = page.entries[0];
        EXPECT_EQ(hooks::CampaignPageEntry::Kind::Level, draw.kind);
        // Pre-latch cut 1: idx = (0 + seq*7) mod 39 over the ordered walk.
        const int expected =
            book.ordered[static_cast<std::size_t>((seq * kDeckStride) %
                                                  kDeckSize)];
        EXPECT_EQ(expected, draw.level) << "seq " << seq;
        EXPECT_EQ(book.stripped.at(expected), draw.label);
        // The note speaks the game's long name.
        std::string long_name;
        for (const BookMode& mode : kBookModes)
        {
            if (book.tag_of.at(expected) == mode.tag)
                long_name = mode.long_name;
        }
        EXPECT_EQ(long_name, draw.note);
        const hooks::CampaignPageEntry& shuffle = page.entries[1];
        EXPECT_EQ(hooks::CampaignPageEntry::Kind::Action, shuffle.kind);
        EXPECT_EQ("shuffle", shuffle.id);
        EXPECT_EQ("SHUFFLE THE DECK", shuffle.label);
        dealt.insert(draw.level);
    }
    EXPECT_EQ(static_cast<std::size_t>(kDeckSize), dealt.size())
        << "stride 7 over 39 deals every arena before any repeat";
    EXPECT_EQ(0, state("deck_cut"))
        << "page fetches never latch the cut — picker_menu stays pure";
}

TEST_F(ModesBookTest, picker_menu_is_pure_and_render_stable)
{
    const CampaignPickerSession::DecoratedPage first = open_root();
    const CampaignPickerSession::DecoratedPage second = open_root();
    EXPECT_EQ(first.title, second.title);
    ASSERT_EQ(first.lines.size(), second.lines.size());
    for (std::size_t i = 0; i < first.lines.size(); i++)
        EXPECT_EQ(first.lines[i], second.lines[i]);
    ASSERT_EQ(first.rows.size(), second.rows.size());
    for (std::size_t i = 0; i < first.rows.size(); i++)
    {
        EXPECT_EQ(first.rows[i].id, second.rows[i].id);
        EXPECT_EQ(first.rows[i].label, second.rows[i].label);
        EXPECT_EQ(first.rows[i].note, second.rows[i].note);
    }
    EXPECT_EQ(0, state("card_seq"));
    EXPECT_EQ(0, state("deck_cut"));
    EXPECT_EQ(0, state("book_signed"));
}

TEST_F(ModesBookTest, shuffle_advances_and_latches_the_cut_exactly_once)
{
    const DerivedBook book = derive_book();
    save_.team_list[0] = make_member(0, "Valkyrie", 4, 1234);
    save_.team_list[1] = make_member(1, "Bo Vine", 2, 77);
    save_.team_size = 2;
    const int cut = expected_cut(save_);
    ASSERT_GE(cut, 1);
    ASSERT_LE(cut, kDeckSize);

    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(kRootCardIndex).kind);

    // First shuffle: the deal advances and the roster salt latches.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(1).kind);
    EXPECT_EQ("The Gamesmaster deals again.", session.take_message());
    EXPECT_EQ(1, state("card_seq"));
    EXPECT_EQ(cut, state("deck_cut")) << "the FIRST shuffle latches";
    const int expected_draw = book.ordered[static_cast<std::size_t>(
        ((cut - 1) + 1 * kDeckStride) % kDeckSize)];
    EXPECT_EQ(expected_draw, session.page().rows[0].level)
        << "the refetched card follows the latched formula";

    // Second shuffle: the deal advances, the cut holds.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(1).kind);
    EXPECT_EQ(2, state("card_seq"));
    EXPECT_EQ(cut, state("deck_cut")) << "latched exactly once";

    // The deal wraps mod 39.
    ASSERT_TRUE(save_.campaign_state_set("modes", "card_seq",
                                         kDeckSize - 1));
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(1).kind);
    EXPECT_EQ(0, state("card_seq")) << "seq wraps mod 39";
    EXPECT_EQ(cut, state("deck_cut"));
}

TEST_F(ModesBookTest, empty_roster_shuffle_cuts_one)
{
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(kRootCardIndex).kind);
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(1).kind);
    EXPECT_EQ(1, state("deck_cut")) << "salt 0 cuts 1 — the same deck as "
                                       "the pre-latch default";
}

// ---------------------------------------------------------------------------
// SIGN THE BOOK
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, sign_the_book_materializes_latches_and_retitles)
{
    const DerivedBook book = derive_book();
    save_.team_list[0] = make_member(0, "Valkyrie", 4, 1234);
    save_.team_size = 1;
    complete_all(book);

    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    {
        const CampaignPickerSession::DecoratedPage& page = session.page();
        EXPECT_EQ("THE GAMESMASTER'S BOOK", page.title);
        ASSERT_EQ(4u, page.lines.size()) << "the sign prompt joins at 39/39";
        EXPECT_EQ("Stamped: 39 of 39.", page.lines[2]);
        EXPECT_EQ("The book is full. Sign it.", page.lines[3]);
        ASSERT_EQ(kRootSignIndex + 1, page.rows.size());
        const CampaignPickerSession::Row& sign = page.rows[kRootSignIndex];
        EXPECT_EQ(CampaignPickerSession::Kind::Action, sign.kind);
        EXPECT_EQ("sign", sign.id);
        EXPECT_EQ("SIGN THE BOOK", sign.label);
        EXPECT_EQ(0, sign.cost) << "the signature is free";
    }

    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(kRootSignIndex).kind);
    EXPECT_EQ("Your name goes in the book.", session.take_message());
    EXPECT_EQ(1, state("book_signed"));
    {
        const CampaignPickerSession::DecoratedPage& page = session.page();
        EXPECT_EQ("THE BOOK OF VALKYRIE", page.title)
            << "the lead hero's name, uppercased, takes the cover";
        EXPECT_EQ(kRootRowsUnsigned, page.rows.size())
            << "the signature row is spent";
        EXPECT_EQ(3u, page.lines.size()) << "the sign prompt is spent too";
    }
}

TEST_F(ModesBookTest, signed_book_with_no_roster_keeps_the_old_title)
{
    const DerivedBook book = derive_book();
    complete_all(book);
    ASSERT_TRUE(save_.campaign_state_set("modes", "book_signed", 1));

    const CampaignPickerSession::DecoratedPage page = open_root();
    EXPECT_EQ("THE GAMESMASTER'S BOOK", page.title)
        << "an empty roster leaves nobody to sign as";
    EXPECT_EQ(kRootRowsUnsigned, page.rows.size());
}

// ---------------------------------------------------------------------------
// MATCH SETUP (#212)
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, match_setup_spells_the_posted_rules_honestly)
{
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(kRootSetupIndex).kind);
    const CampaignPickerSession::DecoratedPage& page = session.page();
    EXPECT_EQ("MATCH SETUP", page.title);
    ASSERT_EQ(2u, page.lines.size());
    EXPECT_EQ("Teams: Auto. Score: map.", page.lines[0])
        << "0 spells its MATCHUP meaning";
    EXPECT_EQ("Troops: all.", page.lines[1]);
    ASSERT_EQ(4u, page.rows.size());
    const char* labels[] = {"TWO SIDES, FAIR BOTS", "FOUR SIDES, FAIR BOTS",
                            "CLASSIC RULES", "SHORT MATCH"};
    const char* ids[] = {"two_sides", "four_sides", "classic_rules",
                         "short_match"};
    for (std::size_t i = 0; i < 4; i++)
    {
        EXPECT_EQ(CampaignPickerSession::Kind::Action, page.rows[i].kind);
        EXPECT_EQ(ids[i], page.rows[i].id);
        EXPECT_EQ(labels[i], page.rows[i].label);
        EXPECT_EQ(0, page.rows[i].cost) << "presets are free";
        EXPECT_TRUE(page.rows[i].affordable);
    }

    // An off-menu troops value spells itself rather than lying.
    save_.ctf_strip_scenario_troops = 1;
    session.refresh();
    EXPECT_EQ("Troops: 1.", session.page().lines[1]);
}

TEST_F(ModesBookTest, host_presets_write_the_knobs_and_arm_the_dirty_flag)
{
    (void)og::data::consume_match_settings_dirty();
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(kRootSetupIndex).kind);

    // TWO SIDES, FAIR BOTS.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(0).kind);
    EXPECT_EQ("The Gamesmaster posts it.", session.take_message());
    EXPECT_EQ(2, save_.ctf_team_count);
    EXPECT_EQ(3, save_.ctf_strip_scenario_troops);
    // The write is visible through the same seam the script reads.
    const hooks::CampaignProviders readback =
        og::data::make_campaign_providers(save_);
    EXPECT_EQ(2, readback.match_get("team_count"));
    EXPECT_EQ(3, readback.match_get("strip_troops"));
    EXPECT_TRUE(og::data::consume_match_settings_dirty())
        << "a successful preset arms the session tail";
    EXPECT_FALSE(og::data::consume_match_settings_dirty())
        << "the flag is consumed";
    EXPECT_EQ("Teams: 2. Score: map.", session.page().lines[0])
        << "the refetched page re-derives the posted rules";
    EXPECT_EQ("Troops: fair.", session.page().lines[1]);

    // SHORT MATCH stacks onto the posted teams.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(3).kind);
    EXPECT_EQ(5, save_.ctf_capture_limit);
    EXPECT_EQ("Teams: 2. Score: 5.", session.page().lines[0]);
    EXPECT_TRUE(og::data::consume_match_settings_dirty());

    // FOUR SIDES, FAIR BOTS.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(1).kind);
    EXPECT_EQ(4, save_.ctf_team_count);
    EXPECT_EQ(3, save_.ctf_strip_scenario_troops);

    // CLASSIC RULES clears all three knobs.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(2).kind);
    EXPECT_EQ(0, save_.ctf_team_count);
    EXPECT_EQ(0, save_.ctf_strip_scenario_troops);
    EXPECT_EQ(0, save_.ctf_capture_limit);
    EXPECT_EQ("Teams: Auto. Score: map.", session.page().lines[0]);
    EXPECT_EQ("Troops: all.", session.page().lines[1]);
    EXPECT_TRUE(og::data::consume_match_settings_dirty());
}

TEST_F(ModesBookTest, non_hosts_are_refused_without_writing)
{
    install_providers([] { return false; });
    (void)og::data::consume_match_settings_dirty();

    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              session.choose(kRootSetupIndex).kind);
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              session.choose(0).kind);
    EXPECT_EQ("The host posts the rules.", session.take_message());
    EXPECT_EQ(0, save_.ctf_team_count) << "no knob moved";
    EXPECT_EQ(0, save_.ctf_strip_scenario_troops);
    EXPECT_FALSE(og::data::consume_match_settings_dirty())
        << "a refusal never arms the session tail";
}

// ---------------------------------------------------------------------------
// Budgets: the book has to fit the faces on every client
// ---------------------------------------------------------------------------

TEST_F(ModesBookTest, every_page_fits_its_budgets)
{
    std::vector<std::string> pages = {"", "card", "setup"};
    for (const BookMode& mode : kBookModes)
        pages.push_back(mode.tag);

    // The decorated sweep goes through the session for the root; raw
    // hooks pages cover the rest (labels are script-authored throughout).
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
            EXPECT_FALSE(entry.label.empty())
                << id << " row " << entry.id;
            EXPECT_LE(entry.label.size(), kLabelBudget)
                << id << ": " << entry.label;
            EXPECT_LE(entry.note.size(), kNoteBudget)
                << id << ": " << entry.note;
        }
    }

    // Composed rows reach the SDL mission face uncut.
    const CampaignPickerSession::DecoratedPage root = open_root();
    for (const CampaignPickerSession::Row& row : root.rows)
    {
        const std::string composed =
            og::ui::campaign_picker_row_text(row, kSdlRowFaceChars);
        EXPECT_EQ(composed, og::ui::campaign_picker_row_text(
                                row, kSdlRowFaceChars * 4))
            << "root row clipped on the SDL face: " << composed;
    }
}
