/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The earned-roads level-selection gate (docs/camp-controls-design.md).
// One frontier computation and one predicate, shared by every surface that
// can write the campaign cursor — the SDL browsers, the Lua camp/book level
// rows, and the raw terminal prompts. Lives in og::data so the SDL-free
// terminals reach the same rule the SDL client applies.

#include <vector>

class SaveData;

namespace og::data {

// The ACCESSIBLE set for the mounted campaign, sorted ascending:
// {campaign.yaml first_level} ∪ {save.scen_num} ∪
// completed_levels[campaign] ∪ exits(completed). Exemptions do not apply
// here — this is the raw frontier (the PROGRESS report shows it verbatim).
std::vector<int> accessible_levels(const SaveData& save);

// False when the save's campaign is exempt from progression gating:
// `matchup: versus` (an arena picker's whole point is free field selection)
// or `mode: tower` (its own IProgression; never writes completed_levels).
bool level_selection_gating_active(const SaveData& save);

// The gate: true when a player-facing surface may set the cursor to
// `level` — always for an exempt campaign, otherwise iff `level` is in the
// ACCESSIBLE set. Refusals speak kCampaignLevelClosedMessage and return
// before any autosave tail.
bool level_selection_allowed(const SaveData& save, int level);

// Drop the memoized per-level exit scans (keyed on the mounted campaign).
// clear_campaign_metadata_cache() calls this so an editor save or package
// rewrite re-derives the road graph.
void clear_level_exit_cache();

} // namespace og::data
