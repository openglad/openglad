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
#include <string_view>
#include <set>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <format>
#include <openglad/resources/gparser.h>
#include <openglad/core/util.h>
#include <openglad/resources/og_file.h>

#include <yaml.h>

int toInt(const std::string& s);


cfg_store cfg;

namespace
{

int ogfile_read_handler(void* data, unsigned char* buffer, std::size_t size, std::size_t* size_read)
{
    auto* file = static_cast<og::io::OgFile*>(data);
    if(!file || !size_read)
        return 0;
    *size_read = file->read(buffer, 1, size);
    return 1;
}

int ogfile_write_handler(void* data, unsigned char* buffer, std::size_t size)
{
    auto* file = static_cast<og::io::OgFile*>(data);
    if(!file)
        return 0;
    return file->write(buffer, 1, size) == size ? 1 : 0;
}

std::string scalar_value(const yaml_node_t* node)
{
    if(!node || node->type != YAML_SCALAR_NODE)
        return {};
    const auto* value = reinterpret_cast<const char*>(node->data.scalar.value);
    const auto length = static_cast<std::size_t>(node->data.scalar.length);
    return value ? std::string(value, length) : std::string();
}

void apply_settings_mapping(cfg_store& store,
                            yaml_document_t& document,
                            const std::string& category,
                            yaml_node_t* mapping)
{
    if(!mapping || mapping->type != YAML_MAPPING_NODE)
        return;

    for(yaml_node_pair_t* pair = mapping->data.mapping.pairs.start;
        pair < mapping->data.mapping.pairs.top;
        ++pair)
    {
        yaml_node_t* key_node = yaml_document_get_node(&document, pair->key);
        yaml_node_t* value_node = yaml_document_get_node(&document, pair->value);
        if(!key_node || key_node->type != YAML_SCALAR_NODE ||
           !value_node || value_node->type != YAML_SCALAR_NODE)
            continue;

        store.apply_setting(category, scalar_value(key_node), scalar_value(value_node));
    }
}

void apply_settings_document(cfg_store& store, yaml_document_t& document)
{
    yaml_node_t* root = yaml_document_get_root_node(&document);
    if(!root || root->type != YAML_MAPPING_NODE)
        return;

    for(yaml_node_pair_t* pair = root->data.mapping.pairs.start;
        pair < root->data.mapping.pairs.top;
        ++pair)
    {
        yaml_node_t* key_node = yaml_document_get_node(&document, pair->key);
        yaml_node_t* value_node = yaml_document_get_node(&document, pair->value);
        if(!key_node || key_node->type != YAML_SCALAR_NODE || !value_node)
            continue;

        const std::string key = scalar_value(key_node);
        if(value_node->type == YAML_SCALAR_NODE)
            store.apply_setting("", key, scalar_value(value_node));
        else if(value_node->type == YAML_MAPPING_NODE)
            apply_settings_mapping(store, document, key, value_node);
    }
}

bool emit_event(yaml_emitter_t& emitter, yaml_event_t* event)
{
    return yaml_emitter_emit(&emitter, event) != 0;
}

bool emit_scalar(yaml_emitter_t& emitter, std::string_view value)
{
    yaml_event_t event;
    auto* text = reinterpret_cast<yaml_char_t*>(const_cast<char*>(value.data()));
    if(!yaml_scalar_event_initialize(
           &event,
           nullptr,
           nullptr,
           text,
           static_cast<int>(value.size()),
           1,
           1,
           YAML_ANY_SCALAR_STYLE))
        return false;

    return emit_event(emitter, &event);
}

bool emit_pair(yaml_emitter_t& emitter, std::string_view key, std::string_view value)
{
    return emit_scalar(emitter, key) && emit_scalar(emitter, value);
}

bool emit_mapping_start(yaml_emitter_t& emitter)
{
    yaml_event_t event;
    if(!yaml_mapping_start_event_initialize(&event, nullptr, nullptr, 1, YAML_ANY_MAPPING_STYLE))
        return false;
    return emit_event(emitter, &event);
}

bool emit_mapping_end(yaml_emitter_t& emitter)
{
    yaml_event_t event;
    if(!yaml_mapping_end_event_initialize(&event))
        return false;
    return emit_event(emitter, &event);
}

} // namespace

void cfg_store::apply_setting(const std::string& category, const std::string& setting, const std::string& value)
{
    data[category][setting] = value;
}

void cfg_store::apply_override(const std::string& category, const std::string& setting, const std::string& value)
{
    overrides[category][setting] = value;
}

