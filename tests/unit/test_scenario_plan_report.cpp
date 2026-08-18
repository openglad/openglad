// The plan-driven VIEW LEVEL preview (issue #218): build_scenario_roster_report
// evaluates the SAME registered on_mode_plan the launch chain runs (over the
// scratch world's census plus the save/lobby-marshaled request knobs and
// roster head-counts), so the preview's activation, per-mode authored domain,
// fills and score facts are the plan's — not a C++ twin's.
//
// The two "red-then-green" cases below were proven FAILING against the
// pre-rewire tree (commit B): the old preview derived its authored domain
// from start markers on every mode (wrong for CTF's flag domain) and used the
// count-only clamp under TROOPS: ALL + TEAMS: Auto (wrong where the manifest
// row.teams default differs from the authored count).

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/match_plan.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/save_data.h>

#include "../modes_pack_fixture.h"

#include <array>
#include <string>
#include <vector>

using namespace og::modes_test;
using og::sim::MatchPlanFill;

namespace {

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

// A deployed roster of `count` members on `team` (the preview marshals
// head-counts from the save's team_list, mirroring the spawn paths).
void deploy_company(SaveData& save, int team, int count)
{
    for (int k = 0; k < count; ++k)
    {
        for (auto& slot : save.team_list)
        {
            if (slot != nullptr)
                continue;
            slot = std::make_unique<guy>(FAMILY_SOLDIER);
            slot->teamnum = static_cast<short>(team);
            save.team_size++;
            break;
        }
    }
}

}  // namespace

using ScenarioPlanReport = ModesPackTest;

// RED against commit B: the old preview's authored domain was start markers
// for every mode; CTF's real domain is the first-flag-per-team fold.
TEST_F(ScenarioPlanReport, ctf_domain_is_flag_teams_not_markers)
{
    ModesCtfWorld fx(kCtfLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.spawn_anchor(2, 96, 528);  // markers on THREE teams...
    fx.spawn_flag(flag_family_, 0, 100, 100);
    fx.spawn_flag(flag_family_, 1, 132, 100);  // ...flags on TWO

    SaveData save;
    save.ctf_strip_scenario_troops = 0;
    save.ctf_team_count = 0;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_TRUE(report.is_versus);
    EXPECT_TRUE(report.team_authored[0]);
    EXPECT_TRUE(report.team_authored[1]);
    EXPECT_FALSE(report.team_authored[2])
        << "a marker team with no flag is not a CTF team (the flag fold "
           "authors the domain, not the markers)";
    EXPECT_TRUE(report.will_activate);
    EXPECT_TRUE(report.team_active[0]);
    EXPECT_TRUE(report.team_active[1]);
    EXPECT_FALSE(report.team_active[2]);

    EXPECT_TRUE(report.plan_valid);
    EXPECT_FALSE(report.plan_error);
    EXPECT_EQ("CTF", report.mode_name);
    EXPECT_EQ(1, report.team_anchor_count[0])
        << "anchor counts come from the engine scan on the scratch world";
    EXPECT_EQ(1, report.team_anchor_count[2]);
    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "MATCH: CTF - 2 OF 2 TEAMS ACTIVE"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// RED against commit B: TROOPS: ALL + TEAMS: Auto takes soccer's manifest
// row.teams default (9301 declares teams = 2), not the full authored mask.
TEST_F(ScenarioPlanReport, troops_all_auto_takes_the_manifest_default)
{
    ModesCtfWorld fx(kSoccerLevelA);  // row.teams = 2
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);

    SaveData save;
    save.ctf_strip_scenario_troops = 0;  // TROOPS: ALL
    save.ctf_team_count = 0;             // TEAMS: Auto

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_TRUE(report.will_activate);
    EXPECT_TRUE(report.team_active[0]);
    EXPECT_TRUE(report.team_active[1]);
    EXPECT_FALSE(report.team_active[2])
        << "ALL + Auto = the manifest row.teams default (2), not the "
           "authored count (the divergence the deleted twin never covered)";
    EXPECT_FALSE(report.team_active[3]);
    EXPECT_TRUE(report.team_authored[2])
        << "soccer's authored domain is the anchor census";
    EXPECT_EQ("SOCCER", report.mode_name);
    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "MATCH: SOCCER - 2 OF 4 TEAMS ACTIVE"));
    EXPECT_TRUE(any_line_is(lines, "  BLUE TEAM  INACTIVE"));
    EXPECT_TRUE(any_line_is(lines, "  YELLOW TEAM  INACTIVE"));
}

