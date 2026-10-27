/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The session's keyboard pointed at a test-owned array (the HeldEditorKeys
// idiom), so a key is "down" exactly as SDL_GetKeyboardState would say it.
// Shared by the SPEC-key sampler tests (test_game_context.cpp) and the
// PAUSED menu's ABILITIES flows (test_pause_menu.cpp).

#include <SDL3/SDL.h>

#include <openglad/interface/native_input.h>
#include <openglad/interface/session_state.h>

#include <array>
#include <cstddef>

class FakeKeyboard
{
public:
    FakeKeyboard() : saved_(og::runtime::current_session->keystates_)
    {
        keys_.fill(false);
        og::runtime::current_session->keystates_ = keys_.data();
    }
    ~FakeKeyboard() { og::runtime::current_session->keystates_ = saved_; }
    FakeKeyboard(const FakeKeyboard&) = delete;
    FakeKeyboard& operator=(const FakeKeyboard&) = delete;

    // Holds or lets go of the key player `player` has bound to `slot`.
    // False when the slot has no key (nothing to press).
    bool set(int player, int slot, bool down)
    {
        const int keycode = og::runtime::current_session->player_keys_[player][slot];
        if (keycode == 0)
            return false;
        const int scancode = og::input_native::scancode_from_key(keycode);
        if (scancode <= 0 || scancode >= SDL_SCANCODE_COUNT)
            return false;
        keys_[static_cast<std::size_t>(scancode)] = down;
        return true;
    }

private:
    const bool* saved_;
    std::array<bool, SDL_SCANCODE_COUNT> keys_{};
};
