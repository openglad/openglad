#include <openglad/interface/input.h>
#include <openglad/platform/game_context.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/sai2x.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <array>
#include <atomic>
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
    const bool* saved = nullptr;
    std::array<bool, MAXKEYS> fake{};
    KeyStateGuard()
    {
        saved = og::runtime::current_session->keystates_;
        fake.fill(false);
        og::runtime::current_session->keystates_ = fake.data();
    }
    ~KeyStateGuard()
    {
        og::runtime::current_session->keystates_ = saved;
    }
    void pulse(int scancode, int down_ms = 30)
    {
        fake[static_cast<std::size_t>(scancode)] = true;
        SDL_Delay(down_ms);
        fake[static_cast<std::size_t>(scancode)] = false;
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

struct TeamInfoInjectorState
{
    KeyStateGuard* keys = nullptr;
    std::atomic<bool> done{false};
};

static int injector_thread_team_info(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<TeamInfoInjectorState*>(data);
    SDL_Delay(50);
    state->keys->pulse(KEYSTATE_t);

    // The recursive options screen starts only after the gameplay backdrop
    // has been redrawn. Repeated ESC pulses make the exit deterministic even
    // on a slow smart-filter build.
    while (!state->done.load(std::memory_order_relaxed))
    {
        state->keys->fake[KEYSTATE_ESCAPE] = true;
        SDL_Delay(25);
        state->keys->fake[KEYSTATE_ESCAPE] = false;
        SDL_Delay(10);
    }
    state->keys->fake[KEYSTATE_ESCAPE] = false;
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

    // Save ALL per-view prefs: the driven hotkeys mutate several of them
    // (view size via [/], the r/h/f/s/c/b toggles), and options_menu()
    // persists the final values into the session prefs store
    // (prefsob->save) on exit. A poisoned store leaks into every
    // later-constructed viewscreen (the ctor loads from it), so the store
    // must be re-synced after the restore below.
    signed char saved_prefs[10];
    for (int i = 0; i < 10; i++)
        saved_prefs[i] = vs->prefs[i];

    KeyStateGuard ks;

    SDL_Thread* th = SDL_CreateThread(injector_thread_options_menu, "opts_fake_keystates", &ks);
    ASSERT_TRUE(th != nullptr) << "injector thread started";

    vs->options_menu();

    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    // Restore every pref the menu mutated, re-derive the geometry, and
    // re-sync the session prefs store options_menu persisted on exit —
    // otherwise a later viewscreen recreation reloads the stale prefs (the
    // og_test_view --gtest_shuffle seed-27 failure chain).
    for (int i = 0; i < 10; i++)
        vs->prefs[i] = saved_prefs[i];
    vs->resize(vs->prefs[PREF_VIEW]);
    og::runtime::current_session->theprefs_->save(vs);
}

TEST(ViewOptionsMenuDriven, team_info_return_redraws_the_split_world_canvas)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    screen* const game = og::runtime::current_session->myscreen_;
    viewscreen* const vs = game->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    ASSERT_NE(nullptr, E_Screen);

    if (!game->world().grid.valid())
        game->world().create_new_grid();
    if (!vs->control)
    {
        walker* w = game->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, w);
        w->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
        w->set_team_num(0);
        w->set_user(0);
        vs->control = w;
    }

    const int saved_zoom = E_Screen->world_zoom_steps();
    og::WorldScaleMode saved_smoothing = E_Screen->world_scale().mode;
    if (saved_smoothing == og::WorldScaleMode::Legacy)
        saved_smoothing = og::WorldScaleMode::Integer;
    const CanvasTarget saved_target = E_Screen->active_canvas();
    const signed char saved_view = vs->prefs[PREF_VIEW];

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai);
    vs->prefs[PREF_VIEW] = PREF_VIEW_FULL;
    game->relayout_views();
    E_Screen->set_active_canvas(CanvasTarget::World);
    ASSERT_EQ(640, E_Screen->render->w);
    ASSERT_EQ(400, E_Screen->render->h);

    constexpr Uint8 sentinel_r = 231;
    constexpr Uint8 sentinel_g = 17;
    constexpr Uint8 sentinel_b = 199;
    const SDL_PixelFormatDetails* const details =
        SDL_GetPixelFormatDetails(E_Screen->render->format);
    ASSERT_NE(nullptr, details);
    const Uint32 sentinel = SDL_MapRGB(
        details, nullptr, sentinel_r, sentinel_g, sentinel_b);
    ASSERT_TRUE(SDL_FillSurfaceRect(E_Screen->render, nullptr, sentinel));

    KeyStateGuard keys;
    TeamInfoInjectorState state{&keys};
    SDL_Thread* const thread = SDL_CreateThread(
        injector_thread_team_info, "opts_team_info", &state);
    ASSERT_NE(nullptr, thread);

    vs->options_menu();
    state.done.store(true, std::memory_order_relaxed);
    int thread_code = 0;
    SDL_WaitThread(thread, &thread_code);

    EXPECT_EQ(CanvasTarget::World, E_Screen->active_canvas())
        << "the outer modal scope must restore gameplay routing";
    bool extended_world_changed = false;
    for (int y = 16; y < 384 && !extended_world_changed; y += 11)
    {
        for (int x = 336; x < 624; x += 13)
        {
            Uint8 r = 0;
            Uint8 g = 0;
            Uint8 b = 0;
            game->get_pixel(x, y, &r, &g, &b);
            if (r != sentinel_r || g != sentinel_g || b != sentinel_b)
            {
                extended_world_changed = true;
                break;
            }
        }
    }
    EXPECT_TRUE(extended_world_changed)
        << "Team Info return must redraw beyond the fixed 320px UI width";

    E_Screen->set_world_zoom(saved_zoom, saved_smoothing);
    E_Screen->set_active_canvas(saved_target);
    vs->prefs[PREF_VIEW] = saved_view;
    game->relayout_views();
    og::runtime::current_session->theprefs_->save(vs);
}
