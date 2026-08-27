/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The pure LINEUP helpers (docs/lineup-design.md §2.1, §3.1, §5, §6): the
// one team setter, the editable predicate, the four team bands, every label
// the page pins, the three SPLIT modes and the Networking machine rows.
// Headless by construction — no SDL, no lobby client, no Lua.

#include <gtest/gtest.h>

#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/save_data.h>

#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using og::ui::LineupSplit;
using og::ui::LineupTeamBand;

namespace {

// A roster slot: family, team, deployed, level.
void put(SaveData& save, int slot, short team, bool deployed, short level = 1)
{
    auto member = std::make_unique<guy>(FAMILY_SOLDIER);
    member->name = "F" + std::to_string(slot);
    member->teamnum = team;
    member->deployed = deployed;
    member->level = level;
    save.team_list[static_cast<std::size_t>(slot)] = std::move(member);
}

og::sim::LobbyPlayer seat(std::uint8_t index, short team,
                          std::string company = "IRON KETTLE",
                          bool host = false, bool ready = false,
                          og::sim::LobbyMachineId machine = 1)
{
    og::sim::LobbyPlayer player;
    player.player_index = index;
    player.seat_id = index;
    player.machine_id = machine;
    player.company = std::move(company);
    player.team = team;
    player.is_host = host;
    player.ready = ready;
    return player;
}

void add_character(og::sim::LobbyPlayer& player, short team, bool deployed)
{
    og::sim::LobbyCharacterSlot slot;
    slot.slot_index = static_cast<std::uint8_t>(player.character_slots.size());
    slot.character.family = FAMILY_SOLDIER;
    slot.character.teamnum = team;
    slot.character.level = 1;
    slot.deployed = deployed;
    player.character_slots.push_back(slot);
}

// A metric that prices a fighter by its level, so the tests can state the
// exact totals the bands and the FAIR draft must produce.
og::ui::LineupPowerFn level_power()
{
    return [](const guy& g) -> std::optional<long long> {
        return static_cast<long long>(g.level) * 100;
    };
}

// Restores the process-global slot-editable callback whatever the test does.
class EditableCallbackGuard {
public:
    explicit EditableCallbackGuard(og::ui::PickerSaveSlotEditableCallback cb)
        : previous_(og::ui::g_picker_save_slot_editable_callback)
    {
        og::ui::g_picker_save_slot_editable_callback = std::move(cb);
    }
    ~EditableCallbackGuard()
    {
        og::ui::g_picker_save_slot_editable_callback = previous_;
    }

private:
    og::ui::PickerSaveSlotEditableCallback previous_;
};

}  // namespace

// --- set_guy_team ------------------------------------------------------

TEST(LineupCommon, set_guy_team_writes_and_refuses)
{
    SaveData save;
    put(save, 0, 0, true);

    EXPECT_TRUE(og::ui::set_guy_team(save, 0, 3));
    EXPECT_EQ(3, save.team_list[0]->teamnum);

    // Out of the four colours: no write at all.
    EXPECT_FALSE(og::ui::set_guy_team(save, 0, 4));
    EXPECT_FALSE(og::ui::set_guy_team(save, 0, -1));
    EXPECT_EQ(3, save.team_list[0]->teamnum);

    EXPECT_FALSE(og::ui::set_guy_team(save, 1, 1)) << "empty slot";
    EXPECT_FALSE(og::ui::set_guy_team(save, -1, 1));
    EXPECT_FALSE(og::ui::set_guy_team(save, MAX_TEAM_SIZE, 1));
}

TEST(LineupCommon, cycle_guy_team_still_wraps_through_the_setter)
{
    SaveData save;
    put(save, 0, 3, true);
    EXPECT_EQ(0, og::ui::cycle_guy_team(save, 0, 1)) << "3 -> 0 wrap";
    EXPECT_EQ(3, og::ui::cycle_guy_team(save, 0, -1)) << "0 -> 3 wrap";
    EXPECT_EQ(3, save.team_list[0]->teamnum);
    EXPECT_EQ(-1, og::ui::cycle_guy_team(save, 1, 1));
}

// --- lineup_fighter_team_editable --------------------------------------

TEST(LineupCommon, fighter_team_editable_predicate)
{
    SaveData save;
    put(save, 0, 0, true);

    EXPECT_TRUE(og::ui::lineup_fighter_team_editable(save, 0, true, false));
    EXPECT_FALSE(og::ui::lineup_fighter_team_editable(save, 0, false, false))
        << "the zone composition cleared can_team";
    EXPECT_FALSE(og::ui::lineup_fighter_team_editable(save, 0, true, true))
        << "assign mode owns the chip column";
    EXPECT_FALSE(og::ui::lineup_fighter_team_editable(save, 1, true, false))
        << "empty slot";
    EXPECT_FALSE(og::ui::lineup_fighter_team_editable(save, -1, true, false));

    EditableCallbackGuard guard([](int slot) { return slot != 0; });
    EXPECT_FALSE(og::ui::lineup_fighter_team_editable(save, 0, true, false))
        << "a locked save slot is never editable";
}

