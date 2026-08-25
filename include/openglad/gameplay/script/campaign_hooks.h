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

// Base Camp zone bounds (docs/basecamp-zones-design.md, "The widget
// contract" / "Bounds arithmetic"). Unlike the page parser's clip rule,
// every zone bound is a hard rejection: an over-budget composition is
// malformed and the caller falls to the default (scriptless) zone. The
// action-row cap is TOTAL across the zone — it is what closes the 50→72
// appended-ordinal arithmetic (16 rows + 4 pagers + 3 spare).
inline constexpr int kCampaignZoneMaxWidgets = 6;
inline constexpr int kCampaignZoneMaxReadoutWidgets = 1;
inline constexpr int kCampaignZoneMaxActionsWidgets = 2;
inline constexpr int kCampaignZoneMaxTextWidgets = 2;
inline constexpr int kCampaignZoneMaxTextLines = 6;      // per text widget
inline constexpr int kCampaignZoneMaxActionEntries = 16; // TOTAL, whole zone
inline constexpr int kCampaignZoneMaxReadoutItems = 3;
inline constexpr int kCampaignZoneMaxRosterLocks = 24;   // one per roster slot
// A widget's `weight` is integer ROW UNITS on the 14px grid, and the zone
// band is exactly 8 units tall. A weight past the whole band cannot lay out
// beside ANY sibling, so it is a named parse rejection rather than a silent
// layout-time fall to the default zone — the same "every bound is a hard
// rejection" rule, applied where the author can be told which widget it was.
// CampaignZoneSession::kZoneRowUnits static_asserts against this.
inline constexpr int kCampaignZoneMaxWeight = 8;

// 1..kCampaignVarNameMax chars of [a-z0-9_] — the safe-id charset every
// campaign var/state key must satisfy.
bool valid_campaign_var_name(std::string_view name);

// One selectable row of a scripted picker page.
struct CampaignPageEntry {
    std::string id, label, note;
    enum class Kind { Level, Page, Action } kind = Kind::Level;
    int level = 0;
    int cost = 0;
    // `done = true` retires a costed action the book has already honored (a
    // bought kit, a signed contract): the row stops advertising its price,
    // draws on the spent face and no longer dispatches. Without it a
    // purchased item still reads as for sale at full price — the book knows
    // it is spent, and only the book can.
    bool done = false;
    // `replay = true` (#207, level rows): once the level is CLEARED,
    // clicking the row arms the replay excursion — the engine loads the
    // authored census (the completed-level purge skips) and a win restores
    // the campaign cursor to where the player left it. On an uncleared row
    // the mark is inert (a normal set); the engine owns the cleared check,
    // never the script.
    bool replay = false;
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
    // Optional scenario the action answers with (`level` on the returned
    // table): the caller routes it through its OWN gated level-set tail —
    // host gate, earned-roads gate, load-rollback — never a bare cursor
    // write. -1 = no level carried; the parser rejects anything outside
    // the loader's id range (0..32767) back to -1.
    int level = -1;
};

// One deploy refusal on the zone's roster widget. Heroes are addressed by
// the persisted campaign_tag byte, never by guy id (ids regenerate every
// mission — the red-team rule): tag 1..255 locks own heroes whose
// campaign_tag equals tag; unset = true locks heroes still unassigned
// (campaign_tag == 0). Exactly one of the two forms per lock (parser
// enforced). The reason is delivered as a message-line toast, never a
// modal (a modal strands a networked joiner mid-GO).
struct CampaignRosterLock {
    int tag = -1;        // 1..255, or -1 when this is an `unset` lock
    bool unset = false;  // true: locks unassigned heroes
    std::string reason;  // toast on the refused deploy ("" = silent refusal)
};

// The roster widget's assignment chip (assign = { key, labels, frozen }).
// Cycling writes the clicked row's campaign_tag through the assign_set
// provider: unset(0) → 1 → 2 → 1, never back to unset; labels[0]/[1] name
// tags 1/2. A non-empty `frozen` keeps chips visible but refuses cycling
// with that reason.
struct CampaignAssignSpec {
    std::string key;                  // channel name, [a-z0-9_] like vars
    std::vector<std::string> labels;  // exactly two, both non-empty
    std::string frozen;               // "" = cycling allowed
    bool active = false;              // true when the widget declared assign
};

