// The staged VIEW LEVEL preview (issue #218): build_scenario_roster_report
// READS a staged world — the world mode_stage_init already assembled (the
// exact function MatchStage runs at stage time) — so the preview's
// activation, fills and squad facts are OBSERVATIONS of the world the
// launch adopts, never a C++ rule twin.
//
// The two RED-proven regression guards survive resemantified (both were
// proven FAILING against the pre-plan tree, commit B, and their teeth now
// live in the staged init itself): CTF's authored domain is the
// first-flag-per-team fold (a marker team with no flag fields NOTHING in
// the staged world), and TROOPS: ALL + TEAMS: Auto takes the manifest
// row.teams default (the extra authored teams stay empty).

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/save_data.h>

#include "../modes_pack_fixture.h"

#include <array>
#include <string>
#include <vector>

using namespace og::modes_test;
using og::ui::ScenarioFill;

namespace {

// The modes.core bot provenance contract (mode_caps.lua BOT_MARK_BIT),
// pinned against picker_common's reader constant.
constexpr std::int32_t kBotMarkBit = 65536;

bool any_line_is(const std::vector<std::string>& lines,
                 const std::string& exact)
{
    for (const auto& line : lines)
    {
        if (line == exact)
            return true;
    }
    return false;
}

bool any_line_contains(const std::vector<std::string>& lines,
                       const std::string& needle)
{
    for (const auto& line : lines)
    {
        if (line.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

// Stage the fixture world exactly as MatchStage does for a scripted level:
// the once-only mode_stage_init (respawn resolve + anchor scan + real
// on_mode_init), leaving the world dormant at tick 0.
void stage_fixture_world(ModesCtfWorld& fx)
{
    og::sim::mode_stage_init(fx.world());
    ASSERT_EQ(0u, fx.world().tick_count_) << "staging must not tick";
}

og::ui::ScenarioRosterReport staged_report(ModesCtfWorld& fx,
                                           const SaveData& save)
{
    return og::ui::build_scenario_roster_report(
        &fx.world(), og::ui::StagePreviewStatus::Staged, save, nullptr);
}

int marked_bots_on(const GameWorld& world, int team)
{
    int count = 0;
    for (const auto& entry : world.oblist)
    {
        const walker* const w = entry.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->team_num() != team || w->myguy != nullptr)
            continue;
        if (w->stats() != nullptr &&
            (w->stats()->bit_flags() & kBotMarkBit) != 0)
            ++count;
    }
    return count;
}

}  // namespace

using ScenarioStagedReport = ModesPackTest;

// RED-guard 1 resemantified: CTF's authored domain is the
// first-flag-per-team fold, not start markers — a marker team with no flag
// gets NO squad at staged init, so the census reports exactly the two flag
// teams and the pane shows the third team's absence honestly.
TEST_F(ScenarioStagedReport, ctf_domain_is_flag_teams_not_markers)
{
    ModesCtfWorld fx(kCtfLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.spawn_anchor(2, 96, 528);  // markers on THREE teams...
    fx.spawn_flag(flag_family_, 0, 100, 100);
    fx.spawn_flag(flag_family_, 1, 132, 100);  // ...flags on TWO
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;

    stage_fixture_world(fx);
    ASSERT_TRUE(fx.world().mode.active);

    SaveData save;
    const og::ui::ScenarioRosterReport report = staged_report(fx, save);

    EXPECT_TRUE(report.staged);
    EXPECT_TRUE(report.is_versus);
    EXPECT_TRUE(report.will_activate);
    EXPECT_FALSE(report.refusing);
    EXPECT_EQ("CTF", report.mode_name);
    EXPECT_TRUE(report.team_active[0]);
    EXPECT_TRUE(report.team_active[1]);
    EXPECT_FALSE(report.team_active[2])
        << "a marker team with no flag is not a CTF team (the flag fold "
           "authors the domain; the staged world fields it nothing)";
    EXPECT_EQ(ScenarioFill::Bots, report.team_fill[0]);
    EXPECT_EQ(5, report.team_fill_count[0]);
    EXPECT_EQ(1, report.team_anchor_count[0])
        << "anchor counts come from the real mode_stage_init scan";
    EXPECT_EQ(1, report.team_anchor_count[2]);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "MATCH: CTF - 2 TEAMS ACTIVE"));
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  ACTIVE - BOT SQUAD (5)"));
    EXPECT_TRUE(any_line_is(lines, "  GREEN TEAM  ACTIVE - BOT SQUAD (5)"));
    EXPECT_FALSE(any_line_contains(lines, "BLUE TEAM  ACTIVE"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// RED-guard 2 resemantified: TROOPS: ALL + TEAMS: Auto takes soccer's
// manifest row.teams default (9301 declares teams = 2), not the full
// authored mask — the staged init fields nothing on teams 2/3, so the
// census shows exactly two active teams.
TEST_F(ScenarioStagedReport, troops_all_auto_takes_the_manifest_default)
{
    ModesCtfWorld fx(kSoccerLevelA);  // row.teams = 2
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    fx.world().ctf_requested_strip_scenario_troops = 0;  // TROOPS: ALL
    fx.world().ctf_requested_team_count = 0;             // TEAMS: Auto

    stage_fixture_world(fx);
    ASSERT_TRUE(fx.world().mode.active);

    SaveData save;
    const og::ui::ScenarioRosterReport report = staged_report(fx, save);

    EXPECT_TRUE(report.will_activate);
    EXPECT_TRUE(report.team_active[0]);
    EXPECT_TRUE(report.team_active[1]);
    EXPECT_FALSE(report.team_active[2])
        << "ALL + Auto = the manifest row.teams default (2), not the "
           "authored count (the divergence the deleted twin never covered)";
    EXPECT_FALSE(report.team_active[3]);
    EXPECT_EQ("SOCCER", report.mode_name);
    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "MATCH: SOCCER - 2 TEAMS ACTIVE"));
    EXPECT_FALSE(any_line_contains(lines, "BLUE TEAM  ACTIVE"));
    EXPECT_FALSE(any_line_contains(lines, "YELLOW TEAM  ACTIVE"));
}

// The staged fill lines, pinned exactly (48-char budget throughout): FAIR
// with one deployed company = COMPANY for the roster team, MATCHED BOTS at
// the min headcount everywhere else. The old "BOT CLASSES DRAWN AT START"
// legend is GONE by design: the squads below ARE the staged squads (#235
// delivered by deletion). This is also the kModeVarMatchedSize pin (the
// shared MATCHED.SIZE mode var, slot 5) and the BOT_MARK_BIT provenance pin
// (every squad member add_squad_member spawns carries stats bit 65536).
TEST_F(ScenarioStagedReport, fair_fill_lines_pin)
{
    ModesCtfWorld fx(kSoccerLevelB);  // teams = 4
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 700, 1);
    fx.spawn_hero(FAMILY_SOLDIER, 0, 128, 700, 2);
    fx.world().ctf_requested_strip_scenario_troops =
        static_cast<short>(og::sim::kTroopsMatched);  // FAIR
    fx.world().ctf_requested_team_count = 0;

    stage_fixture_world(fx);
    ASSERT_TRUE(fx.world().mode.active);

    SaveData save;
    save.my_team = 0;
    const og::ui::ScenarioRosterReport report = staged_report(fx, save);

    EXPECT_TRUE(report.staged);
    EXPECT_TRUE(report.will_activate);
    EXPECT_EQ(ScenarioFill::Company, report.team_fill[0]);
    EXPECT_EQ(2, report.team_fill_count[0]);
    EXPECT_EQ(ScenarioFill::Matched, report.team_fill[1]);
    EXPECT_EQ(2, report.team_fill_count[1])
        << "matched squads truncate to the min roster headcount";

    // kModeVarMatchedSize: the FAIR census banked the exact headcount in
    // the shared MATCHED.SIZE slot (5) — the fact the MATCHED label reads.
    EXPECT_EQ(2, fx.world().mode.vars[5]);
    // BOT_MARK_BIT provenance: every squad member carries the bit; the
    // roster heroes carry none.
    EXPECT_EQ(2, marked_bots_on(fx.world(), 1));
    EXPECT_EQ(2, marked_bots_on(fx.world(), 2));
    EXPECT_EQ(2, marked_bots_on(fx.world(), 3));
    EXPECT_EQ(0, marked_bots_on(fx.world(), 0))
        << "company fighters never carry the bot mark";

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "MATCH: SOCCER - 4 TEAMS ACTIVE"));
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  ACTIVE - COMPANY (2)"));
    EXPECT_TRUE(any_line_is(lines, "  GREEN TEAM  ACTIVE - MATCHED BOTS (2)"));
    EXPECT_TRUE(any_line_is(lines, "  BLUE TEAM  ACTIVE - MATCHED BOTS (2)"));
    EXPECT_TRUE(any_line_is(lines, "  YELLOW TEAM  ACTIVE - MATCHED BOTS (2)"));
    EXPECT_FALSE(any_line_contains(lines, "BOT CLASSES DRAWN AT START"))
        << "the rows below list the ACTUAL staged squad — no legend";
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// TROOPS: ALL fills — authored map troops stand where present (unmarked
// livings), the legacy squad backfills an empty active team (marked
// livings), and the census tells them apart by the provenance bit alone.
TEST_F(ScenarioStagedReport, troops_all_fill_lines_pin)
{
    ModesCtfWorld fx(kSoccerLevelA);  // teams = 2
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.spawn_living(FAMILY_ORC, 1, 300, 300);
    fx.spawn_living(FAMILY_ORC, 1, 332, 300);
    fx.spawn_living(FAMILY_ORC, 1, 364, 300);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;

    stage_fixture_world(fx);
    ASSERT_TRUE(fx.world().mode.active);

    SaveData save;
    const og::ui::ScenarioRosterReport report = staged_report(fx, save);

    EXPECT_EQ(ScenarioFill::Bots, report.team_fill[0])
        << "an active team with nothing at all gets the legacy squad";
    EXPECT_EQ(5, report.team_fill_count[0]);
    EXPECT_EQ(ScenarioFill::Troops, report.team_fill[1]);
    EXPECT_EQ(3, report.team_fill_count[1]);
    EXPECT_EQ(5, marked_bots_on(fx.world(), 0));
    EXPECT_EQ(0, marked_bots_on(fx.world(), 1))
        << "authored troops carry no bot mark";

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  ACTIVE - BOT SQUAD (5)"));
    EXPECT_TRUE(any_line_is(lines, "  GREEN TEAM  ACTIVE - MAP TROOPS (3)"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// Onslaught's staged fills: foundries survive OWN (GENERATORS); a stripped
// team with no foundry has NOTHING in the staged world (E8, D17 no-bots),
// so the census counts two active teams and the pane shows the third
// team's emptiness as honest absence — no INACTIVE row, no NO FORCES row
// (the per-mode active-mask bank is mode-private; any C++ read of it would
// be a rule twin).
TEST_F(ScenarioStagedReport, onslaught_generator_lines_and_empty_team_absence)
{
    ModesCtfWorld fx(kOnsLevelB);  // teams = 3
    fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    fx.spawn_generator(FAMILY_TENT, 0, 192, 320);
    fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
    fx.spawn_living(FAMILY_ORC, 2, 300, 640);
    fx.world().ctf_requested_strip_scenario_troops = 2;  // OWN
    fx.world().ctf_requested_team_count = 0;

    stage_fixture_world(fx);
    ASSERT_TRUE(fx.world().mode.active);

    SaveData save;
    const og::ui::ScenarioRosterReport report = staged_report(fx, save);

    EXPECT_EQ("ONSLAUGHT", report.mode_name);
    EXPECT_EQ(ScenarioFill::Generators, report.team_fill[0]);
    EXPECT_EQ(2, report.team_fill_count[0]);
    EXPECT_EQ(ScenarioFill::Generators, report.team_fill[1]);
    EXPECT_EQ(1, report.team_fill_count[1]);
    EXPECT_FALSE(report.team_active[2])
        << "stripped troops + no foundry = nothing to census (E8)";

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "MATCH: ONSLAUGHT - 2 TEAMS ACTIVE"));
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  ACTIVE - GENERATORS (2)"));
    EXPECT_TRUE(any_line_is(lines, "  GREEN TEAM  ACTIVE - GENERATORS (1)"));
    EXPECT_FALSE(any_line_contains(lines, "BLUE TEAM"))
        << "an empty active team is honest absence, never a fabricated row";
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// A staged mode can legitimately refuse (soccer with one anchor team): the
// report says so with the verbatim honest sentence — never the fallback's
// false "FEWER THAN 2 AUTHORED TEAMS" sentence — and the kept post-refusal
// world (classic rules) still lists its rows.
TEST_F(ScenarioStagedReport, not_starting_prints_the_honest_sentence)
{
    ModesCtfWorld fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 96);  // one anchor team: soccer refuses
    fx.spawn_living(FAMILY_ORC, 0, 300, 300);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;

    stage_fixture_world(fx);
    ASSERT_FALSE(fx.world().mode.active);
    ASSERT_TRUE(fx.world().mode.init_attempted);

    SaveData save;
    const og::ui::ScenarioRosterReport report = staged_report(fx, save);

    EXPECT_TRUE(report.staged);
    EXPECT_FALSE(report.will_activate);
    EXPECT_TRUE(report.refusing)
        << "a registered on_mode_init that attempted and refused";
    for (int t = 0; t < 4; ++t)
        EXPECT_FALSE(report.team_active[static_cast<std::size_t>(t)]);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines,
                            "MATCH WILL NOT START: FEWER THAN 2 TEAMS"));
    EXPECT_FALSE(any_line_contains(lines, "AUTHORED TEAMS"))
        << "the fallback's marker-count sentence would be false here";
    EXPECT_TRUE(any_line_contains(lines, "1x ORC"))
        << "the kept post-refusal world still lists its rows";
}

