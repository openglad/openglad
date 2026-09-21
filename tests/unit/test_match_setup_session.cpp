/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The SETUP wizard's PURE rules (docs/match-setup-design.md §2.4-§2.6):
// the SIDES/FILL macros, the TIME LIMIT and SCORE wheels, the RULES faces
// and their packed lines, the team-line cells and the M4 GO predicate.
// Headless by construction — no SDL, no lobby client, no Lua.
//
// Suite MatchSetupRules is the PURE half of this file; MatchSetupSession
// and ModesSetupSession below are the step-machine and terminal-driver
// halves (WP5), which need a mounted campaign and a Lua book.
//
// Ten of the macro tests below are ports of tests/unit/test_modes_book.cpp
// :1421-1935, which drove the SAME rules through campaign_picker.lua: the
// faces and the fill arrays are the same bytes, which is the point of the
// port. The said lines they also carried are GONE (PR #307 round 2, R2-1)
// and the FILL rule is corrected by fix B (R2-2).

#include <gtest/gtest.h>

#include <openglad/core/util.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/match_setup_session.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/terminal_menu_model.h>
#include <openglad/resources/campaign_state_providers.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/packs.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/match_stage.h>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

std::string get_asset_path();

namespace {

constexpr short kNone = og::sim::kFillNone;
constexpr short kWeak = og::sim::kFillWeak;
constexpr short kFair = og::sim::kFillFair;
constexpr short kStrong = og::sim::kFillStrong;
constexpr short kBrutal = og::sim::kFillBrutal;

constexpr std::uint8_t kFourSides = 0b1111;

using Fills = std::array<short, 4>;

// A roster slot, the tests/unit/test_lineup_common.cpp fixture shape.
void put(SaveData& save, int slot, short team, bool deployed)
{
    auto member = std::make_unique<guy>(FAMILY_SOLDIER);
    member->name = "F" + std::to_string(slot);
    member->teamnum = team;
    member->deployed = deployed;
    member->level = 1;
    save.team_list[static_cast<std::size_t>(slot)] = std::move(member);
}

og::sim::LobbyPlayer seat(std::uint8_t index, short team)
{
    og::sim::LobbyPlayer player;
    player.player_index = index;
    player.seat_id = index;
    player.machine_id = 1;
    player.company = "IRON KETTLE";
    player.team = team;
    return player;
}

std::string sides(const SaveData& save, int my_team,
                  std::uint8_t mask = kFourSides)
{
    return og::ui::match_sides_face(save, my_team, mask);
}

std::string fill(const SaveData& save, int my_team,
                 std::uint8_t mask = kFourSides)
{
    return og::ui::match_fill_face(save, my_team, mask);
}

void turn_sides(SaveData& save, int my_team,
                std::uint8_t mask = kFourSides)
{
    og::ui::turn_match_sides(save, my_team, mask);
}

void turn_fill(SaveData& save, int my_team, std::uint8_t mask = kFourSides)
{
    og::ui::turn_match_fill(save, my_team, mask);
}

} // namespace

// --- The SIDES / FILL macros (amendment 5 G2-G3, amendment 6 H1-H3) ----
//
// Ported from ModesBookTest::teams_macro_deals_fair_sides_ascending_and_wraps
// (test_modes_book.cpp:1627). The all-NONE rest reads SIDES: 1, a value off
// the 2 -> 3 -> 4 wheel, so the first turn rejoins at the head.
TEST(MatchSetupRules, sides_wheel_deals_fair_ascending_and_wraps)
{
    SaveData save;
    EXPECT_EQ("SIDES: 1", sides(save, 0))
        << "the all-NONE rest: the local side alone";

    struct Step {
        const char* sides_face;
        const char* fill_face;
        Fills fills;
    };
    const std::vector<Step> steps = {
        {"SIDES: 2", "FILL: FAIR", {kNone, kFair, kNone, kNone}},
        {"SIDES: 3", "FILL: FAIR", {kNone, kFair, kFair, kNone}},
        {"SIDES: 4", "FILL: FAIR", {kNone, kFair, kFair, kFair}},
        {"SIDES: 2", "FILL: FAIR", {kNone, kFair, kNone, kNone}},
    };
    for (const Step& step : steps)
    {
        turn_sides(save, 0);
        EXPECT_EQ(step.fills, save.fill) << step.sides_face;
        EXPECT_EQ(step.sides_face, sides(save, 0))
            << "the face derives from the array, never from a second store";
        EXPECT_EQ(step.fill_face, fill(save, 0));
    }
    EXPECT_EQ(kNone, save.fill[0])
        << "a SIDES turn never deals the local band (H1 is FILL's rule)";
}

// Ported from ModesBookTest::teams_macro_copies_the_fill_face_and_fair_on_mixed
// (test_modes_book.cpp:1678).
TEST(MatchSetupRules, sides_copy_the_fill_face_and_fair_on_mixed)
{
    SaveData save;
    save.fill[1] = kStrong;
    EXPECT_EQ("SIDES: 2", sides(save, 0));
    EXPECT_EQ("FILL: STRONG", fill(save, 0));

    turn_sides(save, 0);
    EXPECT_EQ("SIDES: 3", sides(save, 0));
    EXPECT_EQ((Fills{kNone, kStrong, kStrong, kNone}), save.fill)
        << "the new side copies the face, the standing one keeps it";

    // LINEUP diverges a band (G1 keeps that its right): the face reads
    // MIXED, and the next SIDES turn falls back to FAIR for every side it
    // deals, because a diverged pair names nothing a new side could copy.
    save.fill[1] = kWeak;
    EXPECT_EQ("SIDES: 3", sides(save, 0));
    EXPECT_EQ("FILL: MIXED", fill(save, 0));
    turn_sides(save, 0);
    EXPECT_EQ("SIDES: 4", sides(save, 0));
    EXPECT_EQ((Fills{kNone, kFair, kFair, kFair}), save.fill);
}

// Fix B (PR #307 round 2, R2-2; recon2/fill-bug.md §1.3): with NO opponent
// on, a FILL turn lights EVERY authored opponent -- never just the lowest,
// which stranded teams 3 and 4 on a four-side arena the moment the player
// lapped the wheel. NONE is off the wheel too, so the lap that produced the
// trap cannot happen either; a deliberate SIDES value is still respected
// while bands are on.
TEST(MatchSetupRules, fill_lights_every_authored_opponent_when_none_is_on)
{
    SaveData save;
    struct Step {
        const char* fill_face;
        const char* sides_face;
        Fills fills;
    };
    const std::vector<Step> steps = {
        {"FILL: WEAK", "SIDES: 4", {kWeak, kWeak, kWeak, kWeak}},
        {"FILL: FAIR", "SIDES: 4", {kFair, kFair, kFair, kFair}},
        {"FILL: STRONG", "SIDES: 4", {kStrong, kStrong, kStrong, kStrong}},
        {"FILL: BRUTAL", "SIDES: 4", {kBrutal, kBrutal, kBrutal, kBrutal}},
        {"FILL: WEAK", "SIDES: 4", {kWeak, kWeak, kWeak, kWeak}},
    };
    for (const Step& step : steps)
    {
        turn_fill(save, 0);
        EXPECT_EQ(step.fills, save.fill) << step.fill_face;
        EXPECT_EQ(step.fill_face, fill(save, 0));
        EXPECT_EQ(step.sides_face, sides(save, 0))
            << "the wheel never empties the arena, so it never collapses it";
    }

    // With sides standing, the step writes every ON opponent and the own
    // band with them (H1) -- and leaves the sides the player chose alone.
    save.fill = {kNone, kFair, kFair, kNone};
    turn_fill(save, 0);
    EXPECT_EQ((Fills{kStrong, kStrong, kStrong, kNone}), save.fill);
    EXPECT_EQ("SIDES: 3", sides(save, 0))
        << "a deliberate SIDES value is respected while bands are on";

    // A MIXED face is off the wheel and rejoins at the HEAD, WEAK.
    save.fill = {kStrong, kFair, kWeak, kNone};
    EXPECT_EQ("FILL: MIXED", fill(save, 0));
    turn_fill(save, 0);
    EXPECT_EQ((Fills{kWeak, kWeak, kWeak, kNone}), save.fill);
    EXPECT_EQ("SIDES: 3", sides(save, 0));
}

// Ported from ModesBookTest::macros_answer_to_the_local_seats_team
// (test_modes_book.cpp:1790). my_team is an ARGUMENT here — the picker
// binding the Lua read through og.campaign_my_team.
TEST(MatchSetupRules, macros_answer_to_the_local_seats_team)
{
    SaveData save;
    save.fill[2] = kBrutal;
    EXPECT_EQ("SIDES: 1", sides(save, 2))
        << "the own band is not an opponent, however strong it is";
    EXPECT_EQ("FILL: BRUTAL", fill(save, 2));

    turn_sides(save, 2);
    EXPECT_EQ((Fills{kBrutal, kNone, kBrutal, kNone}), save.fill)
        << "team 0 is the lowest opponent of a seat on team 2";
    EXPECT_EQ("SIDES: 2", sides(save, 2));

    // BRUTAL wraps to WEAK over the ON set {0} plus the own band.
    turn_fill(save, 2);
    EXPECT_EQ((Fills{kWeak, kNone, kWeak, kNone}), save.fill);
    EXPECT_EQ("SIDES: 2", sides(save, 2));

    // Nothing on: every authored opponent of a seat on team 2 lights up.
    save.fill = {};
    turn_fill(save, 2);
    EXPECT_EQ((Fills{kWeak, kWeak, kWeak, kWeak}), save.fill);
    EXPECT_EQ("SIDES: 4", sides(save, 2));

    turn_sides(save, 2);
    EXPECT_EQ((Fills{kWeak, kNone, kWeak, kNone}), save.fill)
        << "the {2, 3, 4} wheel wraps 4 -> 2";
    EXPECT_EQ("SIDES: 2", sides(save, 2));
}

// Ported from ModesBookTest::dealt_arena_rest_reads_through_the_macro_faces
// (test_modes_book.cpp:1589): what the arena's own FAIR deal reads as.
TEST(MatchSetupRules, dealt_arena_rest_reads_through_the_faces)
{
    SaveData save;
    save.fill = {kFair, kFair, kNone, kNone};
    EXPECT_EQ("SIDES: 2", sides(save, 0))
        << "the own band and one dealt opponent: two sides";
    EXPECT_EQ("FILL: FAIR", fill(save, 0))
        << "every non-NONE band holds FAIR, own included (H2)";

    save.fill = {kFair, kFair, kFair, kFair};
    EXPECT_EQ("SIDES: 4", sides(save, 0));
    EXPECT_EQ("FILL: FAIR", fill(save, 0));
}

// --- The TIME LIMIT and SCORE wheels -----------------------------------

TEST(MatchSetupRules, time_limit_wheel_faces)
{
    SaveData save;
    EXPECT_EQ("TIME LIMIT: MAP", og::ui::format_time_limit_label(save));

    struct Step {
        short ticks;
        const char* face;
    };
    const std::vector<Step> steps = {
        {3600, "TIME LIMIT: 5 MIN"},
        {7200, "TIME LIMIT: 10 MIN"},
        {10800, "TIME LIMIT: 15 MIN"},
        {14400, "TIME LIMIT: 20 MIN"},
        {0, "TIME LIMIT: MAP"},
    };
    for (const Step& step : steps)
    {
        og::ui::cycle_time_limit(save);
        EXPECT_EQ(step.ticks, save.time_limit);
        EXPECT_EQ(step.face, og::ui::format_time_limit_label(save));
    }

    // A clock settled from a lobby or an older save wears the minutes it
    // holds (integer division, the determinism contract) and rejoins the
    // wheel at its head.
    save.time_limit = 2160;
    EXPECT_EQ("TIME LIMIT: 3 MIN", og::ui::format_time_limit_label(save));
    save.time_limit = 5400;
    EXPECT_EQ("TIME LIMIT: 7 MIN", og::ui::format_time_limit_label(save));
    og::ui::cycle_time_limit(save);
    EXPECT_EQ(0, save.time_limit);
}

TEST(MatchSetupRules, score_wheel_laps_forward)
{
    SaveData save;
    // Five stops, forward only: a full lap is what an overshoot costs.
    const std::vector<short> forward = {1, 3, 5, 10, 0};
    for (const short expected : forward)
    {
        og::ui::cycle_ctf_capture_limit(save);
        EXPECT_EQ(expected, save.ctf_capture_limit);
    }

    // Junk rejoins at the head.
    save.ctf_capture_limit = 42;
    og::ui::cycle_ctf_capture_limit(save);
    EXPECT_EQ(0, save.ctf_capture_limit);
}

// The clamp the Lua could not make (F6): campaign_picker.lua's opponents()
// looped 0..3 with no idea what the arena authored, so a two-side map
// offered TEAMS: 3 and dealt a squad to a team with no start markers. The
// wheel runs 2..N over the AUTHORED mask instead, and the opponents are
// the authored teams alone.
TEST(MatchSetupRules, sides_wheel_is_clamped_to_the_authored_sides)
{
    EXPECT_EQ(2, og::ui::match_sides_count(0b0011));
    EXPECT_EQ("2", og::ui::match_sides_note(0b0011));
    EXPECT_EQ(3, og::ui::match_sides_count(0b0111));
    EXPECT_EQ("2, 3", og::ui::match_sides_note(0b0111));
    EXPECT_EQ(4, og::ui::match_sides_count(0b1111));
    EXPECT_EQ("2, 3, 4", og::ui::match_sides_note(0b1111));
    EXPECT_EQ(4, og::ui::match_sides_count(0))
        << "nothing loaded yet: the four-team as-built wheel, never a "
           "narrower one";
    EXPECT_EQ("2, 3, 4", og::ui::match_sides_note(0));

    // A two-side arena has exactly one legal value, so every turn lands on
    // it and team 2 is never dealt anything.
    SaveData two;
    turn_sides(two, 0, 0b0011);
    EXPECT_EQ((Fills{kNone, kFair, kNone, kNone}), two.fill);
    turn_sides(two, 0, 0b0011);
    EXPECT_EQ((Fills{kNone, kFair, kNone, kNone}), two.fill);
    EXPECT_EQ("SIDES: 2", sides(two, 0, 0b0011));

    // A three-side arena wheels 2 -> 3 -> 2.
    SaveData three;
    turn_sides(three, 0, 0b0111);
    EXPECT_EQ((Fills{kNone, kFair, kNone, kNone}), three.fill);
    turn_sides(three, 0, 0b0111);
    EXPECT_EQ((Fills{kNone, kFair, kFair, kNone}), three.fill);
    turn_sides(three, 0, 0b0111);
    EXPECT_EQ((Fills{kNone, kFair, kNone, kNone}), three.fill);

    // A gap in the mask: the opponents of a seat on team 0 are 1 then 3,
    // and team 2 — which the map authors no markers for — is never written.
    SaveData gap;
    gap.fill[2] = kBrutal;  // a stale value the macro must leave alone
    turn_sides(gap, 0, 0b1011);
    turn_sides(gap, 0, 0b1011);
    EXPECT_EQ((Fills{kNone, kFair, kBrutal, kFair}), gap.fill);

    // A mask of 0 behaves exactly as the four-side one.
    SaveData unknown;
    SaveData four;
    for (int step = 0; step < 5; ++step)
    {
        turn_sides(four, 0, 0b1111);
        turn_sides(unknown, 0, 0);
        EXPECT_EQ(four.fill, unknown.fill);
    }
}

