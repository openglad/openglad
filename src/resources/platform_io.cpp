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

#include <openglad/resources/io.h>
#include <openglad/resources/campaign_yaml.h>
#include <openglad/resources/gparser.h>
#include <openglad/core/util.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/zip_api.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/core/pixdefs.h>
#include <cstring>

bool write_pixie_png(const char* filepath, const PixieData& data);
#include <format>
#include <filesystem>
#include <array>
#include <memory>
#include <string>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace og::io {
SDL_IOStream* physfsrw_open_read(const char* path);
SDL_IOStream* physfsrw_open_write(const char* path);
} // namespace og::io

// remove_file_or_log, explode, list_files, campaign mount/unmount/list,
// archive helpers, delete_level/campaign, restore_default_campaigns/settings,
// create_dir, and other SDL-free I/O helpers are now in
// src/io/platform_io_common.cpp (shared by both SDL and headless builds).

#ifdef _WIN32
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
SDL_IOStream is used to access the data in the files retrieved from PhysFS.

Scenario packages are stored in the user directory so more can be installed, etc.
The default pix and sound assets are installed with the rest of the program, presumably without write access.

*/

int rwops_read_handler(void *data, unsigned char *buffer, size_t size, size_t *size_read)
{
    SDL_IOStream *rwops = static_cast<SDL_IOStream*>(data);

    *size_read = SDL_RWread(rwops, buffer, 1, size);
    return 1;
}


int rwops_write_handler(void *data, unsigned char *buffer, size_t size)
{
    SDL_IOStream *rwops = static_cast<SDL_IOStream*>(data);

    SDL_RWwrite(rwops, buffer, 1, size);
    return 1;
}

std::string get_user_path()
{
    auto normalize_dir = [](std::string path) {
        while (path.size() > 1 && path.back() == '/') {
            path.pop_back();
        }
        if (path.empty()) {
            return std::string("./");
        }
        if (path.back() != '/') {
            path.push_back('/');
        }
        return path;
    };

    if (const char* config_dir = std::getenv("OPENGLAD_CONFIG_DIR")) {
        if (config_dir[0] != '\0') {
            return normalize_dir(config_dir);
        }
    }

#ifdef __EMSCRIPTEN__
    // Use IDBFS mount point for persistent storage in browser
    return "/persist/";
#elif defined(ANDROID)
    std::string path = SDL_GetAndroidInternalStoragePath();
    return path + "/";
#elif defined(__IPHONEOS__)
    return "../";
#elif defined(_WIN32)
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

        return normalize_dir(s + "/.openglad");
    }
    return "";
#else
    const char* home = std::getenv("HOME");
    if (!home) {
        return "./";
    }
    return normalize_dir(std::string(home) + "/.openglad");
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
#elif defined(_WIN32)
    // Assuming the cwd is set to the program's installation directory
    return "";
#else
    // Assumes UNIX with /proc. NOTE: kept on the raw readlink() syscall on
    // purpose — Emscripten's virtual FS satisfies readlink("/proc/self/exe")
    // but std::filesystem::read_symlink() fails there, which would spam the
    // WASM console with errors at startup. The buffer is bounded and always
    // explicitly null-terminated, so there is no overflow.
    constexpr size_t maxPathSize = 512;
    std::array<char, maxPathSize> path;
    std::fill_n(path.data(), maxPathSize, '\0');

    const ssize_t read_len = readlink("/proc/self/exe", path.data(), maxPathSize - 1);
    if (read_len < 0)
    {
        LogError("get_asset_path: readlink(/proc/self/exe) failed\n");
        return "./";
    }
    path[static_cast<size_t>(read_len)] = '\0';

    std::string s = path.data();
    size_t slash = s.find_last_of('/');
    if(slash != std::string::npos)
    {
        s = s.substr(0, slash);
    }
    s += '/';

    return s;