// A scripted level with NO on_mode_init hook registered is not refusing —
// it has no mode — and the count-only fallback answers over the STAGED
// world itself (markers survive an init that never ran; the anchor counts
// were banked by the real stage scan).
TEST_F(ScenarioStagedReport, hookless_scripted_level_uses_the_count_fallback)
{
    ModesCtfWorld fx(9999);  // no registration anywhere for this id
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;

    stage_fixture_world(fx);
    ASSERT_FALSE(fx.world().mode.active);
    ASSERT_TRUE(fx.world().mode.init_attempted);

    SaveData save;
    const og::ui::ScenarioRosterReport report = staged_report(fx, save);

    EXPECT_TRUE(report.staged);
    EXPECT_FALSE(report.refusing) << "no hook = no mode, not a refusal";
    EXPECT_EQ("", report.mode_name);
    EXPECT_TRUE(report.will_activate);
    EXPECT_TRUE(report.team_authored[0]);
    EXPECT_TRUE(report.team_authored[1]);
    EXPECT_EQ(1, report.team_anchor_count[0]);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "MATCH: 2 AUTHORED TEAMS"));
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  MARKERS: 1  ACTIVE"));
    EXPECT_FALSE(any_line_contains(lines, "MATCH WILL NOT START"));
    EXPECT_FALSE(any_line_contains(lines, "STAGING FAILED"));
}

