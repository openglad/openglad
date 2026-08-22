#pragma once

class walker;
struct PlayerInput;
class screen;
class GameWorld;

// Cheat team-hop (Cheat+Switch): walk the teams after team_in_out, wrapping,
// and return the first live Living body found there. On success team_in_out
// holds that team; on failure it is left untouched, so the caller can hand
// control back to the walker it borrowed it from. The lap is bounded at
// MAX_TEAM steps: a Free For All seat carries a team number from the 16-31
// band, which the mod-MAX_TEAM cycle can never reach.
walker* cheat_cycle_next_team(GameWorld& world, short& team_in_out);

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