// --- The RULES faces and their packed lines (§2.5, §2.6) ---------------

TEST(MatchSetupRules, rules_faces_and_lines_pack_two_per_line)
{
    SaveData save;
    og::ui::MatchRulesInputs inputs;
    inputs.save = &save;
    inputs.difficulty = 1;

    EXPECT_TRUE(og::ui::match_rules_faces({}).empty())
        << "no save, no rules: the composer never invents a face";
    EXPECT_TRUE(og::ui::format_match_rules_lines(og::ui::MatchRulesInputs{})
                    .empty());
    EXPECT_TRUE(og::ui::format_match_rules_lines(
                    std::vector<og::ui::MatchRuleFace>{})
                    .empty());

    const std::vector<og::ui::MatchRuleFace> faces =
        og::ui::match_rules_faces(inputs);
    const std::vector<std::string_view> expected_ids = {
        og::ui::kRulesRowScore,      og::ui::kRulesRowTime,
        og::ui::kRulesRowRespawns,   og::ui::kRulesRowSpawnDelay,
        og::ui::kRulesRowPermadeath, og::ui::kRulesRowGenerators,
        og::ui::kRulesRowDifficulty, og::ui::kRulesRowInfiniteGold};
    const std::vector<std::string> expected_faces = {
        "SCORE: MAP",         "TIME LIMIT: MAP",
        "RESPAWNS: OFF",      "SPAWN DELAY: NORMAL",
        "PERMADEATH: ON",     "GENERATORS: NORMAL",
        "DIFFICULTY: BATTLE", "INFINITE GOLD: OFF"};
    ASSERT_EQ(expected_ids.size(), faces.size());
    for (std::size_t i = 0; i < faces.size(); ++i)
    {
        EXPECT_EQ(expected_ids[i], faces[i].id) << "row " << i;
        EXPECT_EQ(expected_faces[i], faces[i].face) << "row " << i;
    }

    EXPECT_EQ((std::vector<std::string>{"SCORE: MAP  TIME LIMIT: MAP",
                                        "RESPAWNS: OFF  SPAWN DELAY: NORMAL",
                                        "PERMADEATH: ON  GENERATORS: NORMAL",
                                        "DIFFICULTY: BATTLE  "
                                        "INFINITE GOLD: OFF"}),
              og::ui::format_match_rules_lines(inputs));

    // CROSS CONTROL is the networked-only row, and an odd face count
    // leaves the last one alone on its line.
    inputs.networked = true;
    const std::vector<std::string> networked =
        og::ui::format_match_rules_lines(inputs);
    ASSERT_EQ(5u, networked.size());
    EXPECT_EQ("CROSS CONTROL: OWN", networked.back());
    EXPECT_EQ(og::ui::kRulesRowCrossControl,
              og::ui::match_rules_faces(inputs).back().id);
    inputs.networked = false;

    // A knob the campaign hides drops its face and the rest REPACK: there
    // is no blank half-line where a row used to be.
    inputs.show_score = false;
    EXPECT_EQ("TIME LIMIT: MAP  RESPAWNS: OFF",
              og::ui::format_match_rules_lines(inputs).front());
    EXPECT_EQ(og::ui::kRulesRowTime, og::ui::match_rules_faces(inputs).front().id);
    inputs.show_score = true;

    // Every value of every knob, against the two budgets §2.8 sets: a
    // packed line fits 48 and a single face fits the 42-glyph row.
    std::string worst_pair;
    for (const short respawn : {short(0), short(1), short(2), short(3)})
    for (const short ticks : {short(0), short(60), short(360)})
    for (const short keep : {short(0), short(1)})
    for (const short rate : {short(0), short(50), short(200)})
    for (const short gold : {short(0), short(1)})
    for (const int difficulty : {0, 1, 2})
    for (const short score : {short(0), short(1), short(3), short(5), short(10)})
    for (const short clock : {short(0), short(3600), short(7200), short(10800),
                              short(14400)})
    for (const short cross : {short(0), short(1)})
    {
        save.respawn_mode = respawn;
        save.ctf_respawn_ticks = ticks;
        save.keep_fallen_heroes = keep;
        save.generator_rate = rate;
        save.infinite_gold = gold;
        save.ctf_capture_limit = score;
        save.time_limit = clock;
        save.cross_control = cross;
        inputs.difficulty = difficulty;
        for (const bool net : {false, true})
        {
            inputs.networked = net;
            for (const og::ui::MatchRuleFace& face :
                 og::ui::match_rules_faces(inputs))
            {
                EXPECT_LE(face.face.size(), 42u) << face.face;
            }
            for (const std::string& line :
                 og::ui::format_match_rules_lines(inputs))
            {
                EXPECT_LE(line.size(), 48u) << line;
                if (line.size() > worst_pair.size())
                    worst_pair = line;
            }
        }
    }
    EXPECT_EQ("RESPAWNS: TEAM 1 HEROES  SPAWN DELAY: NORMAL", worst_pair)
        << "the widest pair the RULES recap can print (44)";

    // One packing loop, two entry points: the MatchRulesInputs overload IS
    // the faces overload asked for match_rules_faces(inputs), and a
    // caller that has already filtered the faces (the wizard's joiner
    // RULES) gets the same packer.
    SaveData plain;
    og::ui::MatchRulesInputs fresh;
    fresh.save = &plain;
    fresh.difficulty = 1;
    EXPECT_EQ(og::ui::format_match_rules_lines(fresh),
              og::ui::format_match_rules_lines(
                  og::ui::match_rules_faces(fresh)));

    std::vector<og::ui::MatchRuleFace> two;
    for (const og::ui::MatchRuleFace& face : og::ui::match_rules_faces(fresh))
    {
        if (face.id == og::ui::kRulesRowScore ||
            face.id == og::ui::kRulesRowTime)
        {
            two.push_back(face);
        }
    }
    ASSERT_EQ(2u, two.size());
    EXPECT_EQ((std::vector<std::string>{"SCORE: MAP  TIME LIMIT: MAP"}),
              og::ui::format_match_rules_lines(two));
}

// --- The TEAMS step's team-line cells (§2.4) ---------------------------

TEST(MatchSetupRules, compose_setup_team_line_cells_fit_their_columns)
{
    const std::vector<std::string> shapes = {"P1 WASD",  "P2 ARROWS",
                                             "P3 IJKL",  "P4 TFGH",
                                             "P1 JOY1",  "P2 IRO"};
    // Every whole token any label is built from: a cell that cuts a label
    // mid-word would produce a token outside this set.
    std::set<std::string> whole_tokens;
    for (const std::string& label : shapes)
    {
        std::size_t start = 0;
        while (start <= label.size())
        {
            const std::size_t space = label.find(' ', start);
            whole_tokens.insert(label.substr(
                start, space == std::string::npos ? std::string::npos
                                                  : space - start));
            if (space == std::string::npos)
                break;
            start = space + 1;
        }
    }

    for (int seats = 1; seats <= 4; ++seats)
    for (std::size_t offset = 0; offset + static_cast<std::size_t>(seats) <=
                                 shapes.size(); ++offset)
    for (const auto diag : {og::ui::LineupTeamBand::Diag::None,
                            og::ui::LineupTeamBand::Diag::NeedsFighters,
                            og::ui::LineupTeamBand::Diag::NoSeatAi})
    for (int team = 0; team < 4; ++team)
    {
        og::ui::LineupTeamBand band;
        band.team = team;
        band.seat_count = seats;
        band.has_seat = true;
        band.fighter_count = 2;
        band.needs = 1;
        band.diag = diag;
        for (int i = 0; i < seats; ++i)
            band.seat_labels.push_back(
                shapes[offset + static_cast<std::size_t>(i)]);

        const og::ui::SetupTeamLineCells cells =
            og::ui::compose_setup_team_line(band, nullptr, team, 18, 20);
        EXPECT_EQ("TEAM " + std::to_string(team + 1), cells.label);
        EXPECT_LE(cells.seats.size(), 18u) << cells.seats;
        EXPECT_LE(cells.census.size(), 20u) << cells.census;
        EXPECT_EQ(diag != og::ui::LineupTeamBand::Diag::None, cells.diag);

        std::size_t start = 0;
        while (start <= cells.seats.size())
        {
            const std::size_t space = cells.seats.find(' ', start);
            const std::string token = cells.seats.substr(
                start, space == std::string::npos ? std::string::npos
                                                  : space - start);
            EXPECT_TRUE(whole_tokens.count(token) == 1 || token.empty() ||
                        token[0] == '+')
                << "'" << token << "' in '" << cells.seats
                << "' is not a whole token";
            if (space == std::string::npos)
                break;
            start = space + 1;
        }
    }

    // A team index off the board falls back to the band's own census
    // rather than reading past the report's four slots.
    og::ui::LineupTeamBand stray;
    stray.fighter_count = 2;
    og::ui::ScenarioRosterReport staged;
    staged.staged = true;
    staged.mode_census = true;
    staged.team_active = {true, true, true, true};
    EXPECT_EQ("2 FIGHTERS", og::ui::format_match_preview(stray, &staged, 7));
    EXPECT_EQ("2 FIGHTERS", og::ui::format_match_preview(stray, &staged, -1));

    // No seat, no run: the callers spell their own NO SEAT.
    og::ui::LineupTeamBand bench;
    bench.team = 2;
    bench.fighter_count = 3;
    bench.diag = og::ui::LineupTeamBand::Diag::NoSeatAi;
    const og::ui::SetupTeamLineCells cells =
        og::ui::compose_setup_team_line(bench, nullptr, 2, 18, 20);
    EXPECT_EQ("TEAM 3", cells.label);
    EXPECT_EQ("", cells.seats);
    EXPECT_EQ("NO SEAT: AI", cells.census);
    EXPECT_TRUE(cells.diag);
}

// --- The M4 GO predicate (§2.6) ----------------------------------------
//
// LINEUP's per-band "NEEDS k FIGHTERS" is documented as the informational
// decomposition of the ONE refusal the strip GO pops. The agreement is
// PINNED here rather than refactored: two readings of the same roster,
// asserted equal over a seat x deploy matrix.
TEST(MatchSetupRules, local_seats_deployed_for_go_agrees_with_the_lineup_diagnostic)
{
    EXPECT_EQ("DEPLOY FOR EVERY PLAYER",
              std::string(og::ui::kDeployForEveryPlayerTitle));
    EXPECT_EQ("The host sets these for everyone.",
              std::string(og::ui::kHostSetsForEveryoneCaption));
    EXPECT_EQ("weak to brutal", std::string(og::ui::kMatchFillNote))
        << "the MACRO wheel: NONE left it with the trap it opened";
    EXPECT_EQ("none to brutal", std::string(og::ui::kLineupFillNote))
        << "the LINEUP band wheel keeps NONE, and its own note";

    int agreed = 0;
    for (int numplayers = 1; numplayers <= 4; ++numplayers)
    for (int seat_span = 1; seat_span <= 4; ++seat_span)
    for (int deployed0 = 0; deployed0 <= 2; ++deployed0)
    for (int deployed1 = 0; deployed1 <= 2; ++deployed1)
    {
        SaveData save;
        save.numplayers = static_cast<unsigned char>(numplayers);
        std::vector<og::sim::LobbyPlayer> players;
        for (int i = 0; i < numplayers; ++i)
        {
            players.push_back(seat(static_cast<std::uint8_t>(i),
                                   static_cast<short>(i % seat_span)));
        }
        int slot = 0;
        for (int i = 0; i < deployed0; ++i)
            put(save, slot++, 0, true);
        for (int i = 0; i < deployed1; ++i)
            put(save, slot++, 1, true);
        put(save, slot++, 0, false);  // benched: never a control

        const bool ok =
            og::ui::local_seats_deployed_for_go(save, players, false);
        const std::array<og::ui::LineupTeamBand, 4> bands =
            og::ui::build_lineup_bands(save, players, {}, false, {});
        const bool needs_fighters =
            std::any_of(bands.begin(), bands.end(),
                        [](const og::ui::LineupTeamBand& band) {
                            return band.diag ==
                                   og::ui::LineupTeamBand::Diag::NeedsFighters;
                        });
        EXPECT_EQ(!ok, needs_fighters)
            << numplayers << " seats over " << seat_span << " teams, "
            << deployed0 << "/" << deployed1 << " deployed";
        ++agreed;

        EXPECT_TRUE(og::ui::local_seats_deployed_for_go(save, players, true))
            << "a networked machine answers for its own seats";

        // A lobby that has not produced one seat per player yet: the
        // legacy save fields seed the narrow fallback.
        EXPECT_EQ(og::ui::local_seat_teams_have_controls(
                      save, og::ui::derive_local_gameplay_seat_teams(save)),
                  og::ui::local_seats_deployed_for_go(save, {}, false))
            << "the seedless fallback, " << numplayers << " seats";

        save.numplayers = 0;
        EXPECT_TRUE(og::ui::local_seats_deployed_for_go(save, players, false))
            << "a spectator save seats nobody";
    }
    EXPECT_EQ(4 * 4 * 3 * 3, agreed);
}

// ===========================================================================
// The STEP MACHINE half: MatchSetupSession itself (docs/match-setup-design.md
// §3.1). Suite MatchSetupSession drives a SYNTHETIC campaign book so every
// arm is exercised without waiting on any shipped campaign's Lua; suite
// ModesSetupSession pins the shipped `modes` book (red until WP6 lands its
// campaign_picker.lua root + match_knobs).
// ===========================================================================

