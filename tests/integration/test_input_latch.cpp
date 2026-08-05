// Missed-keyup latch resilience (the 3-player iPad arrow-key bug).
//
// User repro, real device: 3-player split-screen in the browser build on an
// iPad with a shared hardware keyboard. The arrow-key seat's UP and DOWN
// stop responding after a while (LEFT/RIGHT keep working); separately the
// same seat was once seen stuck walking UP with no key held.
//
// Mechanism (one bug, two moments): iPad Safari can swallow a keyup (system
// gestures, the Globe/Cmd HUD, shared-keyboard mashing). SDL's keyboard-state
// array then keeps the scancode down forever, and held[] is re-derived from
// that array every sample, so:
//   - a latched UP alone     -> constant walkstep(0,-1): "stuck walking up";
//   - latched UP + real DOWN -> PlayerInput::move_y() nets -1+1 == 0: DOWN
//     looks dead, and re-pressing UP changes nothing (state already down),
//     so UP looks dead too;
//   - LEFT/RIGHT live on the independent X axis: they keep working.
//
// These tests pin the two recovery behaviors:
//   1. last-press priority: a fresh opposite press must win over a stale
//      opposite hold instead of netting to zero (both axes), while
//      correctly-delivered keyups and simultaneous-press cancellation keep
//      their existing semantics;
//   2. focus/visibility loss clears all transient input (SDL keyboard state,
//      the touch seam, direction shaping state).
//
// The keystate timelines poke SDL's real keyboard-state array (the exact
// state a swallowed browser keyup leaves behind) and sample through the real
// input_state_from_sdl pipeline.

#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_direction_grace.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/platform/game_session.h>
#include <openglad/resources/gloader.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
// Borrowed from test_input_keybinds.cpp: scoped raw SDL keyboard state poke.
// set(true) with no later set(false) is EXACTLY the state a swallowed
// browser keyup leaves behind: SDL believes the key is still down.
struct KeyStateGuard
{
    SDL_Scancode sc;
    bool old_value;
    int numkeys;
    bool* keys;

    explicit KeyStateGuard(SDL_Scancode sc_)
        : sc(sc_), old_value(false), numkeys(0), keys(nullptr)
    {
        const bool* read_only = SDL_GetKeyboardState(&numkeys);
        keys = const_cast<bool*>(read_only);
        if (keys && sc >= 0 && sc < numkeys)
            old_value = keys[sc];
    }

    void set(bool pressed)
    {
        if (keys && sc >= 0 && sc < numkeys)
            keys[sc] = pressed;
    }

    ~KeyStateGuard()
    {
        if (keys && sc >= 0 && sc < numkeys)
            keys[sc] = old_value;
    }
};

struct KeyBindingGuard
{
    int player;
    int key_enum;
    int old;

    KeyBindingGuard(int player_, int key_enum_, int new_key)
        : player(player_), key_enum(key_enum_),
          old(og::runtime::current_session->player_keys_[player_][key_enum_])
    {
        og::runtime::current_session->player_keys_[player][key_enum] = new_key;
    }

    ~KeyBindingGuard()
    {
        og::runtime::current_session->player_keys_[player][key_enum] = old;
    }
};

struct ControlModeGuard
{
    int player;
    int old_mode;

    explicit ControlModeGuard(int player_)
        : player(player_), old_mode(get_player_control_mode(player_))
    {}

    ~ControlModeGuard()
    {
        set_player_control_mode(player, old_mode);
    }
};

// Zeroes the per-player direction shaping state on entry and exit so
// scripted timelines are deterministic and no state leaks between tests.
struct GraceStateGuard
{
    GraceStateGuard() { reset(); }
    ~GraceStateGuard() { reset(); }
    static void reset()
    {
        for (auto& g : input_hardware_state().direction_grace)
            g = DirectionGraceState{};
    }
};

// Clears the touch-overlay held-key seam on scope exit.
struct TouchKeystateGuard
{
    ~TouchKeystateGuard()
    {
        InputHardwareState& hw = input_hardware_state();
        for (int p = 0; p < 4; ++p)
            for (int k = 0; k < NUM_KEYS; ++k)
                hw.touch_keystate[p][k] = false;
    }
};

// Borrowed from test_input_keybinds.cpp: snapshot/restore of the complete
// per-player control configuration (used by the tests that reset to factory
// defaults).
struct FullControlSnapshotGuard
{
    int modes[4];
    int default_profiles[4];
    int mode4[4][NUM_KEYS];
    int mode8[4][NUM_KEYS];
    int active[4][NUM_KEYS];
    JoyData joy[4];
    DirectionGraceState direction_grace[4];
    bool touch_keystate[4][NUM_KEYS];

