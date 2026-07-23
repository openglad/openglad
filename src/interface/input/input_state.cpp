#include <openglad/interface/input_state.h>

#include <openglad/interface/input.h>
#include <openglad/interface/input_hardware_state.h>
#include <openglad/interface/session_state.h>
#include <openglad/core/util.h>

#include <array>
#include <format>
#include <string>

int PlayerInput::move_x() const
{
    int dx = 0;
    if (held[static_cast<int>(InputKey::Left)] ||
        held[static_cast<int>(InputKey::UpLeft)] ||
        held[static_cast<int>(InputKey::DownLeft)])
        dx -= 1;
    if (held[static_cast<int>(InputKey::Right)] ||
        held[static_cast<int>(InputKey::UpRight)] ||
        held[static_cast<int>(InputKey::DownRight)])
        dx += 1;
    return dx;
}

int PlayerInput::move_y() const
{
    int dy = 0;
    if (held[static_cast<int>(InputKey::Up)] ||
        held[static_cast<int>(InputKey::UpLeft)] ||
        held[static_cast<int>(InputKey::UpRight)])
        dy -= 1;
    if (held[static_cast<int>(InputKey::Down)] ||
        held[static_cast<int>(InputKey::DownLeft)] ||
        held[static_cast<int>(InputKey::DownRight)])
        dy += 1;
    return dy;
}

void InputState::clear()
{
    for (auto& p : players) {
        for (int i = 0; i < NUM_INPUT_KEYS; i++) {
            p.held[i] = false;
            p.pressed[i] = false;
        }
    }
    quit_requested = false;
    timer_wait_request = kNoTimerWaitRequest;
}

// JoyData's default constructor lives here (SDL-free) rather than in the
// SDL-coupled input.cpp: InputHardwareState value-initializes a JoyData[4], so the
// headless builds (curses/server/text) — which do not compile input.cpp — still
// need to construct one. The hardware-driven JoyData(int) ctor and its event
// methods remain in input.cpp; the headless builds never reference them.
JoyData::JoyData()
    : index(-1), numAxes(0), numButtons(0), numHats(0)
{}

// ---------------------------------------------------------------------------
// Player control settings / keybindings.
//
// Moved here from src/interface/input/input.cpp (which is SDL-coupled) so the
// per-player keybinding model and the config loader are available WITHOUT SDL.
// This file is compiled into every build — og_interface for the SDL client, and
// the headless source lists for openglad_curses / openglad_server / openglad_text
// — so all of them resolve the SAME bindings from the SAME config, instead of
// hardcoding their own. The keymaps are KEYCODE_* (SDL keycode) values, stored as
// hw().player_mode_keys[player][mode][key] and activated into
// current_session->player_keys_[player][key] for the active control mode.
// ---------------------------------------------------------------------------

InputHardwareState& input_hardware_state()
{
    return *og::runtime::current_session->input_hw_;
}

