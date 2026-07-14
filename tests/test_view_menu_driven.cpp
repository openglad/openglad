#include <SDL3/SDL.h>
#include <openglad/interface/input.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> create_controlled_living(char family)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w)
        return nullptr;
    w->set_team_num(0);
    w->setxy(50, 50);
    return w;
}

struct KeystateOverride
{
    const Uint8* prev = nullptr;
    std::array<Uint8, MAXKEYS> fake{};

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

static void pulse_key(KeystateOverride& ks, int key, int hold_ms = 80, int release_ms = 30)
{
    ks.fake[static_cast<std::size_t>(key)] = 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));
    ks.fake[static_cast<std::size_t>(key)] = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(release_ms));
}

static void pulse_escape_until(KeystateOverride& ks, std::atomic<bool>& done, int timeout_ms = 5000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!done.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < deadline)
        pulse_key(ks, KEYSTATE_ESCAPE, 100, 40);
    ks.fake[KEYSTATE_ESCAPE] = 0;
}

TEST(ViewMenuDriven, viewscreen_options_menu_exercises_key_paths)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "viewob[0] should exist";

    auto control = create_controlled_living(FAMILY_SOLDIER);
    ASSERT_TRUE(control != nullptr) << "create_controlled_living should succeed";
    walker* controlp = control.get();
    v->control = controlp;

    // Save ALL per-view prefs: the driven hotkeys mutate several of them —
    // view size via the ]/[ pair (pulsed ] FIRST, so the pair nets
    // prefs[PREF_VIEW] 0 -> 1, clamped at 0 on the way down) plus the
    // r/h/f/s/c toggles — and options_menu() persists the final values into
    // the session prefs store (prefsob->save) on every exit. A poisoned
    // store leaks into every later-constructed viewscreen (the ctor loads
    // from it), so the store must be re-synced after the restore below.
    signed char saved_prefs[10];
    for (int i = 0; i < 10; i++)
        saved_prefs[i] = v->prefs[i];

    KeystateOverride ks;
    std::atomic<bool> options_done{false};

    // Drive a few option toggles then exit with Escape.
    std::thread driver([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pulse_key(ks, KEYSTATE_RIGHTBRACKET);
        pulse_key(ks, KEYSTATE_LEFTBRACKET);
        pulse_key(ks, KEYSTATE_COMMA);
        pulse_key(ks, KEYSTATE_PERIOD);
        pulse_key(ks, KEYSTATE_r);
        pulse_key(ks, KEYSTATE_h);
        pulse_key(ks, KEYSTATE_f);
        pulse_key(ks, KEYSTATE_s);
        pulse_key(ks, KEYSTATE_c);
        pulse_escape_until(ks, options_done);
    });

    // Runs a tight loop that polls keystates and draws. Our driver thread
    // flips keys to avoid getting stuck in "wait for key release" loops.
    v->options_menu();
    options_done.store(true, std::memory_order_relaxed);

    driver.join();

    // Drive set_key_prefs and view_key_bindings paths directly.
    ASSERT_EQ(1, (int)v->set_key_prefs()) << "set_key_prefs should succeed in testing mode";

    int saved_up = og::runtime::current_session->player_keys_[v->mynum][KEY_UP];
    int saved_fire = og::runtime::current_session->player_keys_[v->mynum][KEY_FIRE];
    int saved_special = og::runtime::current_session->player_keys_[v->mynum][KEY_SPECIAL];
    int saved_yell = og::runtime::current_session->player_keys_[v->mynum][KEY_YELL];
    int saved_shifter = og::runtime::current_session->player_keys_[v->mynum][KEY_SHIFTER];

    // Force key-display abbreviation and truncation branches.
    og::runtime::current_session->player_keys_[v->mynum][KEY_UP] = SDLK_BACKSPACE;      // "BkSpc"
    og::runtime::current_session->player_keys_[v->mynum][KEY_FIRE] = SDLK_LCTRL;        // "LCtrl"
    og::runtime::current_session->player_keys_[v->mynum][KEY_SPECIAL] = SDLK_RALT;      // "RAlt"
    og::runtime::current_session->player_keys_[v->mynum][KEY_YELL] = SDLK_CAPSLOCK;     // "Caps"
    og::runtime::current_session->player_keys_[v->mynum][KEY_SHIFTER] = SDLK_MEDIA_NEXT_TRACK; // long key name -> truncation

    KeystateOverride ks2;
    std::atomic<bool> bindings_done{false};
    std::thread esc_driver([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pulse_escape_until(ks2, bindings_done);
    });
    v->view_key_bindings();
    bindings_done.store(true, std::memory_order_relaxed);
    esc_driver.join();

    og::runtime::current_session->player_keys_[v->mynum][KEY_UP] = saved_up;
    og::runtime::current_session->player_keys_[v->mynum][KEY_FIRE] = saved_fire;
    og::runtime::current_session->player_keys_[v->mynum][KEY_SPECIAL] = saved_special;
    og::runtime::current_session->player_keys_[v->mynum][KEY_YELL] = saved_yell;
    og::runtime::current_session->player_keys_[v->mynum][KEY_SHIFTER] = saved_shifter;

    // Restore every pref the menu mutated, re-derive the geometry, and
    // re-sync the session prefs store options_menu persisted on exit —
    // otherwise a later viewscreen recreation (any picker game start)
    // reloads the stale view size and a subsequent resize(prefs[PREF_VIEW])
    // shifts the shared viewport geometry for the rest of the binary (the
    // og_test_view --gtest_shuffle seed-27 failure chain).
    for (int i = 0; i < 10; i++)
        v->prefs[i] = saved_prefs[i];
    v->resize(v->prefs[PREF_VIEW]);
    og::runtime::current_session->theprefs_->save(v);

    v->control = nullptr;
}