    FullControlSnapshotGuard()
    {
        InputHardwareState& hw = input_hardware_state();
        for (int p = 0; p < 4; ++p)
        {
            modes[p] = get_player_control_mode(p);
            default_profiles[p] = hw.player_control_default_profiles[p];
            joy[p] = player_joy[p];
            direction_grace[p] = hw.direction_grace[p];
            for (int k = 0; k < NUM_KEYS; ++k)
            {
                mode4[p][k] = get_player_key_binding_for_mode(
                    p, static_cast<int>(ControlDirectionMode::FourDirection), k);
                mode8[p][k] = get_player_key_binding_for_mode(
                    p, static_cast<int>(ControlDirectionMode::EightDirection), k);
                active[p][k] =
                    og::runtime::current_session->player_keys_[p][k];
                touch_keystate[p][k] = hw.touch_keystate[p][k];
            }
        }
    }

    void restore()
    {
        InputHardwareState& hw = input_hardware_state();
        for (int p = 0; p < 4; ++p)
        {
            hw.player_control_default_profiles[p] = default_profiles[p];
            set_player_control_mode(p, static_cast<int>(ControlDirectionMode::FourDirection));
            for (int k = 0; k < NUM_KEYS; ++k)
                set_player_key_binding(p, k, mode4[p][k]);
            set_player_control_mode(p, static_cast<int>(ControlDirectionMode::EightDirection));
            for (int k = 0; k < NUM_KEYS; ++k)
                set_player_key_binding(p, k, mode8[p][k]);
            set_player_control_mode(p, modes[p]);
            for (int k = 0; k < NUM_KEYS; ++k)
            {
                og::runtime::current_session->player_keys_[p][k] = active[p][k];
                hw.touch_keystate[p][k] = touch_keystate[p][k];
            }
            player_joy[p] = joy[p];
            hw.direction_grace[p] = direction_grace[p];
        }
    }

    ~FullControlSnapshotGuard() { restore(); }
};

SDL_Scancode scan_of(SDL_Keycode key)
{
    return SDL_GetScancodeFromKey(key, nullptr);
}
} // namespace

// ---------------------------------------------------------------------------
// Last-press priority over a stale opposite hold (the recovery the iPad
// player needs when a keyup was swallowed and no focus event will arrive).
// ---------------------------------------------------------------------------

TEST(InputLatch, fresh_down_press_beats_stale_up_latch_full_arc)
{
    disablePlayerJoystick(0);
    GraceStateGuard grace;
    ControlModeGuard mode_guard(0);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    KeyBindingGuard bind_up(0, KEY_UP, SDLK_W);
    KeyBindingGuard bind_down(0, KEY_DOWN, SDLK_S);

    KeyStateGuard ks_w(scan_of(SDLK_W));
    KeyStateGuard ks_s(scan_of(SDLK_S));
    ks_w.set(false);
    ks_s.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input); // settle: all released

    // UP goes down; its keyup is swallowed (ks_w is never set false below
    // until the very end). The character walks up every sample: the
    // observed "stuck walking up" moment.
    ks_w.set(true);
    for (int t = 0; t < 3; ++t)
    {
        input_state_from_sdl(input);
        ASSERT_EQ(-1, input.players[0].move_y())
            << "latched UP alone must walk up (stuck-walking-up shape), tick " << t;
        ASSERT_EQ(0, input.players[0].move_x());
    }

    // The player presses DOWN (a real, delivered keydown). Today this nets
    // -1+1 == 0 — "DOWN is dead". A fresh press must beat the stale hold.
    ks_s.set(true);
    input_state_from_sdl(input);
    ASSERT_EQ(1, input.players[0].move_y())
        << "fresh DOWN press must override the stale UP latch, not cancel";
    ASSERT_TRUE(input.players[0].pressed[KEY_DOWN])
        << "the fresh DOWN press should edge normally";

    // Priority persists while DOWN stays physically held.
    for (int t = 0; t < 3; ++t)
    {
        input_state_from_sdl(input);
        ASSERT_EQ(1, input.players[0].move_y())
            << "DOWN must keep winning while held, tick " << t;
    }

    // DOWN released (delivered keyup): the stale UP latch resumes walking
    // up. That residual is expected — it is healed by the focus-loss reset
    // or the web shell's stale-key release, not by this layer.
    ks_s.set(false);
    input_state_from_sdl(input);
    ASSERT_EQ(-1, input.players[0].move_y())
        << "with only the stale UP left, the latch resumes (documented residual)";

    // Re-asserting DOWN works every time.
    ks_s.set(true);
    input_state_from_sdl(input);
    ASSERT_EQ(1, input.players[0].move_y())
        << "re-pressing DOWN must re-assert priority";

    // The swallowed UP keyup finally arrives (delivered): normal keyup
    // semantics — DOWN, still held, keeps moving down.
    ks_w.set(false);
    input_state_from_sdl(input);
    ASSERT_EQ(1, input.players[0].move_y())
        << "a delivered UP keyup must leave the held DOWN in effect";

    ks_s.set(false);
    input_state_from_sdl(input);
    ASSERT_EQ(0, input.players[0].move_y());
}

