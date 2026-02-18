#pragma once

union SDL_Event;
class walker;
struct PlayerInput;
class screen;

// Handle cheat/debug key combos during gameplay.
// Extracted from viewscreen::input() so render code does not mutate sim state.
void handle_cheat_keys(walker*& control, short mynum,
                       const SDL_Event& event, const PlayerInput& pi,
                       screen* game_screen);