namespace
{
inline auto& hw() { return input_hardware_state(); }

constexpr int kModeFourIndex = 0;
constexpr int kModeEightIndex = 1;

// KEY_LOOKUP (the render-time look-up hold) defaults: all four players share
// one physical keyboard, so each default must not collide with ANY player's
// bindings in EITHER control mode (modes are per-player, so any combination
// can be active). P1: 'v' (both modes; pre-existing overlap with P4's 8-dir
// DOWN-LEFT 'v' is a known quirk). P2: Right Ctrl (adjacent to the arrow
// cluster; free everywhere). P3: 'p' (top row next to the IJKL/U cluster;
// free everywhere). P4: 'b' in 4-direction mode only (below its G/H keys;
// free from every OTHER player, and P4's own 8-dir 'b' = DOWN is never
// active while the 4-dir map is) — P4's 8-direction cluster consumes every
// key around T/F/G/H (t y h n b v f r), so its 8-dir look-up starts unbound
// and can be bound on the CONTROLS remap screen.
constexpr int kDefaultFourDirKeys[4][NUM_KEYS] = {
    {
        KEYCODE_w, KEYCODE_UNKNOWN, KEYCODE_d, KEYCODE_UNKNOWN,  // movements
        KEYCODE_s, KEYCODE_UNKNOWN, KEYCODE_a, KEYCODE_UNKNOWN,
        KEYCODE_LCTRL, KEYCODE_LALT,                  // fire & special
        KEYCODE_BACKQUOTE,                         // switch guys
        KEYCODE_TAB,                               // change special
        KEYCODE_e,                                 // Yell
        KEYCODE_LSHIFT,                            // Shifter
        KEYCODE_1,                                 // Options menu
        KEYCODE_F5,                                // Cheat key
        KEYCODE_v,                                 // Look up (hold)
    },
    {
        KEYCODE_UP, KEYCODE_UNKNOWN, KEYCODE_RIGHT, KEYCODE_UNKNOWN,  // movements
        KEYCODE_DOWN, KEYCODE_UNKNOWN, KEYCODE_LEFT, KEYCODE_UNKNOWN,
        KEYCODE_PERIOD, KEYCODE_SLASH,                // fire & special
        KEYCODE_RETURN,                            // switch guys
        KEYCODE_QUOTE,                             // change special
        KEYCODE_BACKSLASH,                         // Yell
        KEYCODE_RSHIFT,                            // Shifter
        KEYCODE_2,                                 // Options menu
        KEYCODE_F6,                                // Cheat key
        KEYCODE_RCTRL,                             // Look up (hold)
    },
    {
        KEYCODE_i, KEYCODE_UNKNOWN, KEYCODE_l, KEYCODE_UNKNOWN,  // movements
        KEYCODE_k, KEYCODE_UNKNOWN, KEYCODE_j, KEYCODE_UNKNOWN,
        KEYCODE_SPACE, KEYCODE_SEMICOLON,             // fire & special
        KEYCODE_MINUS,                             // switch guys
        KEYCODE_9,                                 // change special
        KEYCODE_u,                                 // Yell
        KEYCODE_0,                                 // Shifter
        KEYCODE_3,                                 // Options menu
        KEYCODE_F7,                                // Cheat key
        KEYCODE_p,                                 // Look up (hold)
    },
    {
        KEYCODE_t, KEYCODE_UNKNOWN, KEYCODE_h, KEYCODE_UNKNOWN,  // movements
        KEYCODE_g, KEYCODE_UNKNOWN, KEYCODE_f, KEYCODE_UNKNOWN,
        KEYCODE_5, KEYCODE_6,                         // fire & special
        KEYCODE_EQUALS,                            // switch guys
        KEYCODE_7,                                 // change special
        KEYCODE_y,                                 // Yell
        KEYCODE_8,                                 // Shifter
        KEYCODE_4,                                 // Options menu
        KEYCODE_F8,                                // Cheat key
        KEYCODE_b,                                 // Look up (hold; 4-dir only)
    }
};
constexpr int kDefaultEightDirKeys[4][NUM_KEYS] = {
    {   // P1: clockwise W/E/D/C/X/Z/A/Q, Yell=S
        KEYCODE_w, KEYCODE_e, KEYCODE_d, KEYCODE_c,
        KEYCODE_x, KEYCODE_z, KEYCODE_a, KEYCODE_q,
        KEYCODE_LCTRL, KEYCODE_LALT,
        KEYCODE_BACKQUOTE, KEYCODE_TAB,
        KEYCODE_s, KEYCODE_LSHIFT, KEYCODE_1, KEYCODE_F5,
        KEYCODE_v,                                 // Look up (hold)
    },
    {   // P2: arrows, no diagonal keys
        KEYCODE_UP, KEYCODE_UNKNOWN, KEYCODE_RIGHT, KEYCODE_UNKNOWN,
        KEYCODE_DOWN, KEYCODE_UNKNOWN, KEYCODE_LEFT, KEYCODE_UNKNOWN,
        KEYCODE_PERIOD, KEYCODE_SLASH,
        KEYCODE_RETURN, KEYCODE_QUOTE,
        KEYCODE_BACKSLASH, KEYCODE_RSHIFT, KEYCODE_2, KEYCODE_F6,
        KEYCODE_RCTRL,                             // Look up (hold)
    },
    {   // P3: clockwise I/O/L/./,/M/J/U, Yell=K
        KEYCODE_i, KEYCODE_o, KEYCODE_l, KEYCODE_PERIOD,
        KEYCODE_COMMA, KEYCODE_m, KEYCODE_j, KEYCODE_u,
        KEYCODE_SPACE, KEYCODE_SEMICOLON,
        KEYCODE_MINUS, KEYCODE_9,
        KEYCODE_k, KEYCODE_0, KEYCODE_3, KEYCODE_F7,
        KEYCODE_p,                                 // Look up (hold)
    },
    {   // P4: clockwise T/Y/H/N/B/V/F/R, Yell=G
        KEYCODE_t, KEYCODE_y, KEYCODE_h, KEYCODE_n,
        KEYCODE_b, KEYCODE_v, KEYCODE_f, KEYCODE_r,
        KEYCODE_5, KEYCODE_6,
        KEYCODE_EQUALS, KEYCODE_7,
        KEYCODE_g, KEYCODE_8, KEYCODE_4, KEYCODE_F8,
        KEYCODE_UNKNOWN,                           // Look up (unbound: t/y/h/n/b/v/f/r consume the cluster)
    }
};
constexpr std::array<int, 4> kDefaultControlModes = {
    static_cast<int>(ControlDirectionMode::FourDirection),
    static_cast<int>(ControlDirectionMode::FourDirection),
    static_cast<int>(ControlDirectionMode::FourDirection),
    static_cast<int>(ControlDirectionMode::FourDirection),
};

int normalize_control_mode(int mode)
{
    return (mode == static_cast<int>(ControlDirectionMode::EightDirection))
        ? static_cast<int>(ControlDirectionMode::EightDirection)
        : static_cast<int>(ControlDirectionMode::FourDirection);
}

int control_mode_keymap_index(int mode)
{
    return (normalize_control_mode(mode) == static_cast<int>(ControlDirectionMode::EightDirection))
        ? kModeEightIndex
        : kModeFourIndex;
}

int current_player_mode_keymap_index(int player_index)
{
    return control_mode_keymap_index(get_player_control_mode(player_index));
}

void sync_runtime_keys_to_active_mode(int player_index);
void activate_mode_keymap_for_player(int player_index, int mode);
} // namespace

