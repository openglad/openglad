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

#pragma once

#include <list>
#include <string>

#include <openglad/resources/pixie_data.h>

class CampaignData
{
public:
    enum class IoError
    {
        None = 0,
        PackageMountFailed,
        OpenReadFailed,
        OpenWriteFailed,
        PackageUnpackFailed,
        PackageRepackFailed,
        ParseFailed
    };

    std::string id;
    std::string title;
    float rating;
    std::string version;
    std::string authors;
    std::string contributors;
    std::list<std::string> description;
    int suggested_power;
    int first_level;

    int num_levels;

    PixieData icondata;

    CampaignData(const std::string& campaign_id);
    ~CampaignData();

    bool load();
    bool save();
    bool save_as(const std::string& new_id);
    [[nodiscard]] IoError load_with_error();
    [[nodiscard]] IoError save_with_error();
    [[nodiscard]] IoError save_as_with_error(const std::string& new_id);
    [[nodiscard]] IoError last_io_error() const { return last_io_error_; }

    std::string get_description_line(int i);
    std::string getDescriptionLine(int i);

private:
    IoError last_io_error_ = IoError::None;
};
