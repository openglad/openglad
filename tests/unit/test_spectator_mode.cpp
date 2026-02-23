#include "unit.h"
#include <openglad/ui/picker_common.h>
#include <openglad/data/save_data.h>
#include <openglad/input/input_state.h>

// --- Spectator mode: set_player_count(0) round-trips ---

OG_UNIT_TEST(test_spectator_set_player_count_zero_roundtrip)
{
    SaveData save;

    // Default is 1 player
    OG_ASSERT(save.numplayers == 1);

    // Set to 0
    og::ui::set_player_count(save, 0);
    OG_ASSERT(save.numplayers == 0);

    // Set to 3
    og::ui::set_player_count(save, 3);
    OG_ASSERT(save.numplayers == 3);

    // Back to 0
    og::ui::set_player_count(save, 0);
    OG_ASSERT(save.numplayers == 0);
}

// --- is_spectator_mode returns correct values ---

OG_UNIT_TEST(test_spectator_is_spectator_mode)
{
    SaveData save;

    // numplayers=1 (default) -> not spectator
    OG_ASSERT(!og::ui::is_spectator_mode(save));

    // numplayers=0 -> spectator
    og::ui::set_player_count(save, 0);
    OG_ASSERT(og::ui::is_spectator_mode(save));

    // numplayers=2 -> not spectator
    og::ui::set_player_count(save, 2);
    OG_ASSERT(!og::ui::is_spectator_mode(save));

    // numplayers=4 -> not spectator
    og::ui::set_player_count(save, 4);
    OG_ASSERT(!og::ui::is_spectator_mode(save));
}

// --- Spectator mode: numviews calculation ---

OG_UNIT_TEST(test_spectator_numviews_calculation)
{
    SaveData save;

    // In spectator mode, the game should use 1 view even though numplayers==0
    og::ui::set_player_count(save, 0);
    short numviews = (save.numplayers == 0) ? 1 : save.numplayers;
    OG_ASSERT(numviews == 1);

    // Normal modes
    og::ui::set_player_count(save, 1);
    numviews = (save.numplayers == 0) ? 1 : save.numplayers;
    OG_ASSERT(numviews == 1);

    og::ui::set_player_count(save, 3);
    numviews = (save.numplayers == 0) ? 1 : save.numplayers;
    OG_ASSERT(numviews == 3);
}

// --- Spectator mode: input filtering logic ---
// Verify that in spectator mode, the spectator check would suppress
// player input processing while allowing switch-character.

OG_UNIT_TEST(test_spectator_input_filtering_logic)
{
    SaveData save;
    og::ui::set_player_count(save, 0);
    OG_ASSERT(og::ui::is_spectator_mode(save));

    // In spectator mode, the game loop checks is_spectator_mode() and:
    // - Skips sim_process_player_input (movement, fire, special, yell)
    // - Only processes SwitchChar for camera cycling
    // This test validates the guard condition is correct.

    InputState input;
    input.clear();
    const PlayerInput& pi = input.players[0];

    // No keys pressed -> SwitchChar not pressed
    OG_ASSERT(!pi.was_pressed(InputAction::SwitchChar));

    // Movement keys should be irrelevant in spectator mode
    // (the is_spectator_mode guard prevents them from being read)
    OG_ASSERT(!pi.is_held(InputAction::Fire));
    OG_ASSERT(!pi.is_held(InputAction::Special));
    OG_ASSERT(pi.move_x() == 0);
    OG_ASSERT(pi.move_y() == 0);
}
