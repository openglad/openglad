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
    // rule: When several SPEC keys are pressed (or held) in one input sample, the highest-numbered
    //   key wins.
    // tests: BeltInput.two_keys_down_in_one_sample_take_the_highest
    // tests: BeltInput.a_key_held_before_the_belt_saw_it_is_not_a_press
    // tests: BeltInput.press_edge_sets_special_pressed_and_latches_the_byte
    for (int key = kBeltKeys; key >= 1; --key)
    {
        // rule: Part of the rule above that begins "When several SPEC keys are pressed (or held) in
        //   ..."
        // tests: BeltInput.two_keys_down_in_one_sample_take_the_highest
        // tests: BeltInput.a_key_held_before_the_belt_saw_it_is_not_a_press
        // tests: BeltInput.press_edge_sets_special_pressed_and_latches_the_byte
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
    // rule: A SPEC key press latches that key's ability (SHIFT read at the press, not later) and is
    //   a special press; releasing the active key while another SPEC key is still held promotes
    //   that key as a fresh press; releasing with nothing held clears the latch; a key already held
    //   before the belt saw it is not a press.
    // tests: BeltInput.press_edge_sets_special_pressed_and_latches_the_byte
    // tests: BeltInput.shift_at_press_selects_the_layer_and_a_shift_change_mid_hold_does_not
    // tests: BeltInput.release_of_the_active_key_promotes_the_other_held_key_as_a_press
    // tests: BeltInput.a_key_held_before_the_belt_saw_it_is_not_a_press
    // tests: BeltInput.second_key_during_a_hold_is_a_press_not_a_hold
    if (edge != 0)
    {
        start(latch, edge, keys, belt);
        fold.special_pressed = true;
    }
    else if (latch.active_key != 0 && !keys.held[latch.active_key - 1])
    {
        const int next = highest(keys.held);
        // rule: Part of the rule above that begins "A SPEC key press latches that key's ability
        //   (SHIFT ..."
        // tests: BeltInput.press_edge_sets_special_pressed_and_latches_the_byte
        // tests: BeltInput.shift_at_press_selects_the_layer_and_a_shift_change_mid_hold_does_not
        // tests: BeltInput.release_of_the_active_key_promotes_the_other_held_key_as_a_press
        // tests: BeltInput.a_key_held_before_the_belt_saw_it_is_not_a_press
        // tests: BeltInput.second_key_during_a_hold_is_a_press_not_a_hold
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

    // rule: While a belt key is latched, the special is held exactly as long as that key is held
    //   and the cast byte stays the latched one (a tap inside one sample still sends its press and
    //   byte).
    // tests: BeltInput.a_tap_inside_one_sample_still_sends_its_press_and_byte
    // tests: BeltInput.press_edge_sets_special_pressed_and_latches_the_byte
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
    // rule: With New Ability Select off, SPEC key 1 is the classic special and key 2 is SWITCH
    //   SPECIAL (key 3 ignored); with it on but no controlled walker, key 1 is the classic special
    //   and nothing else; with it on and a walker, the belt fold drives specials and SWITCH SPECIAL
    //   is never sent.
    // tests: BeltInput.off_fold_maps_slot_17_to_switch_special_and_ignores_18
    // tests: BeltInput.on_without_a_walker_is_the_classic_special_and_nothing_else
    // tests: BeltInput.on_with_a_walker_is_the_belt_fold, BeltInput.on_fold_clears_switch_special
    if (!ability_select)
        return fold_classic_keys(keys, latch);
    // rule: Part of the rule above that begins "With New Ability Select off, SPEC key 1 is ..."
    // tests: BeltInput.off_fold_maps_slot_17_to_switch_special_and_ignores_18
    // tests: BeltInput.on_without_a_walker_is_the_classic_special_and_nothing_else
    // tests: BeltInput.on_with_a_walker_is_the_belt_fold, BeltInput.on_fold_clears_switch_special
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