TEST(InputLatch, fresh_up_press_beats_stale_down_latch)
{
    disablePlayerJoystick(0);
    GraceStateGuard grace;
    ControlModeGuard mode_guard(0);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    KeyBindingGuard bind_up(0, KEY_UP, SDLK_W);
    KeyBindingGuard bind_down(0, KEY_DOWN, SDLK_S);

    KeyStateGuard ks_w(scan_of(SDLK_W));
    KeyStateGuard ks_s(scan_of(SDLK_S));
    ks_w.set(false);
    ks_s.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input);

    ks_s.set(true); // DOWN latches (keyup swallowed)
    for (int t = 0; t < 3; ++t)
    {
        input_state_from_sdl(input);
        ASSERT_EQ(1, input.players[0].move_y());
    }

    ks_w.set(true); // fresh UP press
    input_state_from_sdl(input);
    ASSERT_EQ(-1, input.players[0].move_y())
        << "fresh UP press must override the stale DOWN latch";
}

TEST(InputLatch, x_axis_priority_and_cross_axis_independence)
{
    disablePlayerJoystick(0);
    GraceStateGuard grace;
    ControlModeGuard mode_guard(0);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    KeyBindingGuard bind_up(0, KEY_UP, SDLK_W);
    KeyBindingGuard bind_left(0, KEY_LEFT, SDLK_A);
    KeyBindingGuard bind_right(0, KEY_RIGHT, SDLK_D);

    KeyStateGuard ks_w(scan_of(SDLK_W));
    KeyStateGuard ks_a(scan_of(SDLK_A));
    KeyStateGuard ks_d(scan_of(SDLK_D));
    ks_w.set(false);
    ks_a.set(false);
    ks_d.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input);

    // The same latch on the X axis: LEFT stuck, fresh RIGHT must win.
    ks_a.set(true); // LEFT latches
    for (int t = 0; t < 3; ++t)
    {
        input_state_from_sdl(input);
        ASSERT_EQ(-1, input.players[0].move_x());
    }
    ks_d.set(true); // fresh RIGHT press
    input_state_from_sdl(input);
    ASSERT_EQ(1, input.players[0].move_x())
        << "fresh RIGHT press must override the stale LEFT latch";
    ks_d.set(false);
    ks_a.set(false);
    input_state_from_sdl(input);
    ASSERT_EQ(0, input.players[0].move_x());

    // Cross-axis independence: while UP is latched, LEFT/RIGHT keep
    // working (the reported symptom's other half).
    ks_w.set(true); // UP latches
    input_state_from_sdl(input);
    ASSERT_EQ(-1, input.players[0].move_y());
    ks_d.set(true); // RIGHT pressed: rides the stuck UP as a diagonal
    input_state_from_sdl(input);
    ASSERT_EQ(1, input.players[0].move_x())
        << "X axis must stay live while UP is latched";
    ASSERT_EQ(-1, input.players[0].move_y());
}

TEST(InputLatch, simultaneous_opposite_presses_still_cancel)
{
    // Legacy semantics pin: two genuinely simultaneous opposite presses
    // (same sample) cancel, exactly as before the latch fix — priority only
    // engages when one side is fresh and the other stale.
    disablePlayerJoystick(0);
    GraceStateGuard grace;
    ControlModeGuard mode_guard(0);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    KeyBindingGuard bind_up(0, KEY_UP, SDLK_W);
    KeyBindingGuard bind_down(0, KEY_DOWN, SDLK_S);

    KeyStateGuard ks_w(scan_of(SDLK_W));
    KeyStateGuard ks_s(scan_of(SDLK_S));
    ks_w.set(false);
    ks_s.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input);

    ks_w.set(true);
    ks_s.set(true); // both rise in the same sample
    for (int t = 0; t < 3; ++t)
    {
        input_state_from_sdl(input);
        ASSERT_EQ(0, input.players[0].move_y())
            << "simultaneous opposite presses must cancel, tick " << t;
    }

    // A correctly-delivered keyup then leaves the survivor in effect.
    ks_w.set(false);
    input_state_from_sdl(input);
    ASSERT_EQ(1, input.players[0].move_y())
        << "releasing UP must leave the held DOWN walking";
    ks_s.set(false);
    input_state_from_sdl(input);
    ASSERT_EQ(0, input.players[0].move_y());
}

TEST(InputLatch, arrow_seat_default_bindings_recover_up_down)
{
    // The reported seat exactly: seat index 1 (P2) whose factory default
    // profile is the arrow cluster. UP's keyup is swallowed; DOWN must
    // still respond on the real default bindings.
    FullControlSnapshotGuard controls;
    reset_default_player_controls();
    disablePlayerJoystick(1);
    GraceStateGuard grace;
    TouchKeystateGuard touch;

    KeyStateGuard ks_up(scan_of(SDLK_UP));
    KeyStateGuard ks_down(scan_of(SDLK_DOWN));
    ks_up.set(false);
    ks_down.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input);

    ks_up.set(true); // UP latches (keyup swallowed)
    for (int t = 0; t < 3; ++t)
    {
        input_state_from_sdl(input);
        ASSERT_EQ(-1, input.players[1].move_y())
            << "arrow seat: latched UP walks up, tick " << t;
    }

    ks_down.set(true); // the player hammers DOWN
    input_state_from_sdl(input);
    ASSERT_EQ(1, input.players[1].move_y())
        << "arrow seat: fresh DOWN must recover control from the stale UP";
}

