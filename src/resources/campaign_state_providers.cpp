/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/resources/campaign_state_providers.h>

#include <openglad/core/irandom.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace og::data {
namespace {

// The Base Camp gold-label rule (format_base_camp_gold_label in
// src/interface/ui/picker_common.cpp), reimplemented here so og_resources
// needs no og_interface dependency: the acting team is the lowest team
// number present on the roster; only an empty company falls back to
// my_team, clamped to [0,3]. In a networked lobby the autosave merge
// persists only owned teams' wallets, so this rule keeps every scripted
// debit durable.
constexpr int kWalletTeams = 4;

int acting_team(const SaveData& save)
{
    int team = kWalletTeams;
    for (const auto& member : save.team_list)
    {
        if (member != nullptr && member->teamnum >= 0 &&
            member->teamnum < kWalletTeams)
        {
            team = std::min(team, static_cast<int>(member->teamnum));
        }
    }
    if (team == kWalletTeams)
    {
        team = (save.my_team >= 0 && save.my_team < kWalletTeams)
            ? save.my_team
            : 0;
    }
    return team;
}

// Display-name rule from the picker's family_display_name: descriptor name
// when the family is installed, "BEAST" for unknown/uninstalled families.
const char* family_display_name(int family)
{
    const FamilyDescriptor* descriptor = get_family_descriptor(family);
    return descriptor != nullptr ? descriptor->name : "BEAST";
}

// #212: armed by a successful match_set, consumed by the missions surface
// after each Acted outcome (the sync-settings-from-save tail).
bool g_match_settings_dirty = false;

// "<prefix><1-based team digit>" -> the 0-based team, or -1. The ONE parse
// behind both the slot map and the clamp below, so a name the clamp accepts
// is exactly a name the slot map resolves.
int match_setting_team_suffix(const std::string& name, std::string_view prefix)
{
    if (name.size() != prefix.size() + 1)
        return -1;
    if (std::string_view(name).substr(0, prefix.size()) != prefix)
        return -1;
    const int team = name[prefix.size()] - '1';
    return (team >= 0 && team < SCORE_TEAM_COUNT) ? team : -1;
}

// The save knob a match-setting name maps to, or nullptr for a name
// outside the vocabulary (kCampaignMatchSettingNames).
short* match_setting_slot(SaveData& save, const std::string& name)
{
    // "team_count" is gone from the vocabulary (A3) and "strip_troops" went
    // with it (B5): both knobs are inert, and a provider slot for either
    // would hand a campaign a value the lobby snaps back to 0 on its way to
    // the sim. og.match_setting("strip_troops") still READS (always 0) —
    // that is the retired-but-answering side of the same ruling.
    if (name == "score_limit")
        return &save.ctf_capture_limit;
    if (name == "respawn_ticks")
        return &save.ctf_respawn_ticks;
    if (name == "respawn_mode")
        return &save.respawn_mode;
    if (name == "generator_rate")
        return &save.generator_rate;
    if (name == "time_limit")
        return &save.time_limit;
    // The eight per-team band knobs (B1-B4): "fill_1".."fill_4" and
    // "map_units_1".."map_units_4", the trailing digit being the 1-based
    // team.
    if (const int team = match_setting_team_suffix(name, "fill_"); team >= 0)
        return &save.fill[static_cast<std::size_t>(team)];
    if (const int team = match_setting_team_suffix(name, "map_units_");
        team >= 0)
    {
        return &save.map_units[static_cast<std::size_t>(team)];
    }
    return nullptr;
}

} // namespace

