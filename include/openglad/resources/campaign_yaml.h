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

struct CampaignYaml {
    std::string title;
    std::string version;
    std::string authors;
    std::string contributors;
    std::string description;
    // Game-mode identity (og::mode seam): absent/"classic" -> Classic,
    // "tower" -> Tower. The writer emits the key ONLY when non-empty so
    // every existing (mode-less) campaign repack stays byte-stable.
    std::string mode;
    int suggested_power = 0;
    int first_level = 1;
    bool saw_title = false;
    bool saw_version = false;
    bool saw_authors = false;
    bool saw_contributors = false;
    bool saw_description = false;
    bool saw_mode = false;
    bool saw_suggested_power = false;
    bool saw_first_level = false;
};

enum class CampaignYamlReadResult {
    Ok = 0,
    OpenFailed,
    ParseFailed,
};

enum class CampaignYamlWriteResult {
    Ok = 0,
    OpenFailed,
    WriteFailed,
};

CampaignYamlReadResult read_campaign_yaml(const char* path, CampaignYaml& out, bool debug = false);
CampaignYamlWriteResult write_campaign_yaml_with_result(const char* path, const CampaignYaml& data);
CampaignYamlWriteResult write_default_campaign_yaml_with_result(const char* path);
bool write_campaign_yaml(const char* path, const CampaignYaml& data);
bool write_default_campaign_yaml(const char* path);

} // namespace og::data