TEST(LineupCommon, fighter_team_editable_null_zone_is_full_capability)
{
    SaveData save;
    put(save, 0, 0, true);
    const og::ui::CampaignZoneSession* none = nullptr;
    EXPECT_TRUE(og::ui::lineup_fighter_team_editable(save, 0, none, false));
    EXPECT_FALSE(og::ui::lineup_fighter_team_editable(save, 0, none, true));
}

// --- bands -------------------------------------------------------------

TEST(LineupCommon, bands_local_count_own_deployed_fighters)
{
    SaveData save;
    put(save, 0, 1, true);
    put(save, 1, 1, true);
    put(save, 2, 2, false);  // benched: not a fighter
    put(save, 3, 2, true);

    std::vector<og::sim::LobbyPlayer> players{seat(0, 1)};
    const auto bands = og::ui::build_lineup_bands(
        save, players, std::vector<std::uint8_t>{0}, false, {});

    EXPECT_EQ(0, bands[0].fighter_count);
    EXPECT_EQ(2, bands[1].fighter_count);
    EXPECT_EQ(1, bands[2].fighter_count) << "the benched one does not count";
    EXPECT_TRUE(bands[1].has_seat);
    EXPECT_FALSE(bands[2].has_seat);
    EXPECT_EQ(LineupTeamBand::Diag::None, bands[1].diag);
    EXPECT_EQ(LineupTeamBand::Diag::NoSeatAi, bands[2].diag)
        << "fighters with no seat fight under AI";
    EXPECT_EQ(LineupTeamBand::Diag::None, bands[3].diag)
        << "an empty team is not a diagnostic";
    EXPECT_EQ(std::vector<std::string>{"P1 IRO"}, bands[1].seat_labels);
}

TEST(LineupCommon, bands_needs_fighters_when_seats_outnumber_them)
{
    SaveData save;
    put(save, 0, 1, true);
    std::vector<og::sim::LobbyPlayer> players{seat(0, 1), seat(1, 1),
                                              seat(2, 1)};
    const auto bands = og::ui::build_lineup_bands(
        save, players, std::vector<std::uint8_t>{0, 1, 2}, false, {});
    EXPECT_EQ(3, bands[1].seat_count);
    EXPECT_EQ(1, bands[1].fighter_count);
    EXPECT_EQ(LineupTeamBand::Diag::NeedsFighters, bands[1].diag);
    EXPECT_EQ(2, bands[1].needs);
    EXPECT_EQ("NEEDS 2 FIGHTERS", og::ui::format_lineup_census(bands[1]));
}

TEST(LineupCommon, bands_networked_census_is_the_whole_lobby)
{
    SaveData save;
    // The own save is ignored networked: this machine's company reaches the
    // bands as replicated slots, and counting both would double it.
    put(save, 0, 1, true);

    og::sim::LobbyPlayer host = seat(0, 1, "IRON KETTLE", true, true, 1);
    add_character(host, 1, true);
    add_character(host, 1, false);  // benched
    og::sim::LobbyPlayer joiner = seat(1, 2, "BLUE BAND", false, false, 2);
    add_character(joiner, 2, true);
    add_character(joiner, 3, true);  // a colour with no seat

    std::vector<og::sim::LobbyPlayer> players{host, joiner};
    const auto bands = og::ui::build_lineup_bands(
        save, players, std::vector<std::uint8_t>{0}, true, level_power());

    EXPECT_EQ(1, bands[1].fighter_count);
    EXPECT_EQ(1, bands[2].fighter_count);
    EXPECT_EQ(1, bands[3].fighter_count);
    EXPECT_EQ(LineupTeamBand::Diag::NoSeatAi, bands[3].diag);
    EXPECT_EQ(std::vector<std::string>{"P1 IRO"}, bands[1].seat_labels);
    EXPECT_EQ(std::vector<std::string>{"P2 BLU"}, bands[2].seat_labels);
    ASSERT_TRUE(bands[1].power.has_value());
    EXPECT_EQ(100, *bands[1].power);
    EXPECT_FALSE(bands[0].power.has_value()) << "an empty team has no power";
}

TEST(LineupCommon, bands_power_needs_every_fighter_priced)
{
    SaveData save;
    put(save, 0, 1, true, 2);
    put(save, 1, 1, true, 3);
    std::vector<og::sim::LobbyPlayer> players{seat(0, 1)};

    const auto priced = og::ui::build_lineup_bands(
        save, players, std::vector<std::uint8_t>{0}, false, level_power());
    ASSERT_TRUE(priced[1].power.has_value());
    EXPECT_EQ(500, *priced[1].power);

    // One unpriced member voids the band: a total that omits a fighter is
    // worse than no number.
    const og::ui::LineupPowerFn partial =
        [](const guy& g) -> std::optional<long long> {
        if (g.level == 3)
            return std::nullopt;
        return 100;
    };
    const auto mixed = og::ui::build_lineup_bands(
        save, players, std::vector<std::uint8_t>{0}, false, partial);
    EXPECT_FALSE(mixed[1].power.has_value());

    // No metric at all (a classic campaign) is the same answer.
    const auto none = og::ui::build_lineup_bands(
        save, players, std::vector<std::uint8_t>{0}, false, {});
    EXPECT_FALSE(none[1].power.has_value());
}