// Dormancy carve-out made code: delayed-spawn (dormant) walkers are
// excluded from the staged census exactly as the keyframe capture excludes
// them — every client censuses the identical non-dormant world, and the
// cohort reveals at its authored tick after launch.
TEST_F(ScenarioStagedReport, dormant_walkers_are_excluded_from_the_census)
{
    ModesCtfWorld fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.spawn_living(FAMILY_ORC, 1, 300, 300);
    walker* const sleeper = fx.spawn_living(FAMILY_ORC, 1, 332, 300);
    ASSERT_NE(nullptr, sleeper);
    sleeper->set_dormant(true);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;

    stage_fixture_world(fx);
    ASSERT_TRUE(fx.world().mode.active);

    SaveData save;
    const og::ui::ScenarioRosterReport report = staged_report(fx, save);

    EXPECT_EQ(ScenarioFill::Troops, report.team_fill[1]);
    EXPECT_EQ(1, report.team_fill_count[1])
        << "the dormant orc is present-but-uncensused";
    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "  GREEN TEAM  ACTIVE - MAP TROOPS (1)"));
    bool found_troop_row = false;
    for (const auto& line : lines)
    {
        if (line.find("x ORC") != std::string::npos)
        {
            EXPECT_EQ("  1x ORC Lv 1", line)
                << "the roster rows exclude the dormant cohort too";
            found_troop_row = true;
        }
    }
    EXPECT_TRUE(found_troop_row);
}

