#pragma once

#include <cstddef>
#include <cstdint>

struct InputState;
class screen;

namespace og::runtime {

struct SessionState;

bool local_transport_active(const SessionState& session) noexcept;
std::size_t local_transport_client_count(const SessionState& session) noexcept;
bool local_transport_shadow_is_paused(const SessionState& session) noexcept;
void reset_local_transport_shadow(SessionState& session, screen& gameplay_screen);
void clear_local_transport_shadow(SessionState& session) noexcept;
bool local_transport_shadow_toggle_pause(SessionState& session);
void local_transport_shadow_send_input(SessionState& session,
                                       const InputState& input,
                                       std::uint32_t tick);
void local_transport_shadow_finish_tick(SessionState& session);

} // namespace og::runtime
