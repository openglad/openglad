#pragma once

class screen;
struct InputState;

namespace og::sim {
class ReplayPlayer;
}

namespace og::runtime {

bool initialize_replay_screen(screen& game_screen, og::sim::ReplayPlayer& player);
void begin_replay_recording(screen& game_screen);
void record_replay_input(screen& game_screen, const InputState& input);
void finish_replay_recording();

} // namespace og::runtime