// The lobby sanitizer's rules (lobby_server.cpp sanitize_settings),
// applied per knob: the numeric knobs clamp (with their 0 = default/Auto
// sentinels preserved), the enum knobs REFUSE an out-of-range value —
// where the sanitizer reverts to its fallback, a provider write answers
// false without mutating.
bool clamp_match_setting(const std::string& name, std::int32_t value,
                         short& out)
{
    if (name == "score_limit")
    {
        out = static_cast<short>(std::clamp<std::int32_t>(value, 0, 50));
        return true;
    }
    if (name == "respawn_ticks")
    {
        out = value != 0
            ? static_cast<short>(std::clamp<std::int32_t>(value, 12, 1200))
            : static_cast<short>(0);
        return true;
    }
    if (name == "respawn_mode")
    {
        if (value < og::sim::kRespawnModeOff ||
            value > og::sim::kRespawnModeTeamOneHeroes)
        {
            return false;
        }
        out = static_cast<short>(value);
        return true;
    }
    if (name == "generator_rate")
    {
        out = value != 0
            ? static_cast<short>(std::clamp<std::int32_t>(value, 25, 400))
            : static_cast<short>(0); // 0 = default (100)
        return true;
    }
    if (name == "time_limit")
    {
        // Sim ticks (12/s); 0 = the map's own value. The deliberate twin of
        // lobby_server.cpp sanitize_settings' time_limit rule — keep the
        // numbers identical or a scripted preset can publish a value the
        // server bounces. The 720-tick floor is one whole minute, so no UI
        // face can render a 0-minute clock and read as the MAP sentinel.
        out = value != 0
            ? static_cast<short>(std::clamp<std::int32_t>(value, 720, 21600))
            : static_cast<short>(0);
        return true;
    }
    // Per-team band knobs (B1-B4). Every value in range is legal — 0 is
    // FILL: NONE / MAP UNITS ON, the default state (E1), not a refusal —
    // so both clamp rather than refuse; the bounds come from the ONE
    // og::sim implementation the lobby sanitizer uses, so a scripted preset
    // can never publish a value the server would bounce.
    if (match_setting_team_suffix(name, "fill_") >= 0)
    {
        out = static_cast<short>(og::sim::clamp_fill(value));
        return true;
    }
    if (match_setting_team_suffix(name, "map_units_") >= 0)
    {
        out = static_cast<short>(og::sim::clamp_map_units(value));
        return true;
    }
    return false; // unknown name
}

int campaign_my_team_fallback(const SaveData& save)
{
    return std::clamp(static_cast<int>(save.my_team), 0,
                      SCORE_TEAM_COUNT - 1);
}

bool consume_match_settings_dirty()
{
    const bool was_dirty = g_match_settings_dirty;
    g_match_settings_dirty = false;
    return was_dirty;
}