// ---------------------------------------------------------------------------
// Focus/visibility loss clears transient input (the other recovery path:
// system gestures and app switches that DO produce a window event).
// ---------------------------------------------------------------------------

namespace
{
void send_window_event(Uint32 type)
{
    SDL_Event event;
    SDL_memset(&event, 0, sizeof(event));
    event.type = type;
    handle_window_event(static_cast<const void*>(&event));
}
} // namespace

TEST(InputLatch, focus_loss_clears_latched_keyboard_touch_and_grace)
{
    disablePlayerJoystick(0);
    GraceStateGuard grace;
    ControlModeGuard mode_guard(0);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    KeyBindingGuard bind_up(0, KEY_UP, SDLK_W);
    TouchKeystateGuard touch;

    KeyStateGuard ks_w(scan_of(SDLK_W));
    ks_w.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input);

    // Latch UP, hold FIRE on the touch seam, and give the direction-grace
    // shaper some retained state.
    ks_w.set(true);
    input_state_from_sdl(input);
    ASSERT_TRUE(input.players[0].held[KEY_UP]);
    input_hardware_state().touch_keystate[0][KEY_FIRE] = true;
    input_hardware_state().direction_grace[0].hold_mask = 0x03;
    input_hardware_state().direction_grace[0].ticks_left = 2;

    send_window_event(SDL_EVENT_WINDOW_FOCUS_LOST);

    ASSERT_TRUE(!isPlayerHoldingKey(0, KEY_UP))
        << "focus loss must clear SDL's latched keyboard state";
    ASSERT_TRUE(!input_hardware_state().touch_keystate[0][KEY_FIRE])
        << "focus loss must clear the touch held-key seam";
    ASSERT_EQ(0, static_cast<int>(input_hardware_state().direction_grace[0].hold_mask))
        << "focus loss must drop direction-grace retention";

    input_state_from_sdl(input);
    ASSERT_TRUE(!input.players[0].held[KEY_UP])
        << "after focus loss nothing should be reported held";
    ASSERT_EQ(0, input.players[0].move_y());
}

TEST(InputLatch, visibility_hidden_clears_latched_keyboard)
{
    disablePlayerJoystick(0);
    GraceStateGuard grace;
    ControlModeGuard mode_guard(0);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    KeyBindingGuard bind_up(0, KEY_UP, SDLK_W);

    KeyStateGuard ks_w(scan_of(SDLK_W));
    ks_w.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input);

    ks_w.set(true);
    input_state_from_sdl(input);
    ASSERT_TRUE(input.players[0].held[KEY_UP]);

    // On the web, visibilitychange delivers only HIDDEN (no blur, so SDL's
    // own blur-reset never runs). The app must clear held state itself.
    send_window_event(SDL_EVENT_WINDOW_HIDDEN);

    input_state_from_sdl(input);
    ASSERT_TRUE(!input.players[0].held[KEY_UP])
        << "WINDOW_HIDDEN must clear latched keyboard state";
    ASSERT_EQ(0, input.players[0].move_y());
}

// ---------------------------------------------------------------------------
// Sim linkage: the latched-UP walker really walks, and the DOWN re-assert
// really turns the walk around (walkstep intent via lastx/lasty).
// ---------------------------------------------------------------------------

TEST(InputLatch, sim_stuck_walking_up_recovers_on_down_press)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr);
    auto w = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr);
    w->set_team_num(0);
    w->set_real_team_num(255);
    w->set_dead(0);
    w->set_user(0);
    w->setxy(100, 100);
    w->set_act_type(ACT_CONTROL);
    walker* control = w.get();
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w));

    disablePlayerJoystick(0);
    GraceStateGuard grace;
    ControlModeGuard mode_guard(0);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    KeyBindingGuard bind_up(0, KEY_UP, SDLK_W);
    KeyBindingGuard bind_down(0, KEY_DOWN, SDLK_S);

    KeyStateGuard ks_w(scan_of(SDLK_W));
    KeyStateGuard ks_s(scan_of(SDLK_S));
    ks_w.set(false);
    ks_s.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input);

    SimInputDebounce debounce = {};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS];
    og::sim::SimEventLog log;

    // Latched UP: the sim walks the character up (stuck-walking-up).
    ks_w.set(true);
    input_state_from_sdl(input);
    sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    ASSERT_TRUE(control->lasty() < 0.0f)
        << "latched UP must produce an upward walkstep";

    // The player presses DOWN. Today move_y() nets to 0, walkstep is never
    // called, and the character just stands: "DOWN is dead".
    control->set_lastx(0.0f);
    control->set_lasty(0.0f);
    ks_s.set(true);
    input_state_from_sdl(input);
    sim_process_player_input(
        input.players[0], control, og::runtime::current_session->myscreen_->world(),
        0, 0, debounce, special_names, &log);
    ASSERT_TRUE(control->lasty() > 0.0f)
        << "fresh DOWN must walk the character down despite the stale UP latch";

    if (og::runtime::current_session->myscreen_)
        og::runtime::current_session->myscreen_->world().delete_objects();
}

