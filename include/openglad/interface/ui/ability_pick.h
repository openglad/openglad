/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The ABILITIES page's pick rule, shared by the SDL screen
// (abilities_screen.cpp) and the curses page (curses_picker_client.cpp):
// pick a slot, then an ability or another slot. SDL-free and header-only,
// so the zero-SDL terminal client links nothing new. The edits themselves
// are the model's (picker_common.h ability_place / ability_swap /
// ability_clear); this is only which one a pick means.
//
// The selection is one int: a belt slot 0..5, a bench row
// og::sim::belt::kSlots + bench index, or -1 for nothing.

#include <openglad/gameplay/families/belt.h>
#include <openglad/interface/ui/picker_common.h>

#include <cstddef>

struct FamilyDescriptor;

namespace og::ui {

enum class AbilityPickEdit : unsigned char {
    None,      // the pick only moved the selection
    Cleared,   // the selected slot picked again: emptied
    Swapped,   // two belt slots traded places
    Placed,    // a bench ability went onto a slot
};

struct AbilityPick {
    int selected = -1;  // the selection after the pick
    AbilityPickEdit edit = AbilityPickEdit::None;
};

// A belt slot picked. Nothing selected: it is selected. The same slot
// again: it is emptied. Another slot: the two swap. A bench ability: it
// goes onto this slot and what was there goes to the bench.
inline AbilityPick ability_pick_belt(og::sim::belt::Bytes& working,
                                     const FamilyDescriptor* fd,
                                     short new_specials,
                                     const AbilityPage& page, int selected,
                                     int belt_slot)
{
    namespace belt = og::sim::belt;
    if (selected == belt_slot) {
        ability_clear(working, belt_slot);
        return {-1, AbilityPickEdit::Cleared};
    }
    if (selected >= 0 && selected < belt::kSlots) {
        ability_swap(working, fd, new_specials, selected, belt_slot);
        return {-1, AbilityPickEdit::Swapped};
    }
    const int bench = selected - belt::kSlots;
    if (bench >= 0 && static_cast<std::size_t>(bench) < page.bench.size()) {
        ability_place(working, fd, new_specials, belt_slot,
                      *page.bench[static_cast<std::size_t>(bench)].entry);
        return {-1, AbilityPickEdit::Placed};
    }
    return {belt_slot, AbilityPickEdit::None};
}

// A bench ability picked. A belt slot selected: the ability goes there.
// Otherwise it is selected (picked again: let go), so a slot picked next
// takes it.
inline AbilityPick ability_pick_bench(og::sim::belt::Bytes& working,
                                      const FamilyDescriptor* fd,
                                      short new_specials,
                                      const AbilityPage& page, int selected,
                                      int bench_index)
{
    namespace belt = og::sim::belt;
    if (bench_index < 0 ||
        static_cast<std::size_t>(bench_index) >= page.bench.size())
        return {selected, AbilityPickEdit::None};
    if (selected >= 0 && selected < belt::kSlots) {
        ability_place(working, fd, new_specials, selected,
                      *page.bench[static_cast<std::size_t>(bench_index)].entry);
        return {-1, AbilityPickEdit::Placed};
    }
    const int mine = belt::kSlots + bench_index;
    return {selected == mine ? -1 : mine, AbilityPickEdit::None};
}

}  // namespace og::ui
