/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/resources/campaign_state_providers.h>

#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/guy.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
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

} // namespace

og::script::hooks::CampaignProviders make_campaign_providers(SaveData& save)
{
    og::script::hooks::CampaignProviders providers;
    SaveData* const save_ptr = &save;

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
        for (const auto& member : save_ptr->team_list)
        {
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
            roster.push_back(std::move(entry));
        }
        return roster;
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

    return providers;
}

} // namespace og::data
