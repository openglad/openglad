/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <openglad/platform/io.h>
#include <openglad/runtime/game_context.h>
#include <openglad/core/util.h>
#include <openglad/io/physfs_api.h>
#include <openglad/io/zip_api.h>
#include <openglad/io/yaml_stream.h>
#include <openglad/legacy/pixdefs.h>
#include <format>
#include <filesystem>
#include <array>
#include <memory>
#include <string>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace
{
void remove_file_or_log(const std::string& path)
{
    std::error_code ec;
    const bool removed = std::filesystem::remove(path, ec);
    if (ec)
    {
        LogError("Failed to delete file '{}': {}\n", path, ec.message());
        return;
    }
    if (!removed)
    {
        // Not an error; file didn't exist.
        Log("File not found (skip delete): {}\n", path);
    }
}
} // namespace

#ifdef WIN32
#include "windows.h"
#include <shlobj.h>
#include <ctime>
#include <direct.h>

#ifndef mkdir
#define mkdir(path, perms) _mkdir(path)
#endif

#endif


/*
File I/O strategy:
PhysicsFS is set up to look in the scen, pix, and sound directories and in the current scenario package (campaign).
SDL_RWops is used to access the data in the files retrieved from PhysFS.

Scenario packages are stored in the user directory so more can be installed, etc.
The default pix and sound assets are installed with the rest of the program, presumably without write access.

*/

int rwops_read_handler(void *data, unsigned char *buffer, size_t size, size_t *size_read)
{
    SDL_RWops *rwops = static_cast<SDL_RWops*>(data);

    *size_read = SDL_RWread(rwops, buffer, 1, size);
    return 1;
}


int rwops_write_handler(void *data, unsigned char *buffer, size_t size)
{
    SDL_RWops *rwops = static_cast<SDL_RWops*>(data);

    SDL_RWwrite(rwops, buffer, 1, size);
    return 1;
}

std::string get_user_path()
{
#ifdef __EMSCRIPTEN__
    // Use IDBFS mount point for persistent storage in browser
    return "/persist/";
#elif defined(ANDROID)
    std::string path = SDL_AndroidGetInternalStoragePath();
    return path + "/";
#elif defined(__IPHONEOS__)
    return "../";
#elif defined(WIN32)
    char path[MAX_PATH];
    HRESULT hr = SHGetFolderPath(
                     0,                   // hwndOwner
                     CSIDL_LOCAL_APPDATA, // nFolder
                     0,                   // hToken
                     0, //SHGFP_TYPE_CURRENT,  // dwFlags
                     path);               // pszPath
    if(SUCCEEDED(hr))
    {
        std::string s = path;
        // Replace all backslashes
        size_t pos = 0;
        do
        {
            pos = s.find_first_of('\\', pos);
            if(pos != std::string::npos)
                s[pos] = '/';
        } while(pos != std::string::npos);

        return s + "/.openglad/";
    }
    return "";
#else
    std::string path = getenv("HOME");
    path += "/.openglad/";
    return path;
#endif
}

std::string get_asset_path()
{
#ifdef ANDROID
    // RWops will look in the app's assets directory for this path
    return "";
#elif defined(__IPHONEOS__)
    // Assuming the cwd is set to the program's installation directory
    return "";
#elif defined(WIN32)
    // Assuming the cwd is set to the program's installation directory
    return "";
#else
    // Assumes UNIX with /proc
    constexpr size_t maxPathSize = 512;
    char path[maxPathSize];
    std::fill_n(path, maxPathSize, '\0');

    const ssize_t read_len = readlink("/proc/self/exe", path, maxPathSize - 1);
    if (read_len < 0)
    {
        LogError("get_asset_path: readlink(/proc/self/exe) failed\n");
        return "./";
    }
    path[static_cast<size_t>(read_len)] = '\0';

    std::string s = path;
    size_t slash = s.find_last_of('/');
    if(slash != std::string::npos)
    {
        s = s.substr(0, slash);
    }
    s += '/';

    Log("get_asset_path: {}\n", s);
    return s;
#endif
}

	SDL_RWops* open_read_file(const char* file, bool debug)
	{
	    SDL_RWops* rwops = nullptr;
	    
	    if(debug)
		    Log("Trying via PHYSFS: {}", file);
	    rwops = og::io::physfsrw_open_read(file);
	    if(rwops != nullptr) return rwops;

    // now try opening in the current directory
    if(debug)
	    Log("Trying to open: {}", file);
    rwops = SDL_RWFromFile(file, "rb");
    if(rwops != nullptr) return rwops;

    // now try opening in the user directory
    if(debug)
	    Log("Trying to open: {}{}", get_user_path(), file);
    rwops = SDL_RWFromFile((get_user_path() + std::string("/") + file).c_str(), "rb");
    if(rwops != nullptr) return rwops;

    // now try opening in the asset directory
    if(debug)
	    Log("Trying to open: {}{}", get_asset_path(), file);
    rwops = SDL_RWFromFile((get_asset_path() + std::string("/") + file).c_str(), "rb");
    if(rwops != nullptr) return rwops;

    // File not found - this may be expected (e.g., keyprefs.dat on first run)
    if(debug)
        Log("File not found: {} (may be created on first use)\n", file);
    return nullptr;
}

