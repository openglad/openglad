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

#include <openglad/resources/campaign_data.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/yaml_stream.h>
#include <openglad/resources/ogfile_yaml.h>
#include <openglad/legacy/base.h>
#include <openglad/core/util.h>

#include <format>
#include <functional>
#include <string>

int toInt(const std::string& s);

namespace {
class ScopeGuard
{
public:
    explicit ScopeGuard(std::function<void()> fn) : fn_(std::move(fn)) {}
    ~ScopeGuard()
    {
        if (active_)
            fn_();
    }

    void dismiss() { active_ = false; }

private:
    std::function<void()> fn_;
    bool active_ = true;
};
} // namespace

CampaignData::CampaignData(const std::string& campaign_id)
    : id(campaign_id), title("New Campaign"), rating(0.0f), version("1.0"), suggested_power(0), first_level(1), num_levels(0)
{
    description.push_back("No description.");
}

CampaignData::~CampaignData()
{
	icondata.free();
}


bool CampaignData::load()
{
    last_io_error_ = IoError::None;
    std::string old_campaign = get_mounted_campaign();
    (void)unmount_campaign_package_with_error(old_campaign);

    // Load the campaign data from <user_data>/scen/<id>.glad
    if(mount_campaign_package_with_error(id) == CampaignPackageIoError::None)
    {
        auto file = og::io::og_open_read("campaign.yaml", true);
        if(!file)
        {
            last_io_error_ = IoError::OpenReadFailed;
            (void)unmount_campaign_package_with_error(id);
            (void)mount_campaign_package_with_error(old_campaign);
            return false;
        }

        og::io::YamlParser yaml;
        yaml.set_input(ogfile_read_handler, file.get());

        auto parse_result = og::io::YamlParseResult::Ok;
        while((parse_result = yaml.parse_next()) == og::io::YamlParseResult::Ok)
        {
            const og::io::YamlEvent& ev = yaml.event();
            switch(ev.type)
            {
                case og::io::YamlEventType::Pair:
                    if(ev.scalar == "title")
                        title = ev.value;
                    else if(ev.scalar == "version")
                        version = ev.value;
                    else if(ev.scalar == "authors")
                        authors = ev.value;
                    else if(ev.scalar == "contributors")
                        contributors = ev.value;
                    else if(ev.scalar == "description")
                    {
                        std::string desc = ev.value;
                        description = explode(desc, '\n');
                    }
                    else if(ev.scalar == "suggested_power")
                        suggested_power = toInt(ev.value);
                    else if(ev.scalar == "first_level")
                        first_level = toInt(ev.value);
                break;
                default:
                    break;
            }
        }
        if(parse_result == og::io::YamlParseResult::Error)
            last_io_error_ = IoError::ParseFailed;

        yaml.close_input();

        // TODO: Get rating from website
        rating = 0.0f;

        icondata = read_pixie_file("icon.pix");

        // Count the number of levels
        std::list<int> levels = list_levels();
        num_levels = static_cast<int>(levels.size());

        (void)unmount_campaign_package_with_error(id);
    }
    else
    {
        last_io_error_ = IoError::PackageMountFailed;
    }

    (void)mount_campaign_package_with_error(old_campaign);

    return (last_io_error_ == IoError::None);
}

bool CampaignData::save()
{
    last_io_error_ = IoError::None;
    cleanup_unpacked_campaign();
    ScopeGuard cleanup_guard([]() { cleanup_unpacked_campaign(); });

    bool result = true;
    if(unpack_campaign(id))
    {
        // Unmount campaign while it is changed
        //unmount_campaign_package(ascreen->current_campaign);

        auto outfile = og::io::og_open_write("temp/campaign.yaml");
        if(outfile)
        {
            og::io::YamlEmitter yaml;
            if (!yaml.set_output(ogfile_write_handler, outfile.get()))
            {
                LogError("Couldn't initialize YAML emitter for temp/campaign.yaml.\n");
                last_io_error_ = IoError::OpenWriteFailed;
                return false;
            }

            yaml.emit_pair("format_version", "1");
            yaml.emit_pair("title", title.c_str());
            yaml.emit_pair("version", version.c_str());

            std::string buf = std::format("{}", first_level);
            yaml.emit_pair("first_level", buf.c_str());

            buf = std::format("{}", suggested_power);
            yaml.emit_pair("suggested_power", buf.c_str());

            yaml.emit_pair("authors", authors.c_str());
            yaml.emit_pair("contributors", contributors.c_str());

            std::string desc;
            std::size_t desc_len = 0;
            for(const auto& line : description)
                desc_len += line.size() + 1;
            if(desc_len > 0)
                --desc_len;
            desc.reserve(desc_len);
            for(auto e = description.begin(); e != description.end();)
            {
                desc += *e;
                e++;
                if(e != description.end())
                    desc += '\n';
            }

            yaml.emit_pair("description", desc.c_str());

            yaml.close_output();
        }
        else
        {
            Log("Couldn't open temp/campaign.yaml for writing.\n");
            result = false;
            last_io_error_ = IoError::OpenWriteFailed;
        }

        if(result)
        {
            if(repack_campaign(id))
            {
                Log("Campaign saved.\n");
            }
            else
            {
                LogError("campaign_save_failed id={} reason=repack_failed\n", id);
                result = false;
                last_io_error_ = IoError::PackageRepackFailed;
            }
        }
    }
    else
    {
        LogError("campaign_save_failed id={} reason=unpack_failed\n", id);
        result = false;
        last_io_error_ = IoError::PackageUnpackFailed;
    }

    if(result)
        last_io_error_ = IoError::None;
    return result;
}