namespace {

using og::ui::MatchSetupSession;
using Step = MatchSetupSession::Step;
using Kind = MatchSetupSession::OutcomeKind;
using Extra = MatchSetupSession::Row::Extra;
using Knob = MatchSetupSession::Row::Knob;
using Health = og::ui::IPickerLobbyClient::StagedPreviewHealth;

// Chunk names deliberately do NOT start with `packs/`: that prefix declares
// the bytes to the pack-Lua coverage inventory, and these throwaway chunks
// exist nowhere in the repository (the test_classpack_lua_decl discipline).
constexpr const char* kSetupPack = "test.matchsetup";

// The synthetic book: a GAMES root of seven page rows, a four-arena SOCCER
// page, a TEN-arena CTF page (the paging case) and a one-arena TDM page.
// `knobs` is spliced in verbatim, or the hook is left off entirely when it
// is empty.
std::string setup_book_script(const std::string& knobs)
{
    std::string source = R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    if page_id == "" then
      local rows = {
        { id = "tdm",        label = "TEAM DEATHMATCH",  note = "6 arenas",  kind = "page" },
        { id = "ctf",        label = "CAPTURE THE FLAG", note = "10 arenas", kind = "page" },
        { id = "onslaught",  label = "ONSLAUGHT",        note = "4 arenas",  kind = "page" },
        { id = "mutant",     label = "MUTANT",           note = "4 arenas",  kind = "page" },
        { id = "soccer",     label = "SOCCER",           note = "4 arenas",  kind = "page" },
        { id = "basketball", label = "BASKETBALL",       note = "6 arenas",  kind = "page" },
        { id = "ffa",        label = "FREE FOR ALL",     note = "6 arenas",  kind = "page" },
      }
      if og.campaign_is_host() then
        rows[#rows + 1] = { id = "random", kind = "action", label = "RANDOM",
                            note = "any game, any arena" }
      end
      return { title = "GAMES", lines = {}, entries = rows }
    end
    if page_id == "soccer" then
      local rows = {
        { id = "820", label = "THE PITCH",    note = "2 sides, 3 goals", kind = "level", level = 820 },
        { id = "821", label = "THE MUDBOWL",  note = "2 sides, 3 goals", kind = "level", level = 821, replay = true },
        { id = "822", label = "FOURSQUARE",   note = "4 sides, 3 goals", kind = "level", level = 822 },
        { id = "823", label = "BONEYARD CUP", note = "2 sides, 3 goals", kind = "level", level = 823 },
      }
      if og.campaign_is_host() then
        rows[#rows + 1] = { id = "random_soccer", kind = "action",
                            label = "RANDOM ARENA", note = "any arena of this game" }
      end
      return { title = "SOCCER",
               lines = { "Kick the ball into their goal." },
               entries = rows }
    end
    if page_id == "ctf" then
      local rows = {}
      for i = 0, 9 do
        rows[#rows + 1] = { id = tostring(500 + i), label = "FIELD " .. i,
                            note = "4 sides, 20 min", kind = "level", level = 500 + i }
      end
      return { title = "CAPTURE THE FLAG",
               lines = { "Take their flag home." },
               entries = rows }
    end
    if page_id == "tdm" then
      return { title = "TEAM DEATHMATCH", entries = {
        { id = "300", label = "THE CIRCLE", note = "4 sides, to 20", kind = "level", level = 300 },
      } }
    end
    return nil
  end,
  picker_action = function(id)
    -- Deterministic by construction: the wizard's RANDOM rows answer a
    -- LEVEL, and this fixture answers a fixed one so the title the gated
    -- tail speaks can be pinned byte for byte.
    if id == "random" then return { level = 822 } end
    if id == "random_soccer" then return { level = 823 } end
    return nil
  end,
)LUA";
    if (!knobs.empty())
    {
        source += "  match_knobs = function()\n    return " + knobs +
                  "\n  end,\n";
    }
    source += "})";
    return source;
}

// A book-less versus campaign still answers match_knobs: the hook alone is a
// legal registration, so the ARENA step is the mount's manifest under four
// steps.
std::string setup_knobs_only_script(const std::string& knobs)
{
    return "og.register_campaign_hooks({\n  match_knobs = function()\n"
           "    return " + knobs + "\n  end,\n})";
}

class MatchSetupSessionTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        previous_game_ = current_game;
        current_game = nullptr;  // dispatch resolves the shared UI VM
        og::script::clear_pack_scripts();
        og::script::clear_pack_family_chunks();
        og::script::clear_pack_lib_modules();
        og::script::hooks::clear_campaign_providers();
        restore_default_campaigns();
        previous_mount_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("modes"))
            << "builtin/modes.glad should restore and mount";
        // Scripts only: the synthetic book below IS the campaign for these
        // tests, so the shipped modes book must not answer over it.
        og::script::unregister_pack_scripts("modes.core");
        save_.current_campaign = "modes";
        save_.my_team = 0;
        save_.scen_num = 300;
        save_.completed_levels.clear();
        save_.fill = {};
        og::script::hooks::install_campaign_providers(
            og::data::make_campaign_providers(save_));
    }

    void TearDown() override
    {
        og::script::hooks::clear_campaign_providers();
        og::script::clear_pack_scripts();
        og::script::clear_pack_family_chunks();
        og::script::clear_pack_lib_modules();
        (void)unmount_campaign_package_with_error("modes");
        if (!previous_mount_.empty() && previous_mount_ != "modes")
            (void)mount_campaign_package_with_error(previous_mount_);
        current_game = previous_game_;
    }

    void register_book(const std::string& knobs)
    {
        og::script::register_pack_script(
            {kSetupPack, "matchsetup/scripts/book.lua",
             setup_book_script(knobs)});
    }

    void register_knobs_only(const std::string& knobs)
    {
        og::script::register_pack_script(
            {kSetupPack, "matchsetup/scripts/knobs.lua",
             setup_knobs_only_script(knobs)});
    }

    // The per-call Inputs. The spans point at members, so the struct stays
    // valid for as long as the fixture does.
    MatchSetupSession::Inputs inputs(bool host = true,
                                     std::uint8_t mask = 0b0011)
    {
        players_ = og::ui::synthesize_local_lobby_players(save_);
        local_indices_.clear();
        for (const og::sim::LobbyPlayer& player : players_)
            local_indices_.push_back(player.player_index);
        MatchSetupSession::Inputs in;
        in.save = &save_;
        in.is_host = host;
        in.networked = networked_;
        in.my_team = save_.my_team;
        in.session_difficulty = difficulty_;
        in.authored_mask = mask;
        in.players = players_;
        in.local_indices = local_indices_;
        in.map_unit_counts = counts_;
        // The seat cell names the CONTROLLER, the way LINEUP's band header
        // does ("P1 WASD"): the SDL client passes
        // local_seat_owner_short_name and the terminals their own, and
        // without a callback the cell falls back to the company name
        // clipped to three glyphs — which is exactly how the wizard came
        // to say "P1 IRO" where LINEUP said "P1 WASD".
        in.seat_short_name = seat_short_name_;
        in.staged = staged_;
        in.staged_health = health_;
        return in;
    }

    // Find a row by id; -1 when the step does not carry it.
    static int row_index(const MatchSetupSession& session, std::string_view id)
    {
        const std::vector<MatchSetupSession::Row>& rows = session.page().rows;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].base.id == id)
                return static_cast<int>(i);
        }
        return -1;
    }

    static std::vector<std::string> row_ids(const MatchSetupSession& session)
    {
        std::vector<std::string> ids;
        for (const MatchSetupSession::Row& row : session.page().rows)
            ids.push_back(row.base.id);
        return ids;
    }

    SaveData save_;
    std::vector<og::sim::LobbyPlayer> players_;
    std::vector<std::uint8_t> local_indices_;
    std::array<int, 4> counts_{};
    const og::ui::ScenarioRosterReport* staged_ = nullptr;
    Health health_ = Health::None;
    int difficulty_ = 0;
    bool networked_ = false;
    std::function<std::string(std::uint8_t)> seat_short_name_;
    std::string previous_mount_;
    GameplayContext* previous_game_ = nullptr;
};

// The soccer knobs the shipped campaign will answer for a ball arena.
constexpr const char* kSoccerKnobs =
    R"({ arena_page = "soccer", deal = "strong",
         lines = { "STRONG adds a fighter, BRUTAL two." } })";

}  // namespace

// 1. The classic guard: no player path reaches it any more -- the SDL door
// is the versus docket's own row and the terminal door is the versus
// camp's (R2-D11) -- so the refusal is the last line of defence, and it is
// silent (the driver traces it; see the terminal leg below).
TEST_F(MatchSetupSessionTest, open_refuses_a_classic_campaign)
{
    register_book(kSoccerKnobs);
    save_.current_campaign = "gladiator";
    MatchSetupSession session(save_);
    EXPECT_FALSE(session.open(inputs()))
        << "a campaign with no matchup has no arena to set up";
}

// 2. The book decides whether there is a GAME step at all.
TEST_F(MatchSetupSessionTest, steps_are_five_with_a_book_and_four_without)
{
    register_book(kSoccerKnobs);
    MatchSetupSession hosted(save_);
    ASSERT_TRUE(hosted.open(inputs()));
    const std::vector<Step> five(hosted.steps().begin(), hosted.steps().end());
    EXPECT_EQ((std::vector<Step>{Step::Game, Step::Arena, Step::Teams,
                                 Step::Rules, Step::Match}),
              five);
    EXPECT_EQ(Step::Game, hosted.step());
    EXPECT_EQ("SETUP: GAME", hosted.page().title);

    og::script::clear_pack_scripts();
    register_knobs_only(kSoccerKnobs);
    MatchSetupSession bookless(save_);
    ASSERT_TRUE(bookless.open(inputs()));
    const std::vector<Step> four(bookless.steps().begin(),
                                 bookless.steps().end());
    EXPECT_EQ((std::vector<Step>{Step::Arena, Step::Teams, Step::Rules,
                                 Step::Match}),
              four);
    EXPECT_EQ(Step::Arena, bookless.step());
}

// 3. The docket's ARENA: shortcut. Only a root row of Kind::Page descends;
// the "games" alias and an unknown id stay on the root, which IS the same
// content the GAME step shows.
TEST_F(MatchSetupSessionTest,
       open_entry_page_lands_on_the_arena_step_only_for_a_root_page_row)
{
    register_book(kSoccerKnobs);
    for (const std::string& entry : {std::string(), std::string("games"),
                                     std::string("nowhere")}) {
        MatchSetupSession session(save_);
        ASSERT_TRUE(session.open(inputs(), entry)) << entry;
        EXPECT_EQ(Step::Game, session.step()) << entry;
    }

    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs(), "soccer"));
    EXPECT_EQ(Step::Arena, session.step());
    ASSERT_EQ(5u, session.page().rows.size())
        << "four arenas and the host's RANDOM ARENA row, appended LAST";
    EXPECT_EQ(820, session.page().rows[0].base.level);
    EXPECT_EQ("THE PITCH", session.page().rows[0].base.label);
}

// 4. The F1/F24 fix: every entry to ARENA resolves match_knobs.arena_page and
// opens the WINDOW that holds the current arena, so the host never sees a
// page of green rows none of which is theirs.
TEST_F(MatchSetupSessionTest,
       arena_tab_from_game_descends_into_arena_page_and_opens_on_the_current_window)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 820;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    ASSERT_EQ(Step::Game, session.step());
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Arena, inputs()).kind);
    EXPECT_EQ(Step::Arena, session.step());
    ASSERT_EQ(5u, session.page().rows.size())
        << "four arenas and the host's RANDOM ARENA row, appended LAST";
    EXPECT_TRUE(session.page().rows[0].base.current) << "[CURRENT] on THE PITCH";
    EXPECT_EQ(0, session.page().page.page);
    EXPECT_EQ((std::vector<std::string>{"Kick the ball into their goal."}),
              session.page().lines)
        << "one flavour line: the \"Next uncleared\" line left with the "
           "progress vocabulary (R2-4)";

    // From TEAMS/RULES/MATCH the tab resolves the same way.
    for (const Step from : {Step::Teams, Step::Rules, Step::Match}) {
        ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Game, inputs()).kind);
        ASSERT_EQ(Kind::Advanced, session.goto_step(from, inputs()).kind);
        ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Arena, inputs()).kind);
        EXPECT_EQ(820, session.page().rows[0].base.level) << "from a later step";
    }

    // From a page the player browsed to on purpose, the tab is a no-op
    // refetch: it does not drag them back to the campaign's own page.
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Game, inputs()).kind);
    ASSERT_EQ(Kind::Advanced, session.choose(1, inputs()).kind)
        << "row 1 is the CTF page";
    EXPECT_EQ(Step::Arena, session.step());
    ASSERT_EQ(10u, session.page().rows.size());
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Arena, inputs()).kind);
    EXPECT_EQ(10u, session.page().rows.size()) << "a no-op refetch";

    // Ten CTF arenas over ONE line of text (the "Next uncleared" line left
    // with the progress vocabulary, R2-4): eight rows fit, and the window
    // opens on the page that holds the cursor's own arena (508 -> page 1).
    og::script::clear_pack_scripts();
    register_book(R"({ arena_page = "ctf" })");
    save_.scen_num = 508;
    MatchSetupSession ctf(save_);
    ASSERT_TRUE(ctf.open(inputs()));
    ASSERT_EQ(Kind::Advanced, ctf.goto_step(Step::Arena, inputs()).kind);
    ASSERT_EQ(10u, ctf.page().rows.size());
    EXPECT_EQ(8, og::ui::setup_rows_fit(1));
    EXPECT_EQ(8, ctf.page().page.rows_per_page);
    EXPECT_EQ(1, ctf.page().page.page);
    EXPECT_TRUE(ctf.page().page.multi_page());
}

// 5. R4: an arena_page that names no root page row lists the mount's own
// manifest instead of leaving the step empty, and says so in a trace.
TEST_F(MatchSetupSessionTest, unresolved_arena_page_lists_the_manifest_with_its_trace)
{
    register_book(R"({ arena_page = "nowhere" })");
    save_.scen_num = 300;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Arena, inputs()).kind);
    // The `setup arena_page_unresolved` TRACE rides the same branch. A
    // headless unit group links og_game, which is compiled WITHOUT TESTING,
    // so TRACE is a no-op here and trace_contains has nothing to read; the
    // trace itself is pinned from the SDL wizard flow, where the sources are
    // TESTING-compiled. What is pinned HERE is the consequence: the manifest.

    const std::vector<int> manifest = list_levels_v();
    ASSERT_FALSE(manifest.empty());
    ASSERT_EQ(manifest.size(), session.page().rows.size());
    for (std::size_t i = 0; i < manifest.size(); ++i) {
        EXPECT_EQ(manifest[i], session.page().rows[i].base.level) << i;
        EXPECT_EQ(std::to_string(manifest[i]), session.page().rows[i].base.id);
        EXPECT_EQ(og::data::load_scenario_title(
                      ("scen" + std::to_string(manifest[i])).c_str()),
                  session.page().rows[i].base.label)
            << manifest[i];
    }
    // The save's own decoration rides the manifest rows.
    const int cursor = row_index(session, "300");
    ASSERT_GE(cursor, 0);
    EXPECT_TRUE(session.page().rows[static_cast<std::size_t>(cursor)].base.current);
}

// 6. R4's other half: a versus campaign with no book at all.
TEST_F(MatchSetupSessionTest, bookless_versus_campaign_lists_the_manifest_on_a_four_step_wizard)
{
    register_knobs_only("{}");
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    EXPECT_EQ(4u, session.steps().size());
    EXPECT_EQ(Step::Arena, session.step());
    EXPECT_EQ(list_levels_v().size(), session.page().rows.size());
    EXPECT_FALSE(session.page().can_prev) << "ARENA is the first step here";
}

