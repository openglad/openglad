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
// Suite MatchSetupRules is the PURE half of this file. The step machine
// and the terminal driver halves (MatchSetupSession itself) arrive with
// WP5 and land in this same file.
//
// Ten of the macro tests below are ports of tests/unit/test_modes_book.cpp
// :1421-1935, which drove the SAME rules through campaign_picker.lua. The
// said lines, the faces and the fill arrays are the same bytes: that is
// the point of the port, and the Lua twins do not die until both agree.

#include <gtest/gtest.h>

#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <array>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

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

std::string turn_sides(SaveData& save, int my_team, int dir = +1,
                       std::uint8_t mask = kFourSides)
{
    return og::ui::turn_match_sides(save, my_team, mask, dir);
}

std::string turn_fill(SaveData& save, int my_team, int dir = +1,
                      std::uint8_t mask = kFourSides)
{
    return og::ui::turn_match_fill(save, my_team, mask, dir);
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
        const char* said;
        const char* sides_face;
        const char* fill_face;
        Fills fills;
    };
    const std::vector<Step> steps = {
        {"Two sides. One squad at FAIR.", "SIDES: 2", "FILL: FAIR",
         {kNone, kFair, kNone, kNone}},
        {"Three sides. Two squads at FAIR.", "SIDES: 3", "FILL: FAIR",
         {kNone, kFair, kFair, kNone}},
        {"Four sides. Three squads at FAIR.", "SIDES: 4", "FILL: FAIR",
         {kNone, kFair, kFair, kFair}},
        {"Two sides. One squad at FAIR.", "SIDES: 2", "FILL: FAIR",
         {kNone, kFair, kNone, kNone}},
    };
    for (const Step& step : steps)
    {
        EXPECT_EQ(step.said, turn_sides(save, 0)) << step.sides_face;
        EXPECT_EQ(step.fills, save.fill);
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

    EXPECT_EQ("Three sides. Two squads at STRONG.", turn_sides(save, 0));
    EXPECT_EQ((Fills{kNone, kStrong, kStrong, kNone}), save.fill)
        << "the new side copies the face, the standing one keeps it";

    // LINEUP diverges a band (G1 keeps that its right): the face reads
    // MIXED, and the next SIDES turn falls back to FAIR for every side it
    // deals, because a diverged pair names nothing a new side could copy.
    save.fill[1] = kWeak;
    EXPECT_EQ("SIDES: 3", sides(save, 0));
    EXPECT_EQ("FILL: MIXED", fill(save, 0));
    EXPECT_EQ("Four sides. Three squads at FAIR.", turn_sides(save, 0));
    EXPECT_EQ((Fills{kNone, kFair, kFair, kFair}), save.fill);
}

