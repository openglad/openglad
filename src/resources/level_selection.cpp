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

// The earned-roads gate (docs/camp-controls-design.md): the frontier
// computation moved here from src/interface/ui/picker_accessible_levels.cpp
// (which now forwards) so the SDL-free terminal clients apply the same rule.

#include <openglad/resources/level_selection.h>

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/game_mode.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace og::data {

namespace {

// A resources-owned entity loader for the scan loads (the shared headless
// loader lives in the platform layer, out of reach from here). Graphics
// re-derive when the mounted sprite sources change, exactly like every
// other long-lived loader.
loader& scan_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

void wire_scan_world(GameWorld& world)
{
    scan_loader().reload_graphics_if_stale();
    loader* game_loader = &scan_loader();
    world.entity_factory = [game_loader](Order order, std::int32_t family) {
        return game_loader->create_walker_owned(order, family);
    };
    world.entity_configurator =
        [game_loader](walker& entity, Order order,
                      std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(),
                                         entity.family());
    };
    world.entity_derived_stats =
        [game_loader](walker* entity, Order order, std::int32_t family) {
            if (entity != nullptr)
                game_loader->set_derived_stats(entity, order, family);
        };
}

std::map<std::pair<std::string, int>, std::vector<int>>& exit_cache()
{
    static std::map<std::pair<std::string, int>, std::vector<int>> cache;
    return cache;
}

// Destinations of every FAMILY_EXIT treasure in one level of the mounted
// campaign, sorted and deduplicated — the getLevelStats exit scan, lifted
// here so the SDL-free clients reach it. A scan is a full .fss parse
// (walkers included), so results are memoized per (mounted campaign, level)
// until clear_level_exit_cache(); a level that will not load answers no
// exits. The load rides the same two scoped bindings LevelRuntimeData::load
// installs: current_game->world points at the world being built (the level
// scripts' spawn hooks resolve it) and load-time rolls come from the
// throwaway world's own rng, never the session's.
const std::vector<int>& level_exits(int level_id)
{
    auto& cache = exit_cache();
    const auto key = std::make_pair(get_mounted_campaign(), level_id);
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second;

    std::vector<int> exits;
    GameWorld world(0);
    world.id = static_cast<short>(level_id);
    wire_scan_world(world);

    GameplayContext* const context = current_game;
    GameWorld* const original_world =
        context != nullptr ? context->world : nullptr;
    if (context != nullptr)
        context->world = &world;
    IRandom* scan_rng = &world.rng_;
    const bool own_rng_override = gameplay_rng_override() == nullptr;
    if (own_rng_override)
        set_gameplay_rng_override(&scan_rng);

    LevelFileMetadata metadata;
    if (load_level(std::format("scen{}.fss", level_id), world, metadata))
    {
        for (auto& uptr : world.fxlist)
        {
            walker* ob = uptr.get();
            if (!ob)
                continue;
            if (ob->query_order() == Order::Treasure &&
                ob->family() == FAMILY_EXIT)
            {
                exits.push_back(ob->stats()->level());
            }
        }
        std::sort(exits.begin(), exits.end());
        exits.erase(std::unique(exits.begin(), exits.end()), exits.end());
    }

    if (own_rng_override)
        set_gameplay_rng_override(nullptr);
    if (context != nullptr)
        context->world = original_world;
    return cache.emplace(key, std::move(exits)).first->second;
}

} // namespace

// Get list of accessible levels (cleared levels + their exits)
std::vector<int> accessible_levels(const SaveData& save)
{
    std::set<int> accessible;
    std::set<int> to_process;

    // Start with the campaign's entry level (always accessible; the mounted
    // campaign.yaml names it — level 1 for classics, 300 for the modes
    // campaign, whose PROGRESS list used to show a phantom "1" row) and the
    // current level.
    const int first_level = campaign_first_level(get_mounted_campaign());
    accessible.insert(first_level);
    to_process.insert(first_level);

    // Add current level
    accessible.insert(save.scen_num);

    // Add all cleared levels
    const std::string& campaign = save.current_campaign;
    auto it = save.completed_levels.find(campaign);
    if (it != save.completed_levels.end()) {
        for (int level : it->second) {
            accessible.insert(level);
            to_process.insert(level);
        }
    }

    // For each cleared level, add its exits
    while (!to_process.empty()) {
        int level_id = *to_process.begin();
        to_process.erase(to_process.begin());

        if (save.is_level_completed(level_id)) {
            for (int exit_id : level_exits(level_id)) {
                if (accessible.find(exit_id) == accessible.end()) {
                    accessible.insert(exit_id);
                }
            }
        }
    }

    // Convert to sorted vector
    std::vector<int> result(accessible.begin(), accessible.end());
    std::sort(result.begin(), result.end());
    return result;
}

bool level_selection_gating_active(const SaveData& save)
{
    if (campaign_matchup(save.current_campaign) == "versus")
        return false;
    return og::mode::kind_for_mode_string(
               campaign_mode(save.current_campaign)) !=
           og::mode::ProgressionKind::Tower;
}

bool level_selection_allowed(const SaveData& save, int level)
{
    if (!level_selection_gating_active(save))
        return true;
    const std::vector<int> frontier = accessible_levels(save);
    return std::binary_search(frontier.begin(), frontier.end(), level);
}

void clear_level_exit_cache()
{
    exit_cache().clear();
}

} // namespace og::data
