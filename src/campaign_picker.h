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
 
#ifndef _CAMPAIGN_PICKER_H__
#define _CAMPAIGN_PICKER_H__

#include "screen.h"
#include <map>
#include <string>

struct CampaignResult
{
    std::string id;
    int first_level;
    
    CampaignResult()
        : first_level(1)
    {}
};

CampaignResult pick_campaign(screen* screenp, SaveData* save_data, bool enable_delete = false);

int load_campaign(const std::string& campaign, std::map<std::string, int>& current_levels, int first_level = 1);

#endif
