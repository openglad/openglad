#pragma once

#include <cstdint>

namespace og::sim {

// Abstract player commands that the simulation understands.
// These are independent of the physical input device (keyboard, controller, etc.).
enum class PlayerCommand : std::uint32_t {
    None       = 0,
    MoveUp     = 1 << 0,
    MoveDown   = 1 << 1,
    MoveLeft   = 1 << 2,
    MoveRight  = 1 << 3,
    Fire       = 1 << 4,
    Special    = 1 << 5,
    Shifter    = 1 << 6,   // alternate/modifier key
    Yell       = 1 << 7,
    SwitchWeap = 1 << 8,
    SwitchSpec = 1 << 9,
};

inline PlayerCommand operator|(PlayerCommand a, PlayerCommand b)
{
    return static_cast<PlayerCommand>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline PlayerCommand operator&(PlayerCommand a, PlayerCommand b)
{
    return static_cast<PlayerCommand>(
        static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

inline bool has_command(PlayerCommand set, PlayerCommand flag)
{
    return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(flag)) != 0;
}

// Per-player command state for a single simulation tick.
struct PlayerInput final {
    PlayerCommand commands = PlayerCommand::None;

    // Directional input as normalized values (-1, 0, +1).
    // Derived from MoveUp/Down/Left/Right commands for convenience.
    int move_x() const
    {
        int x = 0;
        if (has_command(commands, PlayerCommand::MoveLeft))  x -= 1;
        if (has_command(commands, PlayerCommand::MoveRight)) x += 1;
        return x;
    }

    int move_y() const
    {
        int y = 0;
        if (has_command(commands, PlayerCommand::MoveUp))   y -= 1;
        if (has_command(commands, PlayerCommand::MoveDown)) y += 1;
        return y;
    }
};

// Complete input snapshot for all players in a single simulation tick.
struct CommandSnapshot final {
    static constexpr int MAX_PLAYERS = 4;
    PlayerInput players[MAX_PLAYERS] = {};
    bool quit_requested = false;
};

} // namespace og::sim
