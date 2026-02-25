/* Convenience helpers for the find-foes-in-range-and-iterate pattern.
 *
 * Every call site was previously spelling out:
 *   self->sim_level->find_foes_in_range(self->sim_level->oblist, range, &howmany, self);
 * These thin wrappers eliminate that boilerplate.
 */
#pragma once

#include <openglad/data/level_data.h>
#include <openglad/gameplay/gameplay_context.h>
#include <cstdint>

// Count foes within range of the given entity.
inline std::int32_t count_foes_in_range(walker* self, std::int32_t range)
{
    std::int32_t count = 0;
    og::gameplay::current_game->world->find_foes_in_range(og::gameplay::current_game->world->oblist, range, &count, self);
    return count;
}

// Call fn(walker*) for each non-null foe within range.  Returns total foe count.
template<typename Fn>
std::int32_t for_each_foe_in_range(walker* self, std::int32_t range, Fn&& fn)
{
    std::int32_t count = 0;
    auto foes = og::gameplay::current_game->world->find_foes_in_range(og::gameplay::current_game->world->oblist, range, &count, self);
    for (auto* w : foes)
        if (w) fn(w);
    return count;
}
