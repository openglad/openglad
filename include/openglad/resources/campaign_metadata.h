/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <string>

namespace og::data {

// Human-readable campaign title from the package's campaign.yaml
// ("Gladiator" for "org.openglad.gladiator"). Falls back to the raw id when
// the package, its campaign.yaml, or the title is missing. Memoized.
std::string campaign_display_title(const std::string& campaign_id);

// "N. Title" for scen<N>.fss of the mounted campaign, e.g.
// "1. SOUTH OF TALWOOD (BEGINNING)". Falls back to "N. Level N" when the
// scenario or its title is missing. Memoized per (mounted campaign, N).
std::string scenario_display_name(int scen_num);

// Game-mode identity of a campaign: the raw `mode:` string from its
// campaign.yaml ("" when absent — Classic). Mounted campaigns read the root
// campaign.yaml; others get a throwaway private mount (the
// campaign_display_title shape). Memoized per campaign id; invalidated with
// the display-title cache on mount changes.
std::string campaign_mode(const std::string& campaign_id);

// Matchup axis of a campaign: the raw `matchup:` string from its
// campaign.yaml ("" when absent — cooperative; "versus" — competitive
// multi-team matchup). Same lookup/memoization as campaign_mode.
std::string campaign_matchup(const std::string& campaign_id);

// Game-mode identity of the MOUNTED campaign. Consumed by
// og::mode::current_progression().
std::string mounted_campaign_mode();

// Matchup axis of the MOUNTED campaign.
std::string mounted_campaign_matchup();

// Drop one campaign's memoized title (and mode). Mounting a package calls
// this so a lookup that failed before the package existed (and memoized the
// raw-id fallback) heals once the package is actually mountable.
void forget_campaign_display_title(const std::string& campaign_id);

// Drop all memoized titles. Call after anything that rewrites, installs, or
// removes a campaign package or scenario file.
void clear_campaign_metadata_cache();

} // namespace og::data