// 7. The step order, including NEXT-on-GAME's "keep what is set".
TEST_F(MatchSetupSessionTest, next_and_prev_walk_the_steps_and_game_next_keeps_the_arena)
{
    register_book(kSoccerKnobs);
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    EXPECT_FALSE(session.page().can_prev) << "no PREV on the first step";
    EXPECT_EQ(Step::Teams, session.next_step());
    ASSERT_EQ(Kind::Advanced, session.next(inputs()).kind);
    EXPECT_EQ(Step::Teams, session.step()) << "NEXT on GAME skips ARENA";
    ASSERT_EQ(Kind::Advanced, session.next(inputs()).kind);
    EXPECT_EQ(Step::Rules, session.step());
    ASSERT_EQ(Kind::Advanced, session.next(inputs()).kind);
    EXPECT_EQ(Step::Match, session.step());
    EXPECT_FALSE(session.page().can_next);
    EXPECT_EQ(Kind::Stayed, session.next(inputs()).kind);
    ASSERT_EQ(Kind::Advanced, session.prev(inputs()).kind);
    EXPECT_EQ(Step::Rules, session.step());
    ASSERT_EQ(Kind::Advanced, session.prev(inputs()).kind);
    EXPECT_EQ(Step::Teams, session.step());
    ASSERT_EQ(Kind::Advanced, session.prev(inputs()).kind);
    EXPECT_EQ(Step::Arena, session.step());
    EXPECT_EQ(Step::Teams, session.next_step()) << "ARENA NEXT keeps the arena";
    ASSERT_EQ(Kind::Advanced, session.prev(inputs()).kind);
    EXPECT_EQ(Step::Game, session.step()) << "ARENA PREV pops to the root";

    og::script::clear_pack_scripts();
    register_knobs_only("{}");
    MatchSetupSession bookless(save_);
    ASSERT_TRUE(bookless.open(inputs()));
    EXPECT_EQ(Kind::Stayed, bookless.prev(inputs()).kind);
    EXPECT_EQ(Step::Arena, bookless.step());
}

// 8. The CampaignPickerSession contract: a level row answers SetLevel and
// writes NOTHING; the renderer's own gated tail decides, and only
// level_applied advances the wizard.
TEST_F(MatchSetupSessionTest,
       level_rows_answer_set_level_without_writing_and_refusals_never_advance)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 300;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs(), "soccer"));
    const MatchSetupSession::Outcome answer = session.choose(0, inputs());
    EXPECT_EQ(Kind::SetLevel, answer.kind);
    EXPECT_EQ(820, answer.level);
    EXPECT_FALSE(answer.replay_arm);
    EXPECT_EQ(300, static_cast<int>(save_.scen_num)) << "the session never writes";
    EXPECT_EQ(Step::Arena, session.step()) << "a refused tail must not advance";

    save_.scen_num = 820;
    session.level_applied(inputs());
    EXPECT_EQ(Step::Teams, session.step());

    // A joiner clicking a level row still ANSWERS SetLevel — the gate is the
    // renderer's, not the session's.
    MatchSetupSession joiner(save_);
    ASSERT_TRUE(joiner.open(inputs(false), "soccer"));
    EXPECT_EQ(Kind::SetLevel, joiner.choose(1, inputs(false)).kind);
    EXPECT_EQ(Step::Arena, joiner.step());

    // An out-of-range row is a no-op.
    EXPECT_EQ(Kind::Stayed, joiner.choose(99, inputs(false)).kind);
}

// 8b. The seat cell names the CONTROLLER. Every client hands the session a
// seat_short_name callback — the SDL one is local_seat_owner_short_name,
// the very function LINEUP's band header calls — and without it the cell
// falls back to the band's company abbreviation. That fallback is what put
// "P1 IRO" on the wizard's TEAMS line while LINEUP, one door away, read
// "P1 WASD" about the same seat.
TEST_F(MatchSetupSessionTest, the_seat_cell_names_the_controller_not_the_company)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 820;
    save_.numplayers = 1;
    save_.save_name = "IRON KETTLE";

    MatchSetupSession bare(save_);
    ASSERT_TRUE(bare.open(inputs()));
    ASSERT_EQ(Kind::Advanced, bare.goto_step(Step::Teams, inputs()).kind);
    ASSERT_FALSE(bare.page().team_lines.empty());
    const std::string fallback = bare.page().team_lines[0].seats;
    EXPECT_NE(std::string::npos, fallback.find("P1"))
        << "the seat token is always there: '" << fallback << "'";

    seat_short_name_ = [](std::uint8_t index) {
        return index == 0 ? std::string("WASD") : std::string();
    };
    MatchSetupSession named(save_);
    ASSERT_TRUE(named.open(inputs()));
    ASSERT_EQ(Kind::Advanced, named.goto_step(Step::Teams, inputs()).kind);
    ASSERT_FALSE(named.page().team_lines.empty());
    EXPECT_EQ("P1 WASD", named.page().team_lines[0].seats)
        << "the callback's word wins, and it is the word LINEUP writes";
    EXPECT_NE(fallback, named.page().team_lines[0].seats)
        << "if these two agree the callback is not reaching the bands and "
           "the pin above has no teeth";
}

// 9. The TEAMS lines: one per AUTHORED team, cells inside their columns.
TEST_F(MatchSetupSessionTest, teams_step_prints_authored_teams_only_with_cells_in_budget)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 820;
    save_.numplayers = 1;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Teams, inputs()).kind);
    ASSERT_EQ(2u, session.page().team_lines.size()) << "820 authors two sides";
    EXPECT_EQ("TEAM 1", session.page().team_lines[0].label);
    EXPECT_EQ("TEAM 2", session.page().team_lines[1].label);
    EXPECT_EQ(0u, session.page().team_lines_at);
    // The campaign's own line follows the team lines.
    ASSERT_FALSE(session.page().lines.empty());
    EXPECT_EQ("STRONG adds a fighter, BRUTAL two.", session.page().lines.back());

    // Four authored sides print four lines.
    ASSERT_EQ(Kind::Advanced,
              session.goto_step(Step::Game, inputs(true, 0b1111)).kind);
    ASSERT_EQ(Kind::Advanced,
              session.goto_step(Step::Teams, inputs(true, 0b1111)).kind);
    ASSERT_EQ(4u, session.page().team_lines.size());
    EXPECT_EQ("TEAM 4", session.page().team_lines[3].label);

    for (const MatchSetupSession::TeamLine& line : session.page().team_lines) {
        EXPECT_LE(line.label.size(), 6u) << line.label;
        EXPECT_LE(line.seats.size(), 18u) << line.seats;
        EXPECT_LE(line.census.size(), 20u) << line.census;
    }
    EXPECT_LE(session.page().lines.size() + session.page().team_lines.size(),
              static_cast<std::size_t>(og::ui::kSetupLinesMax));

    // The pointer line appears only while a line carries a diagnostic.
    const bool any_diag = std::any_of(
        session.page().team_lines.begin(), session.page().team_lines.end(),
        [](const MatchSetupSession::TeamLine& l) { return l.diag; });
    const bool pointer =
        std::find(session.page().lines.begin(), session.page().lines.end(),
                  std::string(og::ui::kSetupPointerLine)) !=
        session.page().lines.end();
    EXPECT_EQ(any_diag, pointer);
}

// 10. The F6 clamp: the SIDES row is hidden where one value is the only
// legal one, and the wheel never deals a side the map authors no markers for.
TEST_F(MatchSetupSessionTest, sides_row_hides_on_two_side_arenas_and_clamps_to_the_authored_count)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 820;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Teams, inputs()).kind);
    EXPECT_EQ(-1, row_index(session, "sides")) << "two sides is not a wheel";

    // A three-side arena spells its wheel "2, 3".
    ASSERT_EQ(Kind::Advanced,
              session.goto_step(Step::Teams, inputs(true, 0b0111)).kind);
    int at = row_index(session, "sides");
    ASSERT_GE(at, 0);
    EXPECT_EQ("2, 3", session.page().rows[static_cast<std::size_t>(at)].base.note);

    // Four sides: "2, 3, 4", and the wheel walks 2 -> 3 -> 4 -> 2.
    save_.fill = {};
    ASSERT_EQ(Kind::Advanced,
              session.goto_step(Step::Teams, inputs(true, 0b1111)).kind);
    at = row_index(session, "sides");
    ASSERT_GE(at, 0);
    EXPECT_EQ("2, 3, 4", session.page().rows[static_cast<std::size_t>(at)].base.note);

    std::vector<std::string> faces;
    for (int i = 0; i < 3; ++i) {
        const MatchSetupSession::Outcome out = session.choose(
            static_cast<std::size_t>(at), inputs(true, 0b1111));
        ASSERT_EQ(Kind::Turned, out.kind);
        EXPECT_EQ(Knob::Sides, out.knob);
        faces.push_back(og::ui::match_sides_face(save_, 0, 0b1111));
    }
    EXPECT_EQ((std::vector<std::string>{"SIDES: 2", "SIDES: 3", "SIDES: 4"}),
              faces);
    // The wheel wraps — the only way round it, and the whole cost of an
    // overshoot on a three-stop wheel.
    ASSERT_EQ(Kind::Turned,
              session.choose(static_cast<std::size_t>(at),
                             inputs(true, 0b1111)).kind);
    EXPECT_EQ("SIDES: 2", og::ui::match_sides_face(save_, 0, 0b1111));

    // A three-side arena's wheel is 2 <-> 3 and never reaches 4.
    save_.fill = {};
    ASSERT_EQ(Kind::Advanced,
              session.goto_step(Step::Teams, inputs(true, 0b0111)).kind);
    at = row_index(session, "sides");
    ASSERT_GE(at, 0);
    std::set<std::string> seen;
    for (int i = 0; i < 4; ++i) {
        ASSERT_EQ(Kind::Turned,
                  session.choose(static_cast<std::size_t>(at),
                                 inputs(true, 0b0111)).kind);
        seen.insert(og::ui::match_sides_face(save_, 0, 0b0111));
        EXPECT_EQ(og::sim::kFillNone, save_.fill[3])
            << "the unauthored fourth side is never dealt";
    }
    EXPECT_EQ((std::set<std::string>{"SIDES: 2", "SIDES: 3"}), seen);
}

// 11. match_knobs shapes every step (§3.2's table).
TEST_F(MatchSetupSessionTest, match_knobs_shape_the_steps)
{
    // ffa/mutant: no team macro, the band wheel alone, the band line.
    register_book(R"({ teams = false, fill = "band", arena_page = "ffa",
                       lines = { "FILL sets how strong the bots are." } })");
    save_.scen_num = 820;
    MatchSetupSession band(save_);
    ASSERT_TRUE(band.open(inputs()));
    ASSERT_EQ(Kind::Advanced, band.goto_step(Step::Teams, inputs()).kind);
    EXPECT_TRUE(band.page().team_lines.empty());
    EXPECT_EQ((std::vector<std::string>{"fill", "lineup"}), row_ids(band));
    EXPECT_EQ(Knob::BandFill, band.page().rows[0].knob);
    EXPECT_EQ("FILL: NONE", band.page().rows[0].base.label);
    EXPECT_EQ(std::string(og::ui::kLineupFillNote),
              band.page().rows[0].base.note)
        << "the BAND row wears the BAND wheel's note";
    ASSERT_FALSE(band.page().lines.empty());
    EXPECT_EQ("FILL sets how strong the bots are.", band.page().lines.back());
    // The band wheel writes team 1's own knob, nothing else.
    ASSERT_EQ(Kind::Turned, band.choose(0, inputs()).kind);
    EXPECT_EQ(og::sim::kFillWeak, save_.fill[0]);
    EXPECT_EQ(og::sim::kFillNone, save_.fill[1]);

    // onslaught: no teams, no FILL row at all — the LINEUP door alone, and
    // the step is still there (MAP UNITS is onslaught's live knob).
    og::script::clear_pack_scripts();
    register_book(R"({ teams = false, fill = false, score = false,
                       arena_page = "onslaught" })");
    MatchSetupSession ons(save_);
    ASSERT_TRUE(ons.open(inputs()));
    ASSERT_EQ(Kind::Advanced, ons.goto_step(Step::Teams, inputs()).kind);
    EXPECT_EQ((std::vector<std::string>{"lineup"}), row_ids(ons));
    // score = false drops the SCORE row: RULES is the two scenario knobs
    // (R2-3), so onslaught's RULES is TIME LIMIT alone.
    ASSERT_EQ(Kind::Advanced, ons.goto_step(Step::Rules, inputs()).kind);
    EXPECT_EQ(-1, row_index(ons, "score"));
    EXPECT_EQ((std::vector<std::string>{"time"}), row_ids(ons));
    EXPECT_EQ((std::vector<std::string>{
                  std::string(og::ui::kSetupRulesPointerLine)}),
              ons.page().lines);

    // score = false AND time = false: the step STAYS (five steps always),
    // with no rows, the map line and the pointer line — and neither cell
    // in the MATCH recap.
    og::script::clear_pack_scripts();
    register_book(R"({ score = false, time = false, arena_page = "soccer" })");
    MatchSetupSession quiet(save_);
    ASSERT_TRUE(quiet.open(inputs()));
    EXPECT_EQ(5u, quiet.steps().size());
    EXPECT_NE(quiet.steps().end(),
              std::find(quiet.steps().begin(), quiet.steps().end(),
                        Step::Rules));
    ASSERT_EQ(Kind::Advanced, quiet.goto_step(Step::Rules, inputs()).kind);
    EXPECT_TRUE(quiet.page().rows.empty());
    EXPECT_EQ((std::vector<std::string>{
                  std::string(og::ui::kSetupRulesMapLine),
                  std::string(og::ui::kSetupRulesPointerLine)}),
              quiet.page().lines);

    // NEXT and PREV still land on it: the hard-coded stepper never skips a
    // step, so an empty RULES must be walkable from both sides.
    ASSERT_EQ(Kind::Advanced, quiet.goto_step(Step::Teams, inputs()).kind);
    MatchSetupSession::Outcome step = quiet.next(inputs());
    EXPECT_EQ(Kind::Advanced, step.kind);
    EXPECT_EQ(Step::Rules, quiet.step());
    EXPECT_EQ(Kind::Advanced, quiet.next(inputs()).kind);
    EXPECT_EQ(Step::Match, quiet.step());
    EXPECT_EQ(Kind::Advanced, quiet.prev(inputs()).kind);
    EXPECT_EQ(Step::Rules, quiet.step());

    // ...and the terminal item on TEAMS names it.
    ASSERT_EQ(Kind::Advanced, quiet.goto_step(Step::Teams, inputs()).kind);
    const og::ui::TerminalMatchSetupModel teams_model =
        og::ui::build_terminal_match_setup_model(quiet, inputs());
    EXPECT_TRUE(std::any_of(teams_model.items.begin(), teams_model.items.end(),
                            [](const og::ui::TerminalMatchSetupItem& item) {
                                return item.label == "Next: RULES";
                            }));

    ASSERT_EQ(Kind::Advanced, quiet.goto_step(Step::Match, inputs()).kind);
    for (const std::string& line : quiet.page().lines) {
        EXPECT_EQ(std::string::npos, line.find("SCORE:")) << line;
        EXPECT_EQ(std::string::npos, line.find("TIME LIMIT:")) << line;
    }
}