// ---------------------------------------------------------------------------
// Pure resolver timelines (resolve_opposing_directions), including the
// group semantics the end-to-end samples cannot isolate: diagonal keys are
// suppressed with their whole side, and any key of a side re-asserts it.
// ---------------------------------------------------------------------------

namespace
{
constexpr std::uint8_t bit_of(int key_slot)
{
    return static_cast<std::uint8_t>(1u << key_slot);
}
constexpr std::uint8_t kUpBit = bit_of(KEY_UP);
constexpr std::uint8_t kDownBit = bit_of(KEY_DOWN);
constexpr std::uint8_t kLeftBit = bit_of(KEY_LEFT);
constexpr std::uint8_t kUpLeftBit = bit_of(KEY_UP_LEFT);
constexpr std::uint8_t kUpRightBit = bit_of(KEY_UP_RIGHT);
constexpr std::uint8_t kDownRightBit = bit_of(KEY_DOWN_RIGHT);
} // namespace

TEST(InputLatch, resolver_fresh_press_wins_and_release_restores)
{
    DirectionConflictState state;

    // UP latches; a lone side passes through untouched.
    for (int t = 0; t < 3; ++t)
        ASSERT_EQ(kUpBit, resolve_opposing_directions(kUpBit, state));

    // Fresh DOWN suppresses the stale UP for as long as it is held.
    ASSERT_EQ(kDownBit, resolve_opposing_directions(
        static_cast<std::uint8_t>(kUpBit | kDownBit), state));
    ASSERT_EQ(kDownBit, resolve_opposing_directions(
        static_cast<std::uint8_t>(kUpBit | kDownBit), state));

    // DOWN released: conflict over, the (still latched) UP passes again.
    ASSERT_EQ(kUpBit, resolve_opposing_directions(kUpBit, state));

    // Full release resets everything.
    ASSERT_EQ(0, resolve_opposing_directions(0, state));
    ASSERT_EQ(0, static_cast<int>(state.y_winner));
}

TEST(InputLatch, resolver_same_sample_tie_keeps_cancellation)
{
    DirectionConflictState state;
    const std::uint8_t both = static_cast<std::uint8_t>(kUpBit | kDownBit);
    // Both rise in one sample: legacy cancellation (mask passes through and
    // move_y() nets it to zero), and it stays canceled while held.
    ASSERT_EQ(both, resolve_opposing_directions(both, state));
    ASSERT_EQ(both, resolve_opposing_directions(both, state));
    // Releasing one side leaves the other in effect.
    ASSERT_EQ(kDownBit, resolve_opposing_directions(kDownBit, state));
}

TEST(InputLatch, resolver_suppresses_stale_diagonal_with_its_whole_side)
{
    DirectionConflictState state;

    // A latched UP_LEFT is presumed phantom once DOWN arrives fresh: it
    // must stop contributing to BOTH axes (no leftover LEFT drift).
    resolve_opposing_directions(kUpLeftBit, state);
    ASSERT_EQ(kDownBit, resolve_opposing_directions(
        static_cast<std::uint8_t>(kUpLeftBit | kDownBit), state));

    // Double conflict: latched UP_LEFT vs a fresh DOWN_RIGHT press — the
    // fresh key wins both axes.
    state = DirectionConflictState{};
    resolve_opposing_directions(kUpLeftBit, state);
    ASSERT_EQ(kDownRightBit, resolve_opposing_directions(
        static_cast<std::uint8_t>(kUpLeftBit | kDownRightBit), state));
}

TEST(InputLatch, resolver_any_key_of_a_side_reasserts_it)
{
    DirectionConflictState state;

    // UP latched, DOWN fresh: down side wins.
    resolve_opposing_directions(kUpBit, state);
    ASSERT_EQ(kDownBit, resolve_opposing_directions(
        static_cast<std::uint8_t>(kUpBit | kDownBit), state));

    // UP_RIGHT pressed (a different key of the up side): the up side is
    // fresh again and takes the axis back; its RIGHT contribution stays.
    const std::uint8_t all = static_cast<std::uint8_t>(kUpBit | kDownBit | kUpRightBit);
    ASSERT_EQ(static_cast<std::uint8_t>(kUpBit | kUpRightBit),
              resolve_opposing_directions(all, state));
}

TEST(InputLatch, resolver_x_axis_is_symmetric)
{
    DirectionConflictState state;
    resolve_opposing_directions(kLeftBit, state); // LEFT latches
    const std::uint8_t both = static_cast<std::uint8_t>(kLeftBit | bit_of(KEY_RIGHT));
    ASSERT_EQ(bit_of(KEY_RIGHT), resolve_opposing_directions(both, state))
        << "fresh RIGHT must suppress the stale LEFT";
    ASSERT_EQ(kLeftBit, resolve_opposing_directions(kLeftBit, state))
        << "releasing RIGHT restores the surviving LEFT";
}