// C8, the pure half: with NO presence span the band's resolved_fill is the
// STORED code, whatever it is — the documented honest fallback ("with
// nothing censused there is nothing to resolve against"). The resolved
// path itself needs a registered pack resolver and is pinned in
// test_campaign_hooks (the query) and test_platform_headless (the rows).
TEST(LineupCommon, bands_without_a_census_keep_the_stored_fill)
{
    SaveData save;
    save.fill[0] = og::sim::kFillFair;
    save.fill[1] = og::sim::kFillStrong;
    save.fill[2] = og::sim::kFillNone;
    std::vector<og::sim::LobbyPlayer> players{seat(0, 1)};
    const auto bands = og::ui::build_lineup_bands(
        save, players, std::vector<std::uint8_t>{0}, false, {});
    EXPECT_EQ(og::sim::kFillFair, bands[0].resolved_fill);
    EXPECT_EQ(og::sim::kFillStrong, bands[1].resolved_fill);
    EXPECT_EQ(og::sim::kFillNone, bands[2].resolved_fill);
    EXPECT_EQ("FILL: STRONG",
              og::ui::format_lineup_fill_label(bands[1].resolved_fill));
}

TEST(LineupCommon, seat_label_prefers_a_local_controller_name)
{
    SaveData save;
    std::vector<og::sim::LobbyPlayer> players{
        seat(0, 1, "IRON KETTLE"), seat(2, 1, "BLUE BAND", false, false, 2)};
    const auto bands = og::ui::build_lineup_bands(
        save, players, std::vector<std::uint8_t>{0}, true, {},
        [](std::uint8_t index) {
            return index == 0 ? std::string("WASD") : std::string();
        });
    ASSERT_EQ(2u, bands[1].seat_labels.size());
    EXPECT_EQ("P1 WASD", bands[1].seat_labels[0]);
    EXPECT_EQ("P3 BLU", bands[1].seat_labels[1])
        << "a remote seat has only its company";
    EXPECT_EQ("P1 NET",
              og::ui::lineup_seat_label(seat(0, 1, "  --  "), ""))
        << "a company with no letters still names a seat";
}

// --- labels ------------------------------------------------------------

// D1's scale, pinned numerically: the DEFAULT keeps 0 and the five
// EXPLICIT codes run 1..5 in wheel order. The numbers matter twice over —
// they are the wire's stored codes AND the facts slot's banked codes, and
// the fact encoding is unbiased ONLY because no explicit fill is 0 (the
// digit pair a team that banked nothing leaves behind).
TEST(LineupCommon, fill_codes_leave_zero_free_for_the_facts_slot)
{
    EXPECT_EQ(0, og::sim::kFillDefault);
    EXPECT_EQ(1, og::sim::kFillNone);
    EXPECT_EQ(2, og::sim::kFillWeak);
    EXPECT_EQ(3, og::sim::kFillFair);
    EXPECT_EQ(4, og::sim::kFillStrong);
    EXPECT_EQ(5, og::sim::kFillBrutal);
    EXPECT_EQ(5, og::sim::kMaxFill);
    for (const short explicit_code :
         {og::sim::kFillNone, og::sim::kFillWeak, og::sim::kFillFair,
          og::sim::kFillStrong, og::sim::kFillBrutal})
        EXPECT_NE(og::sim::kFillDefault, explicit_code)
            << "a banked fact code IS the fill, so 0 must stay unclaimed";
    // The clamp is the whole bound: [0, 5], junk lands on an end.
    EXPECT_EQ(og::sim::kFillDefault, og::sim::clamp_fill(-1));
    EXPECT_EQ(og::sim::kFillDefault, og::sim::clamp_fill(-32768));
    EXPECT_EQ(og::sim::kFillDefault, og::sim::clamp_fill(0));
    EXPECT_EQ(og::sim::kFillBrutal, og::sim::clamp_fill(5));
    EXPECT_EQ(og::sim::kFillBrutal, og::sim::clamp_fill(6));
    EXPECT_EQ(og::sim::kFillBrutal, og::sim::clamp_fill(32767));
}

TEST(LineupCommon, fill_label_strings)
{
    // Amendment B2's five values as amended by D1, and no sixth: one
    // wheel, one spelling.
    EXPECT_EQ("FILL: FAIR", og::ui::format_lineup_fill_label(og::sim::kFillFair));
    EXPECT_EQ("FILL: NONE", og::ui::format_lineup_fill_label(og::sim::kFillNone));
    EXPECT_EQ("FILL: WEAK", og::ui::format_lineup_fill_label(og::sim::kFillWeak));
    EXPECT_EQ("FILL: STRONG",
              og::ui::format_lineup_fill_label(og::sim::kFillStrong));
    EXPECT_EQ("FILL: BRUTAL",
              og::ui::format_lineup_fill_label(og::sim::kFillBrutal));
    EXPECT_EQ("FILL: FAIR",
              og::ui::format_lineup_fill_label(og::sim::kFillDefault))
        << "no census to resolve against: the default reads as the "
           "resolver's presence arm";
    EXPECT_EQ("FILL: FAIR", og::ui::format_lineup_fill_label(-3))
        << "a value the clamp would land beside the default reads as FAIR";
    EXPECT_EQ("FILL: FAIR", og::ui::format_lineup_fill_label(99));
    // The 80px face is 12 characters and the two longest values SPEND it.
    for (short value = -2; value <= 8; ++value)
        EXPECT_LE(og::ui::format_lineup_fill_label(value).size(), 12u);
    EXPECT_EQ(12u,
              og::ui::format_lineup_fill_label(og::sim::kFillStrong).size());
    EXPECT_EQ(12u,
              og::ui::format_lineup_fill_label(og::sim::kFillBrutal).size());
    // The bare word is the same string the preview pane appends.
    EXPECT_EQ("BRUTAL", og::ui::lineup_fill_name(og::sim::kFillBrutal));
}

