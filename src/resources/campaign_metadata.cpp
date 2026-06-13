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

#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/ogfile_yaml.h>
#include <openglad/resources/yaml_stream.h>

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

// Harvest the "title" scalar from a campaign.yaml visible at yaml_path.
bool read_campaign_title(const char* yaml_path, std::string& out_title)
{
    auto infile = og::io::og_open_read(yaml_path);
    if (!infile)
        return false;

    og::io::YamlParser yaml;
    yaml.set_input(ogfile_read_handler, infile.get());

    bool found = false;
    while (yaml.parse_next() == og::io::YamlParseResult::Ok)
    {
        const og::io::YamlEvent& ev = yaml.event();
        if (ev.type == og::io::YamlEventType::Pair && ev.scalar == "title")
        {
            out_title = ev.value;
            found = true;
        }
    }
    yaml.close_input();
    return found;
}

std::string lookup_campaign_title(const std::string& campaign_id)
{
    std::string title;

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

void forget_campaign_display_title(const std::string& campaign_id)
{
    campaign_title_cache().erase(campaign_id);
}

void clear_campaign_metadata_cache()
{
    campaign_title_cache().clear();
    scenario_name_cache().clear();
}

} // namespace og::data