// 12. Every RULES row is the shared formatter, upper-cased by WP2's composer,
// with its own note — and the pair fits the 42-glyph face at EVERY value.
TEST_F(MatchSetupSessionTest, rules_rows_are_the_shared_formatters_upper_cased_with_per_row_notes)
{
    register_book(kSoccerKnobs);
    networked_ = true;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Rules, inputs()).kind);
    EXPECT_EQ((std::vector<std::string>{"score", "time"}), row_ids(session));
    EXPECT_EQ(2u, session.page().rows.size())
        << "RULES is the two match_knobs rows and nothing else (R2-3)";
    EXPECT_EQ((std::vector<std::string>{
                  std::string(og::ui::kSetupRulesPointerLine)}),
              session.page().lines)
        << "one host pointer line: the other seven knobs are a Base Camp "
           "door";

    const std::vector<std::string> notes = {
        "map, 1, 3, 5, 10", "map, 5 to 20 min"};
    for (std::size_t i = 0; i < notes.size(); ++i) {
        EXPECT_EQ(notes[i], session.page().rows[i].base.note)
            << session.page().rows[i].base.id;
        EXPECT_EQ(Extra::Cycler, session.page().rows[i].extra);
        EXPECT_NE(Knob::None, session.page().rows[i].knob);
    }

    // The label is byte-for-byte the shared composer's face, filtered to
    // the ids kSetupRulesRows names — the session never re-cases or
    // re-spells one, and never invents a row id of its own.
    const og::ui::MatchRulesInputs rules{&save_, true, true, difficulty_, true};
    std::vector<og::ui::MatchRuleFace> faces;
    for (const og::ui::MatchRuleFace& face : og::ui::match_rules_faces(rules)) {
        if (std::find(og::ui::kSetupRulesRows.begin(),
                      og::ui::kSetupRulesRows.end(),
                      face.id) != og::ui::kSetupRulesRows.end()) {
            faces.push_back(face);
        }
    }
    ASSERT_EQ(faces.size(), session.page().rows.size());
    for (std::size_t i = 0; i < faces.size(); ++i) {
        EXPECT_EQ(faces[i].id, session.page().rows[i].base.id);
        EXPECT_EQ(faces[i].face, session.page().rows[i].base.label);
    }

    // EVERY value of EVERY wheel fits "label - note" in 42 glyphs.
    for (std::size_t r = 0; r < session.page().rows.size(); ++r) {
        for (int step = 0; step < 6; ++step) {
            const MatchSetupSession::Row& row = session.page().rows[r];
            EXPECT_LE(row.base.label.size() + 3 + row.base.note.size(), 42u)
                << row.base.label << " - " << row.base.note;
            ASSERT_EQ(Kind::Turned, session.choose(r, inputs()).kind);
        }
    }

    // CROSS CONTROL is not a wizard row at all any more, networked or not.
    networked_ = false;
    session.refetch(inputs());
    EXPECT_EQ(-1, row_index(session, "cross_control"));
    EXPECT_EQ(2u, session.page().rows.size());
}

// 13 and 14 retired with R2-3. The two surviving wheels' forward lap is
// pinned by MatchSetupRules.time_limit_wheel_faces / score_wheel_laps_forward
// and by the SDL cycler ladder; DIFFICULTY is a Base Camp door again, so
// the session has no difficulty arm to pin.

// 15. The joiner grammar: cut the row, print the line.
TEST_F(MatchSetupSessionTest, joiner_steps_cut_rows_and_print_lines)
{
    register_book(kSoccerKnobs);
    networked_ = true;
    save_.scen_num = 820;
    MatchSetupSession joiner(save_);
    ASSERT_TRUE(joiner.open(inputs(false)));

    ASSERT_EQ(Kind::Advanced, joiner.goto_step(Step::Teams, inputs(false)).kind);
    EXPECT_EQ((std::vector<std::string>{"lineup"}), row_ids(joiner));
    EXPECT_EQ(std::string(og::ui::kHostSetsForEveryoneCaption),
              joiner.page().lines.back());

    // R2-3 verbatim: the joiner's RULES is the caption and the ONE packed
    // line the two scenario knobs make, and zero rows. The joiner's sight
    // of CROSS CONTROL is the Base Camp DIFFICULTY door, read-only.
    ASSERT_EQ(Kind::Advanced, joiner.goto_step(Step::Rules, inputs(false)).kind);
    EXPECT_EQ((std::vector<std::string>{
                  std::string(og::ui::kHostSetsForEveryoneCaption),
                  "SCORE: MAP  TIME LIMIT: MAP"}),
              joiner.page().lines);
    EXPECT_TRUE(joiner.page().rows.empty());
    for (const std::string& line : joiner.page().lines)
    {
        EXPECT_EQ(std::string::npos, line.find("CROSS CONTROL"))
            << "CROSS CONTROL left the wizard with the other six "
               "DIFFICULTY knobs: '" << line << "'";
    }

    // Outside a networked lobby the step reads exactly the same: the two
    // scenario knobs are not networked facts.
    networked_ = false;
    joiner.refetch(inputs(false));
    EXPECT_TRUE(joiner.page().rows.empty());
    EXPECT_EQ(2u, joiner.page().lines.size());

    networked_ = true;
    ASSERT_EQ(Kind::Advanced, joiner.goto_step(Step::Match, inputs(false)).kind);
    // The MATCH step carries NO rules rows, so it keeps all five lines —
    // the rule is "not twice on one step", not "never printed".
    EXPECT_TRUE(std::any_of(joiner.page().lines.begin(),
                            joiner.page().lines.end(),
                            [](const std::string& line) {
                                return line.find("CROSS CONTROL") !=
                                       std::string::npos;
                            }))
        << "MATCH has no rows to duplicate, so the recap is whole there";
    EXPECT_EQ((std::vector<std::string>{"view_level", "ready_pointer"}),
              row_ids(joiner));
    EXPECT_EQ(std::string(og::ui::kSetupJoinerReadyRow),
              joiner.page().rows[1].base.label);
    EXPECT_EQ(og::ui::RowState::Disabled, joiner.page().rows[1].state);
    EXPECT_EQ(Kind::OpenViewLevel, joiner.choose(0, inputs(false)).kind);
}

// 16. The MATCH step states the whole match in ten lines, and drops the rules
// recap LAST-first when the report needs the room.
TEST_F(MatchSetupSessionTest, match_step_title_teams_rules_fit_ten_lines_and_drop_rules_last_first)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 820;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Match, inputs()).kind);
    std::string title = og::data::load_scenario_title("scen820");
    ASSERT_NE("none", title) << "modes scen820 must be readable";
    uppercase(title);
    EXPECT_EQ(title, session.page().lines[0]);
    EXPECT_EQ("SOCCER: THE PITCH", session.page().lines[0]);
    EXPECT_EQ(1u, session.page().team_lines_at);
    EXPECT_TRUE(session.page().team_lines.empty()) << "no report, no team lines";

    // With no report the lines are the title plus the rules recap.
    const og::ui::MatchRulesInputs rules{&save_, true, true, difficulty_, false};
    const std::vector<std::string> recap = og::ui::format_match_rules_lines(rules);
    ASSERT_EQ(1u + recap.size(), session.page().lines.size());

    // A networked four-team staged report is 1 + 4 + 5 == 10 exactly.
    networked_ = true;
    og::ui::ScenarioRosterReport report;
    report.is_versus = true;
    report.staged = true;
    report.mode_census = true;
    report.mode_name = "SOCCER";
    for (int t = 0; t < 4; ++t) {
        report.team_active[static_cast<std::size_t>(t)] = true;
        report.team_fill[static_cast<std::size_t>(t)] =
            og::ui::ScenarioFill::Matched;
        report.team_fill_count[static_cast<std::size_t>(t)] = 2;
    }
    staged_ = &report;
    health_ = Health::Staged;
    session.refetch(inputs(true, 0b1111));
    EXPECT_EQ(4u, session.page().team_lines.size());
    EXPECT_EQ(static_cast<std::size_t>(og::ui::kSetupLinesMax),
              session.page().lines.size() + session.page().team_lines.size());
    for (int t = 0; t < 4; ++t) {
        EXPECT_EQ(og::ui::format_scenario_report_team_line(report, t),
                  session.page().team_lines[static_cast<std::size_t>(t)].label);
    }

    // The refusal leads and the recap drops last-first: CROSS CONTROL first.
    report.stage_failed = true;
    session.refetch(inputs(true, 0b1111));
    EXPECT_EQ("STAGING FAILED", session.page().lines[1]);
    EXPECT_EQ(2u, session.page().team_lines_at);
    const std::string all = [&] {
        std::string joined;
        for (const std::string& line : session.page().lines)
            joined += line + "\n";
        return joined;
    }();
    EXPECT_EQ(std::string::npos, all.find("CROSS CONTROL"))
        << "the last recap line is the first to go:\n" << all;
    EXPECT_LE(session.page().lines.size() + session.page().team_lines.size(),
              static_cast<std::size_t>(og::ui::kSetupLinesMax));
    staged_ = nullptr;
}

// 17. GO says WHY it will not launch, on its own face (§2.6's table).
TEST_F(MatchSetupSessionTest, go_face_follows_the_deploy_predicate_and_stage_health)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 820;
    save_.numplayers = 2;
    put(save_, 0, 0, true);
    save_.team_size = 1;   // two seats, one deployed fighter
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Match, inputs()).kind);
    int at = row_index(session, "go");
    ASSERT_GE(at, 0);
    EXPECT_EQ(std::string(og::ui::kSetupGoDeployFace),
              session.page().rows[static_cast<std::size_t>(at)].base.label);
    EXPECT_EQ(og::ui::RowState::Disabled,
              session.page().rows[static_cast<std::size_t>(at)].state);
    EXPECT_EQ(Kind::Refused,
              session.choose(static_cast<std::size_t>(at), inputs()).kind);

    // Deploy the second fighter: the M4 refusal clears and the stage's own
    // health takes over.
    // (The company defaults to allied_mode, so both seats sit on team 1 and
    // the second fighter has to join them there.)
    put(save_, 1, 0, true);
    save_.team_size = 2;
    session.refetch(inputs());
    at = row_index(session, "go");
    ASSERT_GE(at, 0);
    EXPECT_EQ(std::string(og::ui::kSetupGoStagingFace),
              session.page().rows[static_cast<std::size_t>(at)].base.label);

    og::ui::ScenarioRosterReport report;
    staged_ = &report;
    health_ = Health::Failed;
    session.refetch(inputs());
    EXPECT_EQ(std::string(og::ui::kSetupGoStagingFailedFace),
              session.page().rows[static_cast<std::size_t>(row_index(session, "go"))]
                  .base.label);
    health_ = Health::Unavailable;
    session.refetch(inputs());
    EXPECT_EQ(std::string(og::ui::kSetupGoStagingFailedFace),
              session.page().rows[static_cast<std::size_t>(row_index(session, "go"))]
                  .base.label);

    health_ = Health::Staged;
    session.refetch(inputs());
    at = row_index(session, "go");
    ASSERT_GE(at, 0);
    EXPECT_EQ("GO", session.page().rows[static_cast<std::size_t>(at)].base.label);
    EXPECT_EQ(og::ui::RowState::Visible,
              session.page().rows[static_cast<std::size_t>(at)].state);
    EXPECT_EQ(Extra::Go, session.page().rows[static_cast<std::size_t>(at)].extra);
    EXPECT_EQ(Kind::Go,
              session.choose(static_cast<std::size_t>(at), inputs()).kind);
    staged_ = nullptr;
}

// 18. The §2.8 budget sweep over every step, both viewers, three arenas and
// both book shapes. Nothing here is clipped by the composer — a face that
// overruns is a bug, not a truncation.
TEST_F(MatchSetupSessionTest, every_composed_string_fits_its_budget_on_every_step)
{
    og::ui::ScenarioRosterReport report;
    report.is_versus = true;
    report.staged = true;
    report.mode_census = true;
    report.mode_name = "SOCCER";
    for (int t = 0; t < 4; ++t) {
        report.team_active[static_cast<std::size_t>(t)] = true;
        report.team_fill[static_cast<std::size_t>(t)] =
            og::ui::ScenarioFill::Matched;
        report.team_fill_count[static_cast<std::size_t>(t)] = 2;
    }

    for (const bool booked : {true, false}) {
        for (const bool host : {true, false}) {
            for (const int level : {820, 822, 507}) {
                og::script::clear_pack_scripts();
                if (booked)
                    register_book(kSoccerKnobs);
                else
                    register_knobs_only(kSoccerKnobs);
                save_.scen_num = static_cast<short>(level);
                save_.numplayers = 2;
                staged_ = &report;
                health_ = Health::Staged;
                networked_ = true;
                MatchSetupSession session(save_);
                ASSERT_TRUE(session.open(inputs(host, 0b1111)))
                    << booked << host << level;
                for (const Step step : session.steps()) {
                    ASSERT_EQ(Kind::Advanced,
                              session.goto_step(step, inputs(host, 0b1111)).kind);
                    const MatchSetupSession::Page& page = session.page();
                    const bool hosted_lua =
                        step == Step::Game ||
                        (step == Step::Arena && booked);
                    EXPECT_LE(page.lines.size() + page.team_lines.size(),
                              static_cast<std::size_t>(
                                  hosted_lua ? 6 : og::ui::kSetupLinesMax))
                        << static_cast<int>(step);
                    for (const std::string& line : page.lines)
                        EXPECT_LE(line.size(), 48u) << line;
                    for (const MatchSetupSession::Row& row : page.rows) {
                        const std::size_t width =
                            row.base.label.size() +
                            (row.base.note.empty()
                                 ? 0u
                                 : 3u + row.base.note.size());
                        EXPECT_LE(width, 42u)
                            << row.base.label << " - " << row.base.note;
                    }
                }
            }
        }
    }
    staged_ = nullptr;
}

// ---------------------------------------------------------------------------
// The shared terminal driver (openglad_text and openglad_curses run THIS)
// ---------------------------------------------------------------------------

namespace {

// "@<label>" answers a prompt with the number the prompt ITSELF printed for
// the item whose label starts with <label>, so a case can walk the wizard
// without re-deriving how many rows each step carries.
std::string prompt_item_number(const std::vector<std::string>& lines,
                               std::string_view label_prefix)
{
    for (const std::string& line : lines)
    {
        const std::size_t first = line.find_first_not_of(' ');
        if (first == std::string::npos || line[first] < '0' ||
            line[first] > '9')
        {
            continue;
        }
        std::size_t end = first;
        while (end < line.size() && line[end] >= '0' && line[end] <= '9')
            ++end;
        if (end + 2 > line.size() || line[end] != '.' || line[end + 1] != ' ')
            continue;
        if (line.compare(end + 2, label_prefix.size(), label_prefix) == 0)
            return line.substr(first, end - first);
    }
    return "no such item";  // the loop refuses it and the case fails loudly
}

// A scripted TerminalMatchSetupIo: canned prompt answers (EOF after the
// script runs dry), recorded prompts, notices and autosaves.
// The census is the REAL one, over a MatchStage the fixture owns exactly as
// each terminal client owns one for its page loop.
struct ScriptedSetupIo {
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
    bool applied_replay_arm = false;
    int autosaves = 0;
    int difficulty = 0;
    SaveData* save = nullptr;
    og::server::MatchStage* stage = nullptr;

