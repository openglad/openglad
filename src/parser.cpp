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
#include <config.h>

#include <iostream>
#include <fstream>
#include <string>
#include "parser.h"
using namespace std;

cfg_store cfg;

bool cfg_store::parse(const char *filename)
{
	string line;
	map<string, string> *section = NULL;
	ifstream in(filename);
	if(!in)
	{
		//cerr << "could not open config file " << filename << endl;
		return false;
	}
	while(std::getline(in, line, '\n'))
	{
		int pos = line.find(';');
		if (pos != string::npos)
			line.erase(pos);
		if((pos=line.find('[')) != std::string::npos)
		{
			section = &data[line.substr(pos+1, line.find(']', pos)-1)];
		}
		else if((pos=line.find('=')) != std::string::npos)
		{
			if (section == NULL)
			{
				cerr << "entry outside section\n";
				return false;
			}
			pair<string, string> entry(line.substr(0,pos),
						   line.substr(pos+1, line.size()));
			section->insert(entry);
		}
#if 0
		else
		{
			cerr << "strange line:\n" << line << endl;
			return false;
		}
#endif
	}
	return true;
}

void cfg_store::commandline(int &argc, char **&argv)
{
	const char helpmsg[] = "\
Usage: open(glad|scen) [-dfhsSvnxe]\n\
  -s, --sound		Turn sound on\n\
  -S, --nosound		Turn sound off\n\
  -n, --nostretch	Run at 320x200 resolution\n\
  -d, --double		Double pixel size\n\
  -e, --eagle		Use eagle engine for pixel doubling\n\
  -i, --sai		Use sai2x engine for pixel doubling\n\
  -f, --fullscreen	Use full screen\n\
  -h, --help		Print a summary of the options\n\
  -v, --version		Print the version number\n\
";
	const char versmsg[] = "openglad version " PACKAGE_VERSION "\n";
	const struct option longopts[] = {
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
		c = getopt_long (argc, argv, "dniefhsSv", longopts, NULL);
		switch(c)
		{
		case 'h':
			cout << helpmsg;
			exit (0);
		case 'v':
			cout << versmsg;
			exit (0);
		case 's':
			data["sound"]["sound"] = "on";
			break;
		case 'S':
			data["sound"]["sound"] = "off";
			break;
		case 'n':
			data["graphics"]["render"] = "normal";
			break;
		case 'd':
			data["graphics"]["render"] = "double";
			break;
		case 'e':
			data["graphics"]["render"] = "eagle";
			break;
		case 'x':
			data["graphics"]["render"] = "sai";
			break;
		case 'f':
			data["graphics"]["fullscreen"] = "on";
			break;
		case -1:
			return;
		}
	}
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
	cerr << "config variable not found: section: " << section
	     << " entry: " << entry << endl;
#endif
	return NULL;
}
