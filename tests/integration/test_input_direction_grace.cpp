// Diagonal release retention (client-side input shaping).
//
// User repro: running diagonally on two direction keys and releasing both
// with imperfect timing (one key clears 1-3 input samples first) snapped the
// final facing to the surviving cardinal. Down-right merely *tended* to
// survive because S+D lift together more often than the up-diagonal pairs —
// the sampling pipeline itself was symmetric — so the fix must behave
// identically for all four diagonals.
//
// These tests drive deterministic scripted keystate timelines through the
// pure coalescing helper and through the real input_state_from_sdl sampler.

#include <openglad/gameplay/input_state.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_direction_grace.h>
#include <openglad/interface/session_state.h>
#include <openglad/platform/game_session.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <cstdint>

namespace
{
constexpr std::uint8_t dir_bit(int key_slot)
{
    return static_cast<std::uint8_t>(1u << key_slot);
}

constexpr std::uint8_t kUp = dir_bit(KEY_UP);
constexpr std::uint8_t kRight = dir_bit(KEY_RIGHT);
constexpr std::uint8_t kDown = dir_bit(KEY_DOWN);
constexpr std::uint8_t kLeft = dir_bit(KEY_LEFT);
constexpr std::uint8_t kUpRight = dir_bit(KEY_UP_RIGHT);

struct DiagonalCase
{
    std::uint8_t vertical;
    std::uint8_t horizontal;
    const char* name;
};

constexpr DiagonalCase kAllDiagonals[] = {
    {kUp, kRight, "up-right"},
    {kUp, kLeft, "up-left"},
    {kDown, kRight, "down-right"},
    {kDown, kLeft, "down-left"},
};

// Borrowed from test_input_keybinds.cpp: scoped raw SDL keyboard state poke.
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

// Zeroes the per-player retention state on entry and exit so scripted
// timelines are deterministic and no state leaks between tests.
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
} // namespace

// ---------------------------------------------------------------------------
// Pure helper timelines.
// ---------------------------------------------------------------------------

TEST(InputDirectionGrace, sloppy_release_keeps_diagonal_for_all_four_diagonals)
{
    for (const auto& diag : kAllDiagonals)
    {
        const std::uint8_t both = static_cast<std::uint8_t>(diag.vertical | diag.horizontal);
        // Symmetric in release order: either key of the pair may drop first.
        for (const std::uint8_t survivor : {diag.vertical, diag.horizontal})
        {
            DirectionGraceState state;
            for (int t = 0; t < 5; ++t)
                ASSERT_EQ(both, coalesce_direction_release(both, state))
                    << diag.name << ": both keys held should pass through";

            // 1-3 samples of skew (the user repro) — the diagonal must
            // survive every one of them, regardless of which key remains.
            for (int t = 0; t < 3; ++t)
                ASSERT_EQ(both, coalesce_direction_release(survivor, state))
                    << diag.name << ": sloppy release tick " << t
                    << " should still report the diagonal (survivor mask "
                    << static_cast<int>(survivor) << ")";

            // Full release: stop. The last reported movement was the
            // diagonal, so the walker's facing keeps it.
            ASSERT_EQ(0, coalesce_direction_release(0, state))
                << diag.name << ": full release should report no movement";
            ASSERT_EQ(0, static_cast<int>(state.hold_mask))
                << diag.name << ": retention should be cleared after release";
        }
    }
}

TEST(InputDirectionGrace, deliberate_turn_applies_after_grace_window)
{
    for (const auto& diag : kAllDiagonals)
    {
        const std::uint8_t both = static_cast<std::uint8_t>(diag.vertical | diag.horizontal);
        for (const std::uint8_t survivor : {diag.vertical, diag.horizontal})
        {
            DirectionGraceState state;
            for (int t = 0; t < 5; ++t)
                coalesce_direction_release(both, state);

            // Exactly kDiagonalReleaseGraceTicks samples of retention, then
            // the surviving key wins (the player really is turning). Same
            // count for every diagonal and either release order: symmetric.
            for (int t = 0; t < kDiagonalReleaseGraceTicks; ++t)
                ASSERT_EQ(both, coalesce_direction_release(survivor, state))
                    << diag.name << ": retention tick " << t;
            for (int t = 0; t < 5; ++t)
                ASSERT_EQ(survivor, coalesce_direction_release(survivor, state))
                    << diag.name << ": held past the window should turn";
        }
    }
}

