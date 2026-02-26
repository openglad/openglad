/* Shared platform I/O helpers — SDL-free.
 *
 * Campaign management, archive operations, directory listing, and filesystem
 * helpers that work in both SDL and headless builds.  Extracted from
 * src/platform/sdl/io/platform_io.cpp as part of the over-engineering
 * remediation (step 2).
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/resources/io_common.h>
#include <openglad/core/util.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/zip_api.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <string>

// Path helpers (defined per-platform in platform_io.cpp or platform_headless.cpp)
std::string get_user_path();
std::string get_asset_path();

namespace og::resources {
namespace {
// Sessionless fallback for resources-layer campaign mount state.
std::string s_mounted_campaign;

bool is_safe_campaign_id(const std::string& id)
{
    if (id.empty())
        return false;
    if (id.find("..") != std::string::npos)
        return false;

    for (const unsigned char ch : id)
    {
        if (ch == '/' || ch == '\\')
            return false;
        if (std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-')
            continue;
        return false;
    }

    return true;
}

class ScopeGuard
{
public:
    explicit ScopeGuard(std::function<void()> fn) : fn_(std::move(fn)) {}
    ~ScopeGuard()
    {
        if (active_)
            fn_();
    }

    void dismiss() { active_ = false; }

private:
    std::function<void()> fn_;
    bool active_ = true;
};
} // namespace

std::string get_mounted_campaign()
{
    return s_mounted_campaign;
}

bool set_mounted_campaign(const std::string& id)
{
    if (!is_safe_campaign_id(id))
        return false;
    s_mounted_campaign = id;
    return true;
}

void clear_mounted_campaign()
{
    s_mounted_campaign.clear();
}
} // namespace og::resources

// ---------------------------------------------------------------------------
// File listing
// ---------------------------------------------------------------------------

std::list<std::string> list_files(const std::string& dirname)
{
    return og::resources::enumerate_files_sorted(dirname);
}

// ---------------------------------------------------------------------------
// Campaign mount/unmount
// ---------------------------------------------------------------------------

std::string get_mounted_campaign()
{
    return og::resources::get_mounted_campaign();
}

namespace {
const char* campaign_io_error_string(CampaignPackageIoError err)
{
    switch (err) {
        case CampaignPackageIoError::None: return "none";
        case CampaignPackageIoError::EmptyId: return "empty_id";
        case CampaignPackageIoError::Busy: return "busy";
        case CampaignPackageIoError::MountFailed: return "mount_failed";
        case CampaignPackageIoError::UnmountFailed: return "unmount_failed";
    }
    return "unknown";
}
} // namespace

CampaignPackageIoError mount_campaign_package_with_error(const std::string& id)
{
    if(id.size() == 0)
        return CampaignPackageIoError::EmptyId;
    if (!og::resources::is_safe_campaign_id(id))
    {
        LogError("campaign_mount_failed id={} code={} reason=invalid_campaign_id\n",
            id, campaign_io_error_string(CampaignPackageIoError::MountFailed));
        return CampaignPackageIoError::MountFailed;
    }

    const std::string prev = get_mounted_campaign();
    if (!prev.empty() && prev == id)
        return CampaignPackageIoError::None;

    const std::string filename = get_user_path() + "campaigns/" + id + ".glad";
    if (!prev.empty() && prev != id)
    {
        CampaignPackageIoError unmount_error = unmount_campaign_package_with_error(prev);
        if (unmount_error != CampaignPackageIoError::None)
            return unmount_error;
    }

    Log("Mounting campaign package: {}", id);
    if(!og::resources::mount(filename, nullptr, 0))
    {
        LogError("campaign_mount_failed id={} path={} code={} physfs={}\n",
            id, filename, campaign_io_error_string(CampaignPackageIoError::MountFailed), og::resources::last_error());
        og::resources::clear_mounted_campaign();
        return CampaignPackageIoError::MountFailed;
    }
    if (!og::resources::set_mounted_campaign(id))
    {
        LogError("campaign_mount_failed id={} code={} reason=invalid_campaign_id_after_mount\n",
            id, campaign_io_error_string(CampaignPackageIoError::MountFailed));
        return CampaignPackageIoError::MountFailed;
    }
    return CampaignPackageIoError::None;
}

CampaignPackageIoError unmount_campaign_package_with_error(const std::string& id)
{
    if(id.size() == 0)
    {
        og::resources::clear_mounted_campaign();
        return CampaignPackageIoError::None;
    }
    if (!og::resources::is_safe_campaign_id(id))
    {
        LogError("campaign_unmount_failed id={} code={} reason=invalid_campaign_id\n",
            id, campaign_io_error_string(CampaignPackageIoError::UnmountFailed));
        return CampaignPackageIoError::UnmountFailed;
    }

    const std::string mounted = get_mounted_campaign();
    if (!mounted.empty() && mounted != id)
        return CampaignPackageIoError::None;

    std::string filename = get_user_path() + "campaigns/" + id + ".glad";
    if(!og::resources::unmount(filename))
    {
        if (og::resources::last_error_is_not_mounted())
        {
            if (mounted == id)
                og::resources::clear_mounted_campaign();
            return CampaignPackageIoError::None;
        }
        if (og::resources::last_error_is_files_still_open())
        {
            LogError("campaign_unmount_failed id={} path={} code={} physfs={}\n",
                id, filename, campaign_io_error_string(CampaignPackageIoError::Busy), og::resources::last_error());
            return CampaignPackageIoError::Busy;
        }

        LogError("campaign_unmount_failed id={} path={} code={} physfs={}\n",
            id, filename, campaign_io_error_string(CampaignPackageIoError::UnmountFailed), og::resources::last_error());
        return CampaignPackageIoError::UnmountFailed;
    }
    if (mounted == id)
        og::resources::clear_mounted_campaign();
    return CampaignPackageIoError::None;
}

CampaignPackageIoError remount_campaign_package_with_error()
{
    std::string id = get_mounted_campaign();
    if (id.empty())
        return CampaignPackageIoError::EmptyId;
    if (!og::resources::is_safe_campaign_id(id))
    {
        LogError("campaign_remount_failed id={} code={} reason=invalid_campaign_id\n",
            id, campaign_io_error_string(CampaignPackageIoError::MountFailed));
        return CampaignPackageIoError::MountFailed;
    }

    const std::string filename = get_user_path() + "campaigns/" + id + ".glad";
    if(!og::resources::unmount(filename))
    {
        if (og::resources::last_error_is_files_still_open())
            return CampaignPackageIoError::Busy;
        const std::string physfs_error = og::resources::last_error();
        LogError("campaign_unmount_failed id={} path={} code={} physfs={}\n",
            id, filename, campaign_io_error_string(CampaignPackageIoError::UnmountFailed), physfs_error);
        return CampaignPackageIoError::UnmountFailed;
    }
    og::resources::clear_mounted_campaign();
    return mount_campaign_package_with_error(id);
}

// ---------------------------------------------------------------------------
// Campaign/level listing
// ---------------------------------------------------------------------------

std::list<std::string> list_campaigns()
{
    std::list<std::string> ls = list_files("campaigns/");
    for (auto e = ls.begin(); e != ls.end(); )
    {
        size_t pos = e->rfind(".glad");
        if(pos == std::string::npos)
        {
            e = ls.erase(e);
            continue;
        }
        else
            *e = e->substr(0, pos);
        ++e;
    }
    return ls;
}

std::list<int> list_levels()
{
    std::list<std::string> ls = list_files("scen/");
    std::list<int> result;
    for(std::list<std::string>::iterator e = ls.begin(); e != ls.end(); )
    {
        size_t pos = e->rfind(".fss");
        if(pos == std::string::npos)
        {
            e = ls.erase(e);
            continue;
        }
        else
        {
            *e = e->substr(0, pos);
            if(e->substr(0, 4) != "scen")
            {
                e = ls.erase(e);
                continue;
            }
            *e = e->substr(4, std::string::npos);
            const auto id = parse_int_strict(*e);
            if (id && *id > 0)
                result.push_back(*id);
        }
        e++;
    }

    result.sort();
    return result;
}

std::vector<int> list_levels_v()
{
    std::list<std::string> ls = list_files("scen/");
    std::vector<int> result;
    for(std::list<std::string>::iterator e = ls.begin(); e != ls.end(); )
    {
        size_t pos = e->rfind(".fss");
        if(pos == std::string::npos)
        {
            e = ls.erase(e);
            continue;
        }
        else
        {
            *e = e->substr(0, pos);
            if(e->substr(0, 4) != "scen")
            {
                e = ls.erase(e);
                continue;
            }
            *e = e->substr(4, std::string::npos);
            const auto id = parse_int_strict(*e);
            if (id && *id > 0)
                result.push_back(*id);
        }
        e++;
    }

    std::sort(result.begin(), result.end());
    return result;
}

// ---------------------------------------------------------------------------
// String utility
// ---------------------------------------------------------------------------

std::list<std::string> explode(const std::string& str, char delimiter)
{
    std::list<std::string> result;

    size_t oldPos = 0;
    size_t pos = str.find_first_of(delimiter);
    while(pos != std::string::npos)
    {
        result.push_back(str.substr(oldPos, pos - oldPos));
        oldPos = pos+1;
        pos = str.find_first_of(delimiter, oldPos);
    }

    result.push_back(str.substr(oldPos, std::string::npos));

    return result;
}

// ---------------------------------------------------------------------------
// Directory / filesystem helpers
// ---------------------------------------------------------------------------

bool create_dir(const std::string& dirname)
{
    std::error_code ec;
    std::filesystem::create_directories(dirname, ec);
    return !ec;
}

// ---------------------------------------------------------------------------
// Archive helpers
// ---------------------------------------------------------------------------

ArchiveIoError zip_contents_with_error(const std::string& indirectory, const std::string& outfile)
{
    return og::io::zip_contents_with_error(indirectory, outfile);
}

ArchiveIoError unzip_into_with_error(const std::string& infile, const std::string& outdirectory)
{
    return og::io::unzip_into_with_error(infile, outdirectory);
}

// ---------------------------------------------------------------------------
// Campaign pack/unpack
// ---------------------------------------------------------------------------

bool unpack_campaign(const std::string& campaign_id)
{
    if (!og::resources::is_safe_campaign_id(campaign_id))
    {
        LogError("campaign_unpack_failed id={} reason=invalid_campaign_id\n", campaign_id);
        return false;
    }
    return unzip_into_with_error(get_user_path() + "campaigns/" + campaign_id + ".glad", get_user_path() + "temp/") == ArchiveIoError::None;
}

bool repack_campaign(const std::string& campaign_id)
{
    if (!og::resources::is_safe_campaign_id(campaign_id))
    {
        LogError("campaign_repack_failed id={} reason=invalid_campaign_id\n", campaign_id);
        return false;
    }
    std::string outfile = get_user_path() + "campaigns/" + campaign_id + ".glad";
    std::string tmp_outfile = outfile + ".tmp";
    std::error_code ec;
    og::resources::ScopeGuard tmp_file_cleanup([&tmp_outfile]() {
        std::error_code cleanup_ec;
        std::filesystem::remove(tmp_outfile, cleanup_ec);
    });
    std::filesystem::remove(tmp_outfile, ec);
    if (zip_contents_with_error(get_user_path() + "temp/", tmp_outfile) != ArchiveIoError::None)
        return false;

    std::filesystem::rename(tmp_outfile, outfile, ec);
    if (ec)
    {
        LogError("campaign_repack_failed id={} reason=rename_failed message={}\n", campaign_id, ec.message());
        return false;
    }
    return true;
}

void cleanup_unpacked_campaign()
{
    std::error_code ec;
    std::filesystem::remove_all(get_user_path() + "temp", ec);
}

// ---------------------------------------------------------------------------
// Level/campaign deletion
// ---------------------------------------------------------------------------

void delete_level(int id)
{
    std::string campaign = get_mounted_campaign();
    if(campaign.size() == 0)
        return;

    cleanup_unpacked_campaign();
    if(!unpack_campaign(campaign))
        return;
    // Delete data file
    std::string path = std::format("{}temp/scen/scen{}.fss", get_user_path(), id);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    // Delete terrain file
    path = std::format("{}temp/pix/scen{:04d}.pix", get_user_path(), id);
    std::filesystem::remove(path, ec);
    if (!repack_campaign(campaign))
    {
        LogWarn("delete_level_failed level={} campaign={} reason=repack_failed\n", id, campaign);
        return;
    }

    const CampaignPackageIoError remount_error = remount_campaign_package_with_error();
    if (remount_error != CampaignPackageIoError::None)
        LogWarn("delete_level_failed level={} campaign={} reason=remount_failed code={}\n",
            id, campaign, campaign_io_error_string(remount_error));
}

void delete_campaign(const std::string& id)
{
    if (!og::resources::is_safe_campaign_id(id))
    {
        LogError("campaign_delete_failed id={} reason=invalid_campaign_id\n", id);
        return;
    }
    if (get_mounted_campaign() == id)
    {
        const CampaignPackageIoError unmount_error = unmount_campaign_package_with_error(id);
        if (unmount_error != CampaignPackageIoError::None)
        {
            LogError("campaign_delete_failed id={} reason=unmount_failed code={}\n",
                id, campaign_io_error_string(unmount_error));
            return;
        }
    }

    std::string path = std::format("{}campaigns/{}.glad", get_user_path(), id);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// ---------------------------------------------------------------------------
// Default campaigns/settings restoration (SDL-free using std::filesystem)
// ---------------------------------------------------------------------------

void restore_default_campaigns()
{
    std::error_code ec;
    std::string src = get_asset_path() + "builtin/org.openglad.gladiator.glad";
    std::string dst = get_user_path() + "campaigns/org.openglad.gladiator.glad";
    std::filesystem::copy_file(src, dst,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
        LogWarn("restore_default_campaigns: {} -> {}: {}\n", src, dst, ec.message());
    else
        Log("Restored default campaign: {} -> {}\n", src, dst);
}

void restore_default_settings()
{
    std::error_code ec;
    std::string src = get_asset_path() + "cfg/openglad.yaml";
    std::string dst = get_user_path() + "cfg/openglad.yaml";
    std::filesystem::copy_file(src, dst,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
        LogWarn("restore_default_settings: {} -> {}: {}\n", src, dst, ec.message());
    else
        Log("Restored default settings: {} -> {}\n", src, dst);
}

// ---------------------------------------------------------------------------
// Campaign loading (unmount old, mount new)
// ---------------------------------------------------------------------------

CampaignLoadResult load_campaign_with_error(const std::string& campaign,
    std::map<std::string, int>& current_levels, int first_level)
{
    CampaignLoadResult result;
    result.current_level = first_level;
    if (!og::resources::is_safe_campaign_id(campaign))
    {
        result.error = CampaignLoadError::MountFailed;
        LogError("campaign_load_failed reason=invalid_campaign_id requested={}\n", campaign);
        return result;
    }
    std::string old_campaign = get_mounted_campaign();
    if(old_campaign != campaign)
    {
        const CampaignPackageIoError mount_err = mount_campaign_package_with_error(campaign);
        if (mount_err == CampaignPackageIoError::UnmountFailed)
        {
            LogError("campaign_load_failed reason=unmount_failed old={} requested={}\n", old_campaign, campaign);
            result.error = CampaignLoadError::UnmountFailed;
            return result;
        }
        if (mount_err != CampaignPackageIoError::None)
        {
            result.error = CampaignLoadError::MountFailed;
            return result;
        }
    }

    std::map<std::string, int>::const_iterator g = current_levels.find(campaign);
    if(g != current_levels.end())
        result.current_level = g->second;
    return result;
}

int load_campaign(const std::string& campaign,
    std::map<std::string, int>& current_levels, int first_level)
{
    const CampaignLoadResult result = load_campaign_with_error(campaign, current_levels, first_level);
    switch(result.error)
    {
        case CampaignLoadError::None:
            return result.current_level;
        case CampaignLoadError::MountFailed:
            return -2;
        case CampaignLoadError::UnmountFailed:
            return -3;
    }
    return -1;
}
