#include <openglad/input/input.h>
#include "test_framework.h"

extern int player_keys[4][NUM_KEYS];

namespace
{
struct KeyBindingGuard
{
    int player;
    int key_enum;
    int old;

    KeyBindingGuard(int player_, int key_enum_, int new_key)
        : player(player_), key_enum(key_enum_), old(player_keys[player_][key_enum_])
    {
        player_keys[player][key_enum] = new_key;
    }

    ~KeyBindingGuard()
    {
        player_keys[player][key_enum] = old;
    }
};

struct KeyStateGuard
{
    SDL_Scancode sc;
    Uint8 old_value;
    int numkeys;
    Uint8* keys;

    explicit KeyStateGuard(SDL_Scancode sc_)
        : sc(sc_), old_value(0), numkeys(0), keys(nullptr)
    {
        const Uint8* read_only = SDL_GetKeyboardState(&numkeys);
        keys = const_cast<Uint8*>(read_only);
        if (keys && sc >= 0 && sc < numkeys)
            old_value = keys[sc];
    }

    void set(bool pressed)
    {
        if (keys && sc >= 0 && sc < numkeys)
            keys[sc] = pressed ? 1 : 0;
    }

    ~KeyStateGuard()
    {
        if (keys && sc >= 0 && sc < numkeys)
            keys[sc] = old_value;
    }
};
} // namespace

void test_input_isPlayerHoldingKey_uses_keyboard_state_when_no_joystick_mapping()
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(0, KEY_FIRE, SDLK_v);

    const SDL_Scancode sc = SDL_GetScancodeFromKey(SDLK_v);
    KeyStateGuard ks(sc);

    ks.set(false);
    TEST_ASSERT(!isPlayerHoldingKey(0, KEY_FIRE), "unpressed key should not be held");

    ks.set(true);
    TEST_ASSERT(isPlayerHoldingKey(0, KEY_FIRE), "pressed key should be held");
}
REGISTER_TEST(test_input_isPlayerHoldingKey_uses_keyboard_state_when_no_joystick_mapping);

void test_input_didPlayerPressKey_matches_keydown_and_ignores_repeats()
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(0, KEY_SPECIAL, SDLK_x);

    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_x;
    e.key.repeat = 0;
    TEST_ASSERT(didPlayerPressKey(0, KEY_SPECIAL, e), "matching keydown should be recognized");

    e.key.repeat = 1;
    TEST_ASSERT(!didPlayerPressKey(0, KEY_SPECIAL, e), "repeat keydown should be ignored");

    e.key.repeat = 0;
    e.key.keysym.sym = SDLK_y;
    TEST_ASSERT(!didPlayerPressKey(0, KEY_SPECIAL, e), "non-matching keydown should be ignored");

    e.type = SDL_KEYUP;
    e.key.keysym.sym = SDLK_x;
    TEST_ASSERT(!didPlayerPressKey(0, KEY_SPECIAL, e), "keyup should not be treated as press");
}
REGISTER_TEST(test_input_didPlayerPressKey_matches_keydown_and_ignores_repeats);

void test_input_didPlayerReleaseKey_matches_keyup()
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(1, KEY_YELL, SDLK_q);

    SDL_Event e{};
    e.type = SDL_KEYUP;
    e.key.keysym.sym = SDLK_q;
    TEST_ASSERT(didPlayerReleaseKey(1, KEY_YELL, e), "matching keyup should be recognized");

    e.key.keysym.sym = SDLK_w;
    TEST_ASSERT(!didPlayerReleaseKey(1, KEY_YELL, e), "non-matching keyup should be ignored");

    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_q;
    TEST_ASSERT(!didPlayerReleaseKey(1, KEY_YELL, e), "keydown should not be treated as release");
}
REGISTER_TEST(test_input_didPlayerReleaseKey_matches_keyup);

void test_input_key_binding_helpers_isAnyPlayerKey_and_isPlayerKey()
{
    KeyBindingGuard bind0(2, KEY_SWITCH, SDLK_KP_0);
    KeyBindingGuard bind1(3, KEY_SWITCH, SDLK_KP_1);

    TEST_ASSERT(isAnyPlayerKey(SDLK_KP_0), "isAnyPlayerKey should find mapped key");
    TEST_ASSERT(isPlayerKey(2, SDLK_KP_0), "isPlayerKey should find mapping for specific player");
    TEST_ASSERT(!isPlayerKey(0, SDLK_KP_0), "isPlayerKey should not report mapping for other player");

    TEST_ASSERT(isAnyPlayerKey(SDLK_KP_1), "isAnyPlayerKey should find second mapped key");
    TEST_ASSERT(isPlayerKey(3, SDLK_KP_1), "isPlayerKey should find mapping for specific player");
}
REGISTER_TEST(test_input_key_binding_helpers_isAnyPlayerKey_and_isPlayerKey);

void test_input_wait_for_key_event_returns_fake_escape_in_test_mode()
{
    SDL_Event e = wait_for_key_event();
    TEST_ASSERT_EQ((int)SDL_KEYDOWN, (int)e.type, "wait_for_key_event should return keydown in test mode");
    TEST_ASSERT_EQ((int)SDLK_ESCAPE, (int)e.key.keysym.sym, "wait_for_key_event should return escape in test mode");
}
REGISTER_TEST(test_input_wait_for_key_event_returns_fake_escape_in_test_mode);