    og::ui::TerminalMatchSetupIo io()
    {
        og::ui::TerminalMatchSetupIo out;
        out.base.prompt = [this](const std::string& title,
                                 const std::vector<std::string>& lines,
                                 const std::string& label)
            -> std::optional<std::string> {
            prompts.push_back({title, lines, label});
            if (cursor >= answers.size())
                return std::nullopt;  // scripted input exhausted = EOF
            const std::string scripted_answer = answers[cursor++];
            if (!scripted_answer.empty() && scripted_answer.front() == '@')
                return prompt_item_number(lines, scripted_answer.substr(1));
            return scripted_answer;
        };
        out.base.notice = [this](const std::string& line) {
            notices.push_back(line);
        };
        out.base.is_host = [this] { return host; };
        out.base.apply_level = [this](int level, bool replay_arm) {
            applied_level = level;
            applied_replay_arm = replay_arm;
            if (replay_arm && save->is_level_completed(level))
                save->arm_replay(static_cast<short>(level));
            else
                save->scen_num = static_cast<short>(level);
        };
        out.level_hooks = []() -> const LevelDataHooks& {
            return headless_level_data_hooks();
        };
        out.autosave = [this] { ++autosaves; };
        out.difficulty = [this] { return difficulty; };
        out.census = [this](std::array<int, 4>& counts,
                            og::ui::ScenarioRosterReport& report) {
            return og::ui::census_staged_match_report(*stage, *save, difficulty,
                                                      0u, counts, report);
        };
        return out;
    }

    // The item lines of prompt `index` (everything after the page lines is
    // a numbered item, which is exactly how the clients render it).
    std::string page_text(std::size_t index) const
    {
        std::string joined;
        for (const std::string& line : prompts[index].lines)
            joined += line + "\n";
        return joined;
    }
};

}  // namespace

// 19. Every dispatch arm of the loop, in one walk.
TEST_F(MatchSetupSessionTest, terminal_driver_walks_every_outcome_arm)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 300;
    save_.numplayers = 1;
    put(save_, 0, 0, true);
    save_.team_size = 1;

    og::server::MatchStage stage({
        .networked = false,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &save_,
    });
    ScriptedSetupIo scripted;
    scripted.save = &save_;
    scripted.stage = &stage;
    scripted.answers = {
        "5",    // GAME: SOCCER -> ARENA
        "1",    // ARENA: THE PITCH -> SetLevel 820 -> TEAMS
        "3",    // TEAMS: Next -> RULES
        "1",    // RULES: SCORE forward
        "1-",   // the retired `N-` grammar: not a number, so not an answer
        "3",    // RULES: Next -> MATCH
        "3",    // MATCH: Prev -> RULES
        "3-",   // the same on a stepper
        "x",    // not a number at all
        "0",    // back out
    };
    og::ui::run_terminal_match_setup(save_, scripted.io());

    ASSERT_EQ(scripted.answers.size(), scripted.cursor)
        << "the loop must consume every scripted answer";
    EXPECT_EQ(820, scripted.applied_level);
    EXPECT_FALSE(scripted.applied_replay_arm);
    EXPECT_EQ(820, static_cast<int>(save_.scen_num));

    const std::vector<std::string> titles = [&] {
        std::vector<std::string> out;
        for (const ScriptedSetupIo::Prompt& p : scripted.prompts)
            out.push_back(p.title);
        return out;
    }();
    EXPECT_EQ((std::vector<std::string>{
                  "SETUP: GAME", "SETUP: ARENA", "SETUP: TEAMS",
                  "SETUP: RULES", "SETUP: RULES", "SETUP: RULES",
                  "SETUP: MATCH", "SETUP: RULES", "SETUP: RULES",
                  "SETUP: RULES"}),
              titles);

    // The prompt label names the range and the way out.
    // GAME: seven game pages, RANDOM, Next: TEAMS, Back.
    EXPECT_EQ("Setup # [1-10] (0 = back): ", scripted.prompts[0].label);

    // The TEAMS prompt's navigation items are the tab strip's projection.
    const std::string teams = scripted.page_text(2);
    EXPECT_NE(std::string::npos, teams.find("   3. Next: RULES\n")) << teams;
    EXPECT_NE(std::string::npos, teams.find("   4. Prev: ARENA\n")) << teams;
    EXPECT_NE(std::string::npos, teams.find("   5. Back\n")) << teams;

    // The notices, in order: the gated level set and the THREE refusals --
    // `1-`, `3-` and `x` are all just answers the prompt cannot parse now
    // that the wheels turn forward only. A knob turn says NOTHING (R2-1):
    // the redrawn face is the answer.
    EXPECT_EQ((std::vector<std::string>{
                  "Level set to THE PITCH.", "Invalid setup row.",
                  "Invalid setup row.", "Invalid setup row."}),
              scripted.notices);
    EXPECT_EQ(1, static_cast<int>(save_.ctf_capture_limit))
        << "the one FORWARD turn is the only thing that moved the wheel; "
           "`1-` bought a notice, not a step back";

    // The autosave tail: the arena deal at the first prompt (scen 300 has
    // never been dealt) and one per turned knob -- ONE, now that `1-` turns
    // nothing. The 820 deal that follows moves no band -- the 300 deal
    // already lifted every one of them -- so it banks the memo without an
    // autosave.
    EXPECT_EQ(2, scripted.autosaves);
}

// 20. The per-prompt deal (the present_menu cadence): the TEAMS prompt after
// an arena pick already reads the word the CAMPAIGN deals.
TEST_F(MatchSetupSessionTest, terminal_driver_deals_the_campaign_word_before_the_next_prompt)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 300;
    save_.numplayers = 1;
    put(save_, 0, 0, true);
    save_.team_size = 1;
    // Park the memo on the cursor the wizard opens on, so the first prompt
    // deals nothing and the ONLY deal under test is the arena pick's.
    save_.arena_lineup_dealt_campaign = "modes";
    save_.arena_lineup_dealt_scen = 300;

    og::server::MatchStage stage({
        .networked = false,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &save_,
    });
    ScriptedSetupIo scripted;
    scripted.save = &save_;
    scripted.stage = &stage;
    scripted.answers = {"5", "1", "0"};
    og::ui::run_terminal_match_setup(save_, scripted.io());

    ASSERT_EQ(3u, scripted.prompts.size());
    EXPECT_EQ("SETUP: TEAMS", scripted.prompts[2].title);
    EXPECT_NE(std::string::npos,
              scripted.page_text(2).find("FILL: STRONG - weak to brutal"))
        << scripted.page_text(2);
    EXPECT_EQ(og::sim::kFillStrong, save_.fill[1]);
    EXPECT_EQ(1, scripted.autosaves) << "one deal, nothing else";

    // A book with no `deal` key keeps the FAIR default, byte for byte.
    og::script::clear_pack_scripts();
    register_book(R"({ arena_page = "tdm" })");
    save_.scen_num = 820;
    save_.fill = {};
    save_.arena_lineup_dealt_campaign = "modes";
    save_.arena_lineup_dealt_scen = 820;
    ScriptedSetupIo fair;
    fair.save = &save_;
    fair.stage = &stage;
    fair.answers = {"1", "1", "0"};  // GAME: TDM -> ARENA: THE CIRCLE
    og::ui::run_terminal_match_setup(save_, fair.io());
    ASSERT_EQ(3u, fair.prompts.size());
    EXPECT_EQ(300, fair.applied_level);
    EXPECT_NE(std::string::npos,
              fair.page_text(2).find("FILL: FAIR - weak to brutal"))
        << fair.page_text(2);
    EXPECT_EQ(1, fair.autosaves);

    // The memo: a second visit to the same cursor deals nothing at all.
    ScriptedSetupIo again;
    again.save = &save_;
    again.stage = &stage;
    again.answers = {"0"};
    og::ui::run_terminal_match_setup(save_, again.io());
    EXPECT_EQ(0, again.autosaves);
}

// 21. The joiner's prompt and the classic guard.
TEST_F(MatchSetupSessionTest, terminal_driver_joiner_face_and_classic_guard)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 300;
    save_.arena_lineup_dealt_campaign = "modes";
    save_.arena_lineup_dealt_scen = 300;

    og::server::MatchStage stage({
        .networked = false,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &save_,
    });
    ScriptedSetupIo joiner;
    joiner.save = &save_;
    joiner.stage = &stage;
    joiner.host = false;
    joiner.answers = {
        "5",           // GAME: SOCCER -> ARENA (pages open to every machine)
        "1",           // a level row: refused by the host gate, no advance
        "@Next: TEAMS",  // by LABEL: the row count is the book's business
        "1-",          // the joiner's LINEUP door is not a wheel
        "0",
    };
    og::ui::run_terminal_match_setup(save_, joiner.io());
    EXPECT_EQ(-1, joiner.applied_level);
    EXPECT_EQ(300, static_cast<int>(save_.scen_num));
    EXPECT_EQ((std::vector<std::string>{
                  std::string(og::ui::kCampaignPickerHostGuardMessage),
                  std::string(og::ui::kSetupInvalidRowNotice)}),
              joiner.notices);
    ASSERT_EQ(5u, joiner.prompts.size());
    EXPECT_EQ("SETUP: ARENA", joiner.prompts[2].title)
        << "a refusal never advances";
    EXPECT_EQ("SETUP: TEAMS", joiner.prompts[3].title);
    EXPECT_EQ(0, joiner.autosaves) << "the deal is the host's";
    // The joiner's TEAMS prompt has no wheel at all.
    EXPECT_EQ(std::string::npos, joiner.page_text(3).find("FILL:"))
        << joiner.page_text(3);
    EXPECT_NE(std::string::npos,
              joiner.page_text(3).find(
                  std::string(og::ui::kHostSetsForEveryoneCaption)))
        << joiner.page_text(3);

    // A classic campaign never reaches the driver by any player path (the
    // terminal camp calls the wizard only on a versus campaign), so the
    // driver's own guard says nothing at all: it only traces. The
    // `setup open_refused` TRACE cannot be read here for the reason the
    // manifest test above states -- a headless unit group links og_game,
    // compiled WITHOUT TESTING -- so what is pinned is the consequence.
    save_.current_campaign = "gladiator";
    ScriptedSetupIo classic;
    classic.save = &save_;
    classic.stage = &stage;
    classic.answers = {"1"};
    og::ui::run_terminal_match_setup(save_, classic.io());
    EXPECT_TRUE(classic.prompts.empty());
    EXPECT_TRUE(classic.notices.empty())
        << "no prompt, no notice: the refusal is a trace";
}

// 23. The shared staged census: one ensure_current, two answers.
TEST_F(MatchSetupSessionTest, census_staged_match_report_answers_health_and_report)
{
    save_.scen_num = 820;
    save_.numplayers = 1;
    put(save_, 0, 0, true);
    save_.team_size = 1;

    og::server::MatchStage stage({
        .networked = false,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &save_,
    });
    std::array<int, 4> counts{};
    og::ui::ScenarioRosterReport report;
    EXPECT_EQ(Health::Staged,
              og::ui::census_staged_match_report(stage, save_, 0, 0u, counts,
                                                 report));
    EXPECT_TRUE(report.staged);
    EXPECT_FALSE(report.unavailable);

    // The old bool is the Staged arm of the same call, over the same world.
    std::array<int, 4> bool_counts{};
    og::server::MatchStage twin({
        .networked = false,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &save_,
    });
    EXPECT_TRUE(og::ui::census_staged_lineup_map_units(twin, save_, 0, 0u,
                                                       bool_counts));
    EXPECT_EQ(counts, bool_counts);

    // The Failed arm (StageStatus::Failed) has no deterministic fixture:
    // every refusal a unit test can force -- unmounted campaign, a cursor
    // the mount cannot name -- is caught by an Unavailable guard first, and
    // MatchStage only fails on a load exception or the wire size cap. Wave 3
    // owns the pin (WP8's terminal drives or WP7's SDL flow, which can force
    // a stage the pipeline refuses); until then the arm is four uncovered
    // src/ lines, recorded here rather than left silent.

    // An unmounted campaign has no world to census and says so in the
    // report's own words, not by going blank.
    save_.current_campaign = "not_mounted_anywhere";
    counts = {};
    report = og::ui::ScenarioRosterReport{};
    EXPECT_EQ(Health::Unavailable,
              og::ui::census_staged_match_report(stage, save_, 0, 0u, counts,
                                                 report));
    EXPECT_TRUE(report.unavailable);
    EXPECT_EQ((std::array<int, 4>{0, 0, 0, 0}), counts);
    std::array<int, 4> untouched{7, 7, 7, 7};
    EXPECT_FALSE(og::ui::census_staged_lineup_map_units(stage, save_, 0, 0u,
                                                        untouched));
    EXPECT_EQ((std::array<int, 4>{7, 7, 7, 7}), untouched)
        << "the bool form still leaves `out` alone on every other arm";
}

// 24. The projection: lines and items.
TEST_F(MatchSetupSessionTest, terminal_model_projects_lines_and_items)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 820;
    save_.numplayers = 1;
    put(save_, 0, 0, true);
    save_.team_size = 1;
    save_.fill = {og::sim::kFillStrong, og::sim::kFillStrong, 0, 0};
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Teams, inputs()).kind);

    const og::ui::TerminalMatchSetupModel model =
        og::ui::build_terminal_match_setup_model(session, inputs());
    EXPECT_EQ("SETUP: TEAMS", model.title);
    ASSERT_GE(model.lines.size(), 2u);
    // The colour WORD stands where the pixel surfaces ink a swatch.
    EXPECT_TRUE(model.lines[0].starts_with(
        std::string("TEAM 1 ") + og::sim::team_color_name(0)))
        << model.lines[0];
    EXPECT_TRUE(model.lines[1].starts_with(
        std::string("TEAM 2 ") + og::sim::team_color_name(1)))
        << model.lines[1];
    for (const std::string& line : model.lines)
        EXPECT_LE(line.size(), 72u) << line;
    // Every cell the page composed is on the line, joined by two spaces.
    for (std::size_t i = 0; i < session.page().team_lines.size(); ++i) {
        const MatchSetupSession::TeamLine& cells = session.page().team_lines[i];
        if (!cells.seats.empty()) {
            EXPECT_NE(std::string::npos,
                      model.lines[i].find("  " + cells.seats));
        }
        EXPECT_NE(std::string::npos, model.lines[i].find("  " + cells.census))
            << model.lines[i];
    }

    ASSERT_EQ(5u, model.items.size());
    EXPECT_EQ("FILL: STRONG - weak to brutal", model.items[0].label);
    EXPECT_EQ("LINEUP - fill per team, map units  >", model.items[1].label);
    EXPECT_EQ("Next: RULES", model.items[2].label);
    EXPECT_EQ("Prev: ARENA", model.items[3].label);
    EXPECT_EQ("Back", model.items[4].label);
    EXPECT_EQ(og::ui::TerminalMatchSetupItem::Kind::Row, model.items[0].kind);
    EXPECT_EQ(og::ui::TerminalMatchSetupItem::Kind::Next, model.items[2].kind);
    EXPECT_EQ(og::ui::TerminalMatchSetupItem::Kind::Prev, model.items[3].kind);
    EXPECT_EQ(og::ui::TerminalMatchSetupItem::Kind::Back, model.items[4].kind);
    // The MATCH step's team lines ARE the report lines and print alone.
    og::ui::ScenarioRosterReport report;
    report.is_versus = true;
    report.staged = true;
    report.mode_census = true;
    report.mode_name = "SOCCER";
    report.team_active[0] = true;
    report.team_fill[0] = og::ui::ScenarioFill::Company;
    report.team_fill_count[0] = 1;
    staged_ = &report;
    health_ = Health::Staged;
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Match, inputs()).kind);
    const og::ui::TerminalMatchSetupModel match =
        og::ui::build_terminal_match_setup_model(session, inputs());
    EXPECT_EQ("SOCCER: THE PITCH", match.lines[0]);
    EXPECT_EQ(og::ui::format_scenario_report_team_line(report, 0),
              match.lines[1]);
    staged_ = nullptr;
}