#endif
}

	SDL_IOStream* open_read_file(const char* file, bool debug)
	{
	    SDL_IOStream* rwops = nullptr;
	    
	    if(debug)
		    Log("Trying via PHYSFS: {}", file);
	    rwops = og::io::physfsrw_open_read(file);
	    if(rwops != nullptr) return rwops;

    // now try opening in the current directory
    if(debug)
	    Log("Trying to open: {}", file);
    rwops = SDL_IOFromFile(file, "rb");
    if(rwops != nullptr) return rwops;

    // now try opening in the user directory
    if(debug)
	    Log("Trying to open: {}{}", get_user_path(), file);
    rwops = SDL_IOFromFile((get_user_path() + std::string("/") + file).c_str(), "rb");
    if(rwops != nullptr) return rwops;

    // now try opening in the asset directory
    if(debug)
	    Log("Trying to open: {}{}", get_asset_path(), file);
    rwops = SDL_IOFromFile((get_asset_path() + std::string("/") + file).c_str(), "rb");
    if(rwops != nullptr) return rwops;

    // File not found - this may be expected (e.g., keyprefs.dat on first run)
    if(debug)
        Log("File not found: {} (may be created on first use)\n", file);
    return nullptr;
}

SDL_IOStream* open_read_file(const char* path, const char* file)
{
    return open_read_file((std::string(path) + file).c_str());
}

	SDL_IOStream* open_write_file(const char* file)
	{
	    SDL_IOStream* rwops = og::io::physfsrw_open_write(file);
	    if(rwops != nullptr)
	        return rwops;
	    return SDL_IOFromFile(file, "wb");
	}

SDL_IOStream* open_write_file(const char* path, const char* file)
{
    return open_write_file((std::string(path) + file).c_str());
}



// create_dataopenglad: set up user directory tree (SDL build uses mkdir for compat)
static void create_dataopenglad()
{
    std::string user_path = get_user_path();
    mkdir(user_path.c_str(), 0770);
    mkdir((user_path + "campaigns/").c_str(), 0770);
    mkdir((user_path + "save/").c_str(), 0770);
    mkdir((user_path + "cfg/").c_str(), 0770);
    mkdir((user_path + "extra_pix/").c_str(), 0770);
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

    const std::string user_path = get_user_path();
    if (!og::resources::init(argv[0]))
    {
        std::string msg = std::format("Fatal: Failed to initialize PhysFS: {}",
                                      og::resources::filesystem_last_error());
        LogError("{}\n", msg);
        throw std::runtime_error(msg);
    }
    if (!og::resources::set_write_dir(user_path))
    {
        std::string msg = std::format("Fatal: Failed to set write directory {}: {}",
                                      user_path, og::resources::filesystem_last_error());
        LogError("{}\n", msg);
        throw std::runtime_error(msg);
    }

    if(!og::resources::mount(user_path.c_str(), nullptr, 1))
    {
        std::string msg = std::format("Fatal: Failed to mount user data path: {}", user_path);
        LogError("{}\n", msg);
        throw std::runtime_error(msg);
    }

    restore_default_campaigns();
    
    // NOTES!
    // PhysFS cannot grab files from the assets folder because they're actually inside the apk.
    // SDL_IOStream does some magic to figure out a file descriptor from JNI.
    // This means that I cannot use PhysFS to get any assets at all.
    // So for simple assets, I need to check PhysFS first, then fall back to SDL_IOStream from the assets folder.
    // For campaign packages, I can copy them to the internal storage and they'll live happily there, accessed by PhysFS.
    // SDL_IOStream size checking on Android doesn't seem to work!
    
    // Open up the default campaign
    Log("Mounting default campaign...\n");
    if (mount_campaign_package_with_error("org.openglad.gladiator") != CampaignPackageIoError::None)
    {
        std::string msg = std::format("Fatal: Failed to mount default campaign: {}",
                                      og::resources::filesystem_last_error());
        LogError("{}\n", msg);
        throw std::runtime_error(msg);
    }
    Log("Mounted default campaign\n");
    
    // Set up paths for default assets
    if(!og::resources::mount((get_asset_path() + "pix/").c_str(), "pix/", 1))
    {
        LogWarn("Failed to mount default pix path (may be bundled in campaign)\n");
    }
    if(!og::resources::mount((get_asset_path() + "sound/").c_str(), "sound/", 1))
    {
        LogWarn("Failed to mount default sound path (may be bundled in campaign)\n");
    }
    if(!og::resources::mount((get_asset_path() + "cfg/").c_str(), "cfg/", 1))
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
    og::resources::deinit();
}

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
    }

    if (!new_dir.empty()) {
        if (!og::resources::mount(new_dir.c_str(), "pix/", 0)) {
            LogWarn("Sprite sheet '{}': failed to mount\n", new_dir);
            return false;
        }
        Log("Sprite sheet mounted: {}\n", new_dir);
        s_mounted_sprite_sheet_dir = new_dir;
    }

    return true;
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





