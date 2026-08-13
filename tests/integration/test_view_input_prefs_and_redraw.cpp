#include <openglad/interface/input.h>
#include <openglad/legacy/base.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct KeyStateGuard
{
    const bool* saved = nullptr;
    std::array<bool, SDL_SCANCODE_COUNT> fake{};
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
};

struct KeyBindingGuard
{
    int player;
    int key_enum;
    int old_key;
    KeyBindingGuard(int player_, int key_enum_, int new_key)
        : player(player_), key_enum(key_enum_),
          old_key(og::runtime::current_session->player_keys_[player_][key_enum_])
    {
        og::runtime::current_session->player_keys_[player][key_enum] = new_key;
    }
    ~KeyBindingGuard()
    {
        og::runtime::current_session->player_keys_[player][key_enum] = old_key;
    }
};

static SDL_Event keydown(SDL_Keycode key)
{
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    e.key.repeat = false;
    return e;
}
} // namespace

// Design §7.4: InputAction::OpenPrefs (slot 14) is a RESERVED wire slot with
// nothing behind it — the per-player options menu it used to open is retired
// and its rows live on the pause player screen / GAME SETTINGS now. The old
// menu set screen::redrawme = 1 and persisted prefs on exit, so both are the
// observables: a pressed OpenPrefs must leave the world and the view's prefs
// exactly as they were. The slot is deliberately BOUND here (its shipped
// default is KEYCODE_UNKNOWN) so this pins the missing dispatch, not just the
// missing binding.
TEST(ViewInputPrefsAndRedraw, viewscreen_input_open_prefs_dispatches_nothing)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;

    // Ensure a control exists so input() doesn't early-exit before the slot
    // would have been sampled.
    if (!vs->control)
    {
        walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_TRUE(w != nullptr) << "control walker created";
        if (!w)
            return;
        w->set_team_num(0);
        w->set_user(0);
        w->set_act_type(ACT_CONTROL);
        vs->control = w;
    }

    vs->mynum = 0;
    vs->my_team = 0;

    KeyStateGuard ks;
    KeyBindingGuard bind_prefs(0, KEY_PREFS, SDLK_1);

    std::array<signed char, 10> saved_prefs{};
    std::copy(std::begin(vs->prefs), std::end(vs->prefs), saved_prefs.begin());
    const short saved_redrawme = og::runtime::current_session->myscreen_->redrawme;
    og::runtime::current_session->myscreen_->redrawme = 0;

    // No injector thread: nothing blocks any more. If a modal came back this
    // call would hang the suite instead of returning.
    (void)vs->input(keydown(SDLK_1));

    EXPECT_EQ(0, og::runtime::current_session->myscreen_->redrawme)
        << "a pressed OpenPrefs must not open anything that requests a redraw";
    EXPECT_TRUE(std::equal(saved_prefs.begin(), saved_prefs.end(), std::begin(vs->prefs)))
        << "a pressed OpenPrefs must not touch the view's preferences";

    og::runtime::current_session->myscreen_->redrawme = saved_redrawme;
}


// Moved here when test_view_get_keypress_and_edge_cases.cpp was retired with
// viewscreen::get_keypress and the options menu.
TEST(ViewInputPrefsAndRedraw, viewscreen_input_switch_control_not_in_oblist_logs_and_returns)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "view should exist";
    if (!v)
        return;

    KeyStateGuard ks;

    KeyBindingGuard bind_switch(0, KEY_SWITCH, SDLK_TAB);
    KeyBindingGuard bind_shifter(0, KEY_SHIFTER, SDLK_LSHIFT);
    KeyBindingGuard bind_cheat(0, KEY_CHEAT, SDLK_C);

    // Create a control walker not present in level_data.oblist.
    PixieData px(1, 1, 1, new unsigned char[1]{0});
    walker orphan(px);
    orphan.set_team_num(0);
    orphan.set_real_team_num(255);
    orphan.set_dead(0);
    orphan.set_user(0);
    orphan.set_act_type(ACT_CONTROL);

    walker* saved_control = v->control;
    v->mynum = 0;
    v->my_team = 0;
    v->control = &orphan;

    // Forward switch error: control not in oblist.
    (void)v->input(keydown(SDLK_TAB));

    // Reverse switch error: hold shifter and try again (debounce reset by non-switch event).
    (void)v->input(keydown(SDLK_F1));
    ks.fake[SDL_SCANCODE_LSHIFT] = true;
    (void)v->input(keydown(SDLK_TAB));
    ks.fake[SDL_SCANCODE_LSHIFT] = false;

    v->control = saved_control;
}


// Design §7.2: the Shift+/ briefing chord is DELETED (raw '/' was player
// 2's SPECIAL key; the briefing lives on the PAUSED menu now). The old
// handler called read_scenario and set redrawme=1 — under TESTING
// read_scenario returns immediately, so redrawme is the observable: it must
// stay untouched when the chord arrives.
TEST(ViewInputPrefsAndRedraw, viewscreen_input_shift_slash_chord_is_gone)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;

    if (!vs->control)
    {
        walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_TRUE(w != nullptr) << "control walker created";
        if (!w)
            return;
        w->set_team_num(0);
        w->set_user(0);
        w->set_act_type(ACT_CONTROL);
        vs->control = w;
    }

    vs->mynum = 0;
    vs->my_team = 0;

    const short saved_redrawme = og::runtime::current_session->myscreen_->redrawme;
    KeyStateGuard ks;
    // Hold the shifter key (LSHIFT by default for player 0).
    ks.fake[SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_SHIFTER]), nullptr)] = true;
    og::runtime::current_session->myscreen_->redrawme = 0;
    (void)vs->input(keydown(SDLK_SLASH));
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->redrawme)
        << "Shift+/ must no longer dispatch the scenario briefing";
    ks.fake[SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_SHIFTER]), nullptr)] = false;
    og::runtime::current_session->myscreen_->redrawme = saved_redrawme;
}


TEST(ViewInputPrefsAndRedraw, viewscreen_redraw_negative_scroll_draws_wall_edges_smoke)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
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

