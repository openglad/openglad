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

std::string get_asset_path()
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

SDL_RWops* open_read_file(const char* file)
{
    SDL_RWops* rwops = PHYSFSRWOPS_openRead(file);
    if(rwops != NULL)
        return rwops;
    return SDL_RWFromFile(file, "rb");
}

SDL_RWops* open_read_file(const char* path, const char* file)
{
    return open_read_file((std::string(path) + file).c_str());
}

SDL_RWops* open_write_file(const char* file)
{
    SDL_RWops* rwops = PHYSFSRWOPS_openWrite(file);
    if(rwops != NULL)
        return rwops;
    return SDL_RWFromFile(file, "wb");
}

SDL_RWops* open_write_file(const char* path, const char* file)
{
    return open_write_file((std::string(path) + file).c_str());
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

bool mount_campaign_package(const std::string& id)
{
    std::string filename = get_user_path() + "campaigns/" + id + ".glad";
    if(!PHYSFS_mount(filename.c_str(), NULL, 0))
    {
        Log("Failed to mount campaign %s: %s\n", filename.c_str(), PHYSFS_getLastError());
        return false;
    }
    return true;
}

bool unmount_campaign_package(const std::string& id)
{
    std::string filename = get_user_path() + "campaigns/" + id + ".glad";
    if(!PHYSFS_removeFromSearchPath(filename.c_str()))
    {
        Log("Failed to unmount campaign file %s: %s\n", filename.c_str(), PHYSFS_getLastError());
        return false;
    }
    return true;
}

std::list<std::string> list_campaigns()
{
    std::list<std::string> ls = list_files("campaigns/");
    for(std::list<std::string>::iterator e = ls.begin(); e != ls.end(); e++)
    {
        *e = e->substr(0, e->rfind(".glad"));
    }
    return ls;
}

std::list<std::string> list_levels()
{
    return list_files("scen/");
}

void copy_file(const std::string& filename, const std::string& dest_filename)
{
    Log("Copying file: %s\n", filename.c_str());
    SDL_RWops* in = SDL_RWFromFile(filename.c_str(), "rb");
    if(in == NULL)
    {
        Log("Could not open file to copy.\n");
        return;
    }
    
    long size = 100;
    // Grab the data
    unsigned char* data = (unsigned char*)malloc(size);
    
    // Save it to another file
    Log("Copying to: %s\n", dest_filename.c_str());
    SDL_RWops* out = SDL_RWFromFile(dest_filename.c_str(), "wb");
    if(out == NULL)
    {
        Log("Could not open destination file.\n");
        SDL_RWclose(in);
        return;
    }
    
    long total = 0;
    long len = 0;
    while((len = SDL_RWread(in, data, 1, size)) > 0)
    {
        SDL_RWwrite(out, data, 1, len);
        total += len;
    }
    
    SDL_RWclose(in);
    SDL_RWclose(out);
    free(data);
    
    Log("Copied %d bytes.\n", total);
}

void create_dataopenglad()
{
    std::string user_path = get_user_path();
    mkdir(user_path.c_str(), 0770);
    mkdir((user_path + "campaigns/").c_str(), 0770);
    mkdir((user_path + "save/").c_str(), 0770);
    mkdir((user_path + "cfg/").c_str(), 0770);
}

void io_init(int argc, char* argv[])
{
    // Make sure our directory tree exists and is set up
    create_dataopenglad();
    
    PHYSFS_init(argv[0]);
    PHYSFS_setWriteDir(get_user_path().c_str());
    
    if(!PHYSFS_mount(get_user_path().c_str(), NULL, 1))
    {
        Log("Failed to mount user data path.\n");
        exit(1);
    }
    
    
    if(!PHYSFS_exists("campaigns/org.openglad.gladiator.glad"))
        copy_file("scen/org.openglad.gladiator.glad", get_user_path() + "campaigns/org.openglad.gladiator.glad");
    
    // NOTES!
    // PhysFS cannot grab files from the assets folder because they're actually inside the apk.
    // SDL_RWops does some magic to figure out a file descriptor from JNI.
    // This means that I cannot use PhysFS to get any assets at all.
    // So for simple assets, I need to check PhysFS first, then fall back to SDL_RWops from the assets folder.
    // For campaign packages, I can copy them to the internal storage and they'll live happily there, accessed by PhysFS.
    // SDL_RWops size checking on Android doesn't seem to work!
    
    // Open up the default campaign
    // TODO: Let us change the campaign directory (store this one and unmount it when changing)
    if(!PHYSFS_mount((get_user_path() + "campaigns/org.openglad.gladiator.glad").c_str(), NULL, 1))
    {
        Log("Failed to mount default campaign path: %s\n", PHYSFS_getLastError());
        exit(1);
    }
    
    // Set up paths for default assets
    if(!PHYSFS_mount((get_asset_path() + "pix/").c_str(), "pix/", 1))
    {
        Log("Failed to mount default pix path.\n");
    }
    if(!PHYSFS_mount((get_asset_path() + "sound/").c_str(), "sound/", 1))
    {
        Log("Failed to mount default sound path.\n");
    }
    if(!PHYSFS_mount((get_asset_path() + "cfg/").c_str(), "cfg/", 1))
    {
        Log("Failed to mount default cfg path.\n");
    }
    
    
}

void io_exit()
{
    PHYSFS_deinit();
}
