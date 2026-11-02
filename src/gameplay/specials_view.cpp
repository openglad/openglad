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
    // rule: A class's New Specials kit slot exists only with New Specials on; classic slots are
    //   always in play (unknown family/slot is treated as in play).
    // tests: SpecialsView.off_equals_registry_before_the_feature
    // tests: SpecialsView.alternate_cost_gates_only_when_shifted_and_in_play
    // tests: SpecialsView.cycle_skips_hidden_new_kit_slots
    if (fd == nullptr || !slot_in_range(slot))
        return true;
    return !fd->special_new_kit[slot] || new_specials != 0;
}

bool alternate_in_play(const FamilyDescriptor* fd, int slot,
                       short new_specials) noexcept
{
    // rule: A shifted alternate exists only if its slot's special is in play and the alternate is
    //   either classic or New Specials is on.
    // tests: SpecialsView.alternate_cost_gates_only_when_shifted_and_in_play
    // tests: KitLuaTest.match_setting_and_alternate_down_follow_the_setting
    // tests: SpecialsView.off_view_matches_the_classic_six_literally
    if (!special_in_play(fd, slot, new_specials))
        return false;
    // rule: Part of the rule above that begins "A shifted alternate exists only if its slot's
    //   special ..."
    // tests: SpecialsView.alternate_cost_gates_only_when_shifted_and_in_play
    // tests: KitLuaTest.match_setting_and_alternate_down_follow_the_setting
    // tests: SpecialsView.off_view_matches_the_classic_six_literally
    if (fd == nullptr || !slot_in_range(slot))
        return true;
    return !fd->alternate_new_kit[slot] || new_specials != 0;
}

const char* special_name(const FamilyDescriptor* fd, int slot,
                         short new_specials) noexcept
{
    // rule: A special or alternate that is out of play has no name ('none'), which is what hides it
    //   from the HUD, the belt catalog and bot rolls.
    // tests: SpecialsView.off_equals_registry_before_the_feature
    // tests: SpecialsView.off_view_matches_the_classic_six_literally
    // tests: SpecialsView.alternate_cost_gates_only_when_shifted_and_in_play
    // tests: SemSpecialsView.a_bad_slot_or_no_descriptor_has_no_special_name
    if (fd == nullptr || !slot_in_range(slot) ||
        !special_in_play(fd, slot, new_specials))
        return kSpecialNameNone;
    return fd->special_names[slot];
}

const char* alternate_name(const FamilyDescriptor* fd, int slot,
                           short new_specials) noexcept
{
    // rule: Part of the rule above that begins "A special or alternate that is out of play ..."
    // tests: SpecialsView.off_equals_registry_before_the_feature
    // tests: SpecialsView.off_view_matches_the_classic_six_literally
    // tests: SpecialsView.alternate_cost_gates_only_when_shifted_and_in_play
    // tests: SemSpecialsView.a_bad_slot_or_no_descriptor_has_no_special_name
    if (fd == nullptr || !slot_in_range(slot) ||
        !alternate_in_play(fd, slot, new_specials))
        return kSpecialNameNone;
    return fd->alternate_names[slot];
}

unsigned short alternate_cost_in_play(const FamilyDescriptor* fd, int slot,
                                      short new_specials) noexcept
{
    // rule: A priced alternate has a price only while it is in play; out of play it costs 0
    //   (meaning 'use the slot's own price').
    // tests: SpecialsView.alternate_cost_gates_only_when_shifted_and_in_play
    // tests: KitThief.mine_is_named_and_priced_only_with_the_setting_on
    if (fd == nullptr || !slot_in_range(slot) ||
        !alternate_in_play(fd, slot, new_specials))
        return 0;
    return fd->alternate_cost[slot];
}

int slot_open_level(const FamilyDescriptor* fd, int slot) noexcept
{
    // rule: A special slot opens at the level its class declares, or at the classic 3n-2
    //   (1,4,7,10,13) when none is declared.
    // tests: SpecialsView.a_slot_opens_at_its_own_level_or_at_three_n_minus_two
    if (fd != nullptr && slot_in_range(slot) && fd->special_level[slot] != 0)
        return fd->special_level[slot];
    return 3 * slot - 2;
}

bool slot_open(const FamilyDescriptor* fd, int slot, int level) noexcept
{
    return level >= slot_open_level(fd, slot);
}

