#include <openglad/input/input.h>
#include <openglad/runtime/game_context.h>
#include <openglad/entities/walker.h>
#include <openglad/runtime/screen.h>
#include <openglad/render/view.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <array>
#include <cstring>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* ctx) { set_global_context(ctx); }
    ~GlobalContextGuard() { set_global_context(nullptr); }
    GlobalContextGuard(const GlobalContextGuard&) = delete;
    GlobalContextGuard& operator=(const GlobalContextGuard&) = delete;
};

struct KeyStateGuard
{
    const Uint8* saved = nullptr;
    std::array<Uint8, MAXKEYS> fake{};
    KeyStateGuard()
    {
        saved = keystates;
        fake.fill(0);
        keystates = fake.data();
    }
    ~KeyStateGuard()
    {
        keystates = saved;
    }
    void pulse(SDL_Scancode sc, int down_ms = 30)
    {
        fake[sc] = 1;
        SDL_Delay(down_ms);
        fake[sc] = 0;
        SDL_Delay(5);
    }
};

static int injector_thread_options_menu(void* data)
{
    KeyStateGuard* ks = static_cast<KeyStateGuard*>(data);
    SDL_Delay(50);

    // Drive a representative set of hotkeys, ensuring each key is released so
    // the menu's internal "wait for release" loops terminate deterministically.
    ks->pulse(KEYSTATE_KP_PLUS);
    ks->pulse(KEYSTATE_KP_MINUS);
    ks->pulse(KEYSTATE_LEFTBRACKET);
    ks->pulse(KEYSTATE_RIGHTBRACKET);
    ks->pulse(KEYSTATE_COMMA);
    ks->pulse(KEYSTATE_PERIOD);
    ks->pulse(KEYSTATE_r);
    ks->pulse(KEYSTATE_h);
    ks->pulse(KEYSTATE_f);
    ks->pulse(KEYSTATE_s);
    ks->pulse(KEYSTATE_c);
    ks->pulse(KEYSTATE_b);

    // Exit menu.
    ks->fake[KEYSTATE_ESCAPE] = 1;
    SDL_Delay(30);
    ks->fake[KEYSTATE_ESCAPE] = 0;
    SDL_Delay(10);
    return 0;
}
} // namespace

void test_viewscreen_options_menu_driven_exercises_hotkeys()
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    viewscreen* vs = myscreen->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen exists");
    if (!vs)
        return;

    // Ensure we have a controlled living so options_menu doesn't early-return.
    if (!vs->control)
    {
        walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
        TEST_ASSERT(w != nullptr, "control walker created");
        if (w)
        {
            w->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
            w->team_num = 0;
            vs->control = w;
        }
    }

    // Save prefs that options_menu() may mutate and persist to keyprefs.dat.
    const signed char saved_view = vs->prefs[PREF_VIEW];

    KeyStateGuard ks;

    SDL_Thread* th = SDL_CreateThread(injector_thread_options_menu, "opts_fake_keystates", &ks);
    TEST_ASSERT(th != nullptr, "injector thread started");

    vs->options_menu();

    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    // Restore view pref that was mutated by the [ and ] hotkeys.
    vs->prefs[PREF_VIEW] = saved_view;
    vs->resize(saved_view);
}
REGISTER_TEST(test_viewscreen_options_menu_driven_exercises_hotkeys);
