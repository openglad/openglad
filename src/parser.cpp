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

CfgStore cfg;

bool CfgStore::parse(const char *filename)
{
	string line;
	map<string, string> *section = NULL;
	ifstream in(filename);
	if(!in)
	{
		cerr << "could not open config file " << filename << endl;
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

const char *CfgStore::query(const char *section, const char *entry)
{
	string sec(section), ent(entry);
	//return data[sec][ent].c_str();
	map<string, map<string, string> >::iterator a1 = data.find(sec);
	if(a1 != data.end())
	{
		map<string, string>::iterator a2 = a1->second.find(ent);
		if(a2 != a1->second.end())
			return a2->second.c_str();
	}
	cerr << "config variable not found: section: " << sec
	     << " entry: " << ent << endl;
	return NULL;
}
