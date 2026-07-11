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
#pragma once

#include <string>
#include <map>

class cfg_store
{
public:
    
	bool load_settings();
	void commandline(int &argc, char **&argv);
    bool save_settings();

    // Legacy-key shim, run by load_settings() after parsing (on every exit
    // path): when the settings never mentioned effects/depth_fx, derive it
    // from the retired boolean effects/depth_tint (off -> off, on -> fog)
    // or fall back to the default, "fog". A present depth_fx always wins;
    // a leftover depth_tint key is inert stale data.
    void migrate_depth_fx();

    void apply_setting(const std::string& category, const std::string& setting, const std::string& value);
    // Set a transient, in-memory-only override (e.g. a developer command-line
    // flag). Overrides take precedence over `data` for reads but are never
    // written back by save_settings(), so they don't leak into the user's
    // persisted config.
    void apply_override(const std::string& category, const std::string& setting, const std::string& value);
    std::string get_setting(const std::string& category, const std::string& setting);
	bool is_on(const std::string& category, const std::string& setting);

	// Persisted settings. save_settings() serializes exactly this map.
	std::map<std::string, std::map<std::string, std::string> > data;
	// Ephemeral overrides, consulted by get_setting() but never persisted.
	std::map<std::string, std::map<std::string, std::string> > overrides;
};

// Global cfg_store instance, owned at file scope in data/gparser.cpp.
extern cfg_store cfg;

