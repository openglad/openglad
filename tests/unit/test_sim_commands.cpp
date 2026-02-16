#include <openglad/sim/sim_commands.h>

#include "unit.h"

OG_UNIT_TEST(test_player_command_bitwise_ops)
{
    using og::sim::PlayerCommand;
    using og::sim::has_command;

    auto cmds = PlayerCommand::MoveUp | PlayerCommand::Fire;
    OG_ASSERT(has_command(cmds, PlayerCommand::MoveUp));
    OG_ASSERT(has_command(cmds, PlayerCommand::Fire));
    OG_ASSERT(!has_command(cmds, PlayerCommand::MoveDown));
    OG_ASSERT(!has_command(cmds, PlayerCommand::Special));
}

OG_UNIT_TEST(test_player_input_move_directions)
{
    using og::sim::PlayerCommand;
    og::sim::PlayerInput pi;

    pi.commands = PlayerCommand::MoveLeft | PlayerCommand::MoveUp;
    OG_ASSERT(pi.move_x() == -1);
    OG_ASSERT(pi.move_y() == -1);

    pi.commands = PlayerCommand::MoveRight | PlayerCommand::MoveDown;
    OG_ASSERT(pi.move_x() == 1);
    OG_ASSERT(pi.move_y() == 1);

    pi.commands = PlayerCommand::None;
    OG_ASSERT(pi.move_x() == 0);
    OG_ASSERT(pi.move_y() == 0);

    // Opposing directions cancel out
    pi.commands = PlayerCommand::MoveLeft | PlayerCommand::MoveRight;
    OG_ASSERT(pi.move_x() == 0);
}