// ---------------------------------------------------------------------------
// 25. The SHIPPED modes campaign (the synthetic book above proves the engine;
// these three prove the campaign). Green once WP6 (campaign_picker.lua) lands
// on feat/arena-setup: today the shipped book answers nil at "" and carries
// no match_knobs hook.
// ---------------------------------------------------------------------------

namespace {

class ModesSetupSessionTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        previous_game_ = current_game;
        current_game = nullptr;
        restore_default_campaigns();
        (void)og::resources::mount((get_asset_path() + "packs/").c_str(),
                                   "packs/", 1);
        previous_mount_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("modes"));
        og::resources::refresh_pack_scripts();
        save_.current_campaign = "modes";
        save_.my_team = 0;
        save_.scen_num = 300;
        og::script::hooks::install_campaign_providers(
            og::data::make_campaign_providers(save_));
    }

    void TearDown() override
    {
        og::script::hooks::clear_campaign_providers();
        (void)unmount_campaign_package_with_error("modes");
        if (!previous_mount_.empty() && previous_mount_ != "modes")
            (void)mount_campaign_package_with_error(previous_mount_);
        og::resources::refresh_pack_scripts();
        current_game = previous_game_;
    }

    MatchSetupSession::Inputs inputs()
    {
        players_ = og::ui::synthesize_local_lobby_players(save_);
        local_indices_.clear();
        for (const og::sim::LobbyPlayer& player : players_)
            local_indices_.push_back(player.player_index);
        MatchSetupSession::Inputs in;
        in.save = &save_;
        in.is_host = true;
        in.my_team = save_.my_team;
        in.authored_mask = og::ui::ctf_authored_team_mask_for_save(
            save_, headless_level_data_hooks());
        in.players = players_;
        in.local_indices = local_indices_;
        return in;
    }

    SaveData save_;
    std::vector<og::sim::LobbyPlayer> players_;
    std::vector<std::uint8_t> local_indices_;
    std::string previous_mount_;
    GameplayContext* previous_game_ = nullptr;
};

}  // namespace

TEST_F(ModesSetupSessionTest, modes_root_is_the_games_index_with_seven_games_and_random_last)
{
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    EXPECT_EQ(5u, session.steps().size())
        << "picker_menu(\"\") must answer the games index";
    ASSERT_EQ(Step::Game, session.step());
    ASSERT_EQ(8u, session.page().rows.size())
        << "the seven games, then the host's RANDOM row";
    for (std::size_t i = 0; i < 7; i++) {
        EXPECT_EQ(og::ui::CampaignPickerSession::Kind::Page,
                  session.page().rows[i].base.kind)
            << session.page().rows[i].base.id;
    }
    // The roll is appended LAST, so no game's ordinal moved when it
    // arrived — and it is an ACTION, invisible to the ARENA tab's
    // arena_page lookup, which matches PAGE rows only.
    const MatchSetupSession::Row& random = session.page().rows[7];
    EXPECT_EQ(og::ui::CampaignPickerSession::Kind::Action, random.base.kind);
    EXPECT_EQ("random", random.base.id);
    EXPECT_EQ("RANDOM", random.base.label);
}

TEST_F(ModesSetupSessionTest, modes_820_arena_tab_shows_soccer_with_current_on_the_pitch)
{
    save_.scen_num = 820;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    EXPECT_EQ("soccer", session.knobs().arena_page);
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Arena, inputs()).kind);
    ASSERT_EQ(5u, session.page().rows.size())
        << "soccer's four arenas, then RANDOM ARENA";
    EXPECT_EQ(820, session.page().rows[0].base.level);
    EXPECT_TRUE(session.page().rows[0].base.current);
    EXPECT_EQ(0, session.page().page.page);
    const MatchSetupSession::Row& random = session.page().rows[4];
    EXPECT_EQ(og::ui::CampaignPickerSession::Kind::Action, random.base.kind);
    EXPECT_EQ("random_soccer", random.base.id);
    EXPECT_EQ("RANDOM ARENA", random.base.label);
}

// 508, not 507: with the progress line gone the CTF page carries ONE line
// (the rule line), so its window holds eight rows and 507 — index 7 — sits
// on window 1, where "opens on the window holding [CURRENT]" would pass
// trivially. 508 is index 8, the first row of window 2, which is also the
// window RANDOM ARENA (row 10 of 11) lands on.
TEST_F(ModesSetupSessionTest, modes_508_arena_tab_opens_the_ctf_window_holding_current)
{
    save_.scen_num = 508;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    EXPECT_EQ("ctf", session.knobs().arena_page);
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Arena, inputs()).kind);
    ASSERT_EQ(11u, session.page().rows.size())
        << "ten CTF arenas, then RANDOM ARENA";
    EXPECT_EQ(8, session.page().page.rows_per_page)
        << "one line on the page leaves eight rows per window";
    EXPECT_TRUE(session.page().page.multi_page());
    EXPECT_EQ(1, session.page().page.page)
        << "the window opens on the page that holds [CURRENT]";
    EXPECT_EQ(508, session.page().rows[8].base.level);
    EXPECT_TRUE(session.page().rows[8].base.current);
    EXPECT_EQ("random_ctf", session.page().rows[10].base.id)
        << "the second window holds 508, 509 and the roll";
}

// ---------------------------------------------------------------------------
// The remaining arms of choose() and the driver, each with its own tooth.
// ---------------------------------------------------------------------------

namespace {

// A root page carrying the other two row kinds a book may hold: a free
// action (which answers with the campaign's toast) and a priced one the
// wallet cannot cover (which the session refuses before it dispatches).
constexpr const char* kActionBook = R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    if page_id == "" then
      return { title = "GAMES", entries = {
        { id = "roll",  label = "ROLL",  kind = "action" },
        { id = "buy",   label = "FIELD KIT", kind = "action", cost = 999 },
        { id = "gone",  label = "NOWHERE", kind = "page" },
        { id = "soccer", label = "SOCCER", kind = "page" },
      } }
    end
    if page_id == "soccer" then
      return { title = "SOCCER", entries = {
        { id = "820", label = "THE PITCH", kind = "level", level = 820 },
      } }
    end
    return nil
  end,
  picker_action = function(id)
    return { message = "The dice land on THE PITCH." }
  end,
  match_knobs = function()
    return { arena_page = "soccer" }
  end,
}))LUA";

}  // namespace

// The book's own Action and Page rows keep the CampaignPickerSession
// contract inside the wizard: an action stays on the step and carries the
// campaign's toast, an unaffordable one and an unreadable page are refused,
// and a refusal never advances.
TEST_F(MatchSetupSessionTest, book_action_and_refusal_rows_stay_on_the_step)
{
    og::script::register_pack_script(
        {kSetupPack, "matchsetup/scripts/actions.lua", kActionBook});
    save_.m_totalcash[0] = 10;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    ASSERT_EQ(4u, session.page().rows.size());

    const MatchSetupSession::Outcome acted = session.choose(0, inputs());
    EXPECT_EQ(Kind::Stayed, acted.kind);
    EXPECT_EQ("The dice land on THE PITCH.", acted.message);
    EXPECT_EQ(Step::Game, session.step());

    const MatchSetupSession::Outcome poor = session.choose(1, inputs());
    EXPECT_EQ(Kind::Refused, poor.kind);
    EXPECT_FALSE(poor.message.empty());
    EXPECT_EQ(Step::Game, session.step()) << "a refusal never advances";

    // A page the book will not hand back is refused in the shared words.
    const MatchSetupSession::Outcome unreadable =
        session.choose(2, inputs());
    EXPECT_EQ(Kind::Refused, unreadable.kind);
    EXPECT_EQ(std::string(og::ui::kCampaignPageUnreadableMessage),
              unreadable.message);
    EXPECT_EQ(Step::Game, session.step());

    // A deeper page opened FROM the ARENA step stays on ARENA (a depth-3
    // book is still the arena tab's business).
    ASSERT_EQ(Kind::Advanced, session.choose(3, inputs()).kind);
    EXPECT_EQ(Step::Arena, session.step());
}

// The ARENA window pagers, the manifest's own SetLevel answer, an absent
// step and a scenario the mount cannot name.
TEST_F(MatchSetupSessionTest, manifest_paging_absent_steps_and_a_nameless_scenario)
{
    register_knobs_only("{}");  // bookless: the ARENA step is the manifest
    save_.scen_num = 300;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    ASSERT_EQ(Step::Arena, session.step());
    ASSERT_TRUE(session.page().page.multi_page())
        << "forty arenas do not fit one window";

    const int first_page = session.page().page.page;
    session.page_step(+1);
    EXPECT_EQ(first_page + 1, session.page().page.page);
    session.page_step(-1);
    EXPECT_EQ(first_page, session.page().page.page);
    session.page_step(-1);
    EXPECT_EQ(0, session.page().page.page) << "the window clamps";

    // A manifest row answers SetLevel and writes nothing, exactly as a book
    // level row does.
    const MatchSetupSession::Outcome out = session.choose(0, inputs());
    EXPECT_EQ(Kind::SetLevel, out.kind);
    EXPECT_EQ(list_levels_v().front(), out.level);
    EXPECT_EQ(300, static_cast<int>(save_.scen_num));

    // There is no GAME step on a bookless campaign, so the tab is inert.
    EXPECT_EQ(Kind::Stayed, session.goto_step(Step::Game, inputs()).kind);
    EXPECT_EQ(Step::Arena, session.step());

    // A cursor the mount cannot name says its number rather than the
    // loader's "none".
    save_.scen_num = 32000;
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Match, inputs()).kind);
    EXPECT_EQ("SCEN 32000", session.page().lines[0]);
}

// The three terminal door notices: a prompt reaches LINEUP, VIEW LEVEL and
// GO from Team Build, so the wizard's doors say where the page is instead of
// nesting a second copy of it.
TEST_F(MatchSetupSessionTest, terminal_driver_doors_point_at_their_pages)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 820;
    save_.numplayers = 1;
    put(save_, 0, 0, true);
    save_.team_size = 1;
    save_.arena_lineup_dealt_campaign = "modes";
    save_.arena_lineup_dealt_scen = 820;

    og::server::MatchStage stage({
        .networked = false,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &save_,
    });
    ScriptedSetupIo scripted;
    scripted.save = &save_;
    scripted.stage = &stage;
    scripted.answers = {
        "9",  // GAME: seven game pages, RANDOM, then Next: TEAMS
        "2",  // TEAMS: the LINEUP door
        "3",  // TEAMS: Next -> RULES
        "3",  // RULES: two rule rows, then Next: MATCH
        "1",  // MATCH: the VIEW LEVEL door
        "2",  // MATCH: GO
        "0",
    };
    og::ui::run_terminal_match_setup(save_, scripted.io());

    ASSERT_EQ(scripted.answers.size(), scripted.cursor)
        << "the loop must consume every scripted answer";
    // The whole notice tail, in walk order: three doors, three pointers and
    // nothing else (an extra or reordered notice is a change of behaviour).
    EXPECT_EQ((std::vector<std::string>{
                  std::string(og::ui::kSetupTerminalLineupNotice),
                  std::string(og::ui::kSetupTerminalViewLevelNotice),
                  std::string(og::ui::kSetupTerminalGoNotice)}),
              scripted.notices);
}

// Every wizard knob rides the .gtl now (SIDES/FILL/BandFill write fill[],
// SCORE and TIME LIMIT their own fields), so the driver banks after every
// turn -- the session-only knobs left with R2-3. A four-side arena, one
// turn of each of the four wheels.
TEST_F(MatchSetupSessionTest, terminal_driver_autosaves_after_every_turn)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 822;
    save_.numplayers = 1;
    put(save_, 0, 0, true);
    save_.team_size = 1;

    og::server::MatchStage stage({
        .networked = false,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &save_,
    });
    ScriptedSetupIo scripted;
    scripted.save = &save_;
    scripted.stage = &stage;
    scripted.answers = {"@Next: TEAMS", "@SIDES", "@FILL", "@Next: RULES",
                        "@SCORE", "@TIME LIMIT", "0"};
    og::ui::run_terminal_match_setup(save_, scripted.io());

    ASSERT_EQ(scripted.answers.size(), scripted.cursor)
        << "the loop must consume every scripted answer";
    EXPECT_EQ(1, static_cast<int>(save_.ctf_capture_limit))
        << "the SCORE wheel still steps";
    EXPECT_EQ(3600, static_cast<int>(save_.time_limit))
        << "the TIME LIMIT wheel still steps";
    EXPECT_EQ(5, scripted.autosaves)
        << "the 822 deal plus one per turn";
    EXPECT_TRUE(scripted.notices.empty())
        << "a turn says nothing: the redrawn face is the answer";
}

// The degraded preview reaches the prompt. The census writes its report on
// EVERY arm, so a cursor the mount cannot stage leads the MATCH step with
// the report's own refusal line and puts the refusal on GO's face -- the
// driver must hand the session the report, never a null.
TEST_F(MatchSetupSessionTest, terminal_driver_prints_the_degraded_preview)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 999;  // no such level in the modes mount
    save_.numplayers = 1;
    put(save_, 0, 0, true);
    save_.team_size = 1;
    save_.arena_lineup_dealt_campaign = "modes";
    save_.arena_lineup_dealt_scen = 999;  // the deal is already banked

    og::server::MatchStage stage({
        .networked = false,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &save_,
    });
    ScriptedSetupIo scripted;
    scripted.save = &save_;
    scripted.stage = &stage;
    scripted.answers = {"@Next", "@Next", "@Next", "0"};
    og::ui::run_terminal_match_setup(save_, scripted.io());

    ASSERT_EQ(scripted.answers.size(), scripted.cursor)
        << "the loop must consume every scripted answer";
    ASSERT_EQ(4u, scripted.prompts.size());
    EXPECT_EQ("SETUP: MATCH", scripted.prompts[3].title);

    // The stage falls back to a level that is not this cursor, so the
    // census answers Unavailable and the report says so in its own words.
    const std::string match = scripted.page_text(3);
    EXPECT_NE(std::string::npos, match.find("SCEN 999\n")) << match;
    EXPECT_NE(std::string::npos, match.find("PREVIEW UNAVAILABLE\n"))
        << match;
    EXPECT_NE(std::string::npos,
              match.find(std::string(og::ui::kSetupGoStagingFailedFace)))
        << match;
}

