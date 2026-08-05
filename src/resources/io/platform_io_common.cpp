/* Shared platform I/O helpers — SDL-free.
 *
 * Campaign management, archive operations, directory listing, and filesystem
 * helpers that work in both SDL and headless builds.  Extracted from
 * src/sdl_client/io/platform_io.cpp as part of the over-engineering
 * remediation (step 2).
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/resources/packs.h>
#include <openglad/resources/io_common.h>
#include <openglad/core/campaign_ids.h>
#include <openglad/core/util.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/zip_api.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <system_error>

// Path helpers (defined per-platform in platform_io.cpp or platform_headless.cpp)
std::string get_user_path();
std::string get_asset_path();
namespace {
std::string& mounted_campaign_state();
}

// ---------------------------------------------------------------------------
// File listing
// ---------------------------------------------------------------------------

std::list<std::string> list_files(const std::string& dirname)
{
    return og::resources::list_files(dirname.c_str());
}

// ---------------------------------------------------------------------------
// User-directory file primitives (§3.6)
// ---------------------------------------------------------------------------

bool user_file_exists(const std::string& relative_path)
{
    std::error_code ec;
    return std::filesystem::exists(
        std::filesystem::path(get_user_path()) / relative_path, ec);
}

bool copy_user_file(const std::string& src_relative_path,
                    const std::string& dst_relative_path)
{
    namespace fs = std::filesystem;
    const fs::path src = fs::path(get_user_path()) / src_relative_path;
    const fs::path dst = fs::path(get_user_path()) / dst_relative_path;

    // Read-all first: the source stays untouched and a short read aborts
    // before anything is created at the destination.
    std::ifstream in(src, std::ios::binary);
    if (!in.good())
        return false;
    std::string bytes((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    if (in.bad())
        return false;
    in.close();

    std::error_code ec;
    if (dst.has_parent_path())
        fs::create_directories(dst.parent_path(), ec);

    const fs::path tmp = fs::path(dst.string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.good())
            return false;
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out.good())
        {
            out.close();
            std::error_code cleanup_ec;
            fs::remove(tmp, cleanup_ec);
            return false;
        }
    }

    fs::rename(tmp, dst, ec);
    if (ec)
    {
        LogError("copy_user_file_rename_failed src={} dst={} error={}\n",
                 src_relative_path, dst_relative_path, ec.message());
        std::error_code cleanup_ec;
        fs::remove(tmp, cleanup_ec);
        return false;
    }
    return true;
}

bool remove_user_file(const std::string& relative_path)
{
    std::error_code ec;
    const bool removed = std::filesystem::remove(
        std::filesystem::path(get_user_path()) / relative_path, ec);
    if (ec || !removed)
        return false;
    // Emscripten: persist the deletion to IndexedDB (no-op elsewhere).
    sync_filesystem();
    return true;
}

// ---------------------------------------------------------------------------
// Campaign mount/unmount
// ---------------------------------------------------------------------------

std::string get_mounted_campaign()
{
    return mounted_campaign_state();
}

#ifdef TESTING
void set_mounted_campaign_for_testing(const std::string& id)
{
    mounted_campaign_state() = id;
}
#endif

namespace {
std::string& mounted_campaign_state()
{
    static std::string mounted_campaign;
    return mounted_campaign;
}

const char* campaign_io_error_string(CampaignPackageIoError err)
{
    switch (err) {
        case CampaignPackageIoError::None: return "none";
        case CampaignPackageIoError::EmptyId: return "empty_id";
        case CampaignPackageIoError::MountFailed: return "mount_failed";
        case CampaignPackageIoError::UnmountFailed: return "unmount_failed";
    }
    return "unknown";
}
} // namespace

bool is_safe_campaign_id(std::string_view id)
{
    if (id.empty() || id.size() > 64 || id == "." || id == ".." ||
        id.find("..") != std::string_view::npos)
    {
        return false;
    }

    return std::all_of(id.begin(), id.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-';
    });
}

bool is_safe_virtual_basename(std::string_view name, std::size_t max_length)
{
    if (name.empty() || name.size() > max_length || name == "." ||
        name == ".." || name.find("..") != std::string_view::npos)
    {
        return false;
    }

    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-';
    });
}

// ---------------------------------------------------------------------------
// Sprite-sheet (extra_pix) mount
// ---------------------------------------------------------------------------
// Lives beside the campaign mount code (moved here from platform_io.cpp,
// issue #162) because the two mounts share one PhysFS search path and the
// campaign mount below has to re-assert the sheet's front position.

static std::string s_mounted_sprite_sheet_dir;

// A sprite-sheet pack is always a single directory living directly under
// extra_pix/. Reject any name containing a path separator or a "."/".."
// component so a hand-edited config can't escape extra_pix/ and mount an
// arbitrary host directory over pix/.
static bool is_safe_sprite_sheet_name(const std::string& name)
{
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return false;
    return name != "." && name != "..";
}

bool apply_sprite_sheet_setting()
{
    std::string name = cfg.get_setting("graphics", "sprite_sheet");
    if (!name.empty() && !is_safe_sprite_sheet_name(name)) {
        LogWarn("Sprite sheet '{}': invalid pack name, ignoring\n", name);
        name.clear();
    }
    const std::string new_dir = name.empty() ? "" : (get_user_path() + "extra_pix/" + name);

    if (s_mounted_sprite_sheet_dir == new_dir)
        return true;

    if (!s_mounted_sprite_sheet_dir.empty()) {
        const std::string old_dir = s_mounted_sprite_sheet_dir;
        if (!og::resources::unmount(old_dir.c_str())) {
            LogWarn("Sprite sheet '{}': failed to unmount, keeping previous mount state\n", old_dir);
            return false;
        }
        s_mounted_sprite_sheet_dir.clear();
        og::resources::note_sprite_source_changed();
    }

    if (!new_dir.empty()) {
        if (!og::resources::mount(new_dir.c_str(), "pix/", 0)) {
            LogWarn("Sprite sheet '{}': failed to mount\n", new_dir);
            return false;
        }
        Log("Sprite sheet mounted: {}\n", new_dir);
        s_mounted_sprite_sheet_dir = new_dir;
        og::resources::note_sprite_source_changed();
    }

    return true;
}

// Re-prepends the mounted sheet so it answers pix/ lookups ahead of any
// campaign package mounted after it. note_change=false is the editor-save
// remount: the net source set is unchanged, so a successful re-prepend must
// not mark loaders stale.
static bool reassert_sprite_sheet_mount_impl(bool note_change)
{
    const std::string dir = s_mounted_sprite_sheet_dir;
    if (dir.empty())
        return true; // nothing mounted (headless boots never mount a sheet)

    if (!og::resources::unmount(dir.c_str())) {
        LogWarn("Sprite sheet '{}': failed to unmount for re-assert, keeping current search order\n", dir);
        return false;
    }
    if (!og::resources::mount(dir.c_str(), "pix/", 0)) {
        LogWarn("Sprite sheet '{}': failed to re-mount, sheet is now unmounted\n", dir);
        s_mounted_sprite_sheet_dir.clear();
        // The sheet really left the search path: always a source change.
        og::resources::note_sprite_source_changed();
        return false;
    }
    if (note_change)
        og::resources::note_sprite_source_changed();
    return true;
}

bool reassert_sprite_sheet_mount()
{
    return reassert_sprite_sheet_mount_impl(true);
}

static CampaignPackageIoError mount_campaign_package_impl(const std::string& id,
                                                          bool note_sprite_change)
{
    if(id.size() == 0)
        return CampaignPackageIoError::EmptyId;
    if (!is_safe_campaign_id(id))
    {
        LogError("campaign_mount_failed id={} code=unsafe_id\n", id);
        return CampaignPackageIoError::MountFailed;
    }

    const std::string prev = get_mounted_campaign();
    if (!prev.empty() && prev == id)
        return CampaignPackageIoError::None;
    if (!prev.empty() && prev != id)
    {
        CampaignPackageIoError unmount_error = unmount_campaign_package_with_error(prev);
        if (unmount_error != CampaignPackageIoError::None)
            return unmount_error;
    }

    Log("Mounting campaign package: {}", id);

    std::string filename = get_user_path() + "campaigns/" + id + ".glad";
    if(!og::resources::mount(filename.c_str(), nullptr, 0))
    {
        LogError("campaign_mount_failed id={} path={} code={} physfs={}\n",
            id, filename, campaign_io_error_string(CampaignPackageIoError::MountFailed), og::resources::filesystem_last_error());
        mounted_campaign_state().clear();
        return CampaignPackageIoError::MountFailed;
    }
    mounted_campaign_state() = id;
    // A lookup that ran before this package existed memoized the raw-id
    // fallback; the successful mount proves the package is readable now.
    og::data::forget_campaign_display_title(id);
    // Campaign zips may embed class packs (packs/<id>/ merged into the
    // virtual tree by this mount): rescan the pack-script set.
    og::resources::refresh_pack_scripts();
    // #162: campaign packages carry pix/ entity art and pack sprites, so a
    // mount changes what sprite bytes resolve — except the editor-save
    // remount (note_sprite_change=false), which restores the identical set.
    if (note_sprite_change)
        og::resources::note_sprite_source_changed();
    // Precedence: an explicitly selected user sprite sheet outranks campaign
    // pix/ art for the names the sheet ships. The campaign PREPENDED itself
    // in front of the sheet just now; put the sheet back in front.
    (void)reassert_sprite_sheet_mount_impl(note_sprite_change);
    return CampaignPackageIoError::None;
}

CampaignPackageIoError mount_campaign_package_with_error(const std::string& id)
{
    return mount_campaign_package_impl(id, /*note_sprite_change=*/true);
}

