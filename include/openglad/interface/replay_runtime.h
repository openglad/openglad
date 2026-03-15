#pragma once

class screen;

namespace og::sim {
class ReplayPlayer;
}

namespace og::runtime {

bool initialize_replay_screen(screen& game_screen, og::sim::ReplayPlayer& player);

} // namespace og::runtime