// The label rule D1 pins: label(explicit) is that value's OWN word, and
// label(stored DEFAULT) is the word of whatever it RESOLVED to. The
// formatter's half of the rule is being one-to-one across the five
// explicit codes — the moment two of them share a word, a player cannot
// read the wheel's position off the face, which is exactly how FAIR went
// missing while it shared its code with the default.
TEST(LineupCommon, fill_label_never_re_resolves_an_explicit_stop)
{
    std::set<std::string> words;
    for (const short code :
         {og::sim::kFillNone, og::sim::kFillWeak, og::sim::kFillFair,
          og::sim::kFillStrong, og::sim::kFillBrutal})
        words.insert(og::ui::format_lineup_fill_label(code));
    EXPECT_EQ(5u, words.size())
        << "five explicit codes, five distinct faces";

    // And a band carrying an explicit code renders that code, not a
    // resolution of it. (The resolved arm — a stored DEFAULT taking its
    // resolution's word — needs a registered pack resolver and is pinned
    // in test_campaign_hooks and test_platform_headless.)
    SaveData save;
    save.fill[0] = og::sim::kFillFair;
    save.fill[1] = og::sim::kFillNone;
    std::vector<og::sim::LobbyPlayer> players{seat(0, 1)};
    const auto bands = og::ui::build_lineup_bands(
        save, players, std::vector<std::uint8_t>{0}, false, {});
    EXPECT_EQ("FILL: FAIR",
              og::ui::format_lineup_fill_label(bands[0].resolved_fill));
    EXPECT_EQ("FILL: NONE",
              og::ui::format_lineup_fill_label(bands[1].resolved_fill));
}

TEST(LineupCommon, map_units_label_strings)
{
    // A box (B4), so there are exactly two faces and junk reads as OFF —
    // the clamp lands junk on ON, and toggling from junk lands on ON too.
    EXPECT_EQ("MAP UNITS: ON",
              og::ui::format_lineup_map_units_label(og::sim::kMapUnitsOn));
    EXPECT_EQ("MAP UNITS: OFF",
              og::ui::format_lineup_map_units_label(og::sim::kMapUnitsOff));
    EXPECT_EQ("MAP UNITS: OFF", og::ui::format_lineup_map_units_label(7));
}

TEST(LineupCommon, map_units_census_hint_speaks_only_for_an_empty_team)
{
    og::ui::LineupTeamBand band;
    EXPECT_EQ("NO MAP UNITS", og::ui::format_lineup_map_units_census(band))
        << "no census, no units: the box is inert and says so";
    band.map_unit_count = 3;
    EXPECT_EQ("", og::ui::format_lineup_map_units_census(band))
        << "a live box leaves the column to the fighter census";
}

TEST(LineupCommon, census_and_power_strings)
{
    LineupTeamBand band;
    EXPECT_EQ("NO FIGHTERS", og::ui::format_lineup_census(band));
    band.fighter_count = 1;
    EXPECT_EQ("1 FIGHTER", og::ui::format_lineup_census(band));
    band.fighter_count = 5;
    EXPECT_EQ("5 FIGHTERS", og::ui::format_lineup_census(band));
    band.diag = LineupTeamBand::Diag::NeedsFighters;
    band.needs = 1;
    EXPECT_EQ("NEEDS 1 FIGHTER", og::ui::format_lineup_census(band));
    band.needs = 2;
    EXPECT_EQ("NEEDS 2 FIGHTERS", og::ui::format_lineup_census(band));
    band.diag = LineupTeamBand::Diag::NoSeatAi;
    EXPECT_EQ("NO SEAT: AI", og::ui::format_lineup_census(band));

    EXPECT_EQ("POWER 4200", og::ui::format_lineup_power(4200));
    EXPECT_EQ("POWER --", og::ui::format_lineup_power(std::nullopt));
    EXPECT_EQ("POWER 0", og::ui::format_lineup_power(0));
}

