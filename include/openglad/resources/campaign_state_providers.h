/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Menu-time og.campaign_* provider glue (issue #206,
// docs/campaign-scripting-design.md "Menu-time bindings"). Builds a filled
// og::script::hooks::CampaignProviders over a SaveData so all three
// clients (SDL GameSession, text picker init, curses app session) and the
// unit fixtures share one implementation of the wallet/roster/state rules.
//
// The returned callbacks BORROW the SaveData: install them only for the
// lifetime of the surface that owns it, and clear_campaign_providers()
// before that SaveData dies.

#include <openglad/gameplay/script/campaign_hooks.h>

#include <cstdint>
#include <functional>
#include <string>

class SaveData;

namespace og::data {

// `is_host` gates match_set and answers og.campaign_is_host (#212). An
// empty function means "always host" — local play has no lobby to defer
// to; the SDL install site passes the live lobby host predicate.
// `my_team` answers og.campaign_my_team with the FIRST LOCAL SEAT's team
// (Amendment 5 G4); an empty function falls back to
// campaign_my_team_fallback below. Only a surface that owns a seat view —
// the SDL lobby, a terminal's synthesized seats — passes one.
og::script::hooks::CampaignProviders make_campaign_providers(
    SaveData& save, std::function<bool()> is_host = {},
    std::function<int()> my_team = {});

// The my_team fallback: this save's own seat team, clamped into the four
// teams because save data is not validated on load. Exported so an
// installer that answers from its own seat view ENDS on this same rule
// (an empty lobby, a company with no seats) instead of restating it — one
// fallback, three install sites, no drift.
int campaign_my_team_fallback(const SaveData& save);

// The lobby sanitizer's per-knob rules (lobby_server.cpp
// sanitize_settings) as one callable: writes the sanitized value to `out`
// and answers true, or answers false (leaving `out` alone) for a name
// outside kCampaignMatchSettingNames. Every non-lobby producer of a match
// knob goes through this — og.campaign_match_set and the demo capture
// seam's OPENGLAD_DEMO_MATCH_TIME_LIMIT — so no route can mint a value the
// lobby would bounce.
bool clamp_match_setting(const std::string& name, std::int32_t value,
                         short& out);

// #212 session tail: a successful match_set (a write-through to the
// match knobs) arms this flag; the missions surface checks-and-clears
// it after each Acted outcome and runs the standard
// sync-settings-from-save tail so joiners follow.
bool consume_match_settings_dirty();

} // namespace og::data