NewFileIoError create_new_map_pix_with_error(const std::string& filename, int w, int h)
{
    if (w <= 0 || h <= 0 || w > 255 || h > 255)
    {
        return NewFileIoError::InvalidDimensions;
    }

    PixieData grid;
    grid.frames = 1;
    grid.w = static_cast<unsigned char>(w);
    grid.h = static_cast<unsigned char>(h);
    int size = w * h;
    grid.data = std::make_unique<unsigned char[]>(static_cast<size_t>(size));

    static thread_local std::mt19937 grass_rng{std::random_device{}()};
    std::uniform_int_distribution<int> grass_dist(0, 3);
    const std::array<unsigned char, 4> grass_tiles = {PIX_GRASS1, PIX_GRASS2, PIX_GRASS3, PIX_GRASS4};
    for(int i = 0; i < size; i++)
    {
        grid.data[i] = grass_tiles[static_cast<size_t>(grass_dist(grass_rng))];
    }

    if (!write_pixie_png(filename.c_str(), grid))
        return NewFileIoError::WriteFailed;

    return NewFileIoError::None;
}

NewFileIoError create_new_pix_with_error(const std::string& filename, int w, int h, unsigned char fill_color)
{
    if (w <= 0 || h <= 0 || w > 255 || h > 255)
    {
        return NewFileIoError::InvalidDimensions;
    }

    PixieData grid;
    grid.frames = 1;
    grid.w = static_cast<unsigned char>(w);
    grid.h = static_cast<unsigned char>(h);
    int size = w * h;
    grid.data = std::make_unique<unsigned char[]>(static_cast<size_t>(size));
    memset(grid.data.get(), fill_color, static_cast<size_t>(size));

    if (!write_pixie_png(filename.c_str(), grid))
        return NewFileIoError::WriteFailed;

    return NewFileIoError::None;
}

NewFileIoError create_new_campaign_descriptor_with_error(const std::string& filename)
{
    const auto result = og::data::write_default_campaign_yaml_with_result(filename.c_str());
    if (result == og::data::CampaignYamlWriteResult::OpenFailed)
        return NewFileIoError::OpenWriteFailed;
    if (result != og::data::CampaignYamlWriteResult::Ok)
        return NewFileIoError::WriteFailed;

    return NewFileIoError::None;
}

NewFileIoError create_new_scen_file_with_error(const std::string& scenfile, const std::string& gridname)
{
    GameWorld world;
    world.type = GameWorld::TYPE_CAN_EXIT_WHENEVER;
    world.par_value = 1;
    world.time_bonus_limit = 4000;

    og::data::LevelFileMetadata metadata;
    metadata.grid_file = gridname;
    metadata.description.emplace_back("A new scenario.");

    og::data::LevelFileIoError io_error = og::data::LevelFileIoError::None;
    if (!og::data::save_level_scenario_file(world, scenfile, metadata, &io_error))
    {
        if (io_error == og::data::LevelFileIoError::OpenWriteFailed)
            return NewFileIoError::OpenWriteFailed;
        return NewFileIoError::WriteFailed;
    }

    return NewFileIoError::None;
}