og::script::hooks::CampaignProviders make_campaign_providers(
    SaveData& save, std::function<bool()> is_host,
    std::function<int()> my_team)
{
    og::script::hooks::CampaignProviders providers;
    SaveData* const save_ptr = &save;
    if (!is_host)
        is_host = [] { return true; }; // local play is always host
    // G4: no seat view here — og_resources cannot see the lobby or the
    // picker. A surface that has one passes it in; everyone else answers
    // the save's own team, read live (Base Camp moves my_team while the
    // book is installed).
    if (!my_team)
        my_team = [save_ptr] { return campaign_my_team_fallback(*save_ptr); };

    providers.state_get = [save_ptr](const std::string& key) -> std::int32_t {
        return save_ptr->campaign_state_get(save_ptr->current_campaign, key);
    };
    providers.state_set = [save_ptr](const std::string& key,
                                     std::int32_t value) -> bool {
        return save_ptr->campaign_state_set(save_ptr->current_campaign, key,
                                            value);
    };

    providers.gold_get = [save_ptr]() -> std::int64_t {
        return static_cast<std::int64_t>(
            save_ptr->m_totalcash[acting_team(*save_ptr)]);
    };
    providers.gold_spend = [save_ptr](std::int64_t cost) -> bool {
        if (cost < 0)
            return false;
        // Infinite gold answers affordable but skips the debit — the
        // wallet is never inflated and never written (the hire/train
        // free_purchase rule).
        if (save_ptr->infinite_gold != 0)
            return true;
        std::uint32_t& wallet = save_ptr->m_totalcash[acting_team(*save_ptr)];
        if (cost > static_cast<std::int64_t>(wallet))
            return false;
        wallet -= static_cast<std::uint32_t>(cost);
        return true;
    };
    providers.gold_grant = [save_ptr](std::int64_t amount) {
        if (amount <= 0)
            return;
        std::uint32_t& wallet = save_ptr->m_totalcash[acting_team(*save_ptr)];
        const std::uint64_t headroom =
            std::numeric_limits<std::uint32_t>::max() - wallet;
        wallet += static_cast<std::uint32_t>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(amount), headroom));
    };

    providers.team_snapshot =
        [save_ptr]() -> std::vector<og::script::hooks::CampaignRosterEntry> {
        std::vector<og::script::hooks::CampaignRosterEntry> roster;
        for (std::size_t slot = 0; slot < save_ptr->team_list.size(); slot++)
        {
            const guy* member = save_ptr->team_list[slot].get();
            if (member == nullptr)
                continue;
            og::script::hooks::CampaignRosterEntry entry;
            entry.name = member->name;
            entry.family = family_display_name(member->family);
            entry.level = member->level;
            entry.exp = static_cast<int>(member->exp);
            entry.strength = member->strength;
            entry.dexterity = member->dexterity;
            entry.constitution = member->constitution;
            entry.intelligence = member->intelligence;
            entry.armor = member->armor;
            entry.team =
                std::clamp(static_cast<int>(member->teamnum), 0, 3);
            // Per-hero identity (GTL v16): the campaign_tag byte plus the
            // team_list slot the assign_set write is addressed by. The
            // slot, not a guy id — ids regenerate every mission.
            entry.tag = member->campaign_tag;
            entry.save_slot = static_cast<int>(slot);
            entry.deployed = member->deployed;
            roster.push_back(std::move(entry));
        }
        return roster;
    };

    // Base Camp assign write (GTL v16): check-then-write on the clicked
    // row's slot. Refusals (invalid/unoccupied slot, tag outside the
    // persisted byte's range) answer false with no mutation.
    providers.assign_set = [save_ptr](int save_slot, int tag) -> bool {
        if (save_slot < 0 || save_slot >= MAX_TEAM_SIZE)
            return false;
        if (tag < 0 || tag > 255)
            return false;
        guy* member =
            save_ptr->team_list[static_cast<std::size_t>(save_slot)].get();
        if (member == nullptr)
            return false;
        member->campaign_tag = static_cast<std::uint8_t>(tag);
        return true;
    };

    providers.level_completed = [save_ptr](int level_id) -> bool {
        return save_ptr->is_level_completed(level_id);
    };
    providers.current_level = [save_ptr]() -> int {
        return static_cast<int>(save_ptr->scen_num);
    };
    providers.scenario_title = [](int level_id) -> std::string {
        // load_scenario_title's "none" failure sentinel would read as a
        // real title to a script; the binding contract is "" on failure.
        std::string title;
        if (load_scenario_title_with_error(
                ("scen" + std::to_string(level_id)).c_str(), title) !=
            LevelFileIoError::None)
            return std::string();
        return title;
    };

    // #212: menu-time match knobs. Reads are open to every machine;
    // writes are host-only (the SET LEVEL predicate) and clamp like the
    // lobby sanitizer, so a scripted preset can never publish a value the
    // server would bounce.
    providers.match_get = [save_ptr](const std::string& name) -> std::int32_t {
        const short* slot = match_setting_slot(*save_ptr, name);
        return slot != nullptr ? static_cast<std::int32_t>(*slot) : 0;
    };
    providers.match_set = [save_ptr, is_host](const std::string& name,
                                              std::int32_t value) -> bool {
        if (!is_host())
            return false;
        short* slot = match_setting_slot(*save_ptr, name);
        short clamped = 0;
        if (slot == nullptr || !clamp_match_setting(name, value, clamped))
            return false;
        *slot = clamped;
        g_match_settings_dirty = true;
        return true;
    };
    providers.is_host = std::move(is_host);
    providers.my_team = std::move(my_team);

    // og.campaign_random's default: a process-lifetime menu-side generator,
    // seeded from the wall clock the first time any camp action rolls.
    // Menu-side ONLY — the sim's RNG stream is fenced during campaign
    // dispatch (og.rand errors), and this generator never touches it, so a
    // camp roll can never perturb replay/parity determinism. The n < 1
    // floor is defensive: the binding rejects n < 1 before the provider
    // runs.
    providers.random_pick = [](int n) -> int {
        static SeededRandom menu_rng(static_cast<std::uint32_t>(
            std::chrono::system_clock::now().time_since_epoch().count()));
        if (n < 1)
            return 1;
        return static_cast<int>(
                   menu_rng.next(static_cast<std::uint32_t>(n))) +
            1;
    };

    return providers;
}

} // namespace og::data