TEST(InputDirectionGrace, tap_single_direction_is_unaffected)
{
    DirectionGraceState state;

    // Simple tap: idle -> right -> idle.
    ASSERT_EQ(kRight, coalesce_direction_release(kRight, state));
    ASSERT_EQ(kRight, coalesce_direction_release(kRight, state));
    ASSERT_EQ(0, coalesce_direction_release(0, state));

    // Cardinal-to-cardinal transitions pass straight through too.
    ASSERT_EQ(kUp, coalesce_direction_release(kUp, state));
    ASSERT_EQ(kLeft, coalesce_direction_release(kLeft, state));
    ASSERT_EQ(0, coalesce_direction_release(0, state));
    ASSERT_EQ(0, static_cast<int>(state.hold_mask));
}

TEST(InputDirectionGrace, new_key_press_applies_immediately_during_retention)
{
    DirectionGraceState state;
    const std::uint8_t diag = static_cast<std::uint8_t>(kUp | kRight);

    for (int t = 0; t < 3; ++t)
        coalesce_direction_release(diag, state);
    ASSERT_EQ(diag, coalesce_direction_release(kRight, state))
        << "partial release should retain the diagonal";

    // Pressing DOWN (outside the retained pair) is an intentional new
    // direction: it must not be blended with or delayed by the grace.
    const std::uint8_t turn = static_cast<std::uint8_t>(kRight | kDown);
    ASSERT_EQ(turn, coalesce_direction_release(turn, state))
        << "a new key press must cancel retention immediately";
    ASSERT_EQ(0, static_cast<int>(state.hold_mask));
}

TEST(InputDirectionGrace, repress_during_retention_returns_to_live_diagonal)
{
    DirectionGraceState state;
    const std::uint8_t diag = static_cast<std::uint8_t>(kDown | kLeft);

    for (int t = 0; t < 3; ++t)
        coalesce_direction_release(diag, state);
    ASSERT_EQ(diag, coalesce_direction_release(kDown, state));

    // Re-press of the dropped key: raw agrees with the retained diagonal
    // again, retention ends without ever having misreported.
    ASSERT_EQ(diag, coalesce_direction_release(diag, state));
    ASSERT_EQ(0, static_cast<int>(state.hold_mask))
        << "re-held diagonal should clear the retention state";

    // A later clean simultaneous release stops in one sample.
    ASSERT_EQ(0, coalesce_direction_release(0, state));
}

TEST(InputDirectionGrace, eight_direction_dedicated_key_combo_is_retained)
{
    // 8-direction mode can form a diagonal from a dedicated diagonal key
    // plus a cardinal (e.g. E + D on P1's default 8-dir cluster). Dropping
    // the diagonal key collapses the vector to a cardinal — retained.
    DirectionGraceState state;
    const std::uint8_t combo = static_cast<std::uint8_t>(kUpRight | kRight);

    for (int t = 0; t < 4; ++t)
        ASSERT_EQ(combo, coalesce_direction_release(combo, state));
    ASSERT_EQ(combo, coalesce_direction_release(kRight, state))
        << "dropping the dedicated diagonal key should retain the combo";
    ASSERT_EQ(0, coalesce_direction_release(0, state));

    // Dropping the cardinal instead leaves the dedicated diagonal key: the
    // vector is still diagonal, so no retention is needed or engaged.
    for (int t = 0; t < 4; ++t)
        coalesce_direction_release(combo, state);
    ASSERT_EQ(kUpRight, coalesce_direction_release(kUpRight, state));
    ASSERT_EQ(0, static_cast<int>(state.hold_mask));
}

// ---------------------------------------------------------------------------
// End-to-end: the real sampler (input_state_from_sdl) with scripted SDL
// keystates, P1 in 4-direction mode holding W+D (the user's repro shape).
// ---------------------------------------------------------------------------

