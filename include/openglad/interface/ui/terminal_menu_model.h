/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Terminal menu projection (SDL-free, design: menu-engine G7).
//
// One prepared model per shared menu screen for the text and curses clients:
// the FULL entry list (never filtered — this preserves the 1-based index
// contract by construction), resolved dynamic labels, the context header
// lines, and the cancel item. The clients keep their own input loops and
// rendering; only the content is shared.

#include <openglad/gameplay/lobby_state.h>
#include <openglad/interface/ui/menu_binding.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/picker_common.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class SaveData;
class guy;

namespace og::ui {

struct TerminalMenuEntry {
    const PickerMenuItem* item = nullptr;
    std::string label;
    bool selectable = true;
};

struct TerminalMenuModel {
    std::string title;
    std::vector<std::string> context_lines;   // roster/gold header (Team Build)
    std::vector<TerminalMenuEntry> entries;   // FULL list, never filtered
    const PickerMenuItem* cancel_item = nullptr;  // Quit on Main, Back elsewhere
};

// Build the projection for one shared menu: title and items from
// picker_menu_definition, labels via menu_item_label, context lines for the
// Team Build screen, cancel item via menu_cancel_item.
TerminalMenuModel build_terminal_menu_model(PickerMenuId menu_id,
                                            const MenuLabelContext& context);

// Guard message a terminal client shows when the item is activated while its
// gate denies it ("" when the item is not gated / the gate passes). Currently
// carries the CTF match-settings guard.
std::string_view terminal_gate_message(const PickerMenuItem& item,
                                       const MenuLabelContext& context);

// --- LINEUP, the terminal projection (docs/lineup-design.md §8) ----------
//
// The SDL LINEUP page is four team bands plus one action row. A terminal has
// no grid, so the bands become CONTEXT LINES above a numbered item list — the
// campaign-camp shape (lines + rows + a numeric prompt), which both terminal
// clients already drive. Every string here comes from the shared §2.1/§4
// formatters (format_lineup_bots_label / _level_label / _census / _power), so
// the three clients cannot drift apart on a label.

// One selectable LINEUP row. `team` is meaningful for the two knob kinds only.
struct TerminalLineupItem {
    enum class Kind {
        BotSquad,   // host-only: cycle this team's bot squad preset
        BotLevel,   // host-only: cycle this team's bot level
        Fighters,   // open the fighter list (team + deploy + POWER rows)
        SplitEven,  // §5 SPLIT EVEN
        SplitFair,  // §5 SPLIT FAIR
        Unite,      // §5 ALL TO 1
        Back,
    };

    Kind kind = Kind::Back;
    int team = 0;
    std::string label;
};

struct TerminalLineupModel {
    // Two lines per team: the header line (chip -> "TEAM n", POWER, seats)
    // and the knob/census line. Four bands, always all four, so a team that
    // is off still says so.
    std::vector<std::string> lines;
    std::vector<TerminalLineupItem> items;
    // The censused bands the lines were drawn from, handed on so a terminal
    // can turn the BOTS wheel with og::ui::lineup_bots_wheel_next — the ONE
    // implementation of amendment A2's refusal and step-over, shared with
    // the SDL screen. A terminal that spelled the rule itself would drift.
    std::array<LineupTeamBand, 4> bands{};
};

// What the page needs to know. `players` is the lobby seat census (every
// machine); a local terminal client passes synthesize_local_lobby_players(),
// which is the SAME picture the SDL screens read (M3: one derivation).
// `is_host` hides the eight knob rows for a joiner (§2.3) — the bands still
// show what the host chose.
struct TerminalLineupInputs {
    const SaveData* save = nullptr;
    std::span<const og::sim::LobbyPlayer> players;
    std::span<const std::uint8_t> local_player_indices;
    bool networked = false;
    bool is_host = true;
    // The campaign's bot-squad preset names (og::script::hooks::
    // campaign_lineup_presets); empty offers AUTO/NONE only.
    std::span<const std::string> preset_names;
};

TerminalLineupModel build_terminal_lineup_model(
    const TerminalLineupInputs& inputs);

// §2.3: on a CLASSIC (non-versus) campaign the eight bot knobs are stored
// but the map ignores them, so the SDL screen draws them dimmed over a
// MAP RULES census and its callbacks return without cycling. A terminal
// cannot dim, so the knob rows carry this marker and the write refuses in
// words — one spelling for both clients.
inline constexpr std::string_view kTerminalLineupMapRulesMark = "  (MAP RULES)";
inline constexpr std::string_view kTerminalLineupMapRulesRefusal =
    "MAP RULES: this campaign's levels decide the bots.";

// The §5 SPLIT tail both terminal clients run: this machine's seated teams,
// planned under the campaign's own can_team rule (lineup_zone_can_team) and
// the per-slot editable predicate, then applied through set_guy_team.
// `report` receives the lines the caller shows — "No seats: deploy a
// character first.", "Moved N fighters.", "N fighters locked and stayed
// put." — so text prints them and curses show_texts them from one wording.
// Returns how many slots actually moved; 0 means there is nothing to
// autosave.
int terminal_apply_lineup_split(SaveData& save, LineupSplit mode,
                                std::vector<std::string>& report);

// One fighter-list row: "  1. Arthur (Soldier) LV 3  RED  DEPLOYED
// POWER 120". BENCHED replaces DEPLOYED; an unpriced fighter reads
// "POWER --" through the same §4 formatter the bands use.
std::string format_terminal_lineup_fighter_row(int slot_index,
                                               const guy& member);

// The lines of the fighter list: a header, then one row per occupied slot.
std::vector<std::string> terminal_lineup_fighter_lines(const SaveData& save);

} // namespace og::ui