// Ported from
// ModesBookTest::fill_macro_turns_on_the_lowest_opponent_then_every_on_side
// (test_modes_book.cpp:1723).
TEST(MatchSetupRules, fill_turns_on_the_lowest_opponent_then_every_on_side)
{
    SaveData save;
    struct Step {
        const char* said;
        const char* fill_face;
        const char* sides_face;
        Fills fills;
    };
    const std::vector<Step> steps = {
        {"One squad at WEAK. Yours too.", "FILL: WEAK", "SIDES: 2",
         {kWeak, kWeak, kNone, kNone}},
        {"One squad at FAIR. Yours too.", "FILL: FAIR", "SIDES: 2",
         {kFair, kFair, kNone, kNone}},
        {"One squad at STRONG. Yours too.", "FILL: STRONG", "SIDES: 2",
         {kStrong, kStrong, kNone, kNone}},
        {"One squad at BRUTAL. Yours too.", "FILL: BRUTAL", "SIDES: 2",
         {kBrutal, kBrutal, kNone, kNone}},
        {"No squads.", "FILL: NONE", "SIDES: 1",
         {kNone, kNone, kNone, kNone}},
    };
    for (const Step& step : steps)
    {
        EXPECT_EQ(step.said, turn_fill(save, 0)) << step.fill_face;
        EXPECT_EQ(step.fills, save.fill);
        EXPECT_EQ(step.fill_face, fill(save, 0));
        EXPECT_EQ(step.sides_face, sides(save, 0));
    }

    // With sides standing, the step writes every ON opponent and the own
    // band with them (H1).
    save.fill = {kNone, kFair, kFair, kNone};
    EXPECT_EQ("Two squads at STRONG. Yours too.", turn_fill(save, 0));
    EXPECT_EQ((Fills{kStrong, kStrong, kStrong, kNone}), save.fill);

    // A MIXED face is off the wheel and rejoins at NONE, which clears the
    // own band with the rest.
    save.fill[2] = kWeak;
    EXPECT_EQ("FILL: MIXED", fill(save, 0));
    EXPECT_EQ("No squads.", turn_fill(save, 0));
    EXPECT_EQ((Fills{kNone, kNone, kNone, kNone}), save.fill);
    EXPECT_EQ("SIDES: 1", sides(save, 0));
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

    EXPECT_EQ("Two sides. One squad at BRUTAL.", turn_sides(save, 2));
    EXPECT_EQ((Fills{kBrutal, kNone, kBrutal, kNone}), save.fill)
        << "team 0 is the lowest opponent of a seat on team 2";

    EXPECT_EQ("No squads.", turn_fill(save, 2));
    EXPECT_EQ((Fills{kNone, kNone, kNone, kNone}), save.fill);
    EXPECT_EQ("SIDES: 1", sides(save, 2));

    EXPECT_EQ("One squad at WEAK. Yours too.", turn_fill(save, 2));
    EXPECT_EQ((Fills{kWeak, kNone, kWeak, kNone}), save.fill);

    EXPECT_EQ("Three sides. Two squads at WEAK.", turn_sides(save, 2));
    EXPECT_EQ((Fills{kWeak, kWeak, kWeak, kNone}), save.fill);
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

// Both wheels step backwards, which the Lua's next_value could not do:
// the reverse cell, the terminal "N-" item and a right-click all land
// here. An off-wheel value still rejoins at the HEAD either way — it is
// one rule, not a direction-dependent pair.
TEST(MatchSetupRules, reverse_step_walks_every_wheel_backward)
{
    SaveData save;
    EXPECT_EQ("Two sides. One squad at FAIR.", turn_sides(save, 0, -1))
        << "SIDES: 1 is off the wheel: either direction rejoins at 2";
    EXPECT_EQ((Fills{kNone, kFair, kNone, kNone}), save.fill);
    EXPECT_EQ("Four sides. Three squads at FAIR.", turn_sides(save, 0, -1))
        << "2 steps back to the tail of the 2 -> 3 -> 4 wheel";
    EXPECT_EQ((Fills{kNone, kFair, kFair, kFair}), save.fill);

    SaveData fresh;
    EXPECT_EQ("One squad at BRUTAL. Yours too.", turn_fill(fresh, 0, -1))
        << "NONE steps back to the tail of the fill wheel";
    EXPECT_EQ((Fills{kBrutal, kBrutal, kNone, kNone}), fresh.fill);

    SaveData mixed;
    mixed.fill = {kNone, kWeak, kStrong, kNone};
    EXPECT_EQ("FILL: MIXED", fill(mixed, 0));
    EXPECT_EQ("No squads.", turn_fill(mixed, 0, -1))
        << "MIXED is off the wheel: either direction rejoins at NONE";
    EXPECT_EQ((Fills{kNone, kNone, kNone, kNone}), mixed.fill);
}

// --- The TIME LIMIT and SCORE wheels -----------------------------------

TEST(MatchSetupRules, time_limit_wheel_faces_and_toasts)
{
    SaveData save;
    EXPECT_EQ("TIME LIMIT: MAP", og::ui::format_time_limit_label(save));
    EXPECT_EQ("Clock: the map's own.", og::ui::format_time_limit_said(save));

    struct Step {
        short ticks;
        const char* face;
        const char* said;
    };
    const std::vector<Step> steps = {
        {3600, "TIME LIMIT: 5 MIN", "Clock: 5 minutes."},
        {7200, "TIME LIMIT: 10 MIN", "Clock: 10 minutes."},
        {10800, "TIME LIMIT: 15 MIN", "Clock: 15 minutes."},
        {14400, "TIME LIMIT: 20 MIN", "Clock: 20 minutes."},
        {0, "TIME LIMIT: MAP", "Clock: the map's own."},
    };
    for (const Step& step : steps)
    {
        og::ui::cycle_time_limit(save, +1);
        EXPECT_EQ(step.ticks, save.time_limit);
        EXPECT_EQ(step.face, og::ui::format_time_limit_label(save));
        EXPECT_EQ(step.said, og::ui::format_time_limit_said(save));
    }

    og::ui::cycle_time_limit(save, -1);
    EXPECT_EQ(14400, save.time_limit) << "0 steps back to the wheel's tail";

    // A clock settled from a lobby or an older save wears the minutes it
    // holds (integer division, the determinism contract) and rejoins the
    // wheel at its head from either direction.
    save.time_limit = 2160;
    EXPECT_EQ("TIME LIMIT: 3 MIN", og::ui::format_time_limit_label(save));
    save.time_limit = 5400;
    EXPECT_EQ("TIME LIMIT: 7 MIN", og::ui::format_time_limit_label(save));
    EXPECT_EQ("Clock: 7 minutes.", og::ui::format_time_limit_said(save));
    og::ui::cycle_time_limit(save, +1);
    EXPECT_EQ(0, save.time_limit);
    save.time_limit = 5400;
    og::ui::cycle_time_limit(save, -1);
    EXPECT_EQ(0, save.time_limit);
}

TEST(MatchSetupRules, score_wheel_reverses_and_speaks)
{
    SaveData save;
    EXPECT_EQ("Score: the map's own.", og::ui::format_ctf_score_said(save));

    const std::vector<short> backward = {10, 5, 3, 1, 0};
    for (const short expected : backward)
    {
        og::ui::cycle_ctf_capture_limit(save, -1);
        EXPECT_EQ(expected, save.ctf_capture_limit);
    }

    save.ctf_capture_limit = 5;
    EXPECT_EQ("Score to 5.", og::ui::format_ctf_score_said(save));
    save.ctf_capture_limit = 10;
    EXPECT_EQ("Score to 10.", og::ui::format_ctf_score_said(save));

    // Junk rejoins at the head whichever way it is turned.
    save.ctf_capture_limit = 42;
    og::ui::cycle_ctf_capture_limit(save, -1);
    EXPECT_EQ(0, save.ctf_capture_limit);
    save.ctf_capture_limit = 42;
    og::ui::cycle_ctf_capture_limit(save, +1);
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
    EXPECT_EQ("Two sides. One squad at FAIR.", turn_sides(two, 0, +1, 0b0011));
    EXPECT_EQ((Fills{kNone, kFair, kNone, kNone}), two.fill);
    EXPECT_EQ("Two sides. One squad at FAIR.", turn_sides(two, 0, +1, 0b0011));
    EXPECT_EQ((Fills{kNone, kFair, kNone, kNone}), two.fill);
    EXPECT_EQ("SIDES: 2", sides(two, 0, 0b0011));

    // A three-side arena wheels 2 -> 3 -> 2.
    SaveData three;
    EXPECT_EQ("Two sides. One squad at FAIR.",
              turn_sides(three, 0, +1, 0b0111));
    EXPECT_EQ((Fills{kNone, kFair, kNone, kNone}), three.fill);
    EXPECT_EQ("Three sides. Two squads at FAIR.",
              turn_sides(three, 0, +1, 0b0111));
    EXPECT_EQ((Fills{kNone, kFair, kFair, kNone}), three.fill);
    EXPECT_EQ("Two sides. One squad at FAIR.",
              turn_sides(three, 0, +1, 0b0111));
    EXPECT_EQ((Fills{kNone, kFair, kNone, kNone}), three.fill);

    // A gap in the mask: the opponents of a seat on team 0 are 1 then 3,
    // and team 2 — which the map authors no markers for — is never written.
    SaveData gap;
    gap.fill[2] = kBrutal;  // a stale value the macro must leave alone
    EXPECT_EQ("Two sides. One squad at FAIR.", turn_sides(gap, 0, +1, 0b1011));
    EXPECT_EQ("Three sides. Two squads at FAIR.",
              turn_sides(gap, 0, +1, 0b1011));
    EXPECT_EQ((Fills{kNone, kFair, kBrutal, kFair}), gap.fill);

    // A mask of 0 behaves exactly as the four-side one.
    SaveData unknown;
    SaveData four;
    for (int step = 0; step < 5; ++step)
    {
        EXPECT_EQ(turn_sides(four, 0, +1, 0b1111),
                  turn_sides(unknown, 0, +1, 0));
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
    EXPECT_TRUE(og::ui::format_match_rules_lines({}).empty());

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
    EXPECT_EQ("none to brutal", std::string(og::ui::kMatchFillNote));

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
