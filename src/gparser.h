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
#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <map>

class cfg_store
{
public:
    
	bool load_settings();
	void commandline(int &argc, char **&argv);
    bool save_settings();

    void apply_setting(const std::string& category, const std::string& setting, const std::string& value);
    std::string get_setting(const std::string& category, const std::string& setting);
	bool is_on(const std::string& category, const std::string& setting);

	std::map<std::string, std::map<std::string, std::string> > data;
};

extern cfg_store cfg;

#endif