// The FIGHTERS row's POWER cell has a SIX-character field. A number wider
// than that used to be clipped, which is worse than no number at all: the
// column is read as a comparison, and 1234567 clipped reads as 1234567 ->
// "1234567" spilling into the next column (or, one digit further out, as a
// smaller number than a weaker fighter's). It is rounded into a suffix
// instead, and every tier still fits the field.
TEST(LineupCommon, power_cell_rounds_into_the_six_character_field)
{
    const auto cell = [](long long value) {
        return og::ui::format_lineup_power_cell(value);
    };

    EXPECT_EQ("    --", og::ui::format_lineup_power_cell(std::nullopt));
    EXPECT_EQ("     0", cell(0));
    EXPECT_EQ("   120", cell(120));
    EXPECT_EQ("999999", cell(999999)) << "the widest plain value";
    EXPECT_EQ(" 1000k", cell(1000000)) << "the first value that needs a tier";
    EXPECT_EQ(" 1235k", cell(1234567)) << "rounded, never truncated";
    EXPECT_EQ(" 1234k", cell(1234499));
    EXPECT_EQ("99999k", cell(99999000));
    EXPECT_EQ("  100M", cell(99999999999LL / 1000));
    EXPECT_EQ("  123M", cell(123000000));
    EXPECT_EQ(" 9223P", cell(9223372036854775807LL))
        << "even LLONG_MAX fits the field";
    EXPECT_EQ("-1235k", cell(-1234567)) << "a negative price keeps its sign";

    for (const long long probe : {0LL, 7LL, 999999LL, 1000000LL, 1234567LL,
                                  123456789LL, 987654321098LL,
                                  9223372036854775807LL, -9LL, -1234567LL}) {
        EXPECT_EQ(6u, og::ui::format_lineup_power_cell(probe).size())
            << "value " << probe << " must fill exactly the field";
    }
}

// --- cyclers -----------------------------------------------------------

namespace {

// The word a band shows after `clicks` presses, starting from `stored`
// with the band resolving to `resolved` — the click callback's own
// arithmetic (cycle from the resolved slot, render the stored result), so
// the sequence a player reads off the face is what this returns.
std::vector<std::string> fill_face_sequence(short stored, short resolved,
                                            int clicks)
{
    std::vector<std::string> faces;
    // Before the first click the face is the RESOLVED value; afterwards
    // every value is explicit and is its own resolution.
    faces.push_back(og::ui::format_lineup_fill_label(
        stored == og::sim::kFillDefault ? resolved : stored));
    short value = stored;
    for (int i = 0; i < clicks; ++i)
    {
        value = og::sim::clamp_fill(
            og::ui::cycle_lineup_fill(value, resolved, 1));
        faces.push_back(og::ui::format_lineup_fill_label(value));
        resolved = value;
    }
    return faces;
}

}  // namespace

TEST(LineupCommon, fill_wheel_walks_none_weak_fair_strong_brutal)
{
    // Wheel order, which since D1 is also the storage order: weakest to
    // strongest, the default nowhere on it. An explicit value enters at
    // its OWN slot no matter what the band resolved to, so the second
    // argument cannot move an explicit stop.
    for (const short contradiction :
         {og::sim::kFillDefault, og::sim::kFillNone, og::sim::kFillBrutal})
    {
        EXPECT_EQ(og::sim::kFillWeak,
                  og::ui::cycle_lineup_fill(og::sim::kFillNone,
                                            contradiction, 1));
        EXPECT_EQ(og::sim::kFillFair,
                  og::ui::cycle_lineup_fill(og::sim::kFillWeak,
                                            contradiction, 1));
        EXPECT_EQ(og::sim::kFillStrong,
                  og::ui::cycle_lineup_fill(og::sim::kFillFair,
                                            contradiction, 1));
        EXPECT_EQ(og::sim::kFillBrutal,
                  og::ui::cycle_lineup_fill(og::sim::kFillStrong,
                                            contradiction, 1));
        EXPECT_EQ(og::sim::kFillNone,
                  og::ui::cycle_lineup_fill(og::sim::kFillBrutal,
                                            contradiction, 1))
            << "past the strongest the wheel comes back at NONE";
        EXPECT_EQ(og::sim::kFillWeak,
                  og::ui::cycle_lineup_fill(og::sim::kFillFair,
                                            contradiction, -1));
        EXPECT_EQ(og::sim::kFillBrutal,
                  og::ui::cycle_lineup_fill(og::sim::kFillNone,
                                            contradiction, -1));
    }
    // A stored value on neither the wheel nor the resolution enters at
    // FAIR, which is where the clamp would have put it.
    EXPECT_EQ(og::sim::kFillStrong, og::ui::cycle_lineup_fill(77, 88, 1));
    EXPECT_EQ(og::sim::kFillFair, og::ui::cycle_lineup_fill(77, 88, 0));
    // ...but junk with a legible resolution enters where the face reads.
    EXPECT_EQ(og::sim::kFillWeak,
              og::ui::cycle_lineup_fill(77, og::sim::kFillNone, 1));
    // Five steps is one full turn, and every explicit value is on it
    // exactly once — no sixth position, no unreachable code.
    std::vector<short> seen;
    short value = og::sim::kFillNone;
    for (int step = 0; step < 5; ++step)
    {
        seen.push_back(value);
        value = og::ui::cycle_lineup_fill(value, value, 1);
    }
    EXPECT_EQ(og::sim::kFillNone, value);
    EXPECT_EQ((std::vector<short>{og::sim::kFillNone, og::sim::kFillWeak,
                                 og::sim::kFillFair, og::sim::kFillStrong,
                                 og::sim::kFillBrutal}),
              seen);
}