CampaignPackageIoError unmount_campaign_package_with_error(const std::string& id)
{
    if(id.size() == 0)
        return CampaignPackageIoError::None;
    if (!is_safe_campaign_id(id))
    {
        LogError("campaign_unmount_failed id={} code=unsafe_id\n", id);
        return CampaignPackageIoError::UnmountFailed;
    }

    std::string filename = get_user_path() + "campaigns/" + id + ".glad";
    if(!og::resources::unmount(filename.c_str()))
    {
        LogError("campaign_unmount_failed id={} path={} code={} physfs={}\n",
            id, filename, campaign_io_error_string(CampaignPackageIoError::UnmountFailed), og::resources::filesystem_last_error());
        return CampaignPackageIoError::UnmountFailed;
    }
    mounted_campaign_state().clear();
    // Drop any campaign-embedded pack scripts that just left the tree.
    og::resources::refresh_pack_scripts();
    // #162: the package's pix/ art and pack sprites just left the search
    // path; loaders built while it was mounted are stale.
    og::resources::note_sprite_source_changed();
    return CampaignPackageIoError::None;
}

CampaignPackageIoError remount_campaign_package_with_error()
{
    std::string id = get_mounted_campaign();
    if (id.empty())
        return CampaignPackageIoError::EmptyId;

    const std::string filename = get_user_path() + "campaigns/" + id + ".glad";
    if(!og::resources::unmount(filename.c_str()))
    {
        const std::string physfs_error = og::resources::filesystem_last_error();
        // In long-running flows (notably editor/test paths), remount can be
        // requested while transient campaign files are still open. Keep the
        // current mount instead of forcing repeated unmount failures.
        if (physfs_error.find("files still open") != std::string::npos)
            return CampaignPackageIoError::None;
        LogError("campaign_unmount_failed id={} path={} code={} physfs={}\n",
            id, filename, campaign_io_error_string(CampaignPackageIoError::UnmountFailed), physfs_error);
        return CampaignPackageIoError::UnmountFailed;
    }
    mounted_campaign_state().clear();
    // Same id out, same id in: an editor save re-creates the identical
    // sprite-source set, so this whole cycle must not bump the sprite
    // generation (D13 — editing pack sprites mid-session needs editor
    // re-entry; a bump here would mark live loaders stale on every save).
    return mount_campaign_package_impl(id, /*note_sprite_change=*/false);
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
            // Not a campaign package
            e = ls.erase(e);
            continue;
        }
        else
            *e = e->substr(0, pos);
        if (!is_safe_campaign_id(*e))
        {
            e = ls.erase(e);
            continue;
        }
        ++e;
    }
    return ls;
}

