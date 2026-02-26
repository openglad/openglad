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

#include <openglad/core/version.h>
#include <iostream>
#include <fstream>
#include <string>
#include <set>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <format>
#include <openglad/data/gparser.h>
#include <openglad/core/util.h>
#include <openglad/io/og_file.h>
#include <openglad/io/ogfile_yaml.h>
#include <openglad/io/yaml_stream.h>

int toInt(const std::string& s);


cfg_store cfg;

void cfg_store::apply_setting(const std::string& category, const std::string& setting, const std::string& value)
{
    data[category][setting] = value;
}

std::string cfg_store::get_setting(const std::string& category, const std::string& setting)
{
	std::map<std::string, std::map<std::string, std::string> >::iterator a1 = data.find(category);
	if(a1 != data.end())
	{
		std::map<std::string, std::string>::iterator a2 = a1->second.find(setting);
		if(a2 != a1->second.end())
			return a2->second;
	}

    static std::set<std::string> missing_logged;
    const std::string key = category + "/" + setting;
    if (missing_logged.insert(key).second)
    {
        Log("cfg setting not found: {}/{}\n", category, setting);
    }
	return "";
}

bool cfg_store::load_settings()
{
    // Load defaults
    apply_setting("", "version", "1");
    apply_setting("sound", "sound", "on");

    apply_setting("graphics", "render", "normal");
    apply_setting("graphics", "fullscreen", "on");
    apply_setting("graphics", "overscan_percentage", "0");

    apply_setting("effects", "gore", "on");
    apply_setting("effects", "mini_hp_bar", "on");
    apply_setting("effects", "hit_flash", "on");
    apply_setting("effects", "hit_recoil", "off");
    apply_setting("effects", "attack_lunge", "on");
    apply_setting("effects", "hit_anim", "on");
    apply_setting("effects", "damage_numbers", "off");
    apply_setting("effects", "heal_numbers", "on");

    Log("Loading settings\n");
    auto file = og::io::og_open_read("cfg/openglad.yaml", true);
    if(!file)
	{
		Log("Could not open config file. Using defaults.");
		return false;
	}

    og::io::YamlParser yaml;
    yaml.set_input(ogfile_read_handler, file.get());

    std::string last_scalar;
    std::string current_category;

    og::io::YamlParseResult parse_result;
    while((parse_result = yaml.parse_next()) == og::io::YamlParseResult::Ok)
    {
        const og::io::YamlEvent& ev = yaml.event();
        switch(ev.type)
        {
            case og::io::YamlEventType::BeginSequence:
                break;
            case og::io::YamlEventType::EndSequence:
                break;
            case og::io::YamlEventType::BeginMapping:
                current_category = last_scalar;
                break;
            case og::io::YamlEventType::EndMapping:
                break;
            case og::io::YamlEventType::Alias:
                break;
            case og::io::YamlEventType::Pair:
                apply_setting(current_category, ev.scalar, ev.value);
                break;
            case og::io::YamlEventType::Scalar:
                last_scalar = ev.scalar;
                break;
            default:
                break;
        }
    }

    if(parse_result == og::io::YamlParseResult::Error)
        LogError("Parsing error in config file.\n");

    yaml.close_input();

	return true;
}


bool cfg_store::save_settings()
{
    auto outfile = og::io::og_open_write("cfg/openglad.yaml");
    if(outfile != nullptr)
    {
        Log("Saving settings\n");

        og::io::YamlEmitter yaml;
        if (!yaml.set_output(ogfile_write_handler, outfile.get()))
        {
            LogError("Couldn't initialize YAML emitter for cfg/openglad.yaml.\n");
            return false;
        }

        // Each category is a mapping that holds setting/value pairs
        for(auto& [category, settings] : data)
        {
            if(category.size() > 0)
            {
                yaml.emit_scalar(category.c_str());
                yaml.emit_begin_mapping();
            }

            for(auto& [key, value] : settings)
            {
                yaml.emit_pair(key.c_str(), value.c_str());
            }

            if(category.size() > 0)
            {
                yaml.emit_end_mapping();
            }
        }

        yaml.close_output();

        return true;
    }
    else
    {
        LogError("Couldn't open cfg/openglad.yaml for writing.\n");
        return false;
    }
}

void cfg_store::commandline(int &argc, char **&argv)
{
	const char helpmsg[] =
"Usage: openglad [-d -f ...]\n"
"  -s		Turn sound on\n"
"  -S		Turn sound off\n"
"  -n		Run at 320x200 resolution\n"
"  -d		Double pixel size\n"
"  -e		Use eagle engine for pixel doubling\n"
"  -i		Use sai2x engine for pixel doubling\n"
"  -f		Use full screen\n"
"  -h		Print a summary of the options\n"
"  -v		Print the version number\n";

	const char versmsg[] = "openglad version " OPENGLAD_VERSION_STRING "\n";

	// Begin changes by David Storey (Deathifier)
	// FIX: Handle mutually exclusive arguments being used at the same time?
	// E.G. -s and -S

	// Iterate over arguments, ignoring the first (Program Name).
	for(int argnum = 1; argnum < argc; argnum++)
	{
		// Look for arguments of 2 chars only:
		if(argv[argnum][0] == '-' && std::string(argv[argnum]).size() == 2)
		{
			// To handle arguments which have aditional arguments attached
			// to them, take care of it within the case statement and
			// increment argnum appropriately.
			switch(argv[argnum][1])
			{
				case 'h':
					Log(helpmsg);
                    std::fflush(nullptr);
                    std::_Exit(0);
				case 'v':
					Log(versmsg);
                    std::fflush(nullptr);
                    std::_Exit(0);
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
					Log("Unknown argument {} ignored.", argv[argnum]);
			}
		}
	}

	// End Changes
}


bool cfg_store::is_on(const std::string& category, const std::string& setting)
{
    return get_setting(category, setting) == "on";
}