// The plan arm's fill lines, pinned exactly (48-char budget throughout):
// FAIR with one deployed company = COMPANY for the roster team, MATCHED
// BOTS at the min headcount everywhere else, plus the seeded-squad legend
// (classes are drawn at first spawn — never listed).
TEST_F(ScenarioPlanReport, fair_fill_lines_pin)
{
    ModesCtfWorld fx(kSoccerLevelB);  // teams = 4
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);

    SaveData save;
    save.my_team = 0;
    save.ctf_strip_scenario_troops = og::sim::kTroopsMatched;  // FAIR
    save.ctf_team_count = 0;
    deploy_company(save, 0, 2);

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_TRUE(report.plan_valid);
    EXPECT_TRUE(report.will_activate);
    EXPECT_TRUE(report.seeded_squads);
    EXPECT_EQ(MatchPlanFill::Company, report.team_fill[0]);
    EXPECT_EQ(2, report.team_fill_count[0]);
    EXPECT_EQ(MatchPlanFill::Matched, report.team_fill[1]);
    EXPECT_EQ(2, report.team_fill_count[1])
        << "matched squads truncate to the min roster headcount";

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "MATCH: SOCCER - 4 OF 4 TEAMS ACTIVE"));
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  ACTIVE - COMPANY (2)"));
    EXPECT_TRUE(any_line_is(lines, "  GREEN TEAM  ACTIVE - MATCHED BOTS (2)"));
    EXPECT_TRUE(any_line_is(lines, "  BLUE TEAM  ACTIVE - MATCHED BOTS (2)"));
    EXPECT_TRUE(any_line_is(lines, "  YELLOW TEAM  ACTIVE - MATCHED BOTS (2)"));
    EXPECT_TRUE(any_line_is(lines, "BOT CLASSES DRAWN AT START"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// TROOPS: ALL fills — authored map troops stand where present, the legacy
// squad backfills an empty active team; no seeded legend without a squad
// draw is WRONG (soccer always draws when any bots/matched fill exists),
// so the legend pin rides the BOT SQUAD row here.
TEST_F(ScenarioPlanReport, troops_all_fill_lines_pin)
{
    ModesCtfWorld fx(kSoccerLevelA);  // teams = 2
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.spawn_living(FAMILY_ORC, 1, 300, 300);
    fx.spawn_living(FAMILY_ORC, 1, 332, 300);
    fx.spawn_living(FAMILY_ORC, 1, 364, 300);

    SaveData save;
    save.ctf_strip_scenario_troops = 0;
    save.ctf_team_count = 0;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_TRUE(report.plan_valid);
    EXPECT_EQ(MatchPlanFill::Bots, report.team_fill[0])
        << "an active team with nothing at all gets the legacy squad";
    EXPECT_EQ(5, report.team_fill_count[0]);
    EXPECT_EQ(MatchPlanFill::Troops, report.team_fill[1]);
    EXPECT_EQ(3, report.team_fill_count[1]);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  ACTIVE - BOT SQUAD (5)"));
    EXPECT_TRUE(any_line_is(lines, "  GREEN TEAM  ACTIVE - MAP TROOPS (3)"));
    EXPECT_TRUE(any_line_is(lines, "BOT CLASSES DRAWN AT START"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// Onslaught's fills: foundries survive (GENERATORS), a stripped team with
// no foundry is honestly NO FORCES (E8, D17 no-bots), and onslaught never
// seeds squads, so no legend line.
TEST_F(ScenarioPlanReport, onslaught_generator_and_no_forces_lines)
{
    ModesCtfWorld fx(kOnsLevelB);  // teams = 3
    fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    fx.spawn_generator(FAMILY_TENT, 0, 192, 320);
    fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
    fx.spawn_living(FAMILY_ORC, 2, 300, 640);

    SaveData save;
    save.ctf_strip_scenario_troops = 2;  // OWN: troops strip, foundries stay
    save.ctf_team_count = 0;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_TRUE(report.plan_valid);
    EXPECT_EQ("ONSLAUGHT", report.mode_name);
    EXPECT_FALSE(report.seeded_squads);
    EXPECT_EQ(MatchPlanFill::Generators, report.team_fill[0]);
    EXPECT_EQ(2, report.team_fill_count[0]);
    EXPECT_EQ(MatchPlanFill::Empty, report.team_fill[2])
        << "stripped troops + no foundry = active and empty (E8)";

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines,
                            "MATCH: ONSLAUGHT - 3 OF 3 TEAMS ACTIVE"));
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  ACTIVE - GENERATORS (2)"));
    EXPECT_TRUE(any_line_is(lines, "  GREEN TEAM  ACTIVE - GENERATORS (1)"));
    EXPECT_TRUE(any_line_is(lines, "  BLUE TEAM  ACTIVE - NO FORCES"));
    EXPECT_FALSE(any_line_contains(lines, "BOT CLASSES DRAWN AT START"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// A plan can legitimately refuse from any authored shape; the report says
// so honestly — never the fallback's false "FEWER THAN 2 AUTHORED TEAMS"
// sentence, and no fill rows for a match that will not start.
TEST_F(ScenarioPlanReport, not_starting_prints_the_honest_sentence)
{
    ModesCtfWorld fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 96);  // one anchor team: soccer refuses

    SaveData save;
    save.ctf_strip_scenario_troops = 0;
    save.ctf_team_count = 0;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_TRUE(report.plan_valid);
    EXPECT_FALSE(report.will_activate);
    EXPECT_EQ("soccer: fewer than two anchor teams", report.starts_reason);
    for (int t = 0; t < 4; ++t)
        EXPECT_FALSE(report.team_active[static_cast<std::size_t>(t)]);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines,
                            "MATCH WILL NOT START: FEWER THAN 2 TEAMS"));
    EXPECT_FALSE(any_line_contains(lines, "AUTHORED TEAMS"))
        << "the fallback's marker-count sentence would be false here";
}

// A broken pack at preview time: the plan errors, the report renders the
// count-only fallback plus one honest line, and the process survives
// (broken pack != crashed lobby). 9805's plan raises "plan boom".
TEST_F(ScenarioPlanReport, plan_error_renders_fallback_plus_honest_line)
{
    ModesCtfWorld fx(9805);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);

    SaveData save;
    save.ctf_strip_scenario_troops = 0;
    save.ctf_team_count = 0;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_FALSE(report.plan_valid);
    EXPECT_TRUE(report.plan_error);
    EXPECT_EQ("", report.mode_name);
    EXPECT_TRUE(report.will_activate) << "the count clamp still answers";
    EXPECT_TRUE(report.team_active[0]);
    EXPECT_TRUE(report.team_active[1]);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "MATCH: 2 AUTHORED TEAMS"))
        << "the error arm keeps the fallback's exact lines";
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  MARKERS: 1  ACTIVE"));
    EXPECT_TRUE(any_line_is(lines,
                            "MATCH RULES UNAVAILABLE (SCRIPT ERROR)"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// A scripted level whose mode registers NO plan (Mutant) falls back to the
// count-only clamp with no fill column, no mode name and no error line —
// modes without a plan need zero edits.
TEST_F(ScenarioPlanReport, planless_mode_uses_the_count_fallback)
{
    ModesCtfWorld fx(kMutantLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);

    SaveData save;
    save.ctf_strip_scenario_troops = 0;
    save.ctf_team_count = 0;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_FALSE(report.plan_valid);
    EXPECT_FALSE(report.plan_error);
    EXPECT_EQ("", report.mode_name);
    EXPECT_TRUE(report.will_activate);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "MATCH: 2 AUTHORED TEAMS"));
    EXPECT_TRUE(any_line_is(lines, "  RED TEAM  MARKERS: 1  ACTIVE"));
    EXPECT_FALSE(any_line_contains(lines, "MATCH RULES UNAVAILABLE"));
}

// The networked arm: explicit roster counts (the SDL client passes the
// lobby's combined rosters) override the save's — a REMOTE peer's company
// team previews exactly.
TEST_F(ScenarioPlanReport, lobby_roster_counts_override_the_save)
{
    ModesCtfWorld fx(kSoccerLevelB);  // teams = 4
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);

    SaveData save;  // EMPTY local roster
    save.ctf_strip_scenario_troops = og::sim::kTroopsMatched;
    save.ctf_team_count = 0;

    // Without lobby counts: FAIR with zero rosters anywhere degrades to
    // the legacy squads.
    const og::ui::ScenarioRosterReport local_report =
        og::ui::build_scenario_roster_report(fx.world(), save);
    EXPECT_EQ(MatchPlanFill::Bots, local_report.team_fill[0])
        << "the FAIR -> legacy degrade with no roster anywhere";

    // With the lobby's combined counts (a remote peer deployed 3 on team
    // 2): COMPANY there, matched bots at its headcount elsewhere.
    const std::array<int, 4> lobby_counts{0, 0, 3, 0};
    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save,
                                             &lobby_counts);
    EXPECT_EQ(MatchPlanFill::Company, report.team_fill[2]);
    EXPECT_EQ(3, report.team_fill_count[2]);
    EXPECT_EQ(MatchPlanFill::Matched, report.team_fill[0]);
    EXPECT_EQ(3, report.team_fill_count[0]);
    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines, "  BLUE TEAM  ACTIVE - COMPANY (3)"));
    EXPECT_TRUE(any_line_is(lines,
                            "  RED TEAM  ACTIVE - MATCHED BOTS (3)"));
}