// The count-only fallback's activation clamp: a hook-less scripted level
// with a single authored marker team cannot start, and the clamp folds
// every team to inactive — the pane never shows a one-team "match".
TEST_F(ScenarioStagedReport, hookless_single_marker_level_clamps_inactive)
{
    ModesCtfWorld fx(9999);  // no registration anywhere for this id
    fx.spawn_anchor(0, 96, 96);  // ONE marker team only
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;

    stage_fixture_world(fx);
    ASSERT_FALSE(fx.world().mode.active);
    ASSERT_TRUE(fx.world().mode.init_attempted);

    SaveData save;
    const og::ui::ScenarioRosterReport report = staged_report(fx, save);

    EXPECT_TRUE(report.staged);
    EXPECT_FALSE(report.refusing) << "no hook = no mode, not a refusal";
    EXPECT_FALSE(report.will_activate);
    EXPECT_TRUE(report.team_authored[0]);
    for (int t = 0; t < 4; ++t)
        EXPECT_FALSE(report.team_active[static_cast<std::size_t>(t)])
            << "the sub-2-team clamp folds every team inactive, team " << t;

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "MATCH: 1 AUTHORED TEAMS"));
    EXPECT_TRUE(any_line_is(lines,
                            "MATCH INACTIVE: FEWER THAN 2 AUTHORED TEAMS"));
}

