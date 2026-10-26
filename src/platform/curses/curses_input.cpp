/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Translates Kitty-protocol terminal key events into the engine's per-frame
 * InputState using the game's own keybindings. A terminal key becomes a SDL
 * KEYCODE_* value, then is resolved to an InputAction by looking it up in the
 * player's bound keys (current_session->player_keys_[0]) — exactly the table the
 * SDL client builds from the config. Nothing about which key does what is
 * hardcoded here.
 *
 * Because the protocol delivers real press/release events, "held" is exact: a
 * Press marks an action held (and pressed this frame), a Release clears it.
 */
#include <openglad/platform/curses/curses_input.h>

#include <openglad/interface/input.h>          // KEYCODE_* values
#include <openglad/interface/session_state.h>  // current_session->player_keys_
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

namespace og::curses {

// player_keys_[0] holds NUM_KEYS entries: the first kInputActionCount (16) are
// InputActions; then KEY_LOOKUP, a render-time hold for the SDL client's
// upper-floor ghost overlay (the curses client renders only the followed
// walker's floor, so it sends nothing), and the belt keys SPEC 2 / SPEC 3,
// which sample() folds into the Special bit and the belt byte.
CursesInput::CursesInput()
    : keybindings_(std::span<const int, NUM_KEYS>(
          og::runtime::current_session->player_keys_[0],
          static_cast<std::size_t>(NUM_KEYS)))
{
}

CursesInput::CursesInput(std::span<const int, NUM_KEYS> keybindings) : keybindings_(keybindings) {}

int CursesInput::keycode_for_key(const Key& k)
{
    switch (k.code) {
    case KeyCode::Up:        return KEYCODE_UP;
    case KeyCode::Down:      return KEYCODE_DOWN;
    case KeyCode::Left:      return KEYCODE_LEFT;
    case KeyCode::Right:     return KEYCODE_RIGHT;
    case KeyCode::Enter:     return KEYCODE_RETURN;
    case KeyCode::Tab:       return KEYCODE_TAB;
    case KeyCode::Backspace: return KEYCODE_BACKSPACE;
    case KeyCode::Escape:    return KEYCODE_ESCAPE;
    case KeyCode::Home:      return KEYCODE_HOME;
    case KeyCode::End:       return KEYCODE_END;
    case KeyCode::Delete:    return KEYCODE_DELETE;
    // Modifier keys are real bindings (default Fire = LeftCtrl, Special = LeftAlt,
    // Shifter = LeftShift) — the Kitty protocol is what makes them deliverable.
    case KeyCode::LeftCtrl:   return KEYCODE_LCTRL;
    case KeyCode::LeftAlt:    return KEYCODE_LALT;
    case KeyCode::LeftShift:  return KEYCODE_LSHIFT;
    case KeyCode::RightCtrl:  return KEYCODE_RCTRL;
    case KeyCode::RightAlt:   return KEYCODE_RALT;
    case KeyCode::RightShift: return KEYCODE_RSHIFT;
    case KeyCode::F1:  return KEYCODE_F1;
    case KeyCode::F2:  return KEYCODE_F2;
    case KeyCode::F3:  return KEYCODE_F3;
    case KeyCode::F4:  return KEYCODE_F4;
    case KeyCode::F5:  return KEYCODE_F5;
    case KeyCode::F6:  return KEYCODE_F6;
    case KeyCode::F7:  return KEYCODE_F7;
    case KeyCode::F8:  return KEYCODE_F8;
    case KeyCode::F9:  return KEYCODE_F9;
    case KeyCode::F10: return KEYCODE_F10;
    case KeyCode::F11: return KEYCODE_F11;
    case KeyCode::F12: return KEYCODE_F12;
    case KeyCode::Char: {
        char32_t c = k.ch;
        // SDL keycodes for printable keys are the (lowercase) ASCII value.
        if (c >= U'A' && c <= U'Z')
            c = c + 32;
        if (c < 0x80)
            return static_cast<int>(c);
        return KEYCODE_UNKNOWN;
    }
    case KeyCode::None:
    case KeyCode::Resize:
    case KeyCode::FocusIn:
    case KeyCode::FocusOut:
    case KeyCode::PageUp:
    case KeyCode::PageDown:
    case KeyCode::Insert:
    case KeyCode::LeftSuper:
    case KeyCode::RightSuper:
    default:
        return KEYCODE_UNKNOWN;
    }
}

MetaAction CursesInput::meta_for_key(const Key& k)
{
    // Esc is the universal in-game menu / quit key (matching the SDL client's
    // Esc-opens-exit-prompt). It is intentionally NOT a player binding.
    if (k.code == KeyCode::Escape)
        return MetaAction::OpenGameMenu;
    return MetaAction::None;
}

void CursesInput::feed(const Key& k)
{
    // Losing window focus while a key is down would otherwise leave it stuck held
    // (the release goes to whatever has focus now). Drop everything on focus-out.
    if (k.code == KeyCode::FocusOut) {
        held_.fill(false);
        return;
    }
    const int keycode = keycode_for_key(k);
    if (keycode == KEYCODE_UNKNOWN)
        return;

    // Resolve the key to the slot(s) it is bound to for player 0.
    for (int a = 0; a < NUM_KEYS; ++a) {
        const auto idx = static_cast<std::size_t>(a);
        if (keybindings_[idx] != keycode)
            continue;
        switch (k.event) {
        case KeyEvent::Press:
            pressed_edge_[idx] = true; // down transition this frame
            held_[idx] = true;
            break;
        case KeyEvent::Repeat:
            held_[idx] = true; // still down (auto-repeat), not a fresh press
            break;
        case KeyEvent::Release:
            held_[idx] = false;
            break;
        }
    }
}

InputState CursesInput::sample(const og::sim::belt::Layout* belt)
{
    InputState state;
    PlayerInput& p = state.players[0];
    for (int a = 0; a < kInputActionCount; ++a) {
        const auto idx = static_cast<std::size_t>(a);
        p.held[idx] = held_[idx];
        p.pressed[idx] = pressed_edge_[idx];
    }

    // New Ability Select: the SDL sampler's fold. Kitty reports Shift+1 as
    // '1' with a shift modifier and LeftShift as its own key, so SHIFTER on
    // LeftShift plus a SPEC key reaches the SHIFTER row with no extra rule.
    static constexpr int kSpecKeys[3] = {KEY_SPECIAL, KEY_SPEC2, KEY_SPEC3};
    og::sim::belt::Keys keys;
    for (int i = 0; i < 3; ++i) {
        const auto idx = static_cast<std::size_t>(kSpecKeys[i]);
        keys.held[i] = held_[idx];
        keys.pressed[i] = pressed_edge_[idx];
    }
    keys.shift_held = held_[static_cast<std::size_t>(KEY_SHIFTER)];
    const og::sim::belt::Fold fold = og::sim::belt::fold_keys(
        keys, og::input::ability_select_on(), belt, belt_latch_);
    p.held[static_cast<int>(InputAction::Special)] = fold.special_held;
    p.pressed[static_cast<int>(InputAction::Special)] = fold.special_pressed;
    p.held[static_cast<int>(InputAction::SwitchSpecial)] = fold.switch_held;
    p.pressed[static_cast<int>(InputAction::SwitchSpecial)] = fold.switch_pressed;
    p.belt_cast = fold.belt_cast;

    pressed_edge_.fill(false);
    return state;
}

void CursesInput::reset()
{
    held_.fill(false);
    pressed_edge_.fill(false);
    belt_latch_ = og::sim::belt::Latch{};
}

std::optional<og::sim::belt::Layout>
followed_belt(const GameWorld& world, std::uint32_t followed_id)
{
    if (followed_id == 0)
        return std::nullopt;
    const walker* const hero = world.find_by_id(followed_id);
    if (hero == nullptr || hero->dead() || hero->stats() == nullptr)
        return std::nullopt;
    const FamilyDescriptor* const descriptor = get_family_descriptor(
        static_cast<int>(static_cast<unsigned char>(hero->family())));
    return og::sim::belt::resolve(descriptor, hero->stats()->level(),
                                  world.new_specials,
                                  og::sim::belt::bytes_for(*hero));
}

} // namespace og::curses
