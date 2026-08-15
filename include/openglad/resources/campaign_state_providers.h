/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Menu-time og.campaign_* provider glue (issue #206,
// docs/campaign-scripting-design.md "Menu-time bindings"). Builds a filled
// og::script::hooks::CampaignProviders over a SaveData so all three
// clients (SDL GameSession, text picker init, curses app session) and the
// unit fixtures share one implementation of the wallet/roster/state rules.
//
// The returned callbacks BORROW the SaveData: install them only for the
// lifetime of the surface that owns it, and clear_campaign_providers()
// before that SaveData dies.

#include <openglad/gameplay/script/campaign_hooks.h>

class SaveData;

namespace og::data {

og::script::hooks::CampaignProviders make_campaign_providers(SaveData& save);

} // namespace og::data