// One widget of the Lua-composed Base Camp gameplay zone
// (docs/basecamp-zones-design.md "The widget contract"). Pure data; only
// the fields of the declared kind are read by the parser, the rest keep
// their defaults. `weight` is integer row units on the zone's fixed 14px
// grid (0 = the layout's default share) — never pixels.
struct CampaignZoneWidget {
    enum class Kind { Roster, Text, Actions, Readout };
    Kind kind = Kind::Roster;
    int weight = 0;

    // Roster: capability flags (default on — absence is full capability),
    // deploy locks, and the optional assignment chip.
    bool can_deploy = true;
    bool can_train = true;
    bool can_reorder = true;
    bool can_team = true;
    bool can_hire = true;
    std::vector<CampaignRosterLock> locks;   // <= kCampaignZoneMaxRosterLocks
    CampaignAssignSpec assign;

    // Text: readability-strip lines.
    std::vector<std::string> lines;          // <= kCampaignZoneMaxTextLines

    // Actions: rows in the existing page-entry vocabulary, windowed by the
    // widget's own pager pair UI-side.
    std::vector<CampaignPageEntry> entries;  // <= 16 TOTAL across the zone

    // Readout: one zone row of label/value cells.
    struct ReadoutItem {
        std::string label, value;
    };
    std::vector<ReadoutItem> items;          // <= kCampaignZoneMaxReadoutItems
};

// The base_camp hook's whole answer. Per-kind caps (parser enforced,
// violation = malformed = default zone): exactly one roster, at most one
// readout, <= 2 actions, <= 2 text, <= kCampaignZoneMaxWidgets total.
struct CampaignZone {
    std::vector<CampaignZoneWidget> widgets;
};

// Roster row of og.campaign_team — values, not handles.
struct CampaignRosterEntry {
    std::string name, family;
    int level = 0, exp = 0, strength = 0, dexterity = 0, constitution = 0,
        intelligence = 0, armor = 0, team = 0;
    // Per-hero identity (GTL v16, docs/basecamp-zones-design.md "Per-hero
    // identity"): the persisted campaign_tag byte (0 = unassigned) and the
    // owning SaveData::team_list slot — the address the assign_set
    // provider takes at dispatch time. Never a guy id (ids regenerate).
    int tag = 0;
    int save_slot = -1;
    // Whether this hero is standing in tonight's sortie. A camp that counts
    // its own march rows has to count the party that will actually walk out
    // — the roster's own oath census answers "who swore", never "who goes",
    // and the two diverge the moment anything stands a hero down.
    bool deployed = false;
};

// The og.campaign_match_get / og.campaign_match_set name vocabulary
// (#212): the persisted match knobs the menus edit, one spelling
// shared by the sim's read-only og.match_setting twin (minus its
// "difficulty", plus generator_rate, which the sim never reads by name).
// og.campaign_match_get errors on any name outside this list — the sim
// twin's unknown-name rule — while match_set answers false (policy lives
// in the provider).
// The eight per-team bot knobs (LINEUP §3.1) join the list: "bot_squad_N"
// is a preset ordinal (0 = AUTO, 1 = NONE, 2.. = the campaign's preset) and
// "bot_level_N" a bot level (0 = AUTO, 1..9), N being the 1-based team.
inline constexpr const char* kCampaignMatchSettingNames[] = {
    "team_count",   "score_limit",  "respawn_ticks",
    "strip_troops", "respawn_mode", "generator_rate",
    "time_limit",
    "bot_squad_1",  "bot_squad_2",  "bot_squad_3",  "bot_squad_4",
    "bot_level_1",  "bot_level_2",  "bot_level_3",  "bot_level_4",
};

// The menu-time provider seam. og_gameplay cannot see SaveData or the
// picker (dependency direction), so every og.campaign_* binding resolves
// through these process-global callbacks, installed by the surface that
// owns the SaveData (SDL GameSession, text picker init, curses app
// session, unit-test fixtures — and deliberately none on openglad_server).
// Providers must never dispatch Lua (no re-entry). state_set is
// check-then-write: it returns false on bounds rejection WITHOUT mutating,
// and the binding raises on false. match_set clamps like the lobby
// sanitizer and returns false for unknown names or when the surface is
// not the host (#212); is_host answers the surface's host predicate
// (local play is always host).
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
    std::function<std::int32_t(const std::string&)> match_get;
    std::function<bool(const std::string&, std::int32_t)> match_set;
    std::function<bool()> is_host;
    // og.campaign_random(n) → 1..n. Menu-time randomness ONLY: the default
    // (og::data::make_campaign_providers) is a process-lifetime generator
    // seeded from the wall clock at first use, and the sim RNG stays
    // fenced (og.rand still errors under campaign dispatch). The binding
    // rejects n < 1 before the provider runs; tests install a
    // deterministic provider.
    std::function<int(int)> random_pick;
    // Base Camp assign write (GTL v16): sets team_list[save_slot]'s
    // campaign_tag. Check-then-write like state_set — answers false with
    // NO mutation for an invalid or unoccupied slot or a tag outside
    // 0..255. Not a Lua binding: the assign chip's dispatch tail calls it
    // directly (assign flows through providers, never through og.*).
    std::function<bool(int, int)> assign_set;  // (save_slot, tag)
};

