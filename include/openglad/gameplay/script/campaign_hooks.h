/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Campaign-hook dispatch and menu-time providers (issue #206,
// docs/campaign-scripting-design.md). SDL-free and Lua-free, like
// family_hooks.h: the picker-facing surfaces call these helpers and only
// ever see plain structs — the Lua boundary crossing lives in
// src/gameplay/script/.
//
// og.register_campaign_hooks stores a campaign's picker hooks per VM;
// the dispatchers below run them under the campaign-dispatch fence (the
// world API, og.rand and the three registrars all error while a campaign
// hook is on the stack), so a menu script can never perturb the sim's RNG
// stream or rewrite hook tables. A malformed or erroring hook — and a
// duplicate registration — answers "no scripted picker", so the stock UI
// stays reachable.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace og::script::hooks {

// Bounds shared by the registrar, the og.campaign_state_set binding and the
// save-side load rejection (docs/campaign-scripting-design.md).
inline constexpr int kCampaignVarNameMax = 32;   // chars per var/state key
inline constexpr int kCampaignVarsMax = 64;      // names per registration
inline constexpr int kCampaignPageMaxEntries = 24;
inline constexpr int kCampaignPageMaxLines = 6;

// 1..kCampaignVarNameMax chars of [a-z0-9_] — the safe-id charset every
// campaign var/state key must satisfy.
bool valid_campaign_var_name(std::string_view name);

// One selectable row of a scripted picker page.
struct CampaignPageEntry {
    std::string id, label, note;
    enum class Kind { Level, Page, Action } kind = Kind::Level;
    int level = 0;
    int cost = 0;
};

// One page of the scripted picker: pure data, fetched per navigation or
// action (never per frame) and rendered by all three clients.
struct CampaignPage {
    std::string title;
    std::vector<std::string> lines;
    std::vector<CampaignPageEntry> entries;
};

struct CampaignActionResult {
    bool ok = false;
    std::string message;  // optional toast
};

// Roster row of og.campaign_team — values, not handles.
struct CampaignRosterEntry {
    std::string name, family;
    int level = 0, exp = 0, strength = 0, dexterity = 0, constitution = 0,
        intelligence = 0, armor = 0, team = 0;
};

// The menu-time provider seam. og_gameplay cannot see SaveData or the
// picker (dependency direction), so every og.campaign_* binding resolves
// through these process-global callbacks, installed by the surface that
// owns the SaveData (SDL GameSession, text picker init, curses app
// session, unit-test fixtures — and deliberately none on openglad_server).
// Providers must never dispatch Lua (no re-entry). state_set is
// check-then-write: it returns false on bounds rejection WITHOUT mutating,
// and the binding raises on false.
struct CampaignProviders {
    std::function<std::int32_t(const std::string&)> state_get;
    std::function<bool(const std::string&, std::int32_t)> state_set;
    std::function<std::int64_t()> gold_get;
    std::function<bool(std::int64_t)> gold_spend;
    std::function<void(std::int64_t)> gold_grant;
    std::function<std::vector<CampaignRosterEntry>()> team_snapshot;
    std::function<bool(int)> level_completed;
    std::function<int()> current_level;
    std::function<std::string(int)> scenario_title;
};

void install_campaign_providers(CampaignProviders providers);
void clear_campaign_providers();

// True when the active VM carries exactly one og.register_campaign_hooks
// registration. A duplicate registration answers false (and records the
// conflict as a script error, once); so does a script-less install.
bool campaign_picker_registered();

// Dispatches picker_menu(page_id) ("" = root) and parses the returned page.
// False — with the stock UI as the caller's fallback — when no picker is
// registered, the hook errors, or the page is malformed (recorded as a
// script error naming the field). Entries clip at kCampaignPageMaxEntries,
// lines at kCampaignPageMaxLines.
bool campaign_picker_page(const std::string& page_id, CampaignPage& out);

// Dispatches picker_action(entry_id). False when no scripted picker serves
// actions; true when the hook ran — out.ok false if it errored (mutations
// already applied stick; see the design doc), out.message from the
// returned table's optional `message` string.
bool campaign_picker_action(const std::string& entry_id,
                            CampaignActionResult& out);

// The `vars` names of the active registration, in declared order — the
// list the level-load sync copies from the save into
// GameWorld::campaign_vars. Empty when no picker is registered (or the
// registration is conflicted).
std::vector<std::string> campaign_registered_vars();

// Diagnostic/test seam: every key of the active VM's og table whose value
// is a function, sorted. The sandbox has no pairs(), so the fence-walk
// test enumerates the surface here. No sim path reads it.
std::vector<std::string> og_function_names();

}  // namespace og::script::hooks