bool reset_default_player_controls_for_player(int player_index)
{
    if (player_index < 0 || player_index >= 4)
        return false;

    for (int k = 0; k < NUM_KEYS; ++k)
    {
        hw().player_mode_keys[player_index][kModeFourIndex][k] =
            kDefaultFourDirKeys[player_index][k];
        hw().player_mode_keys[player_index][kModeEightIndex][k] =
            kDefaultEightDirKeys[player_index][k];
    }
    hw().player_control_modes[player_index] =
        kDefaultControlModes[static_cast<std::size_t>(player_index)];
    // Activate the default mode's keymap into player_keys
    activate_mode_keymap_for_player(
        player_index, kDefaultControlModes[static_cast<std::size_t>(player_index)]);
    return true;
}

bool compact_player_controls_after_removal(int removed_player_index, int active_player_count)
{
    if (active_player_count < 1 || active_player_count > 4 ||
        removed_player_index < 0 || removed_player_index >= active_player_count)
        return false;

    for (int source = removed_player_index + 1; source < active_player_count; ++source)
    {
        sync_runtime_keys_to_active_mode(source);
        const int destination = source - 1;
        for (int mode = kModeFourIndex; mode <= kModeEightIndex; ++mode)
        {
            for (int k = 0; k < NUM_KEYS; ++k)
            {
                hw().player_mode_keys[destination][mode][k] =
                    hw().player_mode_keys[source][mode][k];
            }
        }
        hw().player_control_modes[destination] = hw().player_control_modes[source];
        activate_mode_keymap_for_player(
            destination, hw().player_control_modes[destination]);
    }

    reset_default_player_controls_for_player(active_player_count - 1);
    return true;
}

void reset_default_player_controls()
{
    for (int p = 0; p < 4; ++p)
        reset_default_player_controls_for_player(p);
}

int get_player_control_mode(int player_index)
{
    if (player_index < 0 || player_index >= 4)
        return static_cast<int>(ControlDirectionMode::FourDirection);
    return hw().player_control_modes[player_index];
}

