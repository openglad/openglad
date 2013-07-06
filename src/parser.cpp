/* Copyright (C) 2002  Kari Pahula
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

#if defined(_MSC_VER)
	#pragma warning(disable : 4786)
#endif

#include "version.h"

#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <cstring>
#include "parser.h"
#include "util.h"
#include "yam.h"

using namespace std;

cfg_store cfg;

bool cfg_store::parse(const char *filename)
{
    SDL_RWops* rwops = open_read_file(filename);
    if(rwops == NULL)
	{
		Log("Could not open config file. Using defaults.");
		data["sound"]["sound"] = "on";
		data["graphics"]["render"] = "normal";
		return false;
	}
    
    Yam yam;
    yam.set_input(rwops_read_handler, rwops);
    
    
    while(yam.parse_next() == Yam::OK)
    {
        switch(yam.event.type)
        {
            case Yam::PAIR:
                if(strcmp(yam.event.scalar, "sound") == 0)
                    data["sound"]["sound"] = strdup(yam.event.value);
                else if(strcmp(yam.event.scalar, "render") == 0)
                    data["graphics"]["render"] = strdup(yam.event.value);
                else if(strcmp(yam.event.scalar, "fullscreen") == 0)
                    data["graphics"]["fullscreen"] = strdup(yam.event.value);
            break;
            default:
                break;
        }
    }
    
    yam.close_input();
    SDL_RWclose(rwops);
    
	return true;
}

void cfg_store::commandline(int &argc, char **&argv)
{
	const char helpmsg[] = "\
Usage: open(glad|scen) [-d -f ...]\n\
  -s		Turn sound on\n\
  -S		Turn sound off\n\
  -n		Run at 320x200 resolution\n\
  -d		Double pixel size\n\
  -e		Use eagle engine for pixel doubling\n\
  -i		Use sai2x engine for pixel doubling\n\
  -f		Use full screen\n\
  -h		Print a summary of the options\n\
  -v		Print the version number\n\
";

	const char versmsg[] = "openglad version " OPENGLAD_VERSION_STRING "\n";

	// Begin changes by David Storey (Deathifier)
	// FIX: Handle mutually exclusive arguments being used at the same time?
	// E.G. -s and -S

	// Iterate over arguments, ignoring the first (Program Name).
	for(int argnum = 1; argnum < argc; argnum++)
	{
		// Look for arguments of 2 chars only:
		if(argv[argnum][0] == '-' && strlen(argv[argnum]) == 2)
		{
			// To handle arguments which have aditional arguments attached
			// to them, take care of it within the case statement and
			// increment argnum appropriately.
			switch(argv[argnum][1])
			{
				case 'h':
					Log("%s", helpmsg);
					exit (0);
				case 'v':
					Log("%s", versmsg);
					exit (0);
				case 's':
					data["sound"]["sound"] = "on";
					Log("Sound is on.");
					break;
				case 'S':
					data["sound"]["sound"] = "off";
					Log("Sound is off.");
					break;
				case 'n':
					data["graphics"]["render"] = "normal";
					Log("Screen Resolution set to 320x200.");
					break;
				case 'd':
					data["graphics"]["render"] = "double";
					Log("Screen Resolution set to 640x400 (basic mode).");
					break;
				case 'e':
					data["graphics"]["render"] = "eagle";
					Log("Screen Resolution set to 640x400 (eagle mode).");
					break;
				case 'x':
					data["graphics"]["render"] = "sai";
					Log("Screen Resolution set to 640x400 (sai2x mode).");
					break;
				case 'f':
					data["graphics"]["fullscreen"] = "on";
					Log("Running in fullscreen mode.");
					break;
				default:
					Log("Unknown argument %s ignored.", argv[argnum]);
			}
		}
	}

	// End Changes

/* Old version:
 * Ran the same switch as above but with the getopt_int result
	
	const struct option intopts[] = {
		{"help", 0, 0, 'h'},
		{"version", 0, 0, 'v'},
		{"sound", 0, 0, 's'},
		{"nosound", 0, 0, 'S'},
		{"nostretch", 0, 0, 'n'},
		{"double", 0, 0, 'd'},
		{"eagle", 0, 0, 'e'},
		{"sai", 0, 0, 'i'},
		{"fullscreen", 0, 0, 'f'},
		{0, 0, 0, 0}
	};
	while(1)
	{
		int c;
		c = getopt_int (argc, argv, "dniefhsSv", intopts, NULL);
		switch(c)
*/

}

const char *cfg_store::query(const char *section, const char *entry)
{
	//return data[section][entry].c_str();  // may make null entries
	map<string, map<string, string> >::iterator a1 = data.find(section);
	if(a1 != data.end())
	{
		map<string, string>::iterator a2 = a1->second.find(entry);
		if(a2 != a1->second.end())
			return a2->second.c_str();
	}
#if 0	// desired behavior now.  null replies mean use the default.
	Log("config variable not found: section: %s entry: %s", section, entry);
#endif
	return NULL;
}