TEST(InputDirectionGrace, input_state_from_sdl_retains_diagonal_on_sloppy_release)
{
    disablePlayerJoystick(0);
    GraceStateGuard grace;
    ControlModeGuard mode_guard(0);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    KeyBindingGuard bind_up(0, KEY_UP, SDLK_W);
    KeyBindingGuard bind_right(0, KEY_RIGHT, SDLK_D);

    KeyStateGuard ks_w(SDL_GetScancodeFromKey(SDLK_W, nullptr));
    KeyStateGuard ks_d(SDL_GetScancodeFromKey(SDLK_D, nullptr));
    ks_w.set(false);
    ks_d.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input); // settle: all released

    // Run up-right on W+D.
    ks_w.set(true);
    ks_d.set(true);
    for (int t = 0; t < 3; ++t)
    {
        input_state_from_sdl(input);
        ASSERT_TRUE(input.players[0].held[KEY_UP]) << "W held, tick " << t;
        ASSERT_TRUE(input.players[0].held[KEY_RIGHT]) << "D held, tick " << t;
    }
    ASSERT_EQ(1, input.players[0].move_x());
    ASSERT_EQ(-1, input.players[0].move_y());

    // Sloppy release: W clears 2 samples before D. The sampler must keep
    // reporting the full diagonal for those skew ticks.
    ks_w.set(false);
    for (int t = 0; t < 2; ++t)
    {
        input_state_from_sdl(input);
        ASSERT_TRUE(input.players[0].held[KEY_UP])
            << "retention should keep the released W reported, tick " << t;
        ASSERT_TRUE(input.players[0].held[KEY_RIGHT]);
        ASSERT_EQ(1, input.players[0].move_x()) << "tick " << t;
        ASSERT_EQ(-1, input.players[0].move_y())
            << "the movement vector must stay diagonal during the skew";
        ASSERT_TRUE(!input.players[0].pressed[KEY_UP])
            << "retention must not synthesize pressed edges";
    }

    // D follows: full stop, nothing held, no cardinal ever reported.
    ks_d.set(false);
    input_state_from_sdl(input);
    ASSERT_TRUE(!input.players[0].held[KEY_UP]);
    ASSERT_TRUE(!input.players[0].held[KEY_RIGHT]);
    ASSERT_EQ(0, input.players[0].move_x());
    ASSERT_EQ(0, input.players[0].move_y());
}

TEST(InputDirectionGrace, input_state_from_sdl_turn_persists_past_grace_window)
{
    disablePlayerJoystick(0);
    GraceStateGuard grace;
    ControlModeGuard mode_guard(0);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    KeyBindingGuard bind_down(0, KEY_DOWN, SDLK_S);
    KeyBindingGuard bind_left(0, KEY_LEFT, SDLK_A);

    KeyStateGuard ks_s(SDL_GetScancodeFromKey(SDLK_S, nullptr));
    KeyStateGuard ks_a(SDL_GetScancodeFromKey(SDLK_A, nullptr));
    ks_s.set(false);
    ks_a.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input);

    // Run down-left, then release S but KEEP A held: an intentional turn.
    ks_s.set(true);
    ks_a.set(true);
    for (int t = 0; t < 3; ++t)
        input_state_from_sdl(input);

    ks_s.set(false);
    for (int t = 0; t < kDiagonalReleaseGraceTicks; ++t)
    {
        input_state_from_sdl(input);
        ASSERT_TRUE(input.players[0].held[KEY_DOWN])
            << "grace window should retain the diagonal, tick " << t;
        ASSERT_TRUE(input.players[0].held[KEY_LEFT]);
    }
    input_state_from_sdl(input);
    ASSERT_TRUE(!input.players[0].held[KEY_DOWN])
        << "past the window the surviving key becomes the direction";
    ASSERT_TRUE(input.players[0].held[KEY_LEFT]);
    ASSERT_EQ(-1, input.players[0].move_x());
    ASSERT_EQ(0, input.players[0].move_y());
    ASSERT_TRUE(!input.players[0].pressed[KEY_LEFT])
        << "the surviving key was held throughout: no new pressed edge";
}

TEST(InputDirectionGrace, input_state_from_sdl_new_press_cancels_retention)
{
    disablePlayerJoystick(0);
    GraceStateGuard grace;
    ControlModeGuard mode_guard(0);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    KeyBindingGuard bind_up(0, KEY_UP, SDLK_W);
    KeyBindingGuard bind_right(0, KEY_RIGHT, SDLK_D);
    KeyBindingGuard bind_down(0, KEY_DOWN, SDLK_S);

    KeyStateGuard ks_w(SDL_GetScancodeFromKey(SDLK_W, nullptr));
    KeyStateGuard ks_d(SDL_GetScancodeFromKey(SDLK_D, nullptr));
    KeyStateGuard ks_s(SDL_GetScancodeFromKey(SDLK_S, nullptr));
    ks_w.set(false);
    ks_d.set(false);
    ks_s.set(false);

    InputState input{};
    input.clear();
    input_state_from_sdl(input);

    ks_w.set(true);
    ks_d.set(true);
    for (int t = 0; t < 3; ++t)
        input_state_from_sdl(input);

    // Release W (retention engages), then press S while it is active.
    ks_w.set(false);
    input_state_from_sdl(input);
    ASSERT_TRUE(input.players[0].held[KEY_UP]) << "retention engaged";

    ks_s.set(true);
    input_state_from_sdl(input);
    ASSERT_TRUE(!input.players[0].held[KEY_UP])
        << "a new key press must apply immediately and drop the retained key";
    ASSERT_TRUE(input.players[0].held[KEY_DOWN]);
    ASSERT_TRUE(input.players[0].held[KEY_RIGHT]);
    ASSERT_TRUE(input.players[0].pressed[KEY_DOWN])
        << "the new key should edge normally";
}
