#include <openglad/data/gparser.h>
#include <openglad/data/pixie_data.h>
#include <openglad/entities/walker.h>
#include <openglad/input/input.h>
#include <openglad/legacy/base.h>
#include <openglad/render/view.h>
#include <openglad/runtime/screen.h>
#include <openglad/runtime/game_context.h>
#include "test_framework.h"

#include <array>

// myscreen is now a macro defined in base.h (via game_session.h)

// From view.cpp
int get_keypress();

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
    ~KeyBindingGuard()
    {
        og::runtime::current_session->player_keys_[player][key_enum] = old_key;
    }
};

struct KeystateOverride
{
    const Uint8* prev = nullptr;
    std::array<Uint8, SDL_NUM_SCANCODES> fake{};
    KeystateOverride()
    {
        prev = og::runtime::current_session->keystates_;
        fake.fill(0);
        og::runtime::current_session->keystates_ = fake.data();
    }
    ~KeystateOverride()
    {
        og::runtime::current_session->keystates_ = prev;
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

static int injector_clear_escape(void* data)
{
    og::runtime::ensure_thread_session();
    auto* arr = static_cast<std::array<Uint8, SDL_NUM_SCANCODES>*>(data);
    SDL_Delay(20);
    (*arr)[SDL_SCANCODE_ESCAPE] = 0;
    return 0;
}
} // namespace

void test_view_get_keypress_consumes_next_key_event()
{
    // Queue a keydown event so get_keypress() can read it deterministically.
    sendFakeKeyDownEvent(SDLK_a);
    const int k = get_keypress();
    TEST_ASSERT_EQ(static_cast<int>(SDLK_a), k, "get_keypress should return queued SDL key");
}
REGISTER_TEST(test_view_get_keypress_consumes_next_key_event);

void test_viewscreen_options_menu_missing_control_returns()
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(v != nullptr, "view should exist");
    if (!v)
        return;

    walker* saved = v->control;
    v->control = nullptr;
    v->options_menu(); // should early-return without hanging
    v->control = saved;
}
REGISTER_TEST(test_viewscreen_options_menu_missing_control_returns);

void test_viewscreen_input_switch_control_not_in_oblist_logs_and_returns()
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(v != nullptr, "view should exist");
    if (!v)
        return;

    KeystateOverride ks;

    KeyBindingGuard bind_switch(0, KEY_SWITCH, SDLK_TAB);
    KeyBindingGuard bind_shifter(0, KEY_SHIFTER, SDLK_LSHIFT);
    KeyBindingGuard bind_cheat(0, KEY_CHEAT, SDLK_c);

    // Create a control walker not present in level_data.oblist.
    PixieData px(1, 1, 1, new unsigned char[1]{0});
    walker orphan(px);
    orphan.sim_config = &cfg;
    orphan.team_num = 0;
    orphan.real_team_num = 255;
    orphan.dead = 0;
    orphan.user = 0;
    orphan.set_act_type(ACT_CONTROL);

    v->mynum = 0;
    v->my_team = 0;
    v->control = &orphan;

    // Forward switch error: control not in oblist.
    (void)v->input(keydown(SDLK_TAB));

    // Reverse switch error: hold shifter and try again (debounce reset by non-switch event).
    (void)v->input(keydown(SDLK_F1));
    ks.fake[SDL_SCANCODE_LSHIFT] = 1;
    (void)v->input(keydown(SDLK_TAB));
    ks.fake[SDL_SCANCODE_LSHIFT] = 0;
}
REGISTER_TEST(test_viewscreen_input_switch_control_not_in_oblist_logs_and_returns);

void test_viewscreen_options_menu_covers_view_size_label_cases()
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(v != nullptr, "view should exist");
    if (!v)
        return;

    // Ensure a control exists so options_menu does not early-return.
    if (!v->control)
    {
        walker* w = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
        TEST_ASSERT(w != nullptr, "control created");
        if (!w)
            return;
        w->team_num = 0;
        v->control = w;
    }

    KeystateOverride ks;

    // Save original pref so we can restore after testing edge cases.
    const signed char saved_view = v->prefs[PREF_VIEW];

    // Immediately request exit, but clear ESC after a short delay so the
    // "wait for release" loop terminates.
    const std::array<int, 6> view_prefs = {
        static_cast<int>(PREF_VIEW_FULL),
        static_cast<int>(PREF_VIEW_PANELS),
        static_cast<int>(PREF_VIEW_1),
        static_cast<int>(PREF_VIEW_2),
        static_cast<int>(PREF_VIEW_3),
        99,
    };
    for (int view_pref : view_prefs)
    {
        v->prefs[PREF_VIEW] = static_cast<signed char>(view_pref);
        ks.fake[SDL_SCANCODE_ESCAPE] = 1;
        SDL_Thread* th = SDL_CreateThread(injector_clear_escape, "clear_esc", &ks.fake);
        v->options_menu();
        int code = 0;
        if (th)
            SDL_WaitThread(th, &code);
        ks.fake[SDL_SCANCODE_ESCAPE] = 0;
    }

    // Restore original pref — options_menu() saves prefs to keyprefs.dat on
    // every call, so the bogus value 99 was persisted.  Write back a clean
    // value so subsequent tests don't inherit corrupt state.
    v->prefs[PREF_VIEW] = saved_view;
    v->resize(saved_view);
}
REGISTER_TEST(test_viewscreen_options_menu_covers_view_size_label_cases);
