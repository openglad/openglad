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

#include "io.h"
#include "input.h"
#include "util.h"
#include "pixdefs.h"

#include "yam.h"
#include "physfs.h"
#include "physfsrwops.h"
#include <string>
#include <algorithm>
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


int rwops_write_handler(void *data, unsigned char *buffer, size_t size)
{
    SDL_RWops *rwops = (SDL_RWops*)data;

    SDL_RWwrite(rwops, buffer, 1, size);
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
#elif defined(WIN32)
    // Assuming the cwd is set to the program's installation directory
    return "";
#else
    // FIXME: This won't typically work for *nix
    return "/usr/share/openglad/";
#endif
}

SDL_RWops* open_read_file(const char* file)
{
    SDL_RWops* rwops = NULL;
    
    //Log((std::string("Trying via PHYSFS: ") + file).c_str());
    rwops = PHYSFSRWOPS_openRead(file);
    if(rwops != NULL) return rwops;

    // now try opening in the current directory
    //Log((std::string("Trying to open: ") + file).c_str());
    rwops = SDL_RWFromFile(file, "rb");
    if(rwops != NULL) return rwops;

    // now try opening in the user directory
    //Log((std::string("Trying to open: ") + get_user_path() + file).c_str());
    rwops = SDL_RWFromFile((get_user_path() + std::string("/") + file).c_str(), "rb");
    if(rwops != NULL) return rwops;

    // now try opening in the asset directory
    //Log((std::string("Trying to open: ") + get_asset_path() + file).c_str());
    rwops = SDL_RWFromFile((get_asset_path() + std::string("/") + file).c_str(), "rb");
    if(rwops != NULL) return rwops;

    // give up
    Log((std::string("Failed to find: ") + file).c_str());
    return NULL;
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

static std::string mounted_campaign;

std::string get_mounted_campaign()
{
    return mounted_campaign;
}

bool mount_campaign_package(const std::string& id)
{
    if(id.size() == 0)
        return false;

    Log(std::string("Mounting campaign package: " + id).c_str());
    
    std::string filename = get_user_path() + "campaigns/" + id + ".glad";
    if(!PHYSFS_mount(filename.c_str(), NULL, 0))
    {
        Log("Failed to mount campaign %s: %s\n", filename.c_str(), PHYSFS_getLastError());
        mounted_campaign.clear();
        return false;
    }
    mounted_campaign = id;
    return true;
}

bool unmount_campaign_package(const std::string& id)
{
    if(id.size() == 0)
        return true;
    
    std::string filename = get_user_path() + "campaigns/" + id + ".glad";
    if(!PHYSFS_removeFromSearchPath(filename.c_str()))
    {
        Log("Failed to unmount campaign file %s: %s\n", filename.c_str(), PHYSFS_getLastError());
        return false;
    }
    mounted_campaign.clear();
    return true;
}

bool remount_campaign_package()
{
    std::string id = get_mounted_campaign();
    if(!unmount_campaign_package(id))
        return false;
    return mount_campaign_package(id);
}

std::list<std::string> list_campaigns()
{
    std::list<std::string> ls = list_files("campaigns/");
    for(std::list<std::string>::iterator e = ls.begin(); e != ls.end(); e++)
    {
        size_t pos = e->rfind(".glad");
        if(pos == std::string::npos)
            e = ls.erase(e);  // Not a campaign package
        else
            *e = e->substr(0, pos);  // Remove the extension
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
            result.push_back(atoi(e->c_str()));
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
            result.push_back(atoi(e->c_str()));
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
    char path[256];
    // Delete data file
    snprintf(path, 256, "%stemp/scen/scen%d.fss", get_user_path().c_str(), id);
    remove(path);
    // Delete terrain file
    snprintf(path, 256, "%stemp/pix/scen%04d.pix", get_user_path().c_str(), id);
    remove(path);
    repack_campaign(campaign);
    
    // Remount for consistency in PhysFS
    remount_campaign_package();
}

void delete_campaign(const std::string& id)
{
    char path[256];
    snprintf(path, 256, "%scampaigns/%s.glad", get_user_path().c_str(), id.c_str());
    remove(path);
}

void delete_user_file(const std::string& filename)
{
    char path[256];
    snprintf(path, 256, "%s%s", get_user_path().c_str(), filename.c_str());
    remove(path);
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

void restore_default_campaigns()
{
    #ifndef FORCE_RESTORE_DEFAULT_CAMPAIGNS
    if(!PHYSFS_exists("campaigns/org.openglad.gladiator.glad"))
    #endif
        copy_file(get_asset_path() + "builtin/org.openglad.gladiator.glad", get_user_path() + "campaigns/org.openglad.gladiator.glad");
}

void restore_default_settings()
{
    copy_file(get_asset_path() + "cfg/openglad.yaml", get_user_path() + "cfg/openglad.yaml");
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
    
    restore_default_campaigns();
    
    // NOTES!
    // PhysFS cannot grab files from the assets folder because they're actually inside the apk.
    // SDL_RWops does some magic to figure out a file descriptor from JNI.
    // This means that I cannot use PhysFS to get any assets at all.
    // So for simple assets, I need to check PhysFS first, then fall back to SDL_RWops from the assets folder.
    // For campaign packages, I can copy them to the internal storage and they'll live happily there, accessed by PhysFS.
    // SDL_RWops size checking on Android doesn't seem to work!
    
    // Open up the default campaign
    Log("Mounting default campaign...");
    if (!mount_campaign_package("org.openglad.gladiator"))
    {
        Log("Failed to mount default campaign: %s\n", PHYSFS_getLastError());
        exit(1);
    }
    Log("Mounted default campaign...");
    
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





// Zip utils

#include "zip.h"
#include <sys/stat.h>
#include <dirent.h>

// Need to implement for real
// PhysFS would work, but the paths would have to be in the search path
//   and the RWops would have to be gotten from PhysFS and I would have to rewire the zip input (could the archive be opened through PhysFS too?)
// Doing it with goodio would be nice.
std::list<std::string> list_paths_recursively(const std::string& dirname)
{
    std::string _dirname = dirname;
    if(_dirname.size() > 0 && _dirname[_dirname.size()-1] != '/')
        _dirname += '/';
    
    std::list<std::string> ls;

    DIR* dir = opendir(_dirname.c_str());
    dirent* entry;
    
    if(dir == NULL)
        return ls;
    
    while ((entry = readdir(dir)) != NULL)
    {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        
        #ifdef WIN32
        struct stat status;
        stat((_dirname + entry->d_name).c_str(), &status);
        if(status.st_mode & S_IFDIR)
        #else
        if(entry->d_type == DT_DIR)
        #endif
        {
            std::list<std::string> sublist = list_paths_recursively(_dirname + entry->d_name);
            std::string subdir = entry->d_name;
            if(subdir.size() > 0 && subdir[subdir.size()-1] != '/')
                subdir += '/';
            ls.push_back(subdir);
            for(std::list<std::string>::iterator e = sublist.begin(); e != sublist.end(); e++)
            {
                ls.push_back(subdir + *e);
            }
        }
        else
            ls.push_back(entry->d_name);
    }

    closedir(dir);

    return ls;
}

bool zip_contents(const std::string& indirectory, const std::string& outfile)
{
    std::string indir = indirectory;
    if(indir.size() > 0 && indir[indir.size()-1] != '/')
        indir += '/';
    //Log("Zipping %s as %s\n", indir.c_str(), outfile.c_str());
    
    int err = 0;
    zip* archive = zip_open(outfile.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if(archive == NULL)
        return false;
    
    struct zip_source *s;
    char src_name[512];
    char dest_name[512];
    
    std::list<std::string> files = list_paths_recursively(indir);
    for(std::list<std::string>::iterator e = files.begin(); e != files.end(); e++)
    {
        snprintf(src_name, 512, "%s%s", indir.c_str(), e->c_str());
        snprintf(dest_name, 512, "%s", e->c_str());
        
        if(src_name[strlen(src_name)-1] == '/')
        {
            if(zip_dir_add(archive, dest_name, ZIP_FL_ENC_GUESS) < 0)
            {
                // Error
                Log("error adding dir: %s\n", zip_strerror(archive));
            }
        }
        else
        {
            if((s=zip_source_file(archive, src_name, 0, -1)) == NULL || zip_file_add(archive, dest_name, s, ZIP_FL_OVERWRITE | ZIP_FL_ENC_GUESS) < 0)
            {
                // Error
                zip_source_free(s);
                Log("error adding file: %s\n", zip_strerror(archive));
            }
        }
    }
    
    if(zip_close(archive) < 0)
    {
        Log("Error flushing zip file output: %s\n", zip_strerror(archive));
        return false;
    }
    
    return true;
}



#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include <string.h>

// From http://niallohiggins.com/2009/01/08/mkpath-mkdir-p-alike-in-c-for-unix/
/* Function with behaviour like `mkdir -p'  */
int mkpath(const char *s, mode_t mode)
{
    char *q, *parent = NULL, *path = NULL, *up = NULL;
    int rv;

    rv = -1;
    if (strcmp(s, ".") == 0 || strcmp(s, "/") == 0 || (strlen(s) == 3 && s[2] == '/'))
        return 0;

    if ((path = strdup(s)) == NULL)
        exit(1);
 
    if ((q = strdup(s)) == NULL)
        exit(1);

    if ((parent = dirname(q)) == NULL)
        goto out;
    
    if ((up = strdup(parent)) == NULL)
        exit(1);

    if ((mkpath(up, mode) == -1) && (errno != EEXIST))
        goto out;

    if ((mkdir(path, mode) == -1) && (errno != EEXIST))
        rv = -1;
    else
        rv = 0;

out:
    if (up != NULL)
        free(up);
    free(q);
    free(path);
    return (rv);
}

bool create_path_to_file(const char* filename)
{
    const char* c = strrchr(filename, '/');
    if(c == NULL)
        c = strrchr(filename, '\\');
    if(c == NULL)
        return true;
    
    char buf[512];
    snprintf(buf, 512, "%s", filename);
    buf[c - filename] = '\0';
    
    return (mkpath(buf, 0755) >= 0);
}

bool create_dir(const std::string& dirname)
{
    return (mkpath(dirname.c_str(), 0755) >= 0);
}

bool unzip_into(const std::string& infile, const std::string& outdirectory)
{
    std::string outdir = outdirectory;
    if(outdir.size() > 0 && outdir[outdir.size()-1] != '/')
        outdir += '/';
    
    //Log("Unzipping %s\n", infile.c_str());
    
    int err = 0;
    zip* archive = zip_open(infile.c_str(), 0, &err);
    if(archive == NULL)
        return false;
    
    struct zip_stat status;
    struct zip_file* file;
    int buf_size = 512;
    char buf[buf_size];
    
    for(int i = 0; i < zip_get_num_entries(archive, 0); i++)
    {
        if(zip_stat_index(archive, i, 0, &status) == 0)
        {
            int len = strlen(status.name);
            if(status.name[len - 1] == '/')
            {
                snprintf(buf, buf_size, "%s%s", outdir.c_str(), status.name);
                create_dir(buf);
            }
            else
            {
                file = zip_fopen_index(archive, i, 0);
                if(file == NULL)
                {
                    // Error
                    continue;
                }
                
                snprintf(buf, buf_size, "%s%s", outdir.c_str(), status.name);
                create_path_to_file(buf);
                SDL_RWops* rwops = open_write_file(outdir.c_str(), status.name);
                if(rwops == NULL)
                {
                    // Error
                    continue;
                }
 
                size_t sum = 0;
                while(sum < status.size)
                {
                    len = zip_fread(file, buf, buf_size);
                    if(len < 0)
                    {
                        // Error
                        
                    }
                    SDL_RWwrite(rwops, buf, 1, len);
                    sum += len;
                }
                SDL_RWclose(rwops);
                zip_fclose(file);
            }
        }
        else
        {
            // Error
        }
    }
    
    return (zip_close(archive) >= 0);
}

bool create_new_map_pix(const std::string& filename, int w, int h)
{
	// File data in form:
	// <# of frames>      1 byte
	// <x size>                   1 byte
	// <y size>                   1 byte
	// <pixie data>               <x*y*frames> bytes
	
	unsigned char c;
	SDL_RWops* outfile = open_write_file(filename.c_str());
	if(outfile == NULL)
        return false;
    
    c = 1;  // Frames
	SDL_RWwrite(outfile, &c, 1, 1);
    c = w;  // x size
	SDL_RWwrite(outfile, &c, 1, 1);
    c = h;  // y size
	SDL_RWwrite(outfile, &c, 1, 1);
	
	int size = w*h;
	for(int i = 0; i < size; i++)
    {
        // Color
        switch(rand()%4)
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
        SDL_RWwrite(outfile, &c, 1, 1);
    }
    
    SDL_RWclose(outfile);
    return true;
}

bool create_new_pix(const std::string& filename, int w, int h, unsigned char fill_color)
{
	// File data in form:
	// <# of frames>      1 byte
	// <x size>                   1 byte
	// <y size>                   1 byte
	// <pixie data>               <x*y*frames> bytes
	
	unsigned char c;
	SDL_RWops* outfile = open_write_file(filename.c_str());
	if(outfile == NULL)
        return false;
    
    c = 1;  // Frames
	SDL_RWwrite(outfile, &c, 1, 1);
    c = w;  // x size
	SDL_RWwrite(outfile, &c, 1, 1);
    c = h;  // y size
	SDL_RWwrite(outfile, &c, 1, 1);
	
	c = fill_color;  // Color
	int size = w*h;
	for(int i = 0; i < size; i++)
    {
        SDL_RWwrite(outfile, &c, 1, 1);
    }
    
    SDL_RWclose(outfile);
    return true;
}

bool create_new_campaign_descriptor(const std::string& filename)
{
	SDL_RWops* outfile = open_write_file(filename.c_str());
	if(outfile == NULL)
        return false;
    
    Yam yam;
    yam.set_output(rwops_write_handler, outfile);
    
    yam.emit_pair("format_version", "1");
    yam.emit_pair("title", "New Campaign");
    yam.emit_pair("version", "1");
    yam.emit_pair("first_level", "1");
    yam.emit_pair("suggested_power", "0");
    yam.emit_pair("authors", "");
    yam.emit_pair("contributors", "");
    yam.emit_pair("description", "A new campaign.");
    
    yam.close_output();
    SDL_RWclose(outfile);
    return true;
}

bool create_new_scen_file(const std::string& scenfile, const std::string& gridname)
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
	strncpy(grid_file_name, gridname.c_str(), 8);
	
	char scenario_title[30];
	strncpy(scenario_title, "New Level", 30);
	
	unsigned char scenario_type = 1;//SCEN_TYPE_CAN_EXIT;
	
	short par_value = 1;
	
	short num_objects = 0;
	
	//char reserved[20] = "MSTRMSTRMSTRMSTR";
	
	unsigned char num_lines = 1;
	char line_text[50] = "A new scenario.";
	unsigned char line_length = strlen(line_text);
	
	SDL_RWops* outfile;
	if((outfile = open_write_file(scenfile.c_str())) == NULL)
	{
		Log("Could not open file for writing: %s\n", scenfile.c_str());
		return false;
	}
	
	// Write it out
	SDL_RWwrite(outfile, header, 1, 3);
	SDL_RWwrite(outfile, &version, 1, 1);
	SDL_RWwrite(outfile, grid_file_name, 1, 8);
	SDL_RWwrite(outfile, scenario_title, 1, 30);
	SDL_RWwrite(outfile, &scenario_type, 1, 1);
	SDL_RWwrite(outfile, &par_value, 2, 1);

	SDL_RWwrite(outfile, &num_objects, 2, 1);
    // No objects to write
    
	SDL_RWwrite(outfile, &num_lines, 1, 1);
    SDL_RWwrite(outfile, &line_length, 1, 1);
    SDL_RWwrite(outfile, line_text, line_length, 1);

	SDL_RWclose(outfile);
	
    return true;
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
    // Recursive delete
    std::list<std::string> ls = list_paths_recursively(get_user_path() + "temp");
    for(std::list<std::string>::reverse_iterator e = ls.rbegin(); e != ls.rend(); e++)
    {
        std::string path = get_user_path() + "temp/" + *e;
        remove(path.c_str());
        rmdir(path.c_str());
    }
    
    rmdir((get_user_path() + "temp").c_str());
}