void install_campaign_providers(CampaignProviders providers);
void clear_campaign_providers();

// The assign chip's dispatch tail (docs/basecamp-zones-design.md "The widget
// contract"): forwards to the installed providers' assign_set. False — with
// NO mutation — when no providers are installed or the provider refuses
// (invalid/unoccupied slot, tag outside 0..255). Not a Lua binding: assign
// flows through providers, never through og.*.
bool campaign_assign_set(int save_slot, int tag);

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

// True when the active registration carries a base_camp hook (the SDL
// surface's zone-vs-default gate). Same conflict/scriptless rules as
// campaign_picker_registered.
bool campaign_zone_registered();

// Dispatches base_camp() and parses the returned zone under the same
// fence/bracketing discipline as campaign_picker_page. False — the caller
// renders the DEFAULT zone — when no base_camp hook is registered, the
// hook errors, or the composition is malformed/over-budget (recorded as a
// script error naming the field; every bound is a hard rejection, never a
// clip).
bool campaign_zone(CampaignZone& out);

// The `vars` names of the active registration, in declared order — the
// list the level-load sync copies from the save into
// GameWorld::campaign_vars. Empty when no picker is registered (or the
// registration is conflicted).
std::vector<std::string> campaign_registered_vars();

// Diagnostic/test seam: every key of the active VM's og table whose value
// is a function, sorted. The sandbox has no pairs(), so the fence-walk
// test enumerates the surface here. No sim path reads it.
std::vector<std::string> og_function_names();

// --- LINEUP (docs/lineup-design.md §3.3, §4) ---------------------------
//
// The fourth campaign hook is a TABLE, not a function:
//
//   og.register_campaign_hooks({ ...,
//     lineup = {
//       presets = { "BALANCED", "CASTERS" },   -- bot squad names
//       power   = function(row) return <int> end,
//     } })
//
// `presets` names the bot-squad cycler's entries (ordinal 2.. on the
// LINEUP page; 0 = AUTO, 1 = NONE are engine-owned and never named by a
// campaign). `power` prices ONE fighter from its derived stats so the
// bands can show POWER n; the engine never knows what either means.

// The cycler's ceiling. A joiner clamps bot_squad to [0, 1 + this] with
// no preset list at all, so the bound lives in the engine, not the book.
inline constexpr int kMaxBotPresets = 8;
// Preset names ride a 12-char face as "BOTS: <NAME>", so 6 chars is the
// whole room. Longer names are clipped at registration (never refused —
// a name is cosmetic and must not cost a campaign its whole book).
inline constexpr std::size_t kLineupPresetNameMax = 6;

// One fighter, priced. The values are the ENGINE's own derived stats
// (og::ui::compute_derived_stats — the same guy-bonus + family-base
// derivation spawn applies), already truncated to integers, so a
// campaign's power function reads exactly what the sim would field.
struct LineupPowerRow {
    std::string family;
    int level = 0;
    int hp = 0;
    int mp = 0;
    int armor = 0;
    int damage = 0;
    int stepsize = 0;
    int fire_frequency = 0;  // busy ticks after an attack; lower is faster
};

// True when the active registration carries a `lineup` table. Same
// conflict/scriptless rules as campaign_picker_registered.
bool campaign_lineup_registered();

// The registered preset names, in declared order (<= kMaxBotPresets, each
// clipped to kLineupPresetNameMax upper-case chars). False — leaving `out`
// untouched — when no lineup table is registered; true with an EMPTY list
// for a lineup table that registers only `power`.
bool campaign_lineup_presets(std::vector<std::string>& out);

// Dispatches lineup.power(row) under the campaign fence. False — the band
// shows `--` — when no power function is registered, the hook errors, or
// it answers anything but a number.
bool campaign_fighter_power(const LineupPowerRow& row, long long& out);

}  // namespace og::script::hooks
