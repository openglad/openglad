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

#include "graph.h"
#include "campaign_picker.h"
#include "util.h"

#include <list>
#include <string>

bool does_campaign_exist(const std::string& campaign_id)
{
    std::list<std::string> ls = list_campaigns();
    for(auto& s : ls)
    {
        if(campaign_id == s)
            return true;
    }

    return false;
}

bool create_new_campaign(const std::string& campaign_id)
{
    // Delete the temp directory
    cleanup_unpacked_campaign();

    // Create the necessities in the temp directory
    create_dir(get_user_path() + "temp/");
    create_dir(get_user_path() + "temp/pix");
    create_dir(get_user_path() + "temp/scen");
    create_dir(get_user_path() + "temp/sound");
    create_new_pix(get_user_path() + "temp/icon.pix", 32, 32);
    create_new_campaign_descriptor(get_user_path() + "temp/campaign.yaml");
    create_new_scen_file(get_user_path() + "temp/scen/scen1.fss", "scen0001");
    // Create the map file (grid)
    create_new_map_pix(get_user_path() + "temp/pix/scen0001.pix", 40, 60);

    bool result = repack_campaign(campaign_id);
    if(!result)
        return result;

    cleanup_unpacked_campaign();
    return true;
}

void importCampaignPicker()
{
    // TODO: Browse campaigns online and download some
}

void shareCampaign(screen* screenp)
{
    (void)screenp;
    // TODO: Send current campaign to the internets!
}