std::string cfg_store::get_setting(const std::string& category, const std::string& setting)
{
	// Transient overrides win over persisted settings and are never saved.
	auto o1 = overrides.find(category);
	if(o1 != overrides.end())
	{
		auto o2 = o1->second.find(setting);
		if(o2 != o1->second.end())
			return o2->second;
	}

	auto a1 = data.find(category);
	if(a1 != data.end())
	{
		auto a2 = a1->second.find(setting);
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

    yaml_parser_t parser;
    std::memset(&parser, 0, sizeof(parser));
    if(!yaml_parser_initialize(&parser))
    {
        LogError("Couldn't initialize YAML parser for cfg/openglad.yaml.\n");
        return true;
    }

    yaml_parser_set_input(&parser, ogfile_read_handler, file.get());

    while(true)
    {
        yaml_document_t document;
        std::memset(&document, 0, sizeof(document));
        if(!yaml_parser_load(&parser, &document))
        {
            LogError("Parsing error in config file.\n");
            break;
        }

        yaml_node_t* root = yaml_document_get_root_node(&document);
        if(!root)
        {
            yaml_document_delete(&document);
            break;
        }

        apply_settings_document(*this, document);
        yaml_document_delete(&document);
    }

    yaml_parser_delete(&parser);

	return true;
}


bool cfg_store::save_settings()
{
    auto outfile = og::io::og_open_write("cfg/openglad.yaml");
    if(outfile != nullptr)
    {
        Log("Saving settings\n");

        yaml_emitter_t emitter;
        std::memset(&emitter, 0, sizeof(emitter));
        if (!yaml_emitter_initialize(&emitter))
        {
            LogError("Couldn't initialize YAML emitter for cfg/openglad.yaml.\n");
            return false;
        }

        yaml_emitter_set_encoding(&emitter, YAML_UTF8_ENCODING);
        yaml_emitter_set_output(&emitter, ogfile_write_handler, outfile.get());

        bool ok = yaml_emitter_open(&emitter) != 0;
        yaml_event_t event;
        if(ok && yaml_document_start_event_initialize(&event, nullptr, nullptr, nullptr, 0))
            ok = emit_event(emitter, &event);
        else
            ok = false;
        ok = ok && emit_mapping_start(emitter);

        // Each category is a mapping that holds setting/value pairs
        for(auto& [category, settings] : data)
        {
            if(!ok)
                break;

            if(category.size() > 0)
            {
                ok = emit_scalar(emitter, category) && emit_mapping_start(emitter);
            }

            for(auto& [key, value] : settings)
            {
                ok = ok && emit_pair(emitter, key, value);
            }

            if(category.size() > 0)
            {
                ok = ok && emit_mapping_end(emitter);
            }
        }

        ok = ok && emit_mapping_end(emitter);
        if(ok && yaml_document_end_event_initialize(&event, 1))
            ok = emit_event(emitter, &event);
        else
            ok = false;
        if(ok)
            ok = yaml_emitter_close(&emitter) != 0;

        yaml_emitter_delete(&emitter);
        if(!ok)
        {
            LogError("Couldn't write cfg/openglad.yaml.\n");
            return false;
        }

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
	static constexpr std::string_view helpmsg =
"Usage: openglad [-d -f ...]\n"
"  -s		Turn sound on\n"
"  -S		Turn sound off\n"
"  -n		Run at 320x200 resolution\n"
"  -d		Double pixel size\n"
"  -e		Use eagle engine for pixel doubling\n"
"  -i		Use sai2x engine for pixel doubling\n"
"  -f		Use full screen\n"
"  -h		Print a summary of the options\n"
"  -v		Print the version number\n"
"  --show-fps   Overlay render FPS in the top-right corner\n";

	static constexpr std::string_view versmsg = "openglad version " OPENGLAD_VERSION_STRING "\n";

	// Begin changes by David Storey (Deathifier)
	// FIX: Handle mutually exclusive arguments being used at the same time?
	// E.G. -s and -S

	// Iterate over arguments, ignoring the first (Program Name).
	for(int argnum = 1; argnum < argc; argnum++)
	{
		if (std::string(argv[argnum]) == "--show-fps")
		{
			// Developer overlay: ephemeral by design. Route through the override
			// map so it is honored at runtime but never written to the user's
			// persisted config by save_settings().
			apply_override("graphics", "show_fps", "on");
			Log("FPS overlay on.");
			continue;
		}
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
