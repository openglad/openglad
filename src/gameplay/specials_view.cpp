/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/families/specials_view.h>

#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstring>

namespace og::sim {

namespace {

bool slot_in_range(int slot) noexcept
{
    return slot >= 0 && slot < FD_NUM_SPECIALS;
}

}  // namespace

bool special_in_play(const FamilyDescriptor* fd, int slot,
                     short new_specials) noexcept
{
    if (fd == nullptr || !slot_in_range(slot))
        return true;
    return !fd->special_new_kit[slot] || new_specials != 0;
}

bool alternate_in_play(const FamilyDescriptor* fd, int slot,
                       short new_specials) noexcept
{
    if (!special_in_play(fd, slot, new_specials))
        return false;
    if (fd == nullptr || !slot_in_range(slot))
        return true;
    return !fd->alternate_new_kit[slot] || new_specials != 0;
}

const char* special_name(const FamilyDescriptor* fd, int slot,
                         short new_specials) noexcept
{
    if (fd == nullptr || !slot_in_range(slot) ||
        !special_in_play(fd, slot, new_specials))
        return kSpecialNameNone;
    return fd->special_names[slot];
}

const char* alternate_name(const FamilyDescriptor* fd, int slot,
                           short new_specials) noexcept
{
    if (fd == nullptr || !slot_in_range(slot) ||
        !alternate_in_play(fd, slot, new_specials))
        return kSpecialNameNone;
    return fd->alternate_names[slot];
}

unsigned short alternate_cost_in_play(const FamilyDescriptor* fd, int slot,
                                      short new_specials) noexcept
{
    if (fd == nullptr || !slot_in_range(slot) ||
        !alternate_in_play(fd, slot, new_specials))
        return 0;
    return fd->alternate_cost[slot];
}

int slot_open_level(const FamilyDescriptor* fd, int slot) noexcept
{
    if (fd != nullptr && slot_in_range(slot) && fd->special_level[slot] != 0)
        return fd->special_level[slot];
    return 3 * slot - 2;
}

bool slot_open(const FamilyDescriptor* fd, int slot, int level) noexcept
{
    return level >= slot_open_level(fd, slot);
}

int bot_slot_roll_bound(const FamilyDescriptor* fd, int level,
                        short new_specials) noexcept
{
    int bound = (level + 2) / 3;
    for (int slot = 1; slot < FD_NUM_SPECIALS; ++slot) {
        if (slot > bound && slot_open(fd, slot, level) &&
            std::strcmp(special_name(fd, slot, new_specials),
                        kSpecialNameNone) != 0)
            bound = slot;
    }
    return bound;
}

short session_new_specials() noexcept
{
    if (current_game == nullptr || current_game->world == nullptr)
        return 0;
    return current_game->world->new_specials;
}

short session_gameplay_fixes() noexcept
{
    if (current_game == nullptr || current_game->world == nullptr)
        return 0;
    return current_game->world->gameplay_fixes;
}

unsigned short cast_cost(const walker& w, int slot, bool shifted) noexcept
{
    const FamilyDescriptor* fd = get_family_descriptor(
        static_cast<int>(static_cast<unsigned char>(w.family())));
    const short ns = session_new_specials();
    if (!special_in_play(fd, slot, ns))
        return kSpecialCostDisabled;
    if ((w.kit_state() & (KIT_HIDDEN | KIT_CHANNEL)) != 0)
        return 0;
    if (shifted) {
        const unsigned short alternate = alternate_cost_in_play(fd, slot, ns);
        if (alternate > 0)
            return alternate;
    }
    if (w.stats() == nullptr)
        return kSpecialCostDisabled;
    return w.stats()->special_cost(slot);
}

bool cannot_afford_priced_alternate(const walker& w, int slot) noexcept
{
    const FamilyDescriptor* fd = get_family_descriptor(
        static_cast<int>(static_cast<unsigned char>(w.family())));
    const unsigned short price =
        alternate_cost_in_play(fd, slot, session_new_specials());
    if (price == 0 || w.stats() == nullptr)
        return false;
    return w.stats()->magicpoints() < static_cast<float>(price);
}

}  // namespace og::sim
