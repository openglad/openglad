/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Human-readable campaign/scenario titles for display. Selection, wire, and
// settings values keep the raw campaign id — only display text goes through
// these helpers.

#include <openglad/resources/campaign_metadata.h>

#include <openglad/resources/campaign_yaml.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/level_selection.h>

#include <format>
#include <map>
#include <string>
#include <utility>

namespace og::data {

namespace {

std::map<std::string, std::string>& campaign_title_cache()
{
    static std::map<std::string, std::string> cache;
    return cache;
}

std::map<std::pair<std::string, int>, std::string>& scenario_name_cache()
{
    static std::map<std::pair<std::string, int>, std::string> cache;
    return cache;
}

// The campaign.yaml identity axes, cached together (one yaml read fills
// all three): `mode:` drives IProgression, `matchup:` drives the versus
// surfaces, `first_level:` seeds the earned-roads frontier and the
// reset-campaign rewind (absent -> 1, the classic entry level).
struct CampaignModeMatchup
{
    std::string mode;
    std::string matchup;
    int first_level = 1;
};

std::map<std::string, CampaignModeMatchup>& campaign_mode_cache()
{
    static std::map<std::string, CampaignModeMatchup> cache;
    return cache;
}

// Harvest the "title" scalar from a campaign.yaml visible at yaml_path.
bool read_campaign_title(const char* yaml_path, std::string& out_title)
{
    CampaignYaml metadata;
    if (read_campaign_yaml(yaml_path, metadata) == CampaignYamlReadResult::OpenFailed)
        return false;

    out_title = metadata.title;
    return metadata.saw_title;
}

std::string lookup_campaign_title(const std::string& campaign_id)
{
    std::string title;

    // Reject ids that are unsafe to embed in a filesystem/archive path (empty,
    // oversize, "..", absolute, or non-[alnum._-]). This matches every other
    // consumer of the campaigns/<id>.glad pattern and prevents an
    // attacker-controlled id (e.g. from a lobby peer) from steering PHYSFS_mount
    // at a traversed real-world path. Raw id is shown as the fallback display.
    if (!is_safe_campaign_id(campaign_id))
        return campaign_id;

    // Fast path: the active campaign is mounted at the root. This also keeps
    // the slow path off the mounted archive — PhysFS refuses to mount the
    // same archive twice.
    if (campaign_id == get_mounted_campaign())
    {
        if (read_campaign_title("campaign.yaml", title) && !title.empty())
            return title;
        return campaign_id;
    }

    // Other campaigns get a throwaway mount under a private mountpoint so the
    // active campaign mount (and mounted_campaign_state) stays untouched.
    const std::string archive =
        get_user_path() + "campaigns/" + campaign_id + ".glad";
    const std::string mountpoint = ".og_meta/" + campaign_id;
    if (!og::resources::mount(archive.c_str(), mountpoint.c_str(), 1))
        return campaign_id;

    const std::string yaml_path = mountpoint + "/campaign.yaml";
    const bool found = read_campaign_title(yaml_path.c_str(), title);
    (void)og::resources::unmount(archive.c_str());

    if (found && !title.empty())
        return title;
    return campaign_id;
}

// Harvest mode/matchup from a campaign.yaml visible at yaml_path. Mirrors
// read_campaign_title's tolerance: a parse error after the pairs were
// pre-harvested still counts; only a missing file yields nothing.
CampaignModeMatchup read_campaign_mode_matchup(const char* yaml_path)
{
    CampaignYaml metadata;
    CampaignModeMatchup out;
    if (read_campaign_yaml(yaml_path, metadata) ==
        CampaignYamlReadResult::OpenFailed)
    {
        return out;
    }
    if (metadata.saw_mode)
        out.mode = metadata.mode;
    if (metadata.saw_matchup)
        out.matchup = metadata.matchup;
    if (metadata.saw_first_level)
        out.first_level = metadata.first_level;
    return out;
}

// Memoized mode/matchup lookup for any campaign id: mounted fast path (root
// campaign.yaml), else a throwaway private mount — the exact
// lookup_campaign_title shape. Absent keys read as "" (Classic/cooperative),
// including every failure path.
const CampaignModeMatchup& lookup_campaign_mode_matchup(
    const std::string& campaign_id)
{
    static const CampaignModeMatchup empty;
    if (campaign_id.empty())
        return empty;

    auto& cache = campaign_mode_cache();
    auto found = cache.find(campaign_id);
    if (found != cache.end())
        return found->second;

    CampaignModeMatchup value;
    if (!is_safe_campaign_id(campaign_id))
    {
        // Unsafe ids never touch PHYSFS_mount (same rule as the title path).
    }
    else if (campaign_id == get_mounted_campaign())
    {
        value = read_campaign_mode_matchup("campaign.yaml");
    }
    else
    {
        const std::string archive =
            get_user_path() + "campaigns/" + campaign_id + ".glad";
        const std::string mountpoint = ".og_meta/" + campaign_id;
        if (og::resources::mount(archive.c_str(), mountpoint.c_str(), 1))
        {
            const std::string yaml_path = mountpoint + "/campaign.yaml";
            value = read_campaign_mode_matchup(yaml_path.c_str());
            (void)og::resources::unmount(archive.c_str());
        }
    }
    return cache.emplace(campaign_id, std::move(value)).first->second;
}

} // namespace

std::string campaign_display_title(const std::string& campaign_id)
{
    if (campaign_id.empty())
        return campaign_id;

    auto& cache = campaign_title_cache();
    auto found = cache.find(campaign_id);
    if (found != cache.end())
        return found->second;

    std::string title = lookup_campaign_title(campaign_id);
    cache.emplace(campaign_id, title);
    return title;
}

std::string scenario_display_name(int scen_num)
{
    auto& cache = scenario_name_cache();
    const auto key = std::make_pair(get_mounted_campaign(), scen_num);
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second;

    const std::string title =
        load_scenario_title(("scen" + std::to_string(scen_num)).c_str());
    std::string name = (title == "none" || title.empty())
        ? std::format("{}. Level {}", scen_num, scen_num)
        : std::format("{}. {}", scen_num, title);
    cache.emplace(key, name);
    return name;
}

std::string campaign_mode(const std::string& campaign_id)
{
    return lookup_campaign_mode_matchup(campaign_id).mode;
}

std::string campaign_matchup(const std::string& campaign_id)
{
    return lookup_campaign_mode_matchup(campaign_id).matchup;
}

int campaign_first_level(const std::string& campaign_id)
{
    return lookup_campaign_mode_matchup(campaign_id).first_level;
}

std::string mounted_campaign_mode()
{
    // The mode drives the ACTIVE session's progression, so the mounted
    // campaign's root-visible campaign.yaml is the source (the lookup's fast
    // path). No mounted campaign (headless tests, pre-init) -> "" -> Classic,
    // with no IO touched.
    return campaign_mode(get_mounted_campaign());
}

std::string mounted_campaign_matchup()
{
    return campaign_matchup(get_mounted_campaign());
}

void forget_campaign_display_title(const std::string& campaign_id)
{
    campaign_title_cache().erase(campaign_id);
    campaign_mode_cache().erase(campaign_id);
    // Exit scans have no per-campaign eviction; a heal here is as rare as a
    // package install, so the whole road-graph cache re-derives.
    clear_level_exit_cache();
}

void clear_campaign_metadata_cache()
{
    campaign_title_cache().clear();
    scenario_name_cache().clear();
    campaign_mode_cache().clear();
    clear_level_exit_cache();
}

} // namespace og::data
