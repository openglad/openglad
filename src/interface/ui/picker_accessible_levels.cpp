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

#include <openglad/interface/ui/level_picker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/resources/level_io.h>
#include <openglad/resources/gloader.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/level_visuals.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/legacy/base.h>
#include <algorithm>
#include <format>
#include <list>
#include <set>
#include <vector>

#include <openglad/interface/screen.h>

// Get list of accessible levels (cleared levels + their exits)
std::vector<int> get_accessible_levels()
{
    screen* game = og::runtime::current_session->myscreen_;
    if (!game) {
        return {1};
    }

    std::set<int> accessible;
    std::set<int> to_process;

    // Start with level 1 (always accessible) and current level
    accessible.insert(1);
    to_process.insert(1);

    // Add current level
    accessible.insert(game->save_data.scen_num);

    // Add all cleared levels
    const std::string& campaign = game->save_data.current_campaign;
    auto it = game->save_data.completed_levels.find(campaign);
    if (it != game->save_data.completed_levels.end()) {
        for (int level : it->second) {
            accessible.insert(level);
            to_process.insert(level);
        }
    }

    // For each cleared level, add its exits
    while (!to_process.empty()) {
        int level_id = *to_process.begin();
        to_process.erase(to_process.begin());

        if (game->save_data.is_level_completed(level_id)) {
            og::gameplay::GameWorld world;
            world.id = level_id;
            world.myobmap = std::make_unique<obmap>();
            loader ldr;
            wire_loader_to_world(world, ldr, false);
            auto& bridge = og::interface::platform_bridge();
            if (bridge.clear_stale_view_controls)
                world.on_pre_delete_objects = [&bridge](og::gameplay::GameWorld* w) { bridge.clear_stale_view_controls(w); };
            LevelVisuals dummy_visuals;
            og::data::LevelFileMetadata meta;
            std::string thefile = std::format("scen{}.fss", level_id);
            if (og::data::load_level(thefile, world, dummy_visuals, meta)) {
                std::list<int> exits;
                getLevelStats(world, nullptr, nullptr, nullptr, nullptr, exits);
                for (int exit_id : exits) {
                    if (accessible.find(exit_id) == accessible.end()) {
                        accessible.insert(exit_id);
                    }
                }
            }
        }
    }

    // Convert to sorted vector
    std::vector<int> result(accessible.begin(), accessible.end());
    std::sort(result.begin(), result.end());
    return result;
}