int bot_slot_roll_bound(const FamilyDescriptor* fd, int level,
                        short new_specials, short gameplay_fixes) noexcept
{
    int bound = (level + 2) / 3;
    int highest = 0;  // the highest named, in-play slot the level has opened
    // rule: A bot rolls its special among the first (level+2)/3 slots, raised to include any named
    //   slot its class opens early; classic: the bound may exceed the last named slot, so an
    //   out-of-range roll folds back to slot 1; Gameplay Fixes on: the bound stops at the highest
    //   named open slot.
    // tests: SpecialsView.the_bot_slot_roll_bound_is_the_classic_count_unless_a_row_opens_early
    // tests: SpecialsView.the_bot_slot_roll_bound_with_gameplay_fixes_on_stops_at_the_last_named_slot
    for (int slot = 1; slot < FD_NUM_SPECIALS; ++slot) {
        const bool named_open = slot_open(fd, slot, level) &&
            std::strcmp(special_name(fd, slot, new_specials),
                        kSpecialNameNone) != 0;
        // rule: Part of the rule above that begins "A bot rolls its special among the first
        //   (level+2)/3 ..."
        // tests: SpecialsView.the_bot_slot_roll_bound_is_the_classic_count_unless_a_row_opens_early
        // tests: SpecialsView.the_bot_slot_roll_bound_with_gameplay_fixes_on_stops_at_the_last_named_slot
        if (slot > bound && named_open)
            bound = slot;      // the classic raise (a row opened early)
        // rule: Part of the rule above that begins "A bot rolls its special among the first
        //   (level+2)/3 ..."
        // tests: SpecialsView.the_bot_slot_roll_bound_is_the_classic_count_unless_a_row_opens_early
        // tests: SpecialsView.the_bot_slot_roll_bound_with_gameplay_fixes_on_stops_at_the_last_named_slot
        if (named_open)
            highest = slot;
    }
    // Gameplay Fixes: never roll past the last named slot, so a roll is
    // never folded into slot 1 for being out of range. A level-0 bound
    // stays 0 (no draw), as before.
    // rule: Part of the rule above that begins "A bot rolls its special among the first (level+2)/3
    //   ..."
    // tests: SpecialsView.the_bot_slot_roll_bound_is_the_classic_count_unless_a_row_opens_early
    // tests: SpecialsView.the_bot_slot_roll_bound_with_gameplay_fixes_on_stops_at_the_last_named_slot
    if (gameplay_fixes != 0 && bound > highest)
        bound = highest;
    return bound;
}

short session_new_specials() noexcept
{
    // plumbing: Reads the current world's setting, 0 with no world.
    if (current_game == nullptr || current_game->world == nullptr)
        return 0;
    return current_game->world->new_specials;
}

short session_gameplay_fixes() noexcept
{
    // plumbing: Reads the current world's setting, 0 with no world.
    if (current_game == nullptr || current_game->world == nullptr)
        return 0;
    return current_game->world->gameplay_fixes;
}

unsigned short cast_cost(const walker& w, int slot, bool shifted) noexcept
{
    const FamilyDescriptor* fd = get_family_descriptor(
        static_cast<int>(static_cast<unsigned char>(w.family())));
    const short ns = session_new_specials();
    // rule: A special's MP price: an out-of-play slot is priced out (5000); a hidden or channelling
    //   walker's next press (pop up / release) is free; a shifted press of a priced alternate costs
    //   the alternate's price; otherwise the slot's own price.
    // tests: SpecialsView.alternate_cost_gates_only_when_shifted_and_in_play
    // tests: SpecialsView.off_equals_registry_before_the_feature
    // tests: LivingActFlow.the_act_path_never_reaches_an_unaffordable_slot
    // tests: KitSkeleton.second_press_pops_up_free_and_costs_nothing
    // unreachable: cast_cost's no-stats guard; every walker constructor builds stats_ and only the
    //   destructors reset it
    if (!special_in_play(fd, slot, ns))
        return kSpecialCostDisabled;
    // rule: Part of the rule above that begins "A special's MP price: an out-of-play slot is priced
    //   ..."
    // tests: SpecialsView.alternate_cost_gates_only_when_shifted_and_in_play
    // tests: SpecialsView.off_equals_registry_before_the_feature
    // tests: LivingActFlow.the_act_path_never_reaches_an_unaffordable_slot
    // tests: KitSkeleton.second_press_pops_up_free_and_costs_nothing
    if ((w.kit_state() & (KIT_HIDDEN | KIT_CHANNEL)) != 0)
        return 0;
    // rule: Part of the rule above that begins "A special's MP price: an out-of-play slot is priced
    //   ..."
    // tests: SpecialsView.alternate_cost_gates_only_when_shifted_and_in_play
    // tests: SpecialsView.off_equals_registry_before_the_feature
    // tests: LivingActFlow.the_act_path_never_reaches_an_unaffordable_slot
    // tests: KitSkeleton.second_press_pops_up_free_and_costs_nothing
    if (shifted) {
        const unsigned short alternate = alternate_cost_in_play(fd, slot, ns);
        // rule: Part of the rule above that begins "A special's MP price: an out-of-play slot is
        //   priced ..."
        // tests: SpecialsView.alternate_cost_gates_only_when_shifted_and_in_play
        // tests: SpecialsView.off_equals_registry_before_the_feature
        // tests: LivingActFlow.the_act_path_never_reaches_an_unaffordable_slot
        // tests: KitSkeleton.second_press_pops_up_free_and_costs_nothing
        if (alternate > 0)
            return alternate;
    }
    // rule: Part of the rule above that begins "A special's MP price: an out-of-play slot is priced
    //   ..."
    // tests: SpecialsView.alternate_cost_gates_only_when_shifted_and_in_play
    // tests: SpecialsView.off_equals_registry_before_the_feature
    // tests: LivingActFlow.the_act_path_never_reaches_an_unaffordable_slot
    // tests: KitSkeleton.second_press_pops_up_free_and_costs_nothing
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
    // rule: A bot that cannot pay for a priced alternate drops its shift (casts the primary); an
    //   unpriced alternate never drops the shift; with New Specials off nothing is priced so
    //   nothing is dropped.
    // tests: KitBots.unaffordable_priced_alternate_drops_the_shift_only_when_priced
    if (price == 0 || w.stats() == nullptr)
        return false;
    return w.stats()->magicpoints() < static_cast<float>(price);
}

}  // namespace og::sim
