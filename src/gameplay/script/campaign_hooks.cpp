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
    // rule: A campaign decision variable name is 1-32 characters of lowercase letters, digits and
    //   underscore; anything else is refused everywhere names are accepted (campaign hook vars,
    //   og.campaign_var writes, save and replay readers).
    // tests: CampaignHooksTest.bad_vars_are_load_errors
    // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
    // tests: SemCampaignHooks.an_empty_campaign_var_name_is_refused
    // tests: SemCampaignHooks.register_campaign_hooks_refuses_an_empty_var_name
    if (name.empty() || name.size() > static_cast<std::size_t>(kCampaignVarNameMax))
        return false;
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '_';
        // rule: A campaign decision variable name is 1-32 characters of lowercase letters, digits
        //   and underscore. Here: `!ok`.
        // tests: CampaignHooksTest.bad_vars_are_load_errors
        // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
        // tests: SemCampaignHooks.an_empty_campaign_var_name_is_refused
        // tests: SemCampaignHooks.register_campaign_hooks_refuses_an_empty_var_name
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

bool campaign_assign_set(int save_slot, int tag)
{
    // rule: Assigning a hero to a campaign set fails cleanly (false, nothing written) when no
    //   campaign save surface is installed; otherwise the provider's bounded answer is returned.
    // tests: CampaignHooksTest.assign_set_forwards_to_the_provider_and_refuses_without_one
    if (!g_campaign_providers.assign_set)
        return false;
    return g_campaign_providers.assign_set(save_slot, tag);
}

}  // namespace hooks

}  // namespace og::script