// ---------------------------------------------------------------------------
// Cancel mode (issue #157): on platforms with unreliable keyups
// (hw().unreliable_keyups, true by default only on web builds) the
// suppression of the stale side is PERSISTENT — releasing the corrective
// key leaves the phantom dead instead of resuming it — and a cancelled key
// revives only via a delivered keyup or a real key EVENT
// (note_direction_key_event; SDL posts repeat=true keydowns for re-presses
// of an already-down scancode, the only signal a swallowed keyup leaves).
// Native (unreliable_keyups=false) keeps the PR #147 semantics pinned
// above, including the documented residual.
// ---------------------------------------------------------------------------

namespace
{
// Flips the unreliable-keyups platform flag for one test and zeroes the
// per-player conflict state on entry and exit so cancel-mode timelines are
// deterministic and nothing leaks into the legacy-mode tests.
struct UnreliableKeyupsGuard
{
    bool old_value;

    explicit UnreliableKeyupsGuard(bool enable)
        : old_value(input_hardware_state().unreliable_keyups)
    {
        input_hardware_state().unreliable_keyups = enable;
        reset_conflicts();
    }

    ~UnreliableKeyupsGuard()
    {
        input_hardware_state().unreliable_keyups = old_value;
        reset_conflicts();
    }

    static void reset_conflicts()
    {
        for (auto& c : input_hardware_state().direction_conflict)
            c = DirectionConflictState{};
    }
};
} // namespace

TEST(InputLatchCancel, native_build_defaults_to_legacy_mode)
{
    // A fresh InputHardwareState on a native build starts with reliable
    // keyups: the resolver keeps PR #147 last-win-with-restore semantics.
    InputHardwareState fresh{};
    ASSERT_FALSE(fresh.unreliable_keyups)
        << "native builds must not enable persistent cancellation";
}

TEST(InputLatchCancel, fresh_press_kills_stale_side_permanently)
{
    DirectionConflictState state;

    for (int t = 0; t < 3; ++t)
        ASSERT_EQ(kUpBit, resolve_opposing_directions(kUpBit, state, true))
            << "a lone side passes through, tick " << t;

    const std::uint8_t both = static_cast<std::uint8_t>(kUpBit | kDownBit);
    ASSERT_EQ(kDownBit, resolve_opposing_directions(both, state, true))
        << "fresh DOWN must cancel the stale UP";
    ASSERT_EQ(kDownBit, resolve_opposing_directions(both, state, true));

    // DOWN released: the phantom UP stays DEAD — the exact inverse of the
    // legacy documented residual pinned in fresh_down_press_beats_stale_up_
    // latch_full_arc.
    for (int t = 0; t < 3; ++t)
        ASSERT_EQ(0, resolve_opposing_directions(kUpBit, state, true))
            << "the cancelled phantom must not resume walking, tick " << t;
}

TEST(InputLatchCancel, delivered_keyup_forgives_cancelled_key)
{
    DirectionConflictState state;
    resolve_opposing_directions(kUpBit, state, true);
    resolve_opposing_directions(
        static_cast<std::uint8_t>(kUpBit | kDownBit), state, true); // UP cancelled

    // UP's keyup finally arrives (bit leaves raw): the record corrected
    // itself, the cancel is forgiven.
    ASSERT_EQ(kDownBit, resolve_opposing_directions(kDownBit, state, true));
    ASSERT_EQ(0, static_cast<int>(state.cancelled));

    // A later real UP press (keystate edge) passes cleanly.
    ASSERT_EQ(0, resolve_opposing_directions(0, state, true));
    ASSERT_EQ(kUpBit, resolve_opposing_directions(kUpBit, state, true));
}

TEST(InputLatchCancel, event_edge_revives_cancelled_key_and_flips_cancel)
{
    DirectionConflictState state;
    const std::uint8_t both = static_cast<std::uint8_t>(kUpBit | kDownBit);
    resolve_opposing_directions(kUpBit, state, true);
    ASSERT_EQ(kDownBit, resolve_opposing_directions(both, state, true)); // UP cancelled

    // A real UP keydown EVENT arrives (SDL repeat=true re-press) while both
    // stay in the raw mask: UP revives and the cancel flips onto DOWN.
    state.event_edges = kUpBit;
    ASSERT_EQ(kUpBit, resolve_opposing_directions(both, state, true))
        << "the event edge must revive the cancelled UP and cancel DOWN";
    ASSERT_EQ(kUpBit, resolve_opposing_directions(both, state, true))
        << "the flipped cancel persists without further edges";
}

TEST(InputLatchCancel, same_sample_tie_keeps_net_zero_without_cancel)
{
    DirectionConflictState state;
    const std::uint8_t both = static_cast<std::uint8_t>(kUpBit | kDownBit);
    // Both rise in one sample: legacy cancellation (mask passes through,
    // move_y() nets to zero) and NO persistent cancel is recorded.
    ASSERT_EQ(both, resolve_opposing_directions(both, state, true));
    ASSERT_EQ(both, resolve_opposing_directions(both, state, true));
    ASSERT_EQ(0, static_cast<int>(state.cancelled));
    // Releasing one side leaves the other in effect.
    ASSERT_EQ(kDownBit, resolve_opposing_directions(kDownBit, state, true));
}

