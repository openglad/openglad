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

std::map<std::string, std::string>& campaign_mode_cache()
{
    static std::map<std::string, std::string> cache;
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

std::string mounted_campaign_mode()
{
    // Fast path only: the mode drives the ACTIVE session's progression, so
    // the mounted campaign's root-visible campaign.yaml is the sole source
    // (mirrors the campaign_display_title fast path). No mounted campaign
    // (headless tests, pre-init) -> "" -> Classic, with no IO touched.
    const std::string campaign_id = get_mounted_campaign();
    if (campaign_id.empty())
        return {};

    auto& cache = campaign_mode_cache();
    auto found = cache.find(campaign_id);
    if (found != cache.end())
        return found->second;

    // Mirror read_campaign_title's tolerance: only a missing file yields
    // nothing; a parse error after the pair was pre-harvested still counts.
    CampaignYaml metadata;
    std::string mode;
    if (read_campaign_yaml("campaign.yaml", metadata) !=
            CampaignYamlReadResult::OpenFailed &&
        metadata.saw_mode)
    {
        mode = metadata.mode;
    }
    cache.emplace(campaign_id, mode);
    return mode;
}

void forget_campaign_display_title(const std::string& campaign_id)
{
    campaign_title_cache().erase(campaign_id);
    campaign_mode_cache().erase(campaign_id);
}

void clear_campaign_metadata_cache()
{
    campaign_title_cache().clear();
    scenario_name_cache().clear();
    campaign_mode_cache().clear();
}

} // namespace og::data
