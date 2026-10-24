/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/belt_input.h>

#include <cstddef>

namespace og::sim::belt {

namespace {

constexpr int kBeltKeys = 3;

// The highest SPEC key (1..3) whose flag is set, or 0.
int highest(const bool (&flags)[kBeltKeys])
{
    for (int key = kBeltKeys; key >= 1; --key)
    {
        if (flags[key - 1])
            return key;
    }
    return 0;
}

void start(Latch& latch, int key, const Keys& keys, const Layout& belt)
{
    latch.active_key = key;
    latch.byte = cast_byte(
        belt[static_cast<std::size_t>(slot_index(key, keys.shift_held))]);
}

}  // namespace

Fold fold_belt_keys(const Keys& keys, const Layout& belt, Latch& latch)
{
    Fold fold;
    const int edge = highest(keys.pressed);
    if (edge != 0)
    {
        start(latch, edge, keys, belt);
        fold.special_pressed = true;
    }
    else if (latch.active_key != 0 && !keys.held[latch.active_key - 1])
    {
        const int next = highest(keys.held);
        if (next != 0)
        {
            start(latch, next, keys, belt);
            fold.special_pressed = true;
        }
        else
        {
            latch = Latch{};
        }
    }

    if (latch.active_key != 0)
    {
        fold.special_held = keys.held[latch.active_key - 1];
        fold.belt_cast = latch.byte;
    }
    return fold;
}

Fold fold_classic_keys(const Keys& keys, Latch& latch)
{
    latch = Latch{};
    Fold fold;
    fold.special_held = keys.held[0];
    fold.special_pressed = keys.pressed[0];
    fold.switch_held = keys.held[1];
    fold.switch_pressed = keys.pressed[1];
    return fold;
}

Fold fold_keys(const Keys& keys, bool ability_select, const Layout* belt,
               Latch& latch)
{
    if (!ability_select)
        return fold_classic_keys(keys, latch);
    if (belt == nullptr)
    {
        latch = Latch{};
        Fold fold;
        fold.special_held = keys.held[0];
        fold.special_pressed = keys.pressed[0];
        return fold;
    }
    return fold_belt_keys(keys, *belt, latch);
}

}  // namespace og::sim::belt