// ONE census rule, every surface. The wizard's TEAMS cell, the SDL LINEUP
// column and the terminal LINEUP column all answer format_match_preview
// over the SAME staged report (§3.8.4) — a player who walks one door from
// LINEUP into the wizard must not be told two different things about one
// world. What is NOT in that cell anywhere is B4's "the map ships this
// team no units": SDL dims the MAP UNITS caption beside its box, and the
// terminals, which cannot dim anything, print those words as a cell of
// their own beside the census. Same rule, two renderings, one column.
TEST_F(MatchSetupSessionTest, the_census_cell_reads_one_rule_on_every_surface)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 820;
    save_.fill[0] = kFair;
    save_.fill[1] = kFair;

    og::ui::ScenarioRosterReport report;
    report.is_versus = true;
    report.staged = true;
    report.mode_census = true;
    report.mode_name = "SOCCER";
    for (int t = 0; t < 2; ++t) {
        report.team_active[static_cast<std::size_t>(t)] = true;
        report.team_fill[static_cast<std::size_t>(t)] =
            og::ui::ScenarioFill::Matched;
        report.team_fill_count[static_cast<std::size_t>(t)] = 2;
    }
    staged_ = &report;
    health_ = Health::Staged;
    // Team 1 has map units, team 2 has none: the B4 signal is live on
    // exactly one of the two lines.
    counts_ = {3, 0, 0, 0};

    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs(true, 0b0011)));
    ASSERT_EQ(Kind::Advanced, session.goto_step(Step::Teams, inputs(true, 0b0011)).kind);
    const MatchSetupSession::Inputs in = inputs(true, 0b0011);
    ASSERT_EQ(2u, session.page().team_lines.size());

    // The bands both LINEUP pages build, from the same inputs.
    const std::array<og::ui::LineupTeamBand, 4> bands =
        og::ui::build_lineup_bands(save_, in.players, in.local_indices,
                                   in.networked, og::ui::lineup_power_for_guy,
                                   in.seat_short_name, in.map_unit_counts);

    og::ui::TerminalLineupInputs lineup;
    lineup.save = &save_;
    lineup.players = in.players;
    lineup.local_player_indices = in.local_indices;
    lineup.map_unit_counts = in.map_unit_counts;
    lineup.report = &report;
    lineup.networked = in.networked;
    const og::ui::TerminalLineupModel model =
        og::ui::build_terminal_lineup_model(lineup);

    for (int team = 0; team < 2; ++team) {
        const og::ui::LineupTeamBand& band =
            bands[static_cast<std::size_t>(team)];
        // The SDL LINEUP column and the wizard's TEAMS cell are literally
        // the same call (menu_screen_specs.cpp draws cells.census).
        const og::ui::SetupTeamLineCells cells =
            og::ui::compose_setup_team_line(band, &report, team, 18, 20);
        const std::string preview =
            og::ui::format_match_preview(band, &report, team);
        EXPECT_EQ(preview, cells.census)
            << "team " << team << ": the wizard cell IS the preview";
        EXPECT_EQ(cells.census,
                  session.page()
                      .team_lines[static_cast<std::size_t>(team)]
                      .census)
            << "team " << team;
        // The terminal LINEUP line for this band carries the same census…
        const std::string& line =
            model.lines[static_cast<std::size_t>(team) * 2 + 1];
        EXPECT_NE(std::string::npos, line.find(preview))
            << "team " << team << ": the terminal column reads it too: '"
            << line << "'";
        // …and NO surface hides the B4 signal inside it.
        EXPECT_EQ(std::string::npos, cells.census.find("NO MAP UNITS"))
            << "team " << team
            << ": the inert-box signal is never the census cell";
    }

    // The terminal's own rendering of the dim: the words ride BESIDE the
    // cell, and only for the team whose map ships nothing.
    EXPECT_EQ(std::string::npos, model.lines[1].find("NO MAP UNITS"))
        << model.lines[1];
    EXPECT_NE(std::string::npos, model.lines[3].find("NO MAP UNITS"))
        << "a terminal cannot dim a caption, so it says the words: '"
        << model.lines[3] << "'";

    staged_ = nullptr;
    health_ = Health::None;
    counts_ = {};
}


// --- Round 2 (PR #307): fix B, the [CLEARED] seam, the Acted level ------

// A company the previous build collapsed carries the memo, so no re-deal
// runs and fix B cannot see the trap: SIDES stays at the two bands that are
// on. That is the stated residual (R2-R5) for a CURRENT-version file; the
// v19 -> v20 bump is what heals the files that already exist.
TEST(MatchSetupRules, fill_from_the_r1_collapsed_state_respects_the_stamped_memo)
{
    SaveData save;
    save.current_campaign = "modes";
    save.scen_num = 822;
    save.fill = {kWeak, kWeak, kNone, kNone};
    save.arena_lineup_dealt_campaign = "modes";
    save.arena_lineup_dealt_scen = 822;

    EXPECT_FALSE(og::ui::deal_arena_lineup_fill(save, kFourSides, kStrong))
        << "the memo is stamped: this cursor never deals again";
    EXPECT_EQ("SIDES: 2", sides(save, 0));

    turn_fill(save, 0);
    EXPECT_EQ((Fills{kFair, kFair, kNone, kNone}), save.fill)
        << "two bands are on, so the FILL turn writes those two";
    EXPECT_EQ("SIDES: 2", sides(save, 0));

    // The way out the player has without the format bump.
    turn_sides(save, 0);
    EXPECT_EQ((Fills{kFair, kFair, kFair, kNone}), save.fill);
    EXPECT_EQ("SIDES: 3", sides(save, 0));
}

// The note names the WHEEL, not the face: an off-wheel face (NONE, MIXED)
// rejoins at WEAK, so the row keeps saying "weak to brutal" on every face.
// The LINEUP band wheel keeps its own note, which still includes NONE.
TEST_F(MatchSetupSessionTest, fill_note_is_the_wheel_not_the_face)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 822;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    ASSERT_EQ(Kind::Advanced,
              session.goto_step(Step::Teams, inputs(true, 0b1111)).kind);

    const std::vector<std::pair<std::array<short, 4>, const char*>> faces = {
        {{og::sim::kFillStrong, og::sim::kFillStrong, og::sim::kFillStrong,
          og::sim::kFillStrong}, "FILL: STRONG"},
        {{0, 0, 0, 0}, "FILL: NONE"},
        {{0, og::sim::kFillWeak, og::sim::kFillStrong, 0}, "FILL: MIXED"},
    };
    for (const auto& [fills, face] : faces) {
        save_.fill = fills;
        session.refetch(inputs(true, 0b1111));
        const int at = row_index(session, "fill");
        ASSERT_GE(at, 0) << face;
        const MatchSetupSession::Row& row =
            session.page().rows[static_cast<std::size_t>(at)];
        EXPECT_EQ(face, row.base.label);
        EXPECT_EQ(std::string(og::ui::kMatchFillNote), row.base.note)
            << "the note is the wheel's, whatever the face reads";
    }

    // A BAND fixture's row is the LINEUP wheel, which keeps NONE.
    og::script::clear_pack_scripts();
    register_book(R"({ teams = false, fill = "band", arena_page = "soccer" })");
    save_.fill = {};
    MatchSetupSession band(save_);
    ASSERT_TRUE(band.open(inputs()));
    ASSERT_EQ(Kind::Advanced, band.goto_step(Step::Teams, inputs()).kind);
    const int band_at = row_index(band, "fill");
    ASSERT_GE(band_at, 0);
    EXPECT_EQ(std::string(og::ui::kLineupFillNote),
              band.page().rows[static_cast<std::size_t>(band_at)].base.note);
}

// R2-5 / D3: a book ACTION that answered with a level routes through the
// surface's existing gated level tail exactly as a level ROW does -- and
// never arms a replay (#207: only level rows arm).
TEST_F(MatchSetupSessionTest, acted_row_with_a_level_answers_set_level_and_never_arms_replay)
{
    register_book(kSoccerKnobs);
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs()));
    ASSERT_EQ(8u, session.page().rows.size())
        << "seven game pages and the host's RANDOM row, appended LAST";
    EXPECT_EQ("random", session.page().rows[7].base.id);

    const MatchSetupSession::Outcome out = session.choose(7, inputs());
    EXPECT_EQ(Kind::SetLevel, out.kind);
    EXPECT_EQ(822, out.level);
    EXPECT_FALSE(out.replay_arm);
    EXPECT_TRUE(out.message.empty())
        << "the engine's 'Level set to' is the click's one answer";
    EXPECT_EQ(Step::Game, session.step())
        << "the surface's level tail advances, not choose()";
}

// The [CLEARED] seam's second consequence, pinned rather than incidental:
// `cleared` also feeds Row::replay_arms(), so on a versus campaign no
// wizard level row can arm a replay -- while the SAME row read through a
// bare CampaignPickerSession still does.
TEST_F(MatchSetupSessionTest, versus_level_rows_never_arm_replay_in_the_wizard)
{
    register_book(kSoccerKnobs);
    save_.add_level_completed("modes", 821);
    save_.scen_num = 820;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs(), "soccer"));
    ASSERT_EQ(Step::Arena, session.step());
    ASSERT_GE(session.page().rows.size(), 2u);
    ASSERT_EQ(821, session.page().rows[1].base.level);
    EXPECT_FALSE(session.page().rows[1].base.cleared);
    EXPECT_FALSE(session.page().rows[1].base.replay_arms());

    const MatchSetupSession::Outcome out = session.choose(1, inputs());
    EXPECT_EQ(Kind::SetLevel, out.kind);
    EXPECT_EQ(821, out.level);
    EXPECT_FALSE(out.replay_arm);

    // The same book, read raw: the engine's own decoration is untouched.
    og::ui::CampaignPickerSession raw(save_);
    ASSERT_TRUE(raw.open_at("soccer"));
    ASSERT_GE(raw.page().rows.size(), 2u);
    ASSERT_EQ(821, raw.page().rows[1].level);
    EXPECT_TRUE(raw.page().rows[1].cleared);
    EXPECT_TRUE(raw.page().rows[1].replay_arms());
}

// R2-4: Multiplayer Arenas carries no progress vocabulary, so the wizard
// blanks the engine's [CLEARED] tail -- and only the wizard's, so every
// other campaign keeps it.
TEST_F(MatchSetupSessionTest, cleared_arenas_wear_no_tail_in_the_wizard_but_the_raw_book_does)
{
    register_book(kSoccerKnobs);
    save_.add_level_completed("modes", 821);
    save_.scen_num = 820;
    MatchSetupSession session(save_);
    ASSERT_TRUE(session.open(inputs(), "soccer"));
    ASSERT_GE(session.page().rows.size(), 2u);

    const std::string cleared_row =
        og::ui::campaign_picker_row_text(session.page().rows[1].base, 72);
    EXPECT_EQ(std::string::npos, cleared_row.find("[CLEARED]")) << cleared_row;
    const std::string current_row =
        og::ui::campaign_picker_row_text(session.page().rows[0].base, 72);
    EXPECT_NE(std::string::npos, current_row.find("[CURRENT]"))
        << "[CURRENT] stays: it is the only mark that says which arena is "
           "armed -- " << current_row;

    og::ui::CampaignPickerSession raw(save_);
    ASSERT_TRUE(raw.open_at("soccer"));
    ASSERT_GE(raw.page().rows.size(), 2u);
    EXPECT_NE(std::string::npos,
              og::ui::campaign_picker_row_text(raw.page().rows[1], 72)
                  .find("[CLEARED]"));
}

// R2-5 / R2-D15: the terminal driver routes an Acted level through the
// camp's own convention -- a level ROW speaks its row label, an ACTION
// speaks the LOADED title.
TEST_F(MatchSetupSessionTest, terminal_driver_routes_an_acted_level_through_the_gated_tail)
{
    register_book(kSoccerKnobs);
    save_.scen_num = 300;
    save_.numplayers = 1;
    put(save_, 0, 0, true);
    save_.team_size = 1;
    save_.arena_lineup_dealt_campaign = "modes";
    save_.arena_lineup_dealt_scen = 300;

    og::server::MatchStage stage({
        .networked = false,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &save_,
    });
    ScriptedSetupIo scripted;
    scripted.save = &save_;
    scripted.stage = &stage;
    std::vector<int> applied;
    scripted.answers = {
        "5",               // GAME: SOCCER -> ARENA
        "2",               // a level ROW: THE MUDBOWL (821)
        "@Prev: ARENA",
        "@RANDOM ARENA",   // an ACTION answering a level: 823
        "@Prev: ARENA",
        "@Prev: GAME",
        "@RANDOM",         // the root's action: 822
        "0",
    };
    og::ui::TerminalMatchSetupIo io = scripted.io();
    const std::function<void(int, bool)> inner = io.base.apply_level;
    io.base.apply_level = [&](int level, bool replay_arm) {
        applied.push_back(level);
        inner(level, replay_arm);
    };
    og::ui::run_terminal_match_setup(save_, io);

    ASSERT_EQ(scripted.answers.size(), scripted.cursor)
        << "the loop must consume every scripted answer";
    EXPECT_EQ((std::vector<int>{821, 823, 822}), applied);
    EXPECT_EQ(822, static_cast<int>(save_.scen_num));

    const auto loaded_title = [](int level) {
        std::string title;
        (void)og::data::load_scenario_title_with_error(
            ("scen" + std::to_string(level)).c_str(), title);
        return title;
    };
    EXPECT_EQ((std::vector<std::string>{
                  "Level set to THE MUDBOWL.",
                  og::ui::campaign_level_set_message(loaded_title(823)),
                  og::ui::campaign_level_set_message(loaded_title(822))}),
              scripted.notices);
    EXPECT_NE("Level set to BONEYARD CUP.", scripted.notices[1])
        << "the action speaks the LOADED title, the row its own label";

    // Every set lands on TEAMS, the way a level row's set does.
    ASSERT_FALSE(scripted.prompts.empty());
    EXPECT_EQ("SETUP: TEAMS", scripted.prompts.back().title);
}

// R2-D12's message half: a no-level Acted row's Lua voice reaches the
// terminal notice (it used to be dropped -- the Acted arm writes
// Outcome::message and the driver read the session's own slot).
TEST_F(MatchSetupSessionTest, terminal_driver_prints_the_books_voice_for_a_no_level_action)
{
    og::script::register_pack_script(
        {kSetupPack, "matchsetup/scripts/actions.lua", kActionBook});
    save_.scen_num = 820;
    save_.numplayers = 1;
    put(save_, 0, 0, true);
    save_.team_size = 1;
    save_.arena_lineup_dealt_campaign = "modes";
    save_.arena_lineup_dealt_scen = 820;

    og::server::MatchStage stage({
        .networked = false,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &save_,
    });
    ScriptedSetupIo scripted;
    scripted.save = &save_;
    scripted.stage = &stage;
    scripted.answers = {"1", "0"};
    og::ui::run_terminal_match_setup(save_, scripted.io());

    ASSERT_EQ(scripted.answers.size(), scripted.cursor);
    EXPECT_EQ((std::vector<std::string>{"The dice land on THE PITCH."}),
              scripted.notices);
    EXPECT_EQ(820, static_cast<int>(save_.scen_num)) << "no level was carried";
    ASSERT_EQ(2u, scripted.prompts.size());
    EXPECT_EQ("SETUP: GAME", scripted.prompts[0].title);
    EXPECT_EQ("SETUP: GAME", scripted.prompts[1].title);
}