SDL_RWops* open_read_file(const char* path, const char* file)
{
    return open_read_file((std::string(path) + file).c_str());
}

	SDL_RWops* open_write_file(const char* file)
	{
	    SDL_RWops* rwops = og::io::physfsrw_open_write(file);
	    if(rwops != nullptr)
	        return rwops;
	    return SDL_RWFromFile(file, "wb");
	}

SDL_RWops* open_write_file(const char* path, const char* file)
{
    return open_write_file((std::string(path) + file).c_str());
}



std::list<std::string> list_files(const std::string& dirname)
{
    return og::io::physfs_enumerate_files_sorted(dirname);
}

std::string get_mounted_campaign()
{
    return ctx().mounted_campaign;
}

namespace {
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

CampaignPackageIoError mount_campaign_package_with_error(const std::string& id)
{
    if(id.size() == 0)
        return CampaignPackageIoError::EmptyId;

    // Campaigns are mounted at the root. If multiple campaigns are mounted at
    // once they can mask each other's asset directories (e.g. "scen/"), which
    // makes picker flows order-dependent and can hang tests waiting on missing
    // entries. Enforce one mounted campaign at a time.
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
    if(!og::io::physfs_mount(filename, nullptr, 0))
    {
        LogError("campaign_mount_failed id={} path={} code={} physfs={}\n",
            id, filename, campaign_io_error_string(CampaignPackageIoError::MountFailed), og::io::physfs_last_error());
        ctx().mounted_campaign.clear();
        return CampaignPackageIoError::MountFailed;
    }
    ctx().mounted_campaign = id;
    return CampaignPackageIoError::None;
}

CampaignPackageIoError unmount_campaign_package_with_error(const std::string& id)
{
    if(id.size() == 0)
        return CampaignPackageIoError::None;
    
    std::string filename = get_user_path() + "campaigns/" + id + ".glad";
    if(!og::io::physfs_unmount(filename))
    {
        LogError("campaign_unmount_failed id={} path={} code={} physfs={}\n",
            id, filename, campaign_io_error_string(CampaignPackageIoError::UnmountFailed), og::io::physfs_last_error());
        return CampaignPackageIoError::UnmountFailed;
    }
    ctx().mounted_campaign.clear();
    return CampaignPackageIoError::None;
}

CampaignPackageIoError remount_campaign_package_with_error()
{
    std::string id = get_mounted_campaign();
    CampaignPackageIoError unmount_error = unmount_campaign_package_with_error(id);
    if(unmount_error != CampaignPackageIoError::None)
        return unmount_error;
    return mount_campaign_package_with_error(id);
}

bool mount_campaign_package(const std::string& id)
{
    return mount_campaign_package_with_error(id) == CampaignPackageIoError::None;
}

bool unmount_campaign_package(const std::string& id)
{
    return unmount_campaign_package_with_error(id) == CampaignPackageIoError::None;
}

bool remount_campaign_package()
{
    return remount_campaign_package_with_error() == CampaignPackageIoError::None;
}

std::list<std::string> list_campaigns()
{
    std::list<std::string> ls = list_files("campaigns/");
    for (auto e = ls.begin(); e != ls.end(); )
    {
        size_t pos = e->rfind(".glad");
        if(pos == std::string::npos)
        {
            e = ls.erase(e);  // Not a campaign package
            continue;
        }
        else
            *e = e->substr(0, pos);  // Remove the extension
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
            e = ls.erase(e);  // Not a scen file
            continue;
        }
        else
        {
            *e = e->substr(0, pos);  // Remove the extension
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
            e = ls.erase(e);  // Not a scen file
            continue;
        }
        else
        {
            *e = e->substr(0, pos);  // Remove the extension
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
    remove_file_or_log(path);
    // Delete terrain file
    path = std::format("{}temp/pix/scen{:04d}.pix", get_user_path(), id);
    remove_file_or_log(path);
    repack_campaign(campaign);
    
    // Remount for consistency in PhysFS
    remount_campaign_package();
}

void delete_campaign(const std::string& id)
{
    std::string path = std::format("{}campaigns/{}.glad", get_user_path(), id);
    remove_file_or_log(path);
}


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

void copy_file(const std::string& filename, const std::string& dest_filename)
{
    Log("Copying file: {}\n", filename);
    SDL_RWops* in = SDL_RWFromFile(filename.c_str(), "rb");
    if(in == nullptr)
    {
        LogError("Could not open file to copy: {}\n", filename);
        return;
    }
    
    long size = 100;
    // Grab the data
    auto data = std::make_unique<unsigned char[]>(size);
    
    // Save it to another file
    Log("Copying to: {}\n", dest_filename);
    SDL_RWops* out = SDL_RWFromFile(dest_filename.c_str(), "wb");
    if(out == nullptr)
    {
        LogError("Could not open destination file: {}\n", dest_filename);
        SDL_RWclose(in);
        return;
    }
    
    long total = 0;
    long len = 0;
    while((len = SDL_RWread(in, data.get(), 1, size)) > 0)
    {
        SDL_RWwrite(out, data.get(), 1, len);
        total += len;
    }

    SDL_RWclose(in);
    SDL_RWclose(out);
    
    Log("Copied {} bytes.\n", total);
}

void create_dataopenglad()
{
    std::string user_path = get_user_path();
    mkdir(user_path.c_str(), 0770);
    mkdir((user_path + "campaigns/").c_str(), 0770);
    mkdir((user_path + "save/").c_str(), 0770);
    mkdir((user_path + "cfg/").c_str(), 0770);
}

void restore_default_campaigns()
{
    #ifndef FORCE_RESTORE_DEFAULT_CAMPAIGNS
    if(!og::io::physfs_exists("campaigns/org.openglad.gladiator.glad"))
    #endif
        copy_file(get_asset_path() + "builtin/org.openglad.gladiator.glad", get_user_path() + "campaigns/org.openglad.gladiator.glad");
}

void restore_default_settings()
{
    copy_file(get_asset_path() + "cfg/openglad.yaml", get_user_path() + "cfg/openglad.yaml");
}

#ifdef __EMSCRIPTEN__
// Flag to track when IDBFS sync is complete
static std::atomic<bool> idbfs_sync_done{false};

// Called from JavaScript when IDBFS sync completes
extern "C" void EMSCRIPTEN_KEEPALIVE on_idbfs_sync_done()
{
    idbfs_sync_done.store(true, std::memory_order_release);
    Log("IDBFS sync complete\n");
}
#endif

void io_init(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

#ifdef __EMSCRIPTEN__
    // Mount IDBFS for persistent storage in browser
    Log("Setting up IDBFS for persistent storage...\n");
    idbfs_sync_done.store(false, std::memory_order_release);

    EM_ASM({
        // Create mount point
        try {
            FS.mkdir('/persist');
        } catch(e) {
            // Directory may already exist
        }

        // Mount IDBFS
        FS.mount(IDBFS, {}, '/persist');

        // Sync FROM IndexedDB to populate the virtual filesystem
        FS.syncfs(true, function(err) {
            if (err) {
                console.error('IDBFS load error:', err);
            } else {
                console.log('IDBFS loaded from IndexedDB');
            }
            // Signal that sync is done
            Module._on_idbfs_sync_done();
        });
    });

    // Wait for sync to complete (ASYNCIFY allows this)
    Log("Waiting for IDBFS sync...\n");
    while (!idbfs_sync_done.load(std::memory_order_acquire)) {
        emscripten_sleep(10);
    }
    Log("IDBFS ready\n");
#endif

    // Make sure our directory tree exists and is set up
    create_dataopenglad();

    og::io::physfs_init(argv[0]);
    og::io::physfs_set_write_dir(get_user_path());

    if(!og::io::physfs_mount(get_user_path(), nullptr, 1))
    {
        std::string msg = std::format("Fatal: Failed to mount user data path: {}", get_user_path());
        LogError("{}\n", msg);
        throw std::runtime_error(msg);
    }

    restore_default_campaigns();
    
    // NOTES!
    // PhysFS cannot grab files from the assets folder because they're actually inside the apk.
    // SDL_RWops does some magic to figure out a file descriptor from JNI.
    // This means that I cannot use PhysFS to get any assets at all.
    // So for simple assets, I need to check PhysFS first, then fall back to SDL_RWops from the assets folder.
    // For campaign packages, I can copy them to the internal storage and they'll live happily there, accessed by PhysFS.
    // SDL_RWops size checking on Android doesn't seem to work!
    
    // Open up the default campaign
    Log("Mounting default campaign...\n");
    if (!mount_campaign_package("org.openglad.gladiator"))
    {
        std::string msg = std::format("Fatal: Failed to mount default campaign: {}", og::io::physfs_last_error());
        LogError("{}\n", msg);
        throw std::runtime_error(msg);
    }
    Log("Mounted default campaign\n");
    
    // Set up paths for default assets
    if(!og::io::physfs_mount(get_asset_path() + "pix/", "pix/", 1))
    {
        LogWarn("Failed to mount default pix path (may be bundled in campaign)\n");
    }
    if(!og::io::physfs_mount(get_asset_path() + "sound/", "sound/", 1))
    {
        LogWarn("Failed to mount default sound path (may be bundled in campaign)\n");
    }
    if(!og::io::physfs_mount(get_asset_path() + "cfg/", "cfg/", 1))
    {
        LogWarn("Failed to mount default cfg path (may be bundled in campaign)\n");
    }
}

void io_exit()
{
#ifdef __EMSCRIPTEN__
    // Final sync before exit
    sync_filesystem();
#endif
    og::io::physfs_deinit();
}

void sync_filesystem()
{
#ifdef __EMSCRIPTEN__
    // Sync virtual filesystem TO IndexedDB for persistence
    EM_ASM({
        FS.syncfs(false, function(err) {
            if (err) {
                console.error('IDBFS save error:', err);
            } else {
                console.log('IDBFS saved to IndexedDB');
            }
        });
    });
#endif
    // No-op on non-web platforms (they use real filesystem)
}





// Zip utils

// Need to implement for real
// PhysFS would work, but the paths would have to be in the search path
//   and the RWops would have to be gotten from PhysFS and I would have to rewire the zip input (could the archive be opened through PhysFS too?)
// Doing it with goodio would be nice.
ArchiveIoError zip_contents_with_error(const std::string& indirectory, const std::string& outfile)
{
    return og::io::zip_contents_with_error(indirectory, outfile);
}
bool create_dir(const std::string& dirname)
{
    std::error_code ec;
    std::filesystem::create_directories(dirname, ec);
    return !ec;
}

ArchiveIoError unzip_into_with_error(const std::string& infile, const std::string& outdirectory)
{
    return og::io::unzip_into_with_error(infile, outdirectory);
}

bool zip_contents(const std::string& indirectory, const std::string& outfile)
{
    return zip_contents_with_error(indirectory, outfile) == ArchiveIoError::None;
}

bool unzip_into(const std::string& infile, const std::string& outdirectory)
{
    return unzip_into_with_error(infile, outdirectory) == ArchiveIoError::None;
}

namespace {
bool write_rwops_exact(SDL_RWops* rwops, const void* data, size_t size, size_t count)
{
    return rwops && SDL_RWwrite(rwops, data, size, count) == count;
}
} // namespace

NewFileIoError create_new_map_pix_with_error(const std::string& filename, int w, int h)
{
	// File data in form:
	// <# of frames>      1 byte
	// <x size>                   1 byte
	// <y size>                   1 byte
	// <pixie data>               <x*y*frames> bytes
    if (w <= 0 || h <= 0 || w > 255 || h > 255)
    {
        return NewFileIoError::InvalidDimensions;
    }
	
	unsigned char c;
	SDL_RWops* outfile = open_write_file(filename.c_str());
	if(outfile == nullptr)
        return NewFileIoError::OpenWriteFailed;
    
    c = 1;  // Frames
	if(!write_rwops_exact(outfile, &c, 1, 1))
    {
        SDL_RWclose(outfile);
        return NewFileIoError::WriteFailed;
    }
    c = static_cast<unsigned char>(w);  // x size
	if(!write_rwops_exact(outfile, &c, 1, 1))
    {
        SDL_RWclose(outfile);
        return NewFileIoError::WriteFailed;
    }
    c = static_cast<unsigned char>(h);  // y size
	if(!write_rwops_exact(outfile, &c, 1, 1))
    {
        SDL_RWclose(outfile);
        return NewFileIoError::WriteFailed;
    }
	
	int size = w*h;
    static thread_local std::mt19937 grass_rng{std::random_device{}()};
    std::uniform_int_distribution<int> grass_dist(0, 3);
	for(int i = 0; i < size; i++)
    {
        // Color
        switch(grass_dist(grass_rng))
        {
            case 0:
            c = PIX_GRASS1;
            break;
            case 1:
            c = PIX_GRASS2;
            break;
            case 2:
            c = PIX_GRASS3;
            break;
            case 3:
            c = PIX_GRASS4;
            break;
        }
        if(!write_rwops_exact(outfile, &c, 1, 1))
        {
            SDL_RWclose(outfile);
            return NewFileIoError::WriteFailed;
        }
    }
    
    SDL_RWclose(outfile);
    return NewFileIoError::None;
}

NewFileIoError create_new_pix_with_error(const std::string& filename, int w, int h, unsigned char fill_color)
{
	// File data in form:
	// <# of frames>      1 byte
	// <x size>                   1 byte
	// <y size>                   1 byte
	// <pixie data>               <x*y*frames> bytes
    if (w <= 0 || h <= 0 || w > 255 || h > 255)
    {
        return NewFileIoError::InvalidDimensions;
    }
	
	unsigned char c;
	SDL_RWops* outfile = open_write_file(filename.c_str());
	if(outfile == nullptr)
        return NewFileIoError::OpenWriteFailed;
    
    c = 1;  // Frames
	if(!write_rwops_exact(outfile, &c, 1, 1))
    {
        SDL_RWclose(outfile);
        return NewFileIoError::WriteFailed;
    }
    c = static_cast<unsigned char>(w);  // x size
	if(!write_rwops_exact(outfile, &c, 1, 1))
    {
        SDL_RWclose(outfile);
        return NewFileIoError::WriteFailed;
    }
    c = static_cast<unsigned char>(h);  // y size
	if(!write_rwops_exact(outfile, &c, 1, 1))
    {
        SDL_RWclose(outfile);
        return NewFileIoError::WriteFailed;
    }
	
	c = fill_color;  // Color
	int size = w*h;
	for(int i = 0; i < size; i++)
    {
        if(!write_rwops_exact(outfile, &c, 1, 1))
        {
            SDL_RWclose(outfile);
            return NewFileIoError::WriteFailed;
        }
    }
    
    SDL_RWclose(outfile);
    return NewFileIoError::None;
}

NewFileIoError create_new_campaign_descriptor_with_error(const std::string& filename)
{
	SDL_RWops* outfile = open_write_file(filename.c_str());
	if(outfile == nullptr)
        return NewFileIoError::OpenWriteFailed;
    
    og::io::YamlEmitter yaml;
    if (!yaml.set_output(rwops_write_handler, outfile))
    {
        SDL_RWclose(outfile);
        return NewFileIoError::WriteFailed;
    }
    
    yaml.emit_pair("format_version", "1");
    yaml.emit_pair("title", "New Campaign");
    yaml.emit_pair("version", "1");
    yaml.emit_pair("first_level", "1");
    yaml.emit_pair("suggested_power", "0");
    yaml.emit_pair("authors", "");
    yaml.emit_pair("contributors", "");
    yaml.emit_pair("description", "A new campaign.");
    
    yaml.close_output();
    SDL_RWclose(outfile);
    return NewFileIoError::None;
}

NewFileIoError create_new_scen_file_with_error(const std::string& scenfile, const std::string& gridname)
{
    // TODO: It would be nice to store all the level data in a class, then have saving code all in one place.
    
	// Format of a scenario object list file is: (ver. 8)
	// 3-byte header: 'FSS'
	// 1-byte version number (from graph.h)
	// 8-byte grid file name
	// 30-byte scenario title
	// 1-byte scenario_type
	// 2-bytes par-value for level
	// 2-bytes (Sint32) = total objects to follow
	// List of n objects, each of 20-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte Sint32 xpos
	// 2-byte Sint32 ypos
	// 1-byte TEAM
	// 1-byte current facing
	// 1-byte current command
	// 1-byte level // this is 2 bytes in version 7+
	// 12-bytes name
	// 10 bytes RESERVED
	// ---
	// 1-byte # of lines of text to load
	// List of n lines of text, each of form:
	// 1-byte character width of line
	// m bytes == characters on this line
	
	const char* header = "FSS";
	unsigned char version = 8;
	
	char grid_file_name[8];
	snprintf(grid_file_name, sizeof(grid_file_name), "%s", gridname.c_str());
	
	char scenario_title[30];
	snprintf(scenario_title, sizeof(scenario_title), "New Level");
	
	unsigned char scenario_type = 1;//SCEN_TYPE_CAN_EXIT;
	
	short par_value = 1;
	
	short num_objects = 0;
	
	//char reserved[20] = "MSTRMSTRMSTRMSTR";
	
	unsigned char num_lines = 1;
	char line_text[50] = "A new scenario.";
	unsigned char line_length = static_cast<unsigned char>(strlen(line_text));
	
	SDL_RWops* outfile;
	if((outfile = open_write_file(scenfile.c_str())) == nullptr)
	{
		LogError("Could not open file for writing: {}\n", scenfile);
		return NewFileIoError::OpenWriteFailed;
	}
	
	// Write it out
    if(!write_rwops_exact(outfile, header, 1, 3)
       || !write_rwops_exact(outfile, &version, 1, 1)
       || !write_rwops_exact(outfile, grid_file_name, 1, 8)
       || !write_rwops_exact(outfile, scenario_title, 1, 30)
       || !write_rwops_exact(outfile, &scenario_type, 1, 1)
       || !write_rwops_exact(outfile, &par_value, 2, 1)
       || !write_rwops_exact(outfile, &num_objects, 2, 1)
       || !write_rwops_exact(outfile, &num_lines, 1, 1)
       || !write_rwops_exact(outfile, &line_length, 1, 1)
       || !write_rwops_exact(outfile, line_text, line_length, 1))
    {
        SDL_RWclose(outfile);
        return NewFileIoError::WriteFailed;
    }
    // No objects to write

	SDL_RWclose(outfile);
	
    return NewFileIoError::None;
}

bool create_new_map_pix(const std::string& filename, int w, int h)
{
    return create_new_map_pix_with_error(filename, w, h) == NewFileIoError::None;
}

bool create_new_pix(const std::string& filename, int w, int h, unsigned char fill_color)
{
    return create_new_pix_with_error(filename, w, h, fill_color) == NewFileIoError::None;
}

bool create_new_campaign_descriptor(const std::string& filename)
{
    return create_new_campaign_descriptor_with_error(filename) == NewFileIoError::None;
}

bool create_new_scen_file(const std::string& scenfile, const std::string& gridname)
{
    return create_new_scen_file_with_error(scenfile, gridname) == NewFileIoError::None;
}

bool unpack_campaign(const std::string& campaign_id)
{
    return unzip_into(get_user_path() + "campaigns/" + campaign_id + ".glad", get_user_path() + "temp/");
}

bool repack_campaign(const std::string& campaign_id)
{
    std::string outfile = get_user_path() + "campaigns/" + campaign_id + ".glad";
    remove(outfile.c_str());
    return zip_contents(get_user_path() + "temp/", outfile);
}

void cleanup_unpacked_campaign()
{
    std::error_code ec;
    std::filesystem::remove_all(get_user_path() + "temp", ec);
}
