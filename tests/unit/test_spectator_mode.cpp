#include <gtest/gtest.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/input_state.h>

// --- Spectator mode: how many viewscreens a battle opens ---
// og::ui::spectator_view_count is the ONE implementation of the rule the
// battle loader (src/platform/sdl/game.cpp) and Base Camp's ready_for_battle
// (src/interface/ui/picker_team_build.cpp) both read: numplayers == 0 is a
// camera-only spectator battle and still opens exactly one view.

TEST(SpectatorMode, spectator_view_count_gives_the_camera_one_view)
{
    SaveData save;

    og::ui::set_player_count(save, 0);
    EXPECT_TRUE(og::ui::is_spectator_mode(save))
        << "numplayers 0 is spectator mode";
    EXPECT_EQ(1, og::ui::spectator_view_count(save))
        << "spectator mode still opens ONE viewscreen for the camera";

    for (const int count : {1, 2, 3, 4})
    {
        og::ui::set_player_count(save, count);
        EXPECT_FALSE(og::ui::is_spectator_mode(save))
            << "count " << count << " is not spectator mode";
        EXPECT_EQ(static_cast<short>(count), og::ui::spectator_view_count(save))
            << "count " << count << ": one viewscreen per player";
    }
}

// --- Spectator mode: cleared InputState has expected defaults ---
// Verifies that a freshly cleared InputState reports no actions held/pressed
// and zero movement axes — the preconditions the spectator guard relies on.

TEST(SpectatorMode, spectator_cleared_input_state_defaults)
{
    SaveData save;
    og::ui::set_player_count(save, 0);
    ASSERT_TRUE(og::ui::is_spectator_mode(save));

    // In spectator mode, the game loop checks is_spectator_mode() and:
    // - Skips sim_process_player_input (movement, fire, special, yell)
    // - Only processes SwitchChar for camera cycling
    // This test validates the guard condition is correct.

    InputState input;
    input.clear();
    const PlayerInput& pi = input.players[0];

    // No keys pressed -> SwitchChar not pressed
    ASSERT_TRUE(!pi.was_pressed(InputAction::SwitchChar));

    // Movement keys should be irrelevant in spectator mode
    // (the is_spectator_mode guard prevents them from being read)
    ASSERT_TRUE(!pi.is_held(InputAction::Fire));
    ASSERT_TRUE(!pi.is_held(InputAction::Special));
    ASSERT_TRUE(pi.move_x() == 0);
    ASSERT_TRUE(pi.move_y() == 0);
}
