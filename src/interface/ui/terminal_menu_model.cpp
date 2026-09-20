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
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/match_setup_session.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/match_stage.h>

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
// (solo/local sessions have no ready machinery — §2.6 state 1). Scenario
// troops stays ungated on the SCENARIO screen: "strip everything authored"
// is meaningful on classic campaigns too.
//
// The one `Custom` gate left is the SCENARIO submenu's `Replay Level`
// (R2-4): a campaign that carries no progress vocabulary has no cleared
// level to re-fight, so the row stays LISTED and refuses in words. One gate
// for both terminal clients — neither replay_level() knows about it.
//
// Nothing gates the match rules any more. DIFFICULTY is item 11 on every
// campaign (R2-3), and the SETUP wizard has no Team Build item at all: its
// terminal door is the Camp's row 1, the same one door the pixel clients
// tap (R2-D11).
constexpr std::string_view kReadyGuardMessage =
    "Ready applies to networked lobbies only.";

GateBinding terminal_item_gate(PickerMenuCommand command)
{
    switch (command) {
    case PickerMenuCommand::ToggleReady:
        return GateBinding{MenuGate::NetworkedOnly, nullptr,
                           kReadyGuardMessage};
    case PickerMenuCommand::ReplayLevel:
        return GateBinding{MenuGate::Custom,
                           [](const MenuLabelContext& context) {
                               return context.save == nullptr ||
                                      progress_marks_shown(*context.save);
                           },
                           kReplayVersusGuardMessage};
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

IPickerLobbyClient::StagedPreviewHealth census_staged_match_report(
    og::server::MatchStage& stage, const SaveData& save, int difficulty,
    std::uint32_t match_seed, std::array<int, 4>& out_counts,
    ScenarioRosterReport& out_report)
{
    using Health = IPickerLobbyClient::StagedPreviewHealth;

    // Both pages census the SAME world through the SAME seat context they
    // stage with, so the LINEUP bands, the wizard's team lines and the VIEW
    // LEVEL report can never describe two different worlds.
    ScenarioSeatContext seats;
    seats.players = synthesize_local_lobby_players(save);
    for (const og::sim::LobbyPlayer& player : seats.players)
        seats.local_player_indices.push_back(player.player_index);

    const auto unavailable = [&]() {
        out_report = build_scenario_roster_report(
            nullptr, og::ui::StagePreviewStatus::None, save, nullptr, &seats);
        return Health::Unavailable;
    };

    if (get_mounted_campaign() != save.current_campaign)
        return unavailable();

    og::server::MatchStageInputs inputs;
    inputs.equivalent = og::server::build_local_save_equivalent(save);
    inputs.difficulty = difficulty;
    inputs.match_seed = match_seed;
    inputs.replay_level = save.replay_level;
    inputs.replay_origin = save.replay_origin;
    const std::uint64_t now = og::server::stage_clock_now_ms();
    stage.observe_inputs(inputs, now);
    // A page redraw cannot wait out kStageDebounceMs, and a band that
    // rendered last turn's world would be exactly the disagreement this
    // census exists to end — so force the restage the way GO does.
    stage.ensure_current(now);
    if (stage.status() == og::server::StageStatus::Failed) {
        out_report = build_scenario_roster_report(
            nullptr, og::ui::StagePreviewStatus::Failed, save, nullptr,
            &seats);
        return Health::Failed;
    }
    if (stage.status() != og::server::StageStatus::Staged)
        return unavailable();
    const GameWorld* world = stage.world();
    // A stage that fell back must not masquerade as this level's census
    // (the mirror applies the same rule).
    if (world == nullptr || world->id != save.scen_num)
        return unavailable();

    out_counts = census_lineup_map_units(*world);
    out_report = build_scenario_roster_report(
        world, og::ui::StagePreviewStatus::Staged, save, nullptr, &seats);
    return Health::Staged;
}

bool census_staged_lineup_map_units(og::server::MatchStage& stage,
                                    const SaveData& save, int difficulty,
                                    std::uint32_t match_seed,
                                    std::array<int, 4>& out)
{
    // One implementation: the MAP UNITS answer is the report census's
    // Staged arm, and `out` is left untouched on every other arm exactly as
    // this function has always promised.
    ScenarioRosterReport scratch;
    std::array<int, 4> counts{};
    if (census_staged_match_report(stage, save, difficulty, match_seed, counts,
                                   scratch) !=
        IPickerLobbyClient::StagedPreviewHealth::Staged) {
        return false;
    }
    out = counts;
    return true;
}

TerminalMatchSetupModel build_terminal_match_setup_model(
    const MatchSetupSession& session, const MatchSetupSession::Inputs& inputs)
{
    (void)inputs;
    const MatchSetupSession::Page& page = session.page();
    TerminalMatchSetupModel model;
    model.title = page.title;

    const std::size_t at =
        std::min(page.team_lines_at, page.lines.size());
    for (std::size_t i = 0; i < at; ++i)
        model.lines.push_back(page.lines[i]);
    for (const MatchSetupSession::TeamLine& line : page.team_lines) {
        // A MATCH team line IS the report line; it carries no cells, so it
        // prints alone. A TEAMS team line spells the colour word where the
        // pixel surfaces ink a swatch — the LINEUP header's own grammar.
        if (line.seats.empty() && line.census.empty()) {
            model.lines.push_back(line.label);
            continue;
        }
        std::string text = std::format("{} {}", line.label,
                                       og::sim::team_color_name(line.team));
        if (!line.seats.empty()) {
            text += "  ";
            text += line.seats;
        }
        if (!line.census.empty()) {
            text += "  ";
            text += line.census;
        }
        model.lines.push_back(std::move(text));
    }
    for (std::size_t i = at; i < page.lines.size(); ++i)
        model.lines.push_back(page.lines[i]);

    for (std::size_t i = 0; i < page.rows.size(); ++i) {
        const MatchSetupSession::Row& row = page.rows[i];
        model.items.push_back(TerminalMatchSetupItem{
            TerminalMatchSetupItem::Kind::Row, i,
            campaign_picker_row_text(row.base, kCampaignPickerTerminalRowBudget,
                                     true),
            row.extra == MatchSetupSession::Row::Extra::Cycler &&
                row.state == RowState::Visible});
    }
    // The two steppers ARE the tab strip's projection. They name the step
    // the session's own next_step()/prev_step() land on, so an item can
    // never promise a step the stepper does not go to.
    if (page.can_next) {
        model.items.push_back(TerminalMatchSetupItem{
            TerminalMatchSetupItem::Kind::Next, 0,
            std::format("Next: {}",
                        MatchSetupSession::step_word(session.next_step())),
            false});
    }
    if (page.can_prev) {
        model.items.push_back(TerminalMatchSetupItem{
            TerminalMatchSetupItem::Kind::Prev, 0,
            std::format("Prev: {}",
                        MatchSetupSession::step_word(session.prev_step())),
            false});
    }
    model.items.push_back(TerminalMatchSetupItem{
        TerminalMatchSetupItem::Kind::Back, 0, "Back", false});
    return model;
}

TerminalLineupModel build_terminal_lineup_model(
    const TerminalLineupInputs& inputs)
{
    TerminalLineupModel model;
    if (inputs.save == nullptr)
        return model;

    model.bands = build_lineup_bands(
        *inputs.save, inputs.players, inputs.local_player_indices,
        inputs.networked, lineup_power_for_guy, {}, inputs.map_unit_counts);
    // B4: the hint speaks only for a census that actually happened. An empty
    // span leaves every count at 0, which build_lineup_bands cannot tell from
    // a map that ships no units — so the terminal says nothing rather than
    // telling all four teams the map is empty.
    const bool censused = !inputs.map_unit_counts.empty();
    const std::array<LineupTeamBand, 4>& bands = model.bands;

    for (int team = 0; team < 4; ++team) {
        const LineupTeamBand& band = bands[static_cast<std::size_t>(team)];
        // E1: the cell renders the STORED code through the one shared
        // formatter — the same value the SDL band's knob face reads.
        const std::string fill = format_lineup_fill_label(
            inputs.save->fill[static_cast<std::size_t>(team)]);
        const std::string map_units = format_lineup_map_units_label(
            inputs.save->map_units[static_cast<std::size_t>(team)]);

        // Header line: the colour the SDL band paints as a chip, the price,
        // then the seats through the ONE seat-run composition every surface
        // shares. A terminal row has room for all four whole labels, so
        // tier 1 always wins here — the tiers below it exist for the pixel
        // columns and for a sixteen-seat lobby.
        std::string seats = format_lineup_seat_run(
            band.seat_labels, band.seat_count,
            static_cast<int>(kCampaignPickerTerminalRowBudget));
        if (seats.empty())
            seats = "NO SEAT";
        model.lines.push_back(std::format(
            "TEAM {} {}  {}   {}", team + 1, og::sim::team_color_name(team),
            format_lineup_power(band.power), seats));
        // C5: MAP RULES is gone from the middle of this precedence — the
        // knobs are live on every campaign now, so a diagnostic or the plain
        // census is the whole cell, exactly as on a versus campaign.
        //
        // §3.8.4: the column is the SHARED preview formatter, so LINEUP, the
        // wizard's TEAMS line and the SDL band all read one report. With no
        // report (nullptr) format_match_preview answers the band's own
        // fighter census, which is byte-identical to what this column
        // printed before — a terminal that staged nothing says exactly what
        // it always said.
        const std::string census =
            format_match_preview(band, inputs.report, team);
        // B4's hint rides BESIDE the census, never instead of it: the SDL
        // band dims the box and keeps the fighter count, and the shared
        // formatter is deliberately not folded into format_lineup_census.
        const std::string map_units_hint =
            censused ? std::string(format_lineup_map_units_census(band))
                     : std::string();
        model.lines.push_back(std::format(
            "  [{}] [{}]  {}{}{}", fill, map_units, census,
            map_units_hint.empty() ? "" : "  ", map_units_hint));
    }

    // §2.3: the knobs are the HOST's. A joiner gets the bands and the
    // fighter list (its own company) and nothing that would desync.
    if (inputs.is_host) {
        for (int team = 0; team < 4; ++team) {
            // The row is the band cell's spelling verbatim (one label
            // rule), so the knob row renders the same STORED value (E1).
            const std::string fill = format_lineup_fill_label(
                inputs.save->fill[static_cast<std::size_t>(team)]);
            const std::string map_units = format_lineup_map_units_label(
                inputs.save->map_units[static_cast<std::size_t>(team)]);
            // The row text is the shared label VERBATIM behind the team
            // ordinal — never a second spelling of the same value. C5 retired
            // the classic mark: packs/core's stage step applies FILL and the
            // MAP UNITS strip on a mode-less level too, so the row means the
            // same thing on gladiator as it does on modes.
            model.items.push_back(TerminalLineupItem{
                TerminalLineupItem::Kind::Fill, team,
                std::format("TEAM {}  {}", team + 1, fill)});
            model.items.push_back(TerminalLineupItem{
                TerminalLineupItem::Kind::MapUnits, team,
                std::format("TEAM {}  {}", team + 1, map_units)});
        }
    }
    // B6: FIGHTERS is deleted, not moved down — MATCHUP's "move SLOT TEAM"
    // and the DEPLOY row already do both halves of it on every terminal
    // client, so a second page for the same two writes was the rule twin the
    // amendment closed. The strip is BACK | SPLIT EVEN | SPLIT FAIR | UNITE.
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

} // namespace og::ui