TEST(InputLatchCancel, stale_diagonal_cancelled_with_whole_side_kills_drift)
{
    DirectionConflictState state;
    resolve_opposing_directions(kUpLeftBit, state, true); // UP_LEFT latches
    ASSERT_EQ(kDownBit, resolve_opposing_directions(
        static_cast<std::uint8_t>(kUpLeftBit | kDownBit), state, true))
        << "fresh DOWN must cancel the whole stale diagonal";

    // DOWN released: the phantom UP_LEFT is dead on BOTH axes.
    ASSERT_EQ(0, resolve_opposing_directions(kUpLeftBit, state, true));

    // LEFT pressed for real: pure left — the cancelled UP_LEFT must not
    // bleed an upward component in (the permanent up-left drift of gap 3).
    ASSERT_EQ(kLeftBit, resolve_opposing_directions(
        static_cast<std::uint8_t>(kUpLeftBit | kLeftBit), state, true));
}

TEST(InputLatchCancel, event_edge_for_unheld_key_is_ignored_and_consumed)
{
    DirectionConflictState state;
    resolve_opposing_directions(kUpBit, state, true);
    // A DOWN event whose key never entered the sampled mask (pressed and
    // released between samples) must not fabricate a conflict.
    state.event_edges = kDownBit;
    ASSERT_EQ(kUpBit, resolve_opposing_directions(kUpBit, state, true));
    ASSERT_EQ(0, static_cast<int>(state.cancelled));
    ASSERT_EQ(0, static_cast<int>(state.event_edges))
        << "event edges are consumed exactly once per sample";
}

TEST(InputLatchCancel, cross_axis_heal_end_to_end)
{
    // The full user story through the real sampling pipeline: latched UP,
    // one corrective DOWN tap, then LEFT alone must give pure leftward
    // movement (no up-left drift, no dead axis).
    disablePlayerJoystick(0);
    UnreliableKeyupsGuard unreliable(true);
    GraceStateGuard grace;
    ControlModeGuard mode_guard(0);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    KeyBindingGuard bind_up(0, KEY_UP, SDLK_W);
    KeyBindingGuard bind_down(0, KEY_DOWN, SDLK_S);
    KeyBindingGuard bind_left(0, KEY_LEFT, SDLK_A);

    KeyStateGuard ks_w(scan_of(SDLK_W));
    KeyStateGuard ks_s(scan_of(SDLK_S));
    KeyStateGuard ks_a(scan_of(SDLK_A));
    ks_w.set(false);
    ks_s.set(false);
    ks_a.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input);

    ks_w.set(true); // UP latches (keyup swallowed)
    input_state_from_sdl(input);
    ASSERT_EQ(-1, input.players[0].move_y());

    ks_s.set(true); // corrective DOWN tap
    input_state_from_sdl(input);
    ASSERT_EQ(1, input.players[0].move_y());

    ks_s.set(false); // tap ends: the axis must be DEAD, not resume walking up
    input_state_from_sdl(input);
    ASSERT_EQ(0, input.players[0].move_y())
        << "cancel mode must keep the phantom UP dead after the tap";

    ks_a.set(true); // LEFT alone
    input_state_from_sdl(input);
    ASSERT_EQ(-1, input.players[0].move_x());
    ASSERT_EQ(0, input.players[0].move_y())
        << "no up-left drift from the cancelled phantom";
}

TEST(InputLatchCancel, event_repress_revives_cancelled_direction_end_to_end)
{
    disablePlayerJoystick(0);
    UnreliableKeyupsGuard unreliable(true);
    GraceStateGuard grace;
    ControlModeGuard mode_guard(0);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    KeyBindingGuard bind_up(0, KEY_UP, SDLK_W);
    KeyBindingGuard bind_down(0, KEY_DOWN, SDLK_S);

    KeyStateGuard ks_w(scan_of(SDLK_W));
    KeyStateGuard ks_s(scan_of(SDLK_S));
    ks_w.set(false);
    ks_s.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input);

    ks_w.set(true); // UP latches
    input_state_from_sdl(input);
    ks_s.set(true); // DOWN cancels UP
    input_state_from_sdl(input);
    ks_s.set(false); // axis dead
    input_state_from_sdl(input);
    ASSERT_EQ(0, input.players[0].move_y());

    // The player genuinely presses UP again. The keystate cannot edge (it
    // never went up), but the browser DOES deliver the keydown event — the
    // event-layer feed revives the direction.
    note_direction_key_event(static_cast<int>(SDLK_W));
    input_state_from_sdl(input);
    ASSERT_EQ(-1, input.players[0].move_y())
        << "a real UP key event must revive the cancelled UP";
}