void set_player_control_mode(int player_index, int mode)
{
    if (player_index < 0 || player_index >= 4)
        return;
    sync_runtime_keys_to_active_mode(player_index);
    hw().player_control_modes[player_index] = normalize_control_mode(mode);
    activate_mode_keymap_for_player(player_index, hw().player_control_modes[player_index]);
}

bool player_allows_diagonal_movement(int player_index)
{
    return get_player_control_mode(player_index) == static_cast<int>(ControlDirectionMode::EightDirection);
}

int get_player_key_binding_for_mode(int player_index, int mode, int key_enum)
{
    if (player_index < 0 || player_index >= 4 || key_enum < 0 || key_enum >= NUM_KEYS)
        return KEYCODE_UNKNOWN;
    const int mode_index = control_mode_keymap_index(mode);
    return hw().player_mode_keys[player_index][mode_index][key_enum];
}

void set_player_key_binding(int player_index, int key_enum, int keycode)
{
    if (player_index < 0 || player_index >= 4 || key_enum < 0 || key_enum >= NUM_KEYS)
        return;
    const int mode_index = current_player_mode_keymap_index(player_index);
    hw().player_mode_keys[player_index][mode_index][key_enum] = keycode;
    og::runtime::current_session->player_keys_[player_index][key_enum] = keycode;
}

void load_player_control_settings_from_cfg(cfg_store& config)
{
    reset_default_player_controls();

    for (int p = 0; p < 4; ++p)
    {
        const std::string mode_key = std::format("player{}_mode", p + 1);
        const std::string mode_str = config.get_setting("controls", mode_key);
        for (int k = 0; k < NUM_KEYS; ++k)
        {
            const std::string legacy_key_name = std::format("player{}_key{}", p + 1, k);
            const std::string legacy_key_value = config.get_setting("controls", legacy_key_name);
            if (!legacy_key_value.empty())
            {
                const int four_fallback = kDefaultFourDirKeys[p][k];
                const int eight_fallback = kDefaultEightDirKeys[p][k];
                hw().player_mode_keys[p][kModeFourIndex][k] = parse_int_strict(legacy_key_value).value_or(four_fallback);
                hw().player_mode_keys[p][kModeEightIndex][k] = parse_int_strict(legacy_key_value).value_or(eight_fallback);
            }

            const std::string mode4_key_name = std::format("player{}_mode4_key{}", p + 1, k);
            const std::string mode4_key_value = config.get_setting("controls", mode4_key_name);
            if (!mode4_key_value.empty())
            {
                hw().player_mode_keys[p][kModeFourIndex][k] =
                    parse_int_strict(mode4_key_value).value_or(kDefaultFourDirKeys[p][k]);
            }

            const std::string mode8_key_name = std::format("player{}_mode8_key{}", p + 1, k);
            const std::string mode8_key_value = config.get_setting("controls", mode8_key_name);
            if (!mode8_key_value.empty())
            {
                hw().player_mode_keys[p][kModeEightIndex][k] =
                    parse_int_strict(mode8_key_value).value_or(kDefaultEightDirKeys[p][k]);
            }
        }

        hw().player_control_modes[p] = mode_str.empty()
            ? static_cast<int>(ControlDirectionMode::FourDirection)
            : normalize_control_mode(parse_int_strict(mode_str).value_or(
                static_cast<int>(ControlDirectionMode::FourDirection)));
        activate_mode_keymap_for_player(p, hw().player_control_modes[p]);
    }
}

void save_player_control_settings_to_cfg(cfg_store& config)
{
    for (int p = 0; p < 4; ++p)
    {
        sync_runtime_keys_to_active_mode(p);
        config.apply_setting("controls", std::format("player{}_mode", p + 1),
            std::to_string(get_player_control_mode(p)));
        for (int k = 0; k < NUM_KEYS; ++k)
        {
            const int mode_index = current_player_mode_keymap_index(p);
            config.apply_setting("controls", std::format("player{}_key{}", p + 1, k),
                std::to_string(hw().player_mode_keys[p][mode_index][k]));
            config.apply_setting("controls", std::format("player{}_mode4_key{}", p + 1, k),
                std::to_string(hw().player_mode_keys[p][kModeFourIndex][k]));
            config.apply_setting("controls", std::format("player{}_mode8_key{}", p + 1, k),
                std::to_string(hw().player_mode_keys[p][kModeEightIndex][k]));
        }
    }
}

