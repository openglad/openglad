#pragma once

#include <cstddef>
#include <cstdint>

struct InputState;
class screen;

namespace og::sim {
class GameClient;
}

namespace og::runtime {

class GameSession;
struct SessionState;

bool local_transport_active(const SessionState& session) noexcept;
std::size_t local_transport_client_count(const GameSession& session) noexcept;
const og::sim::GameClient* local_transport_display_client(
    const SessionState& session) noexcept;
bool local_transport_shadow_is_paused(const GameSession& session) noexcept;
void reset_local_transport_shadow(GameSession& session, screen& gameplay_screen);
void clear_local_transport_shadow(GameSession& session) noexcept;
bool local_transport_shadow_toggle_pause(GameSession& session);
void local_transport_shadow_send_input(GameSession& session,
                                       const InputState& input,
                                       std::uint32_t tick);
void local_transport_shadow_finish_tick(GameSession& session);

} // namespace og::runtime
