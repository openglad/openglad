#include <openglad/interface/input.h>
#include <openglad/platform/game_context.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include <SDL.h>

#include <array>
#include <cstring>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* ctx) { push_test_context(ctx); }
    ~GlobalContextGuard() { pop_test_context(); }
    GlobalContextGuard(const GlobalContextGuard&) = delete;
    GlobalContextGuard& operator=(const GlobalContextGuard&) = delete;
};

struct KeyStateGuard
{
    const Uint8* saved = nullptr;
    std::array<Uint8, MAXKEYS> fake{};
    KeyStateGuard()
    {
        saved = og::runtime::current_session->keystates_;
        fake.fill(0);
        og::runtime::current_session->keystates_ = fake.data();
    }
    ~KeyStateGuard()
    {
        og::runtime::current_session->keystates_ = saved;
    }
    void pulse(int scancode, int down_ms = 30)
    {
        fake[static_cast<std::size_t>(scancode)] = 1;
        SDL_Delay(down_ms);
        fake[static_cast<std::size_t>(scancode)] = 0;
        SDL_Delay(5);
    }
};

static int injector_thread_options_menu(void* data)
{
    og::runtime::ensure_thread_session();
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

TEST(ViewOptionsMenuDriven, viewscreen_options_menu_driven_exercises_hotkeys)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;

    // Ensure we have a controlled living so options_menu doesn't early-return.
    if (!vs->control)
    {
        walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_TRUE(w != nullptr) << "control walker created";
        if (w)
        {
            w->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
            w->set_team_num(0);
            vs->control = w;
        }
    }

    // Save prefs that options_menu() may mutate and persist to keyprefs.dat.
    const signed char saved_view = vs->prefs[PREF_VIEW];

    KeyStateGuard ks;

    SDL_Thread* th = SDL_CreateThread(injector_thread_options_menu, "opts_fake_keystates", &ks);
    ASSERT_TRUE(th != nullptr) << "injector thread started";

    vs->options_menu();

    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    // Restore view pref that was mutated by the [ and ] hotkeys.
    vs->prefs[PREF_VIEW] = saved_view;
    vs->resize(saved_view);
}

