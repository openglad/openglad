#include "graph.h"
#include "test_framework.h"

#include <array>
#include <chrono>
#include <thread>

extern screen* myscreen;
extern const Uint8* keystates;

static walker* create_controlled_living(char family)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l)
        return nullptr;
    walker* w = l->create_walker(Order::Living, family, myscreen);
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

    walker* control = create_controlled_living(FAMILY_SOLDIER);
    TEST_ASSERT(control != nullptr, "create_controlled_living should succeed");
    v->control = control;

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

    delete control;
    v->control = nullptr;
}
REGISTER_TEST(test_viewscreen_options_menu_exercises_key_paths);

