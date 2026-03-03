#pragma once

class walker;
struct PlayerInput;
class screen;

// Handle cheat/debug key combos during gameplay.
// Extracted from viewscreen::input() so render code does not mutate sim state.
void handle_cheat_keys(walker*& control, short mynum,
                       const void* native_event, const PlayerInput& pi,
                       screen* game_screen);
template <typename EventT>
inline void handle_cheat_keys(walker*& control, short mynum,
                              const EventT& event, const PlayerInput& pi,
                              screen* game_screen)
{
    handle_cheat_keys(control, mynum, static_cast<const void*>(&event), pi, game_screen);
}