TEST(InputLatchCancel, key_event_pump_feeds_event_edges_per_player_binding)
{
    // Event wiring: a crafted SDL keydown through handle_key_event (never
    // SDL_PushEvent + GetKeyboardState — pushed events cannot set SDL's
    // keystate) must mark the event-edge bit for the player/slot bound to
    // that keycode, and only for them.
    UnreliableKeyupsGuard unreliable(true); // zeroes all conflict state
    KeyBindingGuard bind_up(0, KEY_UP, SDLK_W);

    SDL_Event event;
    SDL_memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_W;
    event.key.repeat = true; // repeats are the swallowed-keyup re-press signal
    handle_key_event(static_cast<const void*>(&event));

    InputHardwareState& hw = input_hardware_state();
    ASSERT_EQ(1 << KEY_UP,
              static_cast<int>(hw.direction_conflict[0].event_edges))
        << "P1's UP slot (bound to W) must edge";
    for (int p = 1; p < 4; ++p)
        ASSERT_EQ(0, static_cast<int>(hw.direction_conflict[p].event_edges))
            << "player " << (p + 1) << " has no W direction binding";

    // KEYCODE_UNKNOWN (0) must never match, even though unbound direction
    // slots (e.g. 4-dir diagonals) store exactly that value.
    UnreliableKeyupsGuard::reset_conflicts();
    SDL_memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_UNKNOWN;
    handle_key_event(static_cast<const void*>(&event));
    for (int p = 0; p < 4; ++p)
        ASSERT_EQ(0, static_cast<int>(hw.direction_conflict[p].event_edges))
            << "an unknown keycode must not edge unbound slots";
}

TEST(InputLatchCancel, focus_loss_clears_cancel_and_event_state)
{
    UnreliableKeyupsGuard unreliable(true);
    InputHardwareState& hw = input_hardware_state();
    hw.direction_conflict[0].cancelled = kUpBit;
    hw.direction_conflict[0].event_edges = kDownBit;

    send_window_event(SDL_EVENT_WINDOW_FOCUS_LOST);

    ASSERT_EQ(0, static_cast<int>(hw.direction_conflict[0].cancelled))
        << "focus loss must drop persistent cancels";
    ASSERT_EQ(0, static_cast<int>(hw.direction_conflict[0].event_edges))
        << "focus loss must drop pending event edges";
}

// ---------------------------------------------------------------------------
// Factory default keymaps: no cross-player key collisions. On a shared
// keyboard every player's bindings are live at once (modes are per-player,
// so any 4-dir/8-dir combination can be active). A shared keycode makes one
// player's action drive another player's character.
// ---------------------------------------------------------------------------

TEST(InputKeybindDefaults, no_undocumented_cross_player_default_key_collisions)
{
    FullControlSnapshotGuard controls;
    reset_default_player_controls();

    struct Binding
    {
        int player;
        int mode;
        int key_enum;
        int keycode;
    };
    std::vector<Binding> bindings;
    const int kModes[2] = {
        static_cast<int>(ControlDirectionMode::FourDirection),
        static_cast<int>(ControlDirectionMode::EightDirection),
    };
    for (int p = 0; p < 4; ++p)
        for (int mode : kModes)
            for (int k = 0; k < NUM_KEYS; ++k)
            {
                const int kc = get_player_key_binding_for_mode(p, mode, k);
                if (kc != KEYCODE_UNKNOWN)
                    bindings.push_back(Binding{p, mode, k, kc});
            }

    // The single documented exemption: P1's look-up hold 'v' (present in
    // both of P1's mode maps) overlaps P4's 8-direction DOWN-LEFT 'v' — a
    // known pre-existing quirk (see the KEY_LOOKUP default comment in
    // input_state.cpp). Everything else must be unique across players.
    const auto is_documented_v_quirk = [](const Binding& a, const Binding& b) {
        const auto matches = [](const Binding& p1, const Binding& p4) {
            return p1.player == 0 && p1.key_enum == KEY_LOOKUP &&
                   p1.keycode == KEYCODE_v &&
                   p4.player == 3 && p4.key_enum == KEY_DOWN_LEFT &&
                   p4.mode == static_cast<int>(ControlDirectionMode::EightDirection) &&
                   p4.keycode == KEYCODE_v;
        };
        return matches(a, b) || matches(b, a);
    };

    for (std::size_t i = 0; i < bindings.size(); ++i)
        for (std::size_t j = i + 1; j < bindings.size(); ++j)
        {
            const Binding& a = bindings[i];
            const Binding& b = bindings[j];
            if (a.player == b.player || a.keycode != b.keycode)
                continue;
            if (is_documented_v_quirk(a, b))
                continue;
            ADD_FAILURE()
                << "cross-player default key collision: key '"
                << og::input_native::key_name(a.keycode)
                << "' (keycode " << a.keycode << ") is player "
                << (a.player + 1) << " mode " << a.mode << " key-slot "
                << a.key_enum << " AND player " << (b.player + 1)
                << " mode " << b.mode << " key-slot " << b.key_enum
                << " — one player's press drives another player's character "
                << "on a shared keyboard";
        }
}