std::list<int> list_levels()
{
    std::list<std::string> ls = list_files("scen/");
    std::list<int> result;
    for(auto e = ls.begin(); e != ls.end(); )
    {
        size_t pos = e->rfind(".fss");
        if(pos == std::string::npos)
        {
            // Not a scen file
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
    for(auto e = ls.begin(); e != ls.end(); )
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
    if (!is_safe_campaign_id(campaign_id))
        return false;
    return unzip_into_with_error(get_user_path() + "campaigns/" + campaign_id + ".glad", get_user_path() + "temp/") == ArchiveIoError::None;
}

bool repack_campaign(const std::string& campaign_id)
{
    if (!is_safe_campaign_id(campaign_id))
        return false;
    std::string outfile = get_user_path() + "campaigns/" + campaign_id + ".glad";
    std::remove(outfile.c_str());
    og::data::clear_campaign_metadata_cache();
    return zip_contents_with_error(get_user_path() + "temp/", outfile) == ArchiveIoError::None;
}

void cleanup_unpacked_campaign()
{
    std::error_code ec;
    std::filesystem::remove_all(get_user_path() + "temp", ec);
}

// ---------------------------------------------------------------------------
// Level/campaign deletion
// ---------------------------------------------------------------------------

// Delete this level from the mounted campaign
void delete_level(int id)
{
    std::string campaign = get_mounted_campaign();
    if(campaign.size() == 0)
        return;

    cleanup_unpacked_campaign();
    unpack_campaign(campaign);
    // Delete data file
    std::string path = std::format("{}temp/scen/scen{}.fss", get_user_path(), id);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    // Delete terrain file
    path = std::format("{}temp/pix/scen{:04d}.png", get_user_path(), id);
    std::filesystem::remove(path, ec);
    repack_campaign(campaign);

    (void)remount_campaign_package_with_error();
}

void delete_campaign(const std::string& id)
{
    if (!is_safe_campaign_id(id))
        return;
    std::string path = std::format("{}campaigns/{}.glad", get_user_path(), id);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    og::data::clear_campaign_metadata_cache();
}

// ---------------------------------------------------------------------------
// Default campaigns/settings restoration (SDL-free using std::filesystem)
// ---------------------------------------------------------------------------

void restore_default_campaigns()
{
    namespace fs = std::filesystem;
    const std::string src_dir = get_asset_path() + "builtin";
    std::error_code iter_ec;
    for (const auto& entry : fs::directory_iterator(src_dir, iter_ec))
    {
        if (entry.path().extension() != ".glad")
            continue;
        const std::string src = entry.path().string();
        const std::string dst =
            get_user_path() + "campaigns/" + entry.path().filename().string();
        std::error_code ec;
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            LogWarn("restore_default_campaigns: {} -> {}: {}\n", src, dst, ec.message());
            continue;
        }
        Log("Restored default campaign: {} -> {}\n", src, dst);
        // Installs that predate the reverse-DNS purge left this campaign
        // as "org.openglad.<filename>". Stock copies were overwritten from
        // builtin/ on every restore, so the legacy twin holds no user
        // edits; drop it rather than list the campaign twice.
        const std::string legacy_twin = get_user_path() + "campaigns/" +
            std::string(og::kLegacyIdPrefix) + entry.path().filename().string();
        std::error_code twin_ec;
        if (fs::remove(legacy_twin, twin_ec))
            Log("Removed legacy campaign copy: {}\n", legacy_twin);
    }
    if (iter_ec)
        LogWarn("restore_default_campaigns: {}: {}\n", src_dir, iter_ec.message());

    og::data::clear_campaign_metadata_cache();
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
    std::string old_campaign = get_mounted_campaign();
    if(old_campaign != campaign)
    {
        if(unmount_campaign_package_with_error(old_campaign) != CampaignPackageIoError::None)
        {
            LogError("campaign_load_failed reason=unmount_failed old={} requested={}\n", old_campaign, campaign);
            result.error = CampaignLoadError::UnmountFailed;
            return result;
        }

        if(mount_campaign_package_with_error(campaign) != CampaignPackageIoError::None)
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
