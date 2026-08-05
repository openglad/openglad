/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/resources/campaign_yaml.h>

#include <openglad/core/util.h>
#include <openglad/resources/og_file.h>

#include <yaml.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace og::data {

namespace {

enum class ContainerType {
    Mapping,
    Sequence,
};

std::string read_all(og::io::OgFile& file)
{
    std::string result;
    char buffer[4096];
    while(true)
    {
        const std::size_t read = file.read(buffer, 1, sizeof(buffer));
        if(read == 0)
            break;
        result.append(buffer, read);
    }
    return result;
}

int ogfile_write_handler(void* data, unsigned char* buffer, std::size_t size)
{
    auto* file = static_cast<og::io::OgFile*>(data);
    if(!file)
        return 0;
    return file->write(buffer, 1, size) == size ? 1 : 0;
}

std::string scalar_value(const yaml_event_t& event)
{
    const auto* value = reinterpret_cast<const char*>(event.data.scalar.value);
    const auto length = static_cast<std::size_t>(event.data.scalar.length);
    return value ? std::string(value, length) : std::string();
}

void log_yaml_error(const yaml_parser_t& parser, const char* path)
{
    LogError(
        "YAML error in {}: {} {}\n",
        path ? path : "(unknown)",
        parser.context ? parser.context : "",
        parser.problem ? parser.problem : "");
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

void apply_campaign_pair(CampaignYaml& out, const std::string& key, const std::string& value)
{
    if(key == "title")
    {
        out.title = value;
        out.saw_title = true;
    }
    else if(key == "version")
    {
        out.version = value;
        out.saw_version = true;
    }
    else if(key == "authors")
    {
        out.authors = value;
        out.saw_authors = true;
    }
    else if(key == "contributors")
    {
        out.contributors = value;
        out.saw_contributors = true;
    }
    else if(key == "description")
    {
        out.description = value;
        out.saw_description = true;
    }
    else if(key == "suggested_power")
    {
        out.suggested_power = parse_int_strict(value).value_or(0);
        out.saw_suggested_power = true;
    }
    else if(key == "first_level")
    {
        out.first_level = parse_int_strict(value).value_or(0);
        out.saw_first_level = true;
    }
    else if(key == "mode")
    {
        out.mode = value;
        out.saw_mode = true;
    }
    else if(key == "matchup")
    {
        out.matchup = value;
        out.saw_matchup = true;
    }
}

std::string_view trim_ascii(std::string_view value)
{
    while(!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while(!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return value;
}

bool simple_key(std::string_view key)
{
    if(key.empty())
        return false;
    for(char c : key)
    {
        const bool ok = (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') ||
                        c == '_' ||
                        c == '-';
        if(!ok)
            return false;
    }
    return true;
}

void preharvest_simple_campaign_pairs(std::string_view text, CampaignYaml& out)
{
    while(!text.empty())
    {
        std::size_t end = text.find('\n');
        std::string_view line = end == std::string_view::npos ? text : text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);

        if(!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if(line.empty() || line == "---" || line.front() == '#')
            continue;
        if(line.front() == '\t')
            return;
        if(line.front() == ' ')
            continue;

        const std::size_t colon = line.find(':');
        if(colon == std::string_view::npos)
            continue;

        const std::string_view key = trim_ascii(line.substr(0, colon));
        std::string_view value = trim_ascii(line.substr(colon + 1));
        if(!simple_key(key))
            continue;
        if(value == "|" || value == ">" || value.starts_with('|') || value.starts_with('>'))
            return;

        if((value.size() >= 2 && value.front() == '"' && value.back() == '"') ||
           (value.size() >= 2 && value.front() == '\'' && value.back() == '\''))
        {
            value.remove_prefix(1);
            value.remove_suffix(1);
        }

        apply_campaign_pair(out, std::string(key), std::string(value));
    }
}

} // namespace

CampaignYamlReadResult read_campaign_yaml(const char* path, CampaignYaml& out, bool debug)
{
    auto file = og::io::og_open_read(path, debug);
    if(!file)
        return CampaignYamlReadResult::OpenFailed;

    const std::string yaml_text = read_all(*file);
    preharvest_simple_campaign_pairs(yaml_text, out);

    yaml_parser_t parser;
    std::memset(&parser, 0, sizeof(parser));
    if(!yaml_parser_initialize(&parser))
        return CampaignYamlReadResult::ParseFailed;

    yaml_parser_set_input_string(
        &parser,
        reinterpret_cast<const unsigned char*>(yaml_text.data()),
        yaml_text.size());

    std::vector<ContainerType> containers;
    std::string pending_key;
    bool have_pending_key = false;

    while(true)
    {
        yaml_event_t event;
        std::memset(&event, 0, sizeof(event));
        if(!yaml_parser_parse(&parser, &event))
        {
            log_yaml_error(parser, path);
            yaml_parser_delete(&parser);
            return CampaignYamlReadResult::ParseFailed;
        }

        const yaml_event_type_t type = event.type;
        if(type == YAML_STREAM_END_EVENT)
        {
            yaml_event_delete(&event);
            break;
        }

        switch(type)
        {
            case YAML_MAPPING_START_EVENT:
                if(have_pending_key)
                    have_pending_key = false;
                containers.push_back(ContainerType::Mapping);
                break;

            case YAML_MAPPING_END_EVENT:
                if(!containers.empty() && containers.back() == ContainerType::Mapping)
                    containers.pop_back();
                have_pending_key = false;
                break;

            case YAML_SEQUENCE_START_EVENT:
                if(have_pending_key)
                    have_pending_key = false;
                containers.push_back(ContainerType::Sequence);
                break;

            case YAML_SEQUENCE_END_EVENT:
                if(!containers.empty() && containers.back() == ContainerType::Sequence)
                    containers.pop_back();
                break;

            case YAML_SCALAR_EVENT:
                if(!containers.empty() && containers.back() == ContainerType::Mapping)
                {
                    if(!have_pending_key)
                    {
                        pending_key = scalar_value(event);
                        have_pending_key = true;
                    }
                    else
                    {
                        apply_campaign_pair(out, pending_key, scalar_value(event));
                        have_pending_key = false;
                    }
                }
                break;

            case YAML_ALIAS_EVENT:
                if(have_pending_key)
                    have_pending_key = false;
                break;

            default:
                break;
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    return CampaignYamlReadResult::Ok;
}

CampaignYamlWriteResult write_campaign_yaml_with_result(const char* path, const CampaignYaml& data)
{
    auto file = og::io::og_open_write(path);
    if(!file)
        return CampaignYamlWriteResult::OpenFailed;

    yaml_emitter_t emitter;
    std::memset(&emitter, 0, sizeof(emitter));
    if(!yaml_emitter_initialize(&emitter))
        return CampaignYamlWriteResult::WriteFailed;

    yaml_emitter_set_encoding(&emitter, YAML_UTF8_ENCODING);
    yaml_emitter_set_output(&emitter, ogfile_write_handler, file.get());

    bool ok = yaml_emitter_open(&emitter) != 0;
    yaml_event_t event;

    if(ok && yaml_document_start_event_initialize(&event, nullptr, nullptr, nullptr, 0))
        ok = emit_event(emitter, &event);
    else
        ok = false;

    if(ok && yaml_mapping_start_event_initialize(&event, nullptr, nullptr, 1, YAML_ANY_MAPPING_STYLE))
        ok = emit_event(emitter, &event);
    else
        ok = false;

    ok = ok && emit_pair(emitter, "format_version", "1");
    ok = ok && emit_pair(emitter, "title", data.title);
    ok = ok && emit_pair(emitter, "version", data.version);
    ok = ok && emit_pair(emitter, "first_level", std::to_string(data.first_level));
    ok = ok && emit_pair(emitter, "suggested_power", std::to_string(data.suggested_power));
    ok = ok && emit_pair(emitter, "authors", data.authors);
    ok = ok && emit_pair(emitter, "contributors", data.contributors);
    ok = ok && emit_pair(emitter, "description", data.description);
    // Emit `mode` ONLY when set: keeps every existing campaign repack
    // byte-stable (LOAD-BEARING — pinned by a writer unit test).
    if(!data.mode.empty())
        ok = ok && emit_pair(emitter, "mode", data.mode);
    // `matchup` follows the same only-when-non-empty rule (and sits right
    // after `mode`), so coop repacks stay byte-stable too.
    if(!data.matchup.empty())
        ok = ok && emit_pair(emitter, "matchup", data.matchup);

    if(ok && yaml_mapping_end_event_initialize(&event))
        ok = emit_event(emitter, &event);
    else
        ok = false;

    if(ok && yaml_document_end_event_initialize(&event, 1))
        ok = emit_event(emitter, &event);
    else
        ok = false;

    if(ok)
        ok = yaml_emitter_close(&emitter) != 0;

    yaml_emitter_delete(&emitter);
    return ok ? CampaignYamlWriteResult::Ok : CampaignYamlWriteResult::WriteFailed;
}

CampaignYamlWriteResult write_default_campaign_yaml_with_result(const char* path)
{
    CampaignYaml data;
    data.title = "New Campaign";
    data.version = "1";
    data.first_level = 1;
    data.suggested_power = 0;
    data.authors = "";
    data.contributors = "";
    data.description = "A new campaign.";
    return write_campaign_yaml_with_result(path, data);
}

bool write_campaign_yaml(const char* path, const CampaignYaml& data)
{
    return write_campaign_yaml_with_result(path, data) == CampaignYamlWriteResult::Ok;
}

bool write_default_campaign_yaml(const char* path)
{
    return write_default_campaign_yaml_with_result(path) == CampaignYamlWriteResult::Ok;
}

} // namespace og::data
