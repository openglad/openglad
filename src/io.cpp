#include "io.h"
#include "util.h"

#include "yam.h"
#include "physfs.h"
#include "physfsrwops.h"
#include <string>
#include <stdio.h>
#include <sys/stat.h>

#ifdef WIN32
#include "windows.h"
#include <shlobj.h>
#include <time.h>
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
    SDL_RWops *rwops = (SDL_RWops*)data;

    *size_read = SDL_RWread(rwops, buffer, 1, size);
    return 1;
}

std::string get_user_path()
{
#ifdef ANDROID
    std::string path = SDL_AndroidGetInternalStoragePath();
    return path + "/";
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

std::string get_data_path()
{
#ifdef ANDROID
    // RWops will look in the app's assets directory for this path
    return "";
#elif defined(WINDOWS)
    // Assuming the cwd is set to the program's installation directory
    return "";
#else
    // FIXME: This won't typically work for *nix
    return "";
#endif
}

std::string get_asset_path()
{
    return "";
}

SDL_RWops* open_read_file(const char* file)
{
    return PHYSFSRWOPS_openRead(file);
}

SDL_RWops* open_read_file(const char* path, const char* file)
{
    return open_read_file((std::string(path) + file).c_str());
}

SDL_RWops* open_write_file(const char* file)
{
    return PHYSFSRWOPS_openWrite(file);
}

SDL_RWops* open_write_file(const char* path, const char* file)
{
    return open_write_file((std::string(path) + file).c_str());
}


void create_dataopenglad()
{
    std::string user_path = get_user_path();
    mkdir(user_path.c_str(), 0770);
    mkdir((user_path + "scen/").c_str(), 0770);
    mkdir((user_path + "save/").c_str(), 0770);
    mkdir((user_path + "cfg/").c_str(), 0770);
}


std::list<std::string> list_files(const std::string& dirname)
{
    std::list<std::string> fileList;
    char** files = PHYSFS_enumerateFiles(dirname.c_str());
    char** p = files;
    while(p != NULL && *p != NULL)
    {
        fileList.push_back(*p);
        p++;
    }
    PHYSFS_freeList(files);
    
    fileList.sort();
    
    return fileList;
}

void io_init(int argc, char* argv[])
{
    // Make sure our directory tree exists and is set up
    create_dataopenglad();
    
    PHYSFS_init(argv[0]);
    PHYSFS_setWriteDir(get_user_path().c_str());
    
    
    if(!PHYSFS_mount((get_user_path() + "save/").c_str(), "save/", 1))
    {
        Log("Failed to mount user save path.\n");
        exit(1);
    }
    
    // Custom campaigns will be found here
    if(!PHYSFS_mount((get_user_path() + "scen/").c_str(), "scen/", 1))
    {
        Log("Failed to mount user scen path.\n");
        exit(1);
    }
    
    if(!PHYSFS_mount((get_user_path() + "cfg/").c_str(), "cfg/", 1))
    {
        Log("Failed to mount user cfg path.\n");
        exit(1);
    }
    
    // Open up the default campaign
    // TODO: Let us change the campaign directory (store this one and unmount it when changing)
    if(!PHYSFS_mount((get_asset_path() + "scen/" + "org.openglad.gladiator.glad").c_str(), NULL, 1))
    {
        Log("Failed to mount default campaign path.\n");
        exit(1);
    }
    
    // Set up paths for default assets
    if(!PHYSFS_mount((get_asset_path() + "pix/").c_str(), "pix/", 1))
    {
        Log("Failed to mount default pix path.\n");
        exit(1);
    }
    if(!PHYSFS_mount((get_asset_path() + "sound/").c_str(), "sound/", 1))
    {
        Log("Failed to mount default sound path.\n");
        exit(1);
    }
    if(!PHYSFS_mount((get_asset_path() + "cfg/").c_str(), "cfg/", 1))
    {
        Log("Failed to mount default cfg path.\n");
        exit(1);
    }
    
    
}

void io_exit()
{
    PHYSFS_deinit();
}