// The seat block (#218) on a STAGED versus report: "SEATS: {summary}" and
// the per-seat lines land IMMEDIATELY after the match block with no blank
// between (the first-block TRACE seam carries them), and the roster block
// keeps its blank separation below. Exact indices pin the placement.
TEST_F(ScenarioStagedReport, seat_block_follows_the_match_block_directly)
{
    ModesCtfWorld fx(kSoccerLevelB);  // teams = 4
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 700, 1);
    fx.spawn_hero(FAMILY_SOLDIER, 0, 128, 700, 2);
    fx.world().ctf_requested_strip_scenario_troops =
        static_cast<short>(og::sim::kTroopsMatched);  // FAIR
    fx.world().ctf_requested_team_count = 0;

    stage_fixture_world(fx);
    ASSERT_TRUE(fx.world().mode.active);

    SaveData save;
    save.my_team = 0;
    og::ui::ScenarioSeatContext seats;
    og::sim::LobbyPlayer host_seat;
    host_seat.player_index = 0;
    host_seat.company = "Iron Kettle";
    host_seat.team = 0;
    og::sim::LobbyPlayer joiner_seat;
    joiner_seat.player_index = 1;
    joiner_seat.company = "Keepers Rest";
    joiner_seat.team = 0;
    joiner_seat.ready = true;
    seats.players = {host_seat, joiner_seat};
    seats.local_player_indices = {0};

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(
            &fx.world(), og::ui::StagePreviewStatus::Staged, save, nullptr,
            &seats);
    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    ASSERT_GE(lines.size(), 10u);
    EXPECT_EQ("MATCH: SOCCER - 4 TEAMS ACTIVE", lines[0]);
    EXPECT_EQ("  RED TEAM  ACTIVE - COMPANY (2)", lines[1]);
    EXPECT_EQ("  GREEN TEAM  ACTIVE - MATCHED BOTS (2)", lines[2]);
    EXPECT_EQ("  BLUE TEAM  ACTIVE - MATCHED BOTS (2)", lines[3]);
    EXPECT_EQ("  YELLOW TEAM  ACTIVE - MATCHED BOTS (2)", lines[4]);
    EXPECT_EQ("SEATS: CO-OP", lines[5])
        << "no blank between the match block and the seat block — the "
           "first-block trace seam must carry the seats";
    EXPECT_EQ("  P1 YOU - RED TEAM", lines[6]);
    EXPECT_EQ("  P2 KEE [RDY] - RED TEAM", lines[7]);
    EXPECT_EQ("", lines[8]);
    EXPECT_EQ("RED TEAM (YOURS)", lines[9]);
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// The formatter's closed fill vocabulary: ScenarioFill::Empty renders the
// NO FORCES label with no count suffix. The census never produces an
// active-but-empty team today (activity IS the census), so the display
// contract is pinned directly over the public report struct.
TEST_F(ScenarioStagedReport, empty_fill_renders_the_no_forces_row)
{
    og::ui::ScenarioRosterReport report;
    report.staged = true;
    report.is_versus = true;
    report.will_activate = true;
    report.mode_census = true;
    report.mode_name = "CTF";
    report.team_active[0] = true;
    report.team_fill[0] = ScenarioFill::Empty;
    report.team_fill_count[0] = 0;

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "MATCH: CTF - 1 TEAMS ACTIVE"));
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  ACTIVE - NO FORCES"))
        << "Empty must label NO FORCES, count suffix omitted";
}