// D1's first half: the DEFAULT is off the wheel, and a step from it can
// never return it. Whatever the band resolved to, a turned knob is an
// explicit choice and stays one — the knob precedent, and the reason the
// wheel is five stops rather than six.
TEST(LineupCommon, fill_wheel_never_returns_the_default)
{
    for (const short resolved :
         {og::sim::kFillDefault, og::sim::kFillNone, og::sim::kFillWeak,
          og::sim::kFillFair, og::sim::kFillStrong, og::sim::kFillBrutal,
          static_cast<short>(77)})
    {
        for (short stored = -1; stored <= 6; ++stored)
        {
            for (const int dir : {-1, 0, 1, 4, -7})
            {
                const short next =
                    og::ui::cycle_lineup_fill(stored, resolved, dir);
                EXPECT_NE(og::sim::kFillDefault, next)
                    << "stored " << stored << " resolved " << resolved
                    << " dir " << dir;
                EXPECT_GE(next, og::sim::kFillNone);
                EXPECT_LE(next, og::sim::kFillBrutal);
                EXPECT_EQ(next, og::sim::clamp_fill(next))
                    << "every stop the wheel returns is already in range";
            }
        }
    }
}

// D1's second half, and the bug it was written against: on a band with no
// authored presence the stored default RESOLVES to NONE, so the wheel must
// enter at NONE's slot and the face must read
// NONE -> WEAK -> FAIR -> STRONG -> BRUTAL -> NONE.
//
// The old wheel entered at FAIR's slot while the face said NONE — the
// player's first click skipped WEAK — and its FAIR stop (stored 0) was
// re-resolved back to the word NONE on the way to the face, so the five
// stops read NONE, WEAK, NONE, STRONG, BRUTAL: four words, no FAIR, and
// two of them the same. Both halves are pinned here.
TEST(LineupCommon, fill_wheel_from_the_default_enters_where_the_face_reads)
{
    EXPECT_EQ((std::vector<std::string>{"FILL: NONE", "FILL: WEAK",
                                        "FILL: FAIR", "FILL: STRONG",
                                        "FILL: BRUTAL", "FILL: NONE",
                                        "FILL: WEAK"}),
              fill_face_sequence(og::sim::kFillDefault, og::sim::kFillNone,
                                 6))
        << "a presence-less band walks all five words, FAIR among them";

    // On an authored band the same stored default resolves to FAIR, so the
    // wheel enters at FAIR's slot and the first click steps to STRONG.
    EXPECT_EQ((std::vector<std::string>{"FILL: FAIR", "FILL: STRONG",
                                        "FILL: BRUTAL", "FILL: NONE",
                                        "FILL: WEAK", "FILL: FAIR",
                                        "FILL: STRONG"}),
              fill_face_sequence(og::sim::kFillDefault, og::sim::kFillFair,
                                 6));

    // And the stored codes behind that presence-less walk are the explicit
    // ones, in order — the face is not painting a fiction.
    EXPECT_EQ(og::sim::kFillWeak,
              og::ui::cycle_lineup_fill(og::sim::kFillDefault,
                                        og::sim::kFillNone, 1));
    EXPECT_EQ(og::sim::kFillStrong,
              og::ui::cycle_lineup_fill(og::sim::kFillDefault,
                                        og::sim::kFillFair, 1));
    EXPECT_EQ(og::sim::kFillBrutal,
              og::ui::cycle_lineup_fill(og::sim::kFillDefault,
                                        og::sim::kFillNone, -1))
        << "backwards from a resolved NONE is the strongest stop";
}

TEST(LineupCommon, map_units_box_flips_and_junk_lands_on)
{
    EXPECT_EQ(og::sim::kMapUnitsOff,
              og::ui::toggle_lineup_map_units(og::sim::kMapUnitsOn));
    EXPECT_EQ(og::sim::kMapUnitsOn,
              og::ui::toggle_lineup_map_units(og::sim::kMapUnitsOff));
    EXPECT_EQ(og::sim::kMapUnitsOn, og::ui::toggle_lineup_map_units(9))
        << "junk flips to the default, never to a value nothing reads";
    EXPECT_EQ(og::sim::kMapUnitsOn, og::ui::toggle_lineup_map_units(-4));
}

// --- SPLIT -------------------------------------------------------------

namespace {

using Moves = std::vector<std::pair<int, short>>;

}  // namespace

TEST(LineupCommon, split_even_deals_round_robin_in_slot_order)
{
    SaveData save;
    for (int slot = 0; slot < 6; ++slot)
        put(save, slot, 0, true);
    const std::array<short, 2> seats{1, 2};

    const auto plan = og::ui::split_company(save, seats, LineupSplit::Even,
                                            {}, {});
    EXPECT_EQ(0, plan.locked);
    EXPECT_EQ((Moves{{0, 1}, {1, 2}, {2, 1}, {3, 2}, {4, 1}, {5, 2}}),
              plan.moves);

    EXPECT_EQ(6, og::ui::apply_split(save, plan.moves));
    EXPECT_EQ(1, save.team_list[0]->teamnum);
    EXPECT_EQ(2, save.team_list[1]->teamnum);
    EXPECT_EQ(2, save.team_list[5]->teamnum);
    // Idempotent: a plan that changes nothing moves nobody.
    EXPECT_EQ(0, og::ui::apply_split(save, plan.moves));
}