bool CampaignData::save_as(const std::string& new_id)
{
    last_io_error_ = IoError::None;
    cleanup_unpacked_campaign();
    ScopeGuard cleanup_guard([]() { cleanup_unpacked_campaign(); });

    bool result = true;
    // Unpack the campaign
    if(unpack_campaign(id))
    {
        // Save the descriptor file
        auto outfile = og::io::og_open_write("temp/campaign.yaml");
        if(outfile)
        {
            og::io::YamlEmitter yaml;
            if (!yaml.set_output(ogfile_write_handler, outfile.get()))
            {
                LogError("Couldn't initialize YAML emitter for temp/campaign.yaml.\n");
                result = false;
                last_io_error_ = IoError::OpenWriteFailed;
            }

            if (result)
            {
                yaml.emit_pair("format_version", "1");
                yaml.emit_pair("title", title.c_str());
                yaml.emit_pair("version", version.c_str());

                std::string buf = std::format("{}", first_level);
                yaml.emit_pair("first_level", buf.c_str());

                buf = std::format("{}", suggested_power);
                yaml.emit_pair("suggested_power", buf.c_str());

                yaml.emit_pair("authors", authors.c_str());
                yaml.emit_pair("contributors", contributors.c_str());

                std::string desc;
                std::size_t desc_len = 0;
                for(const auto& line : description)
                    desc_len += line.size() + 1;
                if(desc_len > 0)
                    --desc_len;
                desc.reserve(desc_len);
                for(auto e = description.begin(); e != description.end();)
                {
                    desc += *e;
                    e++;
                    if(e != description.end())
                        desc += '\n';
                }

                yaml.emit_pair("description", desc.c_str());

                yaml.close_output();
            }
        }
        else
        {
            Log("Couldn't open temp/campaign.yaml for writing.\n");
            result = false;
            last_io_error_ = IoError::OpenWriteFailed;
        }

        // Repack the campaign
        if(result)
        {
            if(repack_campaign(new_id))
            {
                // Success!
                id = new_id;
                Log("Campaign saved.\n");
            }
            else
            {
                LogError("campaign_save_as_failed src_id={} dst_id={} reason=repack_failed\n", id, new_id);
                result = false;
                last_io_error_ = IoError::PackageRepackFailed;
            }
        }
    }
    else
    {
        LogError("campaign_save_as_failed src_id={} dst_id={} reason=unpack_failed\n", id, new_id);
        result = false;
        last_io_error_ = IoError::PackageUnpackFailed;
    }

    if(result)
        last_io_error_ = IoError::None;
    return result;
}

CampaignData::IoError CampaignData::load_with_error()
{
    load();
    return last_io_error_;
}

CampaignData::IoError CampaignData::save_with_error()
{
    save();
    return last_io_error_;
}

CampaignData::IoError CampaignData::save_as_with_error(const std::string& new_id)
{
    save_as(new_id);
    return last_io_error_;
}

std::string CampaignData::getDescriptionLine(int i)
{
    return get_description_line(i);
}

std::string CampaignData::get_description_line(int i)
{
    if(i < 0 || i >= int(description.size()))
        return "";

    std::list<std::string>::iterator e = description.begin();
    while(i > 0)
    {
        e++;
        i--;
    }

    return *e;
}
