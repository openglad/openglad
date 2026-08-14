/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/script/campaign_hooks.h>

#include "script_internal.h"

#include <utility>

// The process-global campaign providers. A plain global (not per-VM): the
// providers describe the MACHINE's save surface, which outlives every VM
// rebuild, and the bindings read them only under the campaign-dispatch
// fence. The registrar/dispatch half lives in world_scripts.cpp beside the
// level-hook machinery it clones.

namespace og::script {

namespace {

hooks::CampaignProviders g_campaign_providers;

}  // namespace

const hooks::CampaignProviders& campaign_providers()
{
    return g_campaign_providers;
}

namespace hooks {

bool valid_campaign_var_name(std::string_view name)
{
    if (name.empty() || name.size() > static_cast<std::size_t>(kCampaignVarNameMax))
        return false;
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '_';
        if (!ok)
            return false;
    }
    return true;
}

void install_campaign_providers(CampaignProviders providers)
{
    g_campaign_providers = std::move(providers);
}

void clear_campaign_providers()
{
    g_campaign_providers = {};
}

}  // namespace hooks

}  // namespace og::script
