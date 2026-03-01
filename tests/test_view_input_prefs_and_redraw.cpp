#include <openglad/input/input.h>
#include <openglad/legacy/base.h>
#include <openglad/entities/walker.h>
#include <openglad/render/view.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"

#include <array>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
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
};

static SDL_Event keydown(SDL_Keycode key)
{
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = key;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    e.key.repeat = 0;
    return e;
}

static int injector_press_and_release_escape(void* data)
{
    og::runtime::ensure_thread_session();
    auto* ks = static_cast<KeyStateGuard*>(data);
    SDL_Delay(40);
    ks->fake[KEYSTATE_ESCAPE] = 1;
    SDL_Delay(30);
    ks->fake[KEYSTATE_ESCAPE] = 0;
    SDL_Delay(10);
    return 0;
}
} // namespace

void test_viewscreen_input_key_prefs_triggers_options_menu_branch()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen exists");
    if (!vs)
        return;

    // Ensure a control exists so input() doesn't early-exit and options_menu()
    // doesn't hit its missing-control guard.
    if (!vs->control)
    {
        walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
        TEST_ASSERT(w != nullptr, "control walker created");
        if (!w)
            return;
        w->team_num = 0;
        w->user = 0;
        w->set_act_type(ACT_CONTROL);
        vs->control = w;
    }

    vs->mynum = 0;
    vs->my_team = 0;

    KeyStateGuard ks;
    SDL_Thread* th = SDL_CreateThread(injector_press_and_release_escape, "esc_inject", &ks);
    TEST_ASSERT(th != nullptr, "escape injector started");

    const SDL_Keycode prefs_key = og::runtime::current_session->player_keys_[0][KEY_PREFS];
    (void)vs->input(keydown(prefs_key));

    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);
}
REGISTER_TEST(test_viewscreen_input_key_prefs_triggers_options_menu_branch);

void test_viewscreen_input_shift_slash_triggers_read_scenario_branch()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen exists");
    if (!vs)
        return;

    if (!vs->control)
    {
        walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
        TEST_ASSERT(w != nullptr, "control walker created");
        if (!w)
            return;
        w->team_num = 0;
        w->user = 0;
        w->set_act_type(ACT_CONTROL);
        vs->control = w;
    }

    vs->mynum = 0;
    vs->my_team = 0;

    KeyStateGuard ks;
    // Hold the shifter key (LSHIFT by default for player 0).
    ks.fake[SDL_GetScancodeFromKey(og::runtime::current_session->player_keys_[0][KEY_SHIFTER])] = 1;
    (void)vs->input(keydown(SDLK_SLASH));
    ks.fake[SDL_GetScancodeFromKey(og::runtime::current_session->player_keys_[0][KEY_SHIFTER])] = 0;
}
REGISTER_TEST(test_viewscreen_input_shift_slash_triggers_read_scenario_branch);

void test_viewscreen_redraw_negative_scroll_draws_wall_edges_smoke()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen exists");
    if (!vs)
        return;

    // Ensure the level has a grid so redraw doesn't depend on prior tests.
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const Sint32 saved_topx = og::runtime::current_session->myscreen_->level_visuals_.topx;
    const Sint32 saved_topy = og::runtime::current_session->myscreen_->level_visuals_.topy;
    walker* saved_control = vs->control;

    vs->control = nullptr;
    // Force negative offsets so redraw's wall-edge branches run (j == -2 and j == -1).
    og::runtime::current_session->myscreen_->level_visuals_.topx = -GRID_SIZE - 1;
    og::runtime::current_session->myscreen_->level_visuals_.topy = -GRID_SIZE - 1;
    (void)vs->redraw();

    og::runtime::current_session->myscreen_->level_visuals_.topx = saved_topx;
    og::runtime::current_session->myscreen_->level_visuals_.topy = saved_topy;
    vs->control = saved_control;
}
REGISTER_TEST(test_viewscreen_redraw_negative_scroll_draws_wall_edges_smoke);
