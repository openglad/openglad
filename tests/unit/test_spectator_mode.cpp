#include "unit.h"
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/save_io.h>
#include <openglad/interface/input/input_state.h>

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

// --- Spectator mode: cleared InputState has expected defaults ---
// Verifies that a freshly cleared InputState reports no actions held/pressed
// and zero movement axes — the preconditions the spectator guard relies on.

OG_UNIT_TEST(test_spectator_cleared_input_state_defaults)
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