TEST(LineupCommon, split_fair_snakes_the_draft_by_power)
{
    SaveData save;
    // Levels 6,5,4,3,2,1 in slot order: the headline 6-fighter, 2-seat case.
    for (int slot = 0; slot < 6; ++slot)
        put(save, slot, 0, true, static_cast<short>(6 - slot));
    const std::array<short, 2> seats{1, 2};

    const auto plan = og::ui::split_company(save, seats, LineupSplit::Fair,
                                            level_power(), {});
    // 1,2 | 2,1 | 1,2 — the snake, strongest first.
    EXPECT_EQ((Moves{{0, 1}, {1, 2}, {2, 2}, {3, 1}, {4, 1}, {5, 2}}),
              plan.moves);
}

TEST(LineupCommon, split_fair_breaks_ties_by_slot)
{
    SaveData save;
    for (int slot = 0; slot < 4; ++slot)
        put(save, slot, 0, true, 3);  // every fighter identical
    const std::array<short, 2> seats{2, 1};  // ascending, whatever the order

    const auto plan = og::ui::split_company(save, seats, LineupSplit::Fair,
                                            level_power(), {});
    EXPECT_EQ((Moves{{0, 1}, {1, 2}, {2, 2}, {3, 1}}), plan.moves)
        << "a tie is slot order, and the seats sort ascending";
}

TEST(LineupCommon, split_fair_falls_back_to_level_without_a_metric)
{
    SaveData save;
    put(save, 0, 0, true, 1);
    put(save, 1, 0, true, 9);
    put(save, 2, 0, true, 5);
    const std::array<short, 3> seats{1, 2, 3};

    const auto plan = og::ui::split_company(save, seats, LineupSplit::Fair,
                                            {}, {});
    EXPECT_EQ((Moves{{1, 1}, {2, 2}, {0, 3}}), plan.moves)
        << "level descending: 9, 5, 1";
}

TEST(LineupCommon, split_all_to_first_and_the_single_seat_rule)
{
    SaveData save;
    put(save, 0, 1, true);
    put(save, 1, 2, true);
    put(save, 2, 3, false);  // benched
    const std::array<short, 2> two{3, 2};

    const auto all = og::ui::split_company(save, two, LineupSplit::AllToFirst,
                                           {}, {});
    EXPECT_EQ((Moves{{0, 2}, {1, 2}}), all.moves)
        << "the lowest-numbered seated team, and the bench stays out";

    // One seat makes EVERY mode ALL TO 1.
    const std::array<short, 1> one{3};
    for (const LineupSplit mode : {LineupSplit::Even, LineupSplit::Fair,
                                   LineupSplit::AllToFirst})
    {
        const auto plan = og::ui::split_company(save, one, mode,
                                                level_power(), {});
        EXPECT_EQ((Moves{{0, 3}, {1, 3}}), plan.moves);
    }
}

TEST(LineupCommon, split_reports_locked_slots_and_leaves_them_alone)
{
    SaveData save;
    for (int slot = 0; slot < 4; ++slot)
        put(save, slot, 0, true);
    const std::array<short, 2> seats{1, 2};

    const auto plan = og::ui::split_company(
        save, seats, LineupSplit::Even, {},
        [](int slot) { return slot != 1; });
    EXPECT_EQ(1, plan.locked);
    EXPECT_EQ((Moves{{0, 1}, {2, 2}, {3, 1}}), plan.moves)
        << "the locked slot is skipped, and the deal closes over the rest";
    og::ui::apply_split(save, plan.moves);
    EXPECT_EQ(0, save.team_list[1]->teamnum) << "left where it was";
}

TEST(LineupCommon, split_with_no_seats_or_no_fighters_does_nothing)
{
    SaveData save;
    put(save, 0, 0, true);
    EXPECT_TRUE(
        og::ui::split_company(save, std::vector<short>{}, LineupSplit::Even,
                              {}, {}).moves.empty());
    // Out-of-range seat teams are not teams.
    const std::array<short, 2> bogus{-1, 9};
    EXPECT_TRUE(
        og::ui::split_company(save, bogus, LineupSplit::Even, {}, {})
            .moves.empty());

    SaveData benched;
    put(benched, 0, 0, false);
    const std::array<short, 1> seats{1};
    EXPECT_TRUE(
        og::ui::split_company(benched, seats, LineupSplit::Even, {}, {})
            .moves.empty());
}

TEST(LineupCommon, apply_split_refuses_impossible_moves)
{
    SaveData save;
    put(save, 0, 0, true);
    const Moves moves{{0, 9}, {7, 1}, {-1, 1}, {0, 2}};
    EXPECT_EQ(1, og::ui::apply_split(save, moves));
    EXPECT_EQ(2, save.team_list[0]->teamnum);
}

