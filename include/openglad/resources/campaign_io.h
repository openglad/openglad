/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <map>
#include <string>

// Campaign loading (unmount old, mount new, return current level).
// Shared by SDL and headless builds; uses mount/unmount helpers.
enum class CampaignLoadError
{
    None = 0,
    UnmountFailed,
    MountFailed
};

struct CampaignLoadResult
{
    CampaignLoadError error = CampaignLoadError::None;
    int current_level = 1;
};

CampaignLoadResult load_campaign_with_error(const std::string& campaign,
    std::map<std::string, int>& current_levels, int first_level = 1);
int load_campaign(const std::string& campaign,
    std::map<std::string, int>& current_levels, int first_level = 1);