namespace
{
void sync_runtime_keys_to_active_mode(int player_index)
{
    const int mode_index = current_player_mode_keymap_index(player_index);
    for (int k = 0; k < NUM_KEYS; ++k)
        hw().player_mode_keys[player_index][mode_index][k] = og::runtime::current_session->player_keys_[player_index][k];
}

void activate_mode_keymap_for_player(int player_index, int mode)
{
    const int mode_index = control_mode_keymap_index(mode);
    for (int k = 0; k < NUM_KEYS; ++k)
        og::runtime::current_session->player_keys_[player_index][k] = hw().player_mode_keys[player_index][mode_index][k];
}
} // namespace

// ---------------------------------------------------------------------------
// Diagonal release retention (see input_direction_grace.h for the contract).
// ---------------------------------------------------------------------------

namespace
{
// Effective movement components of a direction mask — same decode rules as
// PlayerInput::move_x()/move_y() above.
int mask_move_x(std::uint8_t mask)
{
    int dx = 0;
    if (mask & static_cast<std::uint8_t>((1u << static_cast<int>(InputKey::Left)) |
                                         (1u << static_cast<int>(InputKey::UpLeft)) |
                                         (1u << static_cast<int>(InputKey::DownLeft))))
        dx -= 1;
    if (mask & static_cast<std::uint8_t>((1u << static_cast<int>(InputKey::Right)) |
                                         (1u << static_cast<int>(InputKey::UpRight)) |
                                         (1u << static_cast<int>(InputKey::DownRight))))
        dx += 1;
    return dx;
}

int mask_move_y(std::uint8_t mask)
{
    int dy = 0;
    if (mask & static_cast<std::uint8_t>((1u << static_cast<int>(InputKey::Up)) |
                                         (1u << static_cast<int>(InputKey::UpLeft)) |
                                         (1u << static_cast<int>(InputKey::UpRight))))
        dy -= 1;
    if (mask & static_cast<std::uint8_t>((1u << static_cast<int>(InputKey::Down)) |
                                         (1u << static_cast<int>(InputKey::DownLeft)) |
                                         (1u << static_cast<int>(InputKey::DownRight))))
        dy += 1;
    return dy;
}

bool mask_is_diagonal(std::uint8_t mask)
{
    return mask_move_x(mask) != 0 && mask_move_y(mask) != 0;
}
} // namespace

std::uint8_t coalesce_direction_release(std::uint8_t raw_mask,
                                        DirectionGraceState& state)
{
    const std::uint8_t prev = state.prev_raw;
    state.prev_raw = raw_mask;

    if (state.hold_mask != 0)
    {
        // Retention active. Three ways out, all reporting the raw state:
        // a key OUTSIDE the retained diagonal (a NEW press — intentional new
        // direction, applies immediately), a full release (stop — the last
        // reported step was the diagonal, so the facing keeps it), or the
        // full diagonal being held again (raw already agrees).
        if ((raw_mask & static_cast<std::uint8_t>(~state.hold_mask)) != 0 ||
            raw_mask == 0 || raw_mask == state.hold_mask)
        {
            state.hold_mask = 0;
            state.ticks_left = 0;
            return raw_mask;
        }
        // The surviving subset persisted past the window: intentional turn.
        if (state.ticks_left == 0)
        {
            state.hold_mask = 0;
            return raw_mask;
        }
        state.ticks_left = static_cast<std::uint8_t>(state.ticks_left - 1);
        return state.hold_mask;
    }

    // No retention active: trigger on a sloppy partial release — keys only
    // left (no new presses), the previous sample's vector was a diagonal,
    // and the survivors collapse it to a cardinal.
    if (raw_mask != 0 && raw_mask != prev &&
        (raw_mask & static_cast<std::uint8_t>(~prev)) == 0 &&
        mask_is_diagonal(prev) && !mask_is_diagonal(raw_mask))
    {
        state.hold_mask = prev;
        state.ticks_left = static_cast<std::uint8_t>(kDiagonalReleaseGraceTicks - 1);
        return prev;
    }
    return raw_mask;
}