// --- Networking machine rows -------------------------------------------

TEST(LineupCommon, machine_rows_group_seats_and_mark_host_and_you)
{
    std::vector<og::sim::LobbyPlayer> players{
        seat(0, 1, "IRON KETTLE", true, true, 11),
        seat(1, 1, "IRON KETTLE", false, true, 11),
        seat(2, 2, "BLUE BAND", false, false, 22),
    };
    const auto rows = og::ui::build_networking_machine_rows(
        players, std::vector<std::uint8_t>{0, 1}, 39);

    ASSERT_EQ(2u, rows.size());
    EXPECT_EQ(11u, rows[0].machine_id);
    EXPECT_TRUE(rows[0].is_host);
    EXPECT_TRUE(rows[0].is_local);
    // The full shape is "M1 IRON KETTLE (HOST) (YOU)  P1 P2  READY" at 41
    // chars: READY is the token the 39-char budget drops.
    EXPECT_EQ("M1 IRON KETTLE (HOST) (YOU)  P1 P2", rows[0].label);
    EXPECT_GE(39u, rows[0].label.size());

    EXPECT_EQ(22u, rows[1].machine_id);
    EXPECT_FALSE(rows[1].is_host);
    EXPECT_FALSE(rows[1].is_local);
    EXPECT_EQ("M2 BLUE BAND  P3", rows[1].label);
}

TEST(LineupCommon, machine_rows_degrade_whole_tokens_to_the_budget)
{
    std::vector<og::sim::LobbyPlayer> players{
        seat(0, 1, "THE LONG SEASON", false, true, 5)};
    players[0].name = "GLADHOUSE";

    // Everything fits: the company leads and READY is the last token.
    EXPECT_EQ("M1 THE LONG SEASON  P1  READY",
              og::ui::build_networking_machine_rows(
                  players, std::vector<std::uint8_t>{}, 64)[0]
                  .label);
    // READY goes first...
    EXPECT_EQ("M1 THE LONG SEASON  P1",
              og::ui::build_networking_machine_rows(
                  players, std::vector<std::uint8_t>{}, 28)[0]
                  .label);
    // ...then the seat list...
    EXPECT_EQ("M1 THE LONG SEASON",
              og::ui::build_networking_machine_rows(
                  players, std::vector<std::uint8_t>{}, 21)[0]
                  .label);
    // ...and only the identity itself ever gets clipped mid-word.
    EXPECT_EQ("M1 THE L",
              og::ui::build_networking_machine_rows(
                  players, std::vector<std::uint8_t>{}, 8)[0]
                  .label);
}

TEST(LineupCommon, machine_rows_order_by_lowest_seat_and_name_from_company)
{
    // Seats arrive out of order, and the machine's identity is whatever its
    // LOWEST seat carries — a later-listed seat with a lower P# names it.
    std::vector<og::sim::LobbyPlayer> players{
        seat(3, 1, "", false, false, 22),
        seat(1, 1, "IRON KETTLE", true, false, 11),
        seat(2, 1, "BLUE BAND", false, false, 22),
    };
    players[2].name = "BLUEBOX";
    const auto rows = og::ui::build_networking_machine_rows(
        players, std::vector<std::uint8_t>{}, 39);
    ASSERT_EQ(2u, rows.size());
    EXPECT_EQ("M1 IRON KETTLE (HOST)  P2", rows[0].label)
        << "a relay lobby leaves `name` empty: the company leads anyway";
    EXPECT_EQ("M2 BLUE BAND  P3 P4", rows[1].label)
        << "the lowest seat's company names the machine, not P4's blank";
}

TEST(LineupCommon, machine_rows_fall_back_to_the_transport_name)
{
    // No company yet (a freshly connected peer): the opaque transport name
    // is all there is, and it beats an empty identity.
    std::vector<og::sim::LobbyPlayer> players{seat(0, 1, "", false, false, 7)};
    players[0].name = "net-4f2a";
    EXPECT_EQ("M1 net-4f2a  P1",
              og::ui::build_networking_machine_rows(
                  players, std::vector<std::uint8_t>{}, 39)[0]
                  .label);

    // Neither: the row is still identifiable by its M#.
    std::vector<og::sim::LobbyPlayer> nameless{seat(1, 1, "", true, false, 7)};
    EXPECT_EQ("M1 (HOST)  P2",
              og::ui::build_networking_machine_rows(
                  nameless, std::vector<std::uint8_t>{}, 39)[0]
                  .label);
}

TEST(LineupCommon, machine_rows_without_a_machine_id_stay_separate)
{
    // The defensive path: seats with no authority grant must never collapse
    // into one machine row.
    std::vector<og::sim::LobbyPlayer> players{
        seat(0, 1, "IRON KETTLE", false, false,
             og::sim::kInvalidLobbyMachineId),
        seat(1, 1, "BLUE BAND", false, false,
             og::sim::kInvalidLobbyMachineId),
    };
    const auto rows = og::ui::build_networking_machine_rows(
        players, std::vector<std::uint8_t>{}, 39);
    EXPECT_EQ(2u, rows.size());
}
