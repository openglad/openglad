#include <openglad/input/button.h>
#include <openglad/input/input.h>
#include "test_framework.h"

#include <chrono>
#include <thread>


// From picker.cpp
#include <openglad/runtime/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons);
void draw_highlight_interior(const button& b);
void draw_highlight(const button& b);

namespace
{
struct KeyBindingGuard
{
    int player;
    int key_enum;
    int old_key;
    KeyBindingGuard(int player_, int key_enum_, int new_key)
        : player(player_), key_enum(key_enum_), old_key(og::runtime::current_session->player_keys_[player_][key_enum_])
    {
        og::runtime::current_session->player_keys_[player][key_enum] = new_key;
    }
    ~KeyBindingGuard() { og::runtime::current_session->player_keys_[player][key_enum] = old_key; }
};

struct KeyStateGuard
{
    int numkeys = 0;
    Uint8* keys = nullptr;
    KeyStateGuard()
    {
        const Uint8* ro = SDL_GetKeyboardState(&numkeys);
        keys = const_cast<Uint8*>(ro);
    }
    void set(SDL_Keycode key, bool pressed)
    {
        if (!keys) return;
        SDL_Scancode sc = SDL_GetScancodeFromKey(key);
        if (sc >= 0 && sc < numkeys)
            keys[sc] = pressed ? 1 : 0;
    }
};

static void release_key_after(KeyStateGuard* ks, SDL_Keycode key, int delay_ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    ks->set(key, false);
}
} // namespace

void test_picker_handle_menu_nav_moves_and_skips_hidden_targets()
{
    disablePlayerJoystick(0);
    KeyBindingGuard b_up(0, KEY_UP, SDLK_UP);
    KeyBindingGuard b_down(0, KEY_DOWN, SDLK_DOWN);
    KeyBindingGuard b_left(0, KEY_LEFT, SDLK_LEFT);
    KeyBindingGuard b_right(0, KEY_RIGHT, SDLK_RIGHT);
    KeyStateGuard ks;

    button buttons[] = {
        button("b0", "A", KEYSTATE_UNKNOWN, 10, 10, 30, 10, 0, 0, MenuNav{.up=-1, .down=1, .left=-1, .right=2}, false),
        button("b1", "B", KEYSTATE_UNKNOWN, 10, 30, 30, 10, 0, 0, MenuNav{.up=0, .down=-1, .left=-1, .right=2}, false),
        button("b2", "C", KEYSTATE_UNKNOWN, 10, 50, 30, 10, 0, 0, MenuNav{.up=-1, .down=-1, .left=1, .right=-1}, true),
    };

    int highlighted = 1;
    Sint32 retvalue = 0;

    ks.set(SDLK_UP, true);
    std::thread release_up(release_key_after, &ks, SDLK_UP, 10);
    bool activated = handle_menu_nav(buttons, highlighted, retvalue, false);
    release_up.join();

    TEST_ASSERT(!activated, "directional nav should not activate button");
    TEST_ASSERT_EQ(0, highlighted, "up key should move highlight to nav.up");
    TEST_ASSERT(pks().menu_nav_enabled, "pressing nav key should enable menu nav");

    // Right key points to hidden button index 2; highlight should stay unchanged.
    ks.set(SDLK_RIGHT, true);
    std::thread release_right(release_key_after, &ks, SDLK_RIGHT, 10);
    activated = handle_menu_nav(buttons, highlighted, retvalue, false);
    release_right.join();
    TEST_ASSERT(!activated, "moving to hidden target should not activate");
    TEST_ASSERT_EQ(0, highlighted, "hidden nav target should be ignored");

    // Down should move from 0 back to 1.
    ks.set(SDLK_DOWN, true);
    std::thread release_down(release_key_after, &ks, SDLK_DOWN, 10);
    activated = handle_menu_nav(buttons, highlighted, retvalue, false);
    release_down.join();
    TEST_ASSERT(!activated, "down movement should not activate");
    TEST_ASSERT_EQ(1, highlighted, "down key should move highlight to nav.down");

    // Left for button1 is invalid (-1), so highlight should remain.
    ks.set(SDLK_LEFT, true);
    std::thread release_left(release_key_after, &ks, SDLK_LEFT, 10);
    activated = handle_menu_nav(buttons, highlighted, retvalue, false);
    release_left.join();
    TEST_ASSERT(!activated, "invalid nav target should not activate");
    TEST_ASSERT_EQ(1, highlighted, "invalid nav target should leave highlight unchanged");
}
REGISTER_TEST(test_picker_handle_menu_nav_moves_and_skips_hidden_targets);

void test_picker_handle_menu_nav_fire_paths_and_highlight_draw_smoke()
{
    disablePlayerJoystick(0);
    KeyBindingGuard b_fire(0, KEY_FIRE, SDLK_SPACE);
    KeyStateGuard ks;

    button buttons[] = {
        button("b0", "A", KEYSTATE_UNKNOWN, 10, 10, 30, 10, 0, 0, MenuNav{}, false),
    };
    int highlighted = 0;
    Sint32 retvalue = 0;

    // Fire while nav disabled: should only mark pressed and re-enable nav.
    pks().menu_nav_enabled = false;
    ks.set(SDLK_SPACE, true);
    std::thread release_fire1(release_key_after, &ks, SDLK_SPACE, 10);
    bool activated = handle_menu_nav(buttons, highlighted, retvalue, false);
    release_fire1.join();
    TEST_ASSERT(!activated, "fire with nav disabled should not activate");
    TEST_ASSERT(pks().menu_nav_enabled, "fire should enable nav mode");

    // Fire while nav enabled with use_global_vbuttons=false should return OK(4).
    retvalue = 0;
    ks.set(SDLK_SPACE, true);
    std::thread release_fire2(release_key_after, &ks, SDLK_SPACE, 10);
    activated = handle_menu_nav(buttons, highlighted, retvalue, false);
    release_fire2.join();
    TEST_ASSERT(activated, "fire with nav enabled should activate");
    TEST_ASSERT_EQ(4, (int)retvalue, "activation without global vbuttons should return OK");

    // Fire while nav enabled with global vbuttons path.
    vbutton* primary_button = init_buttons(buttons, 1);
    (void)primary_button;
    retvalue = 0;
    ks.set(SDLK_SPACE, true);
    std::thread release_fire3(release_key_after, &ks, SDLK_SPACE, 10);
    activated = handle_menu_nav(buttons, highlighted, retvalue, true);
    release_fire3.join();
    TEST_ASSERT(activated, "fire with global vbuttons should activate");
    clear_allbuttons();

    // Smoke draw highlight routines under both nav states.
    pks().menu_nav_enabled = false;
    draw_highlight_interior(buttons[0]);
    draw_highlight(buttons[0]);
    pks().menu_nav_enabled = true;
    draw_highlight_interior(buttons[0]);
    draw_highlight(buttons[0]);

    // Expiry branch (no press + timeout).
    pks().menu_nav_enabled = true;
    pks().menu_nav_enabled_time = SDL_GetTicks() - 6000;
    ks.set(SDLK_SPACE, false);
    (void)handle_menu_nav(buttons, highlighted, retvalue, false);
}
REGISTER_TEST(test_picker_handle_menu_nav_fire_paths_and_highlight_draw_smoke);