// A plan refusing with its own reason still reports its fill columns; a
// row with no fill string at all displays as UNKNOWN (the 9807 VOCAB
// probe's teams[4] row).
TEST_F(ScenarioPlanReport, refusing_plan_keeps_reason_and_unknown_labels)
{
    ModesCtfWorld fx(9807);

    SaveData save;
    save.ctf_team_count = 0;
    save.ctf_strip_scenario_troops = 0;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_TRUE(report.plan_valid);
    EXPECT_EQ("VOCAB", report.mode_name);
    EXPECT_FALSE(report.will_activate);
    EXPECT_EQ("vocab: will not start", report.starts_reason);
    EXPECT_EQ(MatchPlanFill::Other, report.team_fill[3]);
    EXPECT_EQ("UNKNOWN", report.team_fill_label[3])
        << "a fill-less row displays honestly as UNKNOWN";
    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(lines,
                            "MATCH WILL NOT START: FEWER THAN 2 TEAMS"));
}

// Unknown fill vocabulary from a future pack flows through verbatim
// (clipped at the parser's 20-char bound), still inside the line budget.
// The 9800 probe echoes its inputs: team_count -> authored_mask,
// strip_troops -> active_mask, and its teams[4] row carries the overlong
// label.
TEST_F(ScenarioPlanReport, unknown_fill_labels_flow_verbatim)
{
    ModesCtfWorld fx(9800);

    SaveData save;
    save.ctf_team_count = 15;           // authored echo: all four teams
    save.ctf_strip_scenario_troops = 8; // active echo: team 3 alone

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_TRUE(report.plan_valid);
    EXPECT_EQ("PROBE", report.mode_name);
    EXPECT_EQ(MatchPlanFill::Other, report.team_fill[3]);
    EXPECT_EQ("A DELIBERATELY OVERL", report.team_fill_label[3]);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_is(
        lines, "  YELLOW TEAM  ACTIVE - A DELIBERATELY OVERL (4)"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}