// The lineup preset labels (lineup §3.4): a preset squad's fill label
// wears the registered preset name — "BOT SQUAD <NAME> (n)" — read from
// the shared facts slot the spawn seam banked (kModeVarLineupFacts,
// mode_match.lua bank_lineup_facts), with the name resolved through the
// campaign lineup hook. The occupied team keeps its COMPANY label (the
// preset allies appear in the rows below), so the name rides only bot
// fills.
TEST_F(ScenarioStagedReport, preset_fill_label_names_the_squad)
{
    ModesCtfWorld fx(kSoccerLevelB);  // teams = 4
    for (int team = 0; team < 3; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 700, 1);
    fx.world().ctf_requested_strip_scenario_troops = 0;  // ALL
    fx.world().ctf_requested_team_count = 0;
    fx.world().ctf_requested_bot_squad[1] =
        og::sim::kBotSquadPresetBase + 1;  // CASTER on empty team 1
    fx.world().ctf_requested_bot_squad[0] =
        og::sim::kBotSquadPresetBase + 2;  // BRUTES beside the roster

    stage_fixture_world(fx);
    ASSERT_TRUE(fx.world().mode.active);

    SaveData save;
    const og::ui::ScenarioRosterReport report = staged_report(fx, save);
    EXPECT_EQ(ScenarioFill::Company, report.team_fill[0]);
    EXPECT_EQ(1, report.team_fill_count[0]);
    EXPECT_EQ(5, report.team_squad_count[0])
        << "the preset allies beside the roster are counted on the row";
    EXPECT_EQ("BRUTES", report.team_squad_name[0])
        << "an occupied team's squad wears its name too";
    EXPECT_EQ(ScenarioFill::Bots, report.team_fill[1]);
    EXPECT_EQ("CASTER", report.team_squad_name[1]);
    EXPECT_EQ(0, report.team_squad_level[1]);
    EXPECT_EQ(0, report.team_squad_count[1])
        << "a squad-only team counts its bots in team_fill_count alone";

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines,
                            "  RED TEAM  ACTIVE - COMPANY+BRUTES (1+5)"))
        << "no hard shape: the roster plus the full squad, both counted";
    EXPECT_TRUE(any_line_is(lines,
                            "  GREEN TEAM  ACTIVE - BOT SQUAD CASTER (5)"));
    EXPECT_TRUE(any_line_is(lines, "  BLUE TEAM  ACTIVE - BOT SQUAD (5)"))
        << "the AUTO squad keeps its plain label";
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// An LV offset (amendment A6) appends LV+k / LV-k to the bot fill's label
// — the plain squad's and a preset's alike, the sign being the message —
// and never to an occupancy fill. The report carries the OFFSET the
// squad was resolved through, not the resolved level.
TEST_F(ScenarioStagedReport, level_offset_appends_signed_lv_to_bot_labels)
{
    ModesCtfWorld fx(kSoccerLevelB);
    for (int team = 0; team < 3; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;
    fx.world().ctf_requested_bot_level[0] = 5;
    fx.world().ctf_requested_bot_squad[1] =
        og::sim::kBotSquadPresetBase;  // BALANC
    fx.world().ctf_requested_bot_level[1] = 3;
    fx.world().ctf_requested_bot_level[2] = -1;

    stage_fixture_world(fx);
    ASSERT_TRUE(fx.world().mode.active);

    SaveData save;
    const og::ui::ScenarioRosterReport report = staged_report(fx, save);
    EXPECT_EQ(5, report.team_squad_level[0]);
    EXPECT_EQ(3, report.team_squad_level[1]);
    EXPECT_EQ(-1, report.team_squad_level[2]);
    EXPECT_EQ("BALANC", report.team_squad_name[1]);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  ACTIVE - BOT SQUAD (5) LV+5"));
    EXPECT_TRUE(any_line_is(
        lines, "  GREEN TEAM  ACTIVE - BOT SQUAD BALANC (5) LV+3"));
    EXPECT_TRUE(any_line_is(lines, "  BLUE TEAM  ACTIVE - BOT SQUAD (5) LV-1"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// The budget rule (lineup §3.4, A6): the signed token is one character
// wider than the old LVk, so the one row that would overrun the 48-char
// budget — YELLOW with a 6-char preset name — spends the separator space
// before the token instead of clipping the digit; every other row keeps
// the space. Both shapes pinned.
TEST_F(ScenarioStagedReport, preset_label_worst_case_fits_the_budget)
{
    og::ui::ScenarioRosterReport report;
    report.staged = true;
    report.is_versus = true;
    report.will_activate = true;
    report.mode_census = true;
    report.mode_name = "SOCCER";
    report.team_active[3] = true;
    report.team_fill[3] = ScenarioFill::Bots;
    report.team_fill_count[3] = 5;
    report.team_squad_name[3] = "BALANC";
    report.team_squad_level[3] = 2;
    report.team_active[0] = true;
    report.team_fill[0] = ScenarioFill::Bots;
    report.team_fill_count[0] = 5;
    report.team_squad_name[0] = "BALANC";
    report.team_squad_level[0] = -5;

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(
        lines, "  YELLOW TEAM  ACTIVE - BOT SQUAD BALANC (5)LV+2"))
        << "exactly 48: the separator space is what the budget spends";
    EXPECT_TRUE(any_line_is(
        lines, "  RED TEAM  ACTIVE - BOT SQUAD BALANC (5) LV-5"))
        << "a row with room keeps the space";
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// ---------------------------------------------------------------------------
// The lineup review rows (wp/review-lua L1/L2): the band refusal sentence
// and the occupied-team squad label.
// ---------------------------------------------------------------------------

// L1: a band mode (FFA/mutant) that refuses — BOTS: NONE on team 1 with a
// single hero — reports its OWN honest sentence. The reason rides the
// shared facts slot's 10^9 digit (mode_match.lua REFUSAL_BASE, written by
// the band decide fold before error()), never the free-text script error,
// so host and joiner mirrors render the identical line.
TEST_F(ScenarioStagedReport, band_refusal_prints_the_fighters_sentence)
{
    ModesCtfWorld fx(850);  // the shipped FFA level
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 96);
    fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    fx.world().ctf_requested_bot_squad[0] =
        og::sim::kBotSquadNone;  // NONE on the band's pair

    stage_fixture_world(fx);
    ASSERT_FALSE(fx.world().mode.active);
    ASSERT_TRUE(fx.world().mode.init_attempted);
    EXPECT_EQ(1000000000, fx.world().mode.vars[4])
        << "the band fold banked the reason digit (mode_match.lua "
           "REFUSAL_BASE) before it raised";

    SaveData save;
    const og::ui::ScenarioRosterReport report = staged_report(fx, save);
    EXPECT_TRUE(report.staged);
    EXPECT_TRUE(report.refusing);
    EXPECT_TRUE(report.refusal_fighters);
    EXPECT_FALSE(report.will_activate);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines,
                            "MATCH WILL NOT START: FEWER THAN 2 FIGHTERS"));
    EXPECT_FALSE(any_line_contains(lines, "FEWER THAN 2 TEAMS"))
        << "the team sentence would be false for a band";
    EXPECT_TRUE(any_line_contains(lines, "SOLDIER"))
        << "the kept, untouched world still lists its rows";
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// The team modes keep their sentence: reason code 0 is the teams sentence.
TEST_F(ScenarioStagedReport, team_refusal_keeps_the_teams_sentence)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_living(FAMILY_ORC, 3, 300, 300);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;

    stage_fixture_world(fx);
    ASSERT_FALSE(fx.world().mode.active);

    SaveData save;
    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(staged_report(fx, save));
    EXPECT_TRUE(any_line_is(lines,
                            "MATCH WILL NOT START: FEWER THAN 2 TEAMS"));
    EXPECT_FALSE(any_line_contains(lines, "FIGHTERS"));
}

// L2: a preset beside a roster on a hard-shape court is sized to the gap
// and the label counts both halves — "COMPANY+<NAME> (roster+squad)", the
// preset name replacing the BOTS word exactly as "BOT SQUAD <NAME>" does —
// so the preview's count IS the spawned count. A full court leaves no
// squad: the row is the plain COMPANY (n).
TEST_F(ScenarioStagedReport, occupied_team_label_counts_company_plus_squad)
{
    {
        ModesCtfWorld fx(kBballLevelB);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 192, 96);
        for (int k = 0; k < 3; ++k)
            fx.spawn_hero(FAMILY_SOLDIER, 0, static_cast<short>(96 + 32 * k),
                          700, k + 1);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        fx.world().ctf_requested_bot_squad[0] =
            og::sim::kBotSquadPresetBase;  // BALANC
        fx.world().ctf_requested_bot_level[0] = 4;

        stage_fixture_world(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(2, marked_bots_on(fx.world(), 0));

        SaveData save;
        const og::ui::ScenarioRosterReport report = staged_report(fx, save);
        EXPECT_EQ(ScenarioFill::Company, report.team_fill[0]);
        EXPECT_EQ(3, report.team_fill_count[0]);
        EXPECT_EQ(2, report.team_squad_count[0]);
        EXPECT_EQ("BALANC", report.team_squad_name[0]);
        EXPECT_EQ(4, report.team_squad_level[0]);

        const std::vector<std::string> lines =
            og::ui::format_scenario_report_lines(report);
        EXPECT_TRUE(any_line_is(
            lines, "  RED TEAM  ACTIVE - COMPANY+BALANC (3+2) LV+4"));
        EXPECT_TRUE(any_line_is(lines, "  GREEN TEAM  ACTIVE - BOT SQUAD (5)"));
        for (const auto& line : lines)
            EXPECT_LE(line.size(), 48u) << line;
    }
    {
        ModesCtfWorld fx(kBballLevelB);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 192, 96);
        for (int k = 0; k < 5; ++k)
            fx.spawn_hero(FAMILY_SOLDIER, 0, static_cast<short>(96 + 32 * k),
                          700, k + 1);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        fx.world().ctf_requested_bot_squad[0] =
            og::sim::kBotSquadPresetBase;  // BALANC, no room

        stage_fixture_world(fx);
        ASSERT_TRUE(fx.world().mode.active);

        SaveData save;
        const og::ui::ScenarioRosterReport report = staged_report(fx, save);
        EXPECT_EQ(0, report.team_squad_count[0]);
        EXPECT_TRUE(report.team_squad_name[0].empty())
            << "0 spawned = no applied fact, no name";
        const std::vector<std::string> lines =
            og::ui::format_scenario_report_lines(report);
        EXPECT_TRUE(any_line_is(lines, "  RED TEAM  ACTIVE - COMPANY (5)"));
    }
}

