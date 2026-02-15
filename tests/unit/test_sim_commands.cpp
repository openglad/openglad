#include <openglad/sim/sim_commands.h>
#include <openglad/sim/simulator.h>

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

OG_UNIT_TEST(test_command_snapshot_determinism)
{
    og::sim::Simulator s1(42u);
    og::sim::Simulator s2(42u);

    og::sim::CommandSnapshot snap;
    snap.players[0].commands = og::sim::PlayerCommand::MoveUp | og::sim::PlayerCommand::Fire;
    snap.players[1].commands = og::sim::PlayerCommand::MoveDown | og::sim::PlayerCommand::Special;

    for (int i = 0; i < 20; ++i)
    {
        s1.step(snap, 1.0f / 60.0f);
        s2.step(snap, 1.0f / 60.0f);
    }

    OG_ASSERT(s1.state().tick == s2.state().tick);
    OG_ASSERT(s1.state().acc == s2.state().acc);
    OG_ASSERT(s1.events() == s2.events());
    OG_ASSERT(s1.state().tick == 20);
}

OG_UNIT_TEST(test_command_snapshot_different_commands_differ)
{
    og::sim::Simulator s1(42u);
    og::sim::Simulator s2(42u);

    og::sim::CommandSnapshot snap1;
    snap1.players[0].commands = og::sim::PlayerCommand::Fire;

    og::sim::CommandSnapshot snap2;
    snap2.players[0].commands = og::sim::PlayerCommand::Special;

    for (int i = 0; i < 10; ++i)
    {
        s1.step(snap1, 1.0f / 60.0f);
        s2.step(snap2, 1.0f / 60.0f);
    }

    OG_ASSERT(s1.state().acc != s2.state().acc);
}
