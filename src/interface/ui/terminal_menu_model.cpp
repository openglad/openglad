/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* Terminal menu projection implementation (design: menu-engine G7).
 *
 * The strings assembled here were moved VERBATIM from the text/curses
 * clients' private helpers so both terminal surfaces stay byte-identical.
 */

#include <openglad/interface/ui/terminal_menu_model.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

namespace og::ui {
namespace {

// Team + gold header lines shown above the Team Build menu (moved verbatim
// from the curses client; the text client printed the same content inline).
std::vector<std::string> team_build_context_lines(const SaveData& save)
{
    std::vector<std::string> lines;
    if (save.team_size == 0) {
        lines.push_back("Team: (empty)");
    } else {
        std::string team = "Team: ";
        bool first = true;
        for_each_team_member(save, [&](int, const guy& member) {
            if (!first)
                team += ", ";
            team += std::format("{} ({})", member.name,
                                family_display_name(member.family));
            first = false;
        });
        lines.push_back(team);
    }
    lines.push_back(std::format("Gold: {}", format_wallet_amount(save, 0)));
    return lines;
}

// Terminal guard: the §2.5 READY item outside a networked lobby
// (solo/local sessions have no ready machinery — §2.6 state 1). The match
// knobs are the camp's MATCH SETUP page now (docs/camp-controls-design.md),
// so no terminal menu carries them or their old versus guard. Scenario
// troops stays ungated on the SCENARIO screen: "strip everything authored"
// is meaningful on classic campaigns too.
constexpr std::string_view kReadyGuardMessage =
    "Ready applies to networked lobbies only.";

GateBinding terminal_item_gate(PickerMenuCommand command)
{
    switch (command) {
    case PickerMenuCommand::ToggleReady:
        return GateBinding{MenuGate::NetworkedOnly, nullptr,
                           kReadyGuardMessage};
    default:
        return GateBinding{};
    }
}

} // namespace

TerminalMenuModel build_terminal_menu_model(PickerMenuId menu_id,
                                            const MenuLabelContext& context)
{
    const PickerMenuDefinition& definition = picker_menu_definition(menu_id);

    TerminalMenuModel model;
    model.title = std::string(definition.title);
    if (menu_id == PickerMenuId::TeamBuild && context.save != nullptr)
        model.context_lines = team_build_context_lines(*context.save);
    model.entries.reserve(definition.items.size());
    for (const PickerMenuItem& item : definition.items) {
        model.entries.push_back(
            TerminalMenuEntry{&item, menu_item_label(item, context), true});
    }
    model.cancel_item = menu_cancel_item(menu_id);
    return model;
}

std::string_view terminal_gate_message(const PickerMenuItem& item,
                                       const MenuLabelContext& context)
{
    const GateBinding binding = terminal_item_gate(item.command);
    if (binding.guard_message.empty())
        return {};
    if (gate_state(binding, context) == RowState::Visible)
        return {};
    return binding.guard_message;
}

// --- LINEUP (docs/lineup-design.md §8) ----------------------------------

TerminalLineupModel build_terminal_lineup_model(
    const TerminalLineupInputs& inputs)
{
    TerminalLineupModel model;
    if (inputs.save == nullptr)
        return model;

    const std::array<LineupTeamBand, 4> bands = build_lineup_bands(
        *inputs.save, inputs.players, inputs.local_player_indices,
        inputs.networked, lineup_power_for_guy);
    // §2.3: the knobs are stored on every campaign but only a VERSUS
    // campaign's modes read them. The SDL screen says so by dimming the two
    // faces and censusing MAP RULES; the terminal says the same thing in the
    // only ink it has.
    const bool versus = is_versus_campaign(*inputs.save);

    for (int team = 0; team < 4; ++team) {
        const LineupTeamBand& band = bands[static_cast<std::size_t>(team)];
        // A2: a team with a seat or a deployed fighter is ON by definition,
        // so OFF is refused there — the refusal that was wrong for NONE is
        // right for OFF. Seats first: a seated team is the shape the refusal
        // exists to protect, and naming the fighters instead would send the
        // player to bench characters that were never the reason.
        model.off_refusal[static_cast<std::size_t>(team)] =
            band.has_seat
                ? std::format("TEAM {} HAS PLAYERS", team + 1)
                : (band.fighter_count > 0
                       ? std::format("TEAM {} HAS FIGHTERS", team + 1)
                       : std::string());
        const std::string bots = format_lineup_bots_label(
            inputs.save->bot_squad[static_cast<std::size_t>(team)],
            inputs.preset_names);
        const std::string level = format_lineup_level_label(
            inputs.save->bot_level[static_cast<std::size_t>(team)]);

        // Header line: the colour the SDL band paints as a chip, the price,
        // then every seat on the team (the SDL "+n" overflow is a pixel
        // budget; a terminal line has the room to name them all).
        std::string seats;
        for (const std::string& seat_label : band.seat_labels) {
            if (!seats.empty())
                seats += "  ";
            seats += seat_label;
        }
        if (seats.empty())
            seats = "NO SEAT";
        model.lines.push_back(std::format(
            "TEAM {} {}  {}   {}", team + 1, og::sim::team_color_name(team),
            format_lineup_power(band.power), seats));
        // The SDL band's own precedence: a diagnostic outranks MAP RULES,
        // which outranks the plain census.
        const std::string census =
            (!versus && band.diag == LineupTeamBand::Diag::None)
            ? std::string("MAP RULES")
            : format_lineup_census(band);
        model.lines.push_back(
            std::format("  [{}] [{}]  {}", bots, level, census));
    }

    // §2.3: the knobs are the HOST's. A joiner gets the bands and the
    // fighter list (its own company) and nothing that would desync.
    if (inputs.is_host) {
        for (int team = 0; team < 4; ++team) {
            const std::string bots = format_lineup_bots_label(
                inputs.save->bot_squad[static_cast<std::size_t>(team)],
                inputs.preset_names);
            const std::string level = format_lineup_level_label(
                inputs.save->bot_level[static_cast<std::size_t>(team)]);
            // The row text is the shared label VERBATIM behind the team
            // ordinal — never a second spelling of the same value. The rows
            // STAY on a classic campaign (marked, and refused on selection):
            // dropping them would renumber the page under the two 1-based
            // consumers, and the SDL twin keeps its faces too.
            const std::string mark = versus
                ? std::string()
                : std::string(kTerminalLineupMapRulesMark);
            model.items.push_back(TerminalLineupItem{
                TerminalLineupItem::Kind::BotSquad, team,
                std::format("TEAM {}  {}{}", team + 1, bots, mark)});
            model.items.push_back(TerminalLineupItem{
                TerminalLineupItem::Kind::BotLevel, team,
                std::format("TEAM {}  {}{}", team + 1, level, mark)});
        }
    }
    model.items.push_back(TerminalLineupItem{
        TerminalLineupItem::Kind::Fighters, 0, "Fighters"});
    // §5 operates over the teams that have a seat ON THIS MACHINE, and both
    // terminal clients are single-seat by construction (a company file loads
    // with numplayers 1), so both SPLIT rows resolve to UNITE there. The rows
    // STAY — the two 1-based consumers pin every ordinal on this page — but
    // the label says so rather than promising a draft that cannot happen.
    // The mark is derived, not assumed: the same seat picture the split
    // itself plans over (M3), so a multi-seat terminal would simply lose it.
    const std::vector<short> seat_teams =
        derive_local_gameplay_seat_teams(*inputs.save);
    std::vector<short> distinct_teams;
    for (const short team : seat_teams) {
        if (std::find(distinct_teams.begin(), distinct_teams.end(), team) ==
            distinct_teams.end())
        {
            distinct_teams.push_back(team);
        }
    }
    const std::string split_mark =
        distinct_teams.size() == 1 ? "  (one seat: same as Unite)" : "";
    model.items.push_back(TerminalLineupItem{
        TerminalLineupItem::Kind::SplitEven, 0, "Split even" + split_mark});
    model.items.push_back(TerminalLineupItem{
        TerminalLineupItem::Kind::SplitFair, 0, "Split fair" + split_mark});
    model.items.push_back(TerminalLineupItem{
        TerminalLineupItem::Kind::Unite, 0, "Unite"});
    model.items.push_back(TerminalLineupItem{
        TerminalLineupItem::Kind::Back, 0, "Back"});
    return model;
}

TerminalLineupBotsStep terminal_lineup_bots_step(short current,
                                                 int preset_count, int dir,
                                                 std::string_view off_refusal)
{
    TerminalLineupBotsStep step;
    step.value = cycle_lineup_bots(current, preset_count, dir);
    if (off_refusal.empty() || step.value != og::sim::kBotSquadOff)
        return step;
    // OFF is one position wide, so ONE more step of the same sign clears it
    // in either direction. The SDL twin refuses the same value from
    // change_lineup_bots (src/interface/ui/picker.cpp) — one rule, three
    // clients, and the wheel stays usable on a seated team.
    step.value = cycle_lineup_bots(step.value, preset_count,
                                   dir >= 0 ? 1 : -1);
    step.refusal = std::string(off_refusal);
    return step;
}

int terminal_apply_lineup_split(SaveData& save, LineupSplit mode,
                                std::vector<std::string>& report)
{
    // M3: ONE seat derivation. This is the picture the SDL screens and the
    // launch read (synthesize_local_lobby_players wraps the same helper) —
    // my_team first, the other deployed teams after, padded and truncated to
    // numplayers. The unpadded derive_local_seat_teams that used to be here
    // gave the terminals a different lineup from the one they were about to
    // play.
    const std::vector<short> seat_teams = derive_local_gameplay_seat_teams(save);
    if (seat_teams.empty()) {
        report.push_back("No seats: deploy a character first.");
        return 0;
    }
    // §2.2/§5: the campaign's own roster capability gates a SPLIT exactly as
    // it gates a single row's team cycle. Hardcoding can_team=true here let
    // one keystroke do what the fighter list refuses one row at a time.
    const bool can_team = lineup_zone_can_team(save);
    const LineupSplitPlan plan = split_company(
        save, seat_teams, mode, lineup_power_for_guy,
        [&save, can_team](int slot) {
            return lineup_fighter_team_editable(save, slot, can_team,
                                                /*assign_mode=*/false);
        });
    const int moved = apply_split(save, plan.moves);
    report.push_back(
        std::format("Moved {} fighter{}.", moved, moved == 1 ? "" : "s"));
    if (plan.locked > 0) {
        report.push_back(std::format("{} fighter{} locked and stayed put.",
                                     plan.locked,
                                     plan.locked == 1 ? " is" : "s are"));
    }
    return moved;
}

std::string format_terminal_lineup_fighter_row(int slot_index,
                                               const guy& member)
{
    return std::format("{:2}. {} ({}) LV {}  {}  {}  {}", slot_index + 1,
                       member.name, family_display_name(member.family),
                       static_cast<int>(member.level),
                       og::sim::team_color_name(member.teamnum),
                       member.deployed ? "DEPLOYED" : "BENCHED",
                       format_lineup_power(lineup_power_for_guy(member)));
}

std::vector<std::string> terminal_lineup_fighter_lines(const SaveData& save)
{
    std::vector<std::string> lines;
    for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot) {
        const auto& member = save.team_list[static_cast<std::size_t>(slot)];
        if (member == nullptr)
            continue;
        lines.push_back("  " +
                        format_terminal_lineup_fighter_row(slot, *member));
    }
    if (lines.empty())
        lines.emplace_back("  (no characters)");
    return lines;
}

} // namespace og::ui
