#include "SDL.h"
#include <openglad/input/input.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/runtime/screen.h>
#include <openglad/render/view.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <array>
#include <chrono>
#include <memory>
#include <thread>

extern screen* myscreen;
extern const Uint8* keystates;
extern int player_keys[4][NUM_KEYS];

static std::unique_ptr<walker> create_controlled_living(char family)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family, myscreen);
    if (!w)
        return nullptr;
    w->team_num = 0;
    w->setxy(50, 50);
    return w;
}

struct KeystateOverride
{
    const Uint8* prev = nullptr;
    std::array<Uint8, SDL_NUM_SCANCODES> fake{};

    KeystateOverride()
    {
        prev = keystates;
        fake.fill(0);
        keystates = fake.data();
    }

    ~KeystateOverride()
    {
        keystates = prev;
    }
};

static void press_release_after(KeystateOverride& ks, SDL_Scancode sc, int press_ms, int hold_ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(press_ms));
    ks.fake[sc] = 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));
    ks.fake[sc] = 0;
}

void test_viewscreen_options_menu_exercises_key_paths()
{
    viewscreen* v = myscreen->viewob[0].get();
    TEST_ASSERT(v != nullptr, "viewob[0] should exist");

    auto control = create_controlled_living(FAMILY_SOLDIER);
    TEST_ASSERT(control != nullptr, "create_controlled_living should succeed");
    walker* controlp = control.get();
    v->control = controlp;

    KeystateOverride ks;

    // Drive a few option toggles then exit with Escape.
    std::thread driver([&]() {
        press_release_after(ks, SDL_SCANCODE_RIGHTBRACKET, 10, 5);
        press_release_after(ks, SDL_SCANCODE_LEFTBRACKET, 10, 5);
        press_release_after(ks, SDL_SCANCODE_COMMA, 10, 5);
        press_release_after(ks, SDL_SCANCODE_PERIOD, 10, 5);
        press_release_after(ks, SDL_SCANCODE_R, 10, 5);
        press_release_after(ks, SDL_SCANCODE_H, 10, 5);
        press_release_after(ks, SDL_SCANCODE_F, 10, 5);
        press_release_after(ks, SDL_SCANCODE_S, 10, 5);
        press_release_after(ks, SDL_SCANCODE_C, 10, 5);
        press_release_after(ks, SDL_SCANCODE_ESCAPE, 10, 5);
    });

    // Runs a tight loop that polls keystates and draws. Our driver thread
    // flips keys to avoid getting stuck in "wait for key release" loops.
    v->options_menu();

    driver.join();

    // Drive set_key_prefs and view_key_bindings paths directly.
    TEST_ASSERT_EQ(1, (int)v->set_key_prefs(), "set_key_prefs should succeed in testing mode");

    int saved_up = player_keys[v->mynum][KEY_UP];
    int saved_fire = player_keys[v->mynum][KEY_FIRE];
    int saved_special = player_keys[v->mynum][KEY_SPECIAL];
    int saved_yell = player_keys[v->mynum][KEY_YELL];
    int saved_shifter = player_keys[v->mynum][KEY_SHIFTER];

    // Force key-display abbreviation and truncation branches.
    player_keys[v->mynum][KEY_UP] = SDLK_BACKSPACE;      // "BkSpc"
    player_keys[v->mynum][KEY_FIRE] = SDLK_LCTRL;        // "LCtrl"
    player_keys[v->mynum][KEY_SPECIAL] = SDLK_RALT;      // "RAlt"
    player_keys[v->mynum][KEY_YELL] = SDLK_CAPSLOCK;     // "Caps"
    player_keys[v->mynum][KEY_SHIFTER] = SDLK_AUDIONEXT; // long key name -> truncation

    KeystateOverride ks2;
    std::thread esc_driver([&]() {
        press_release_after(ks2, SDL_SCANCODE_ESCAPE, 10, 10);
    });
    v->view_key_bindings();
    esc_driver.join();

    player_keys[v->mynum][KEY_UP] = saved_up;
    player_keys[v->mynum][KEY_FIRE] = saved_fire;
    player_keys[v->mynum][KEY_SPECIAL] = saved_special;
    player_keys[v->mynum][KEY_YELL] = saved_yell;
    player_keys[v->mynum][KEY_SHIFTER] = saved_shifter;

    v->control = nullptr;
}
REGISTER_TEST(test_viewscreen_options_menu_exercises_key_paths);
