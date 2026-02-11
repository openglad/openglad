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
#include "level_picker.h"
#include "game_context.h"
#include <algorithm>
#include <set>
#include <vector>

// Get list of accessible levels (cleared levels + their exits)
std::vector<int> get_accessible_levels()
{
    screen* game = ctx().active_screen() ? ctx().active_screen() : myscreen;
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
            LevelData ld(level_id);
            if (ld.load()) {
                std::list<int> exits;
                getLevelStats(ld, nullptr, nullptr, nullptr, nullptr, exits);
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