// The occupied-team worst case sits exactly on the 48-char budget once
// the separator space is spent: "  YELLOW TEAM  ACTIVE - COMPANY+BALANC
// (3+2)LV+2" (24 + 24); a BLUE row with the same fill keeps its space.
// A squad without a banked name (a FAIR degrade with bots on the roster
// team) reads COMPANY+BOTS.
TEST_F(ScenarioStagedReport, company_plus_squad_worst_case_fits_the_budget)
{
    og::ui::ScenarioRosterReport report;
    report.staged = true;
    report.is_versus = true;
    report.will_activate = true;
    report.mode_census = true;
    report.mode_name = "SOCCER";
    report.team_active[3] = true;
    report.team_fill[3] = ScenarioFill::Company;
    report.team_fill_count[3] = 3;
    report.team_squad_count[3] = 2;
    report.team_squad_name[3] = "BALANC";
    report.team_squad_level[3] = 2;
    report.team_active[2] = true;
    report.team_fill[2] = ScenarioFill::Company;
    report.team_fill_count[2] = 3;
    report.team_squad_count[2] = 2;
    report.team_squad_name[2] = "BALANC";
    report.team_squad_level[2] = -1;
    report.team_active[0] = true;
    report.team_fill[0] = ScenarioFill::Company;
    report.team_fill_count[0] = 2;
    report.team_squad_count[0] = 5;

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(
        lines, "  YELLOW TEAM  ACTIVE - COMPANY+BALANC (3+2)LV+2"));
    EXPECT_TRUE(any_line_is(
        lines, "  BLUE TEAM  ACTIVE - COMPANY+BALANC (3+2) LV-1"));
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  ACTIVE - COMPANY+BOTS (2+5)"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}
