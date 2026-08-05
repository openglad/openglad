#include <openglad/resources/gparser.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/game_context.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <array>
#include <atomic>

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
    const bool* prev = nullptr;
    std::array<bool, SDL_SCANCODE_COUNT> fake{};
    KeystateOverride()
    {
        prev = og::runtime::current_session->keystates_;
        fake.fill(false);
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
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    e.key.repeat = false;
    return e;
}

struct EscapePulseState
{
    std::array<bool, SDL_SCANCODE_COUNT>* keys;
    std::atomic<bool>* done;
};

static int injector_pulse_escape(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<EscapePulseState*>(data);
    SDL_Delay(25);
    while (!state->done->load(std::memory_order_relaxed))
    {
        (*state->keys)[KEYSTATE_ESCAPE] = true;
        SDL_Delay(25);
        (*state->keys)[KEYSTATE_ESCAPE] = false;
        SDL_Delay(10);
    }
    (*state->keys)[KEYSTATE_ESCAPE] = false;
    return 0;
}
} // namespace

TEST(ViewGetKeypressAndEdgeCases, view_get_keypress_consumes_next_key_event)
{
    // Queue a keydown event so get_keypress() can read it deterministically.
    sendFakeKeyDownEvent(SDLK_A);
    const int k = get_keypress();
    ASSERT_EQ(static_cast<int>(SDLK_A), k) << "get_keypress should return queued SDL key";
}


TEST(ViewGetKeypressAndEdgeCases, viewscreen_options_menu_missing_control_returns)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "view should exist";
    if (!v)
        return;

    walker* saved = v->control;
    v->control = nullptr;
    v->options_menu(); // should early-return without hanging
    v->control = saved;
}


TEST(ViewGetKeypressAndEdgeCases, viewscreen_input_switch_control_not_in_oblist_logs_and_returns)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "view should exist";
    if (!v)
        return;

    KeystateOverride ks;

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


TEST(ViewGetKeypressAndEdgeCases, viewscreen_options_menu_covers_view_size_label_cases)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "view should exist";
    if (!v)
        return;

    // Ensure a control exists so options_menu does not early-return.
    if (!v->control)
    {
        walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_TRUE(w != nullptr) << "control created";
        if (!w)
            return;
        w->set_team_num(0);
        v->control = w;
    }

    KeystateOverride ks;

    // Save original pref so we can restore after testing edge cases.
    const signed char saved_view = v->prefs[PREF_VIEW];

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
        std::atomic<bool> done{false};
        EscapePulseState state{&ks.fake, &done};
        SDL_Thread* th = SDL_CreateThread(injector_pulse_escape, "pulse_esc", &state);
        ASSERT_TRUE(th != nullptr) << "escape injector thread started";
        v->options_menu();
        done.store(true, std::memory_order_relaxed);
        int code = 0;
        if (th)
            SDL_WaitThread(th, &code);
        ks.fake[KEYSTATE_ESCAPE] = false;
    }

    // Restore original pref — options_menu() persists prefs into the session
    // prefs store (prefsob->save) on every call, so the bogus value 99 was
    // persisted.  Write back a clean value AND re-sync the store: later
    // viewscreen constructions reload prefs from the store, so a stale view
    // size would otherwise resurface (and this test's own resize(saved_view)
    // restore would apply it to the shared viewport geometry).
    v->prefs[PREF_VIEW] = saved_view;
    v->resize(saved_view);
    og::runtime::current_session->theprefs_->save(v);
}
