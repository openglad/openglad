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

#include <cstdint>
#include <list>
#include <memory>

// Forward declarations
enum class Order : unsigned char;
class walker;
class LevelData;

namespace og::gameplay {

// GameWorld: owns entity lists and living_count.
//
// Phase 1a of the component architecture migration. Entity lists and
// living_count are moved here from LevelData. LevelData retains
// forwarding reference members so existing callers work unchanged.
//
// In later phases, spatial data, tick logic, RNG, and game state flags
// will also move here, eventually replacing both LevelData and SimWorld.
class GameWorld {
public:
    GameWorld() = default;
    ~GameWorld();

    // Non-copyable, non-moveable (entity lists contain unique_ptrs)
    GameWorld(const GameWorld&) = delete;
    GameWorld& operator=(const GameWorld&) = delete;
    GameWorld(GameWorld&&) = delete;
    GameWorld& operator=(GameWorld&&) = delete;

    // Entity storage (moved from LevelData)
    std::list<std::unique_ptr<walker>> oblist;
    std::list<std::unique_ptr<walker>> weaplist;
    std::list<std::unique_ptr<walker>> fxlist;
    std::list<std::unique_ptr<walker>> dead_list;
    int living_count = 0;  // Count of Order::Living entities only (was numobs)

    // Clear all entity lists and reset living_count.
    // Note: this clears only entity storage. Hooks (e.g. stale view control
    // cleanup) and obmap cleanup remain on LevelData::delete_objects().
    void delete_objects();

    // Count living foes not friendly to the given walker.
    short remaining_foes(walker* myguy) const;

    // Entity creation — delegates to LevelData::myloader temporarily.
    // This circular delegation (GameWorld -> LevelData -> loader) is ugly
    // but temporary: Phase 4 eliminates sim_* pointer wiring, and Phase 6
    // replaces the loader path entirely with an entity_factory callback.
    walker* add_ob(Order order, std::int32_t family, bool atstart = false);
    walker* add_fx_ob(Order order, std::int32_t family);
    walker* add_weap_ob(Order order, std::int32_t family);

    // Temporary back-pointer to LevelData for loader delegation.
    // Set by LevelData's constructor; cleared by its destructor.
    void set_level_data(LevelData* ld) { level_data_ = ld; }

private:
    LevelData* level_data_ = nullptr;
};

} // namespace og::gameplay
