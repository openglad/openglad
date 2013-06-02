#ifndef _CAMPAIGN_PICKER_H__
#define _CAMPAIGN_PICKER_H__

#include "screen.h"
#include <map>
#include <string>

void pick_campaign(screen *screenp);

int load_campaign(const std::string& old_campaign, const std::string& campaign, std::map<std::string, int>& current_levels);

#endif
