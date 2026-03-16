#pragma once

#include <cstdint>

struct InputState;
class screen;

namespace og::sim {
struct SimEventBatch;
}

namespace og::runtime {

struct SessionState;

void reset_local_transport_shadow(SessionState& session, screen& gameplay_screen);
void clear_local_transport_shadow(SessionState& session) noexcept;
void local_transport_shadow_send_input(SessionState& session,
                                       const InputState& input,
                                       std::uint32_t tick);
void local_transport_shadow_capture_events(SessionState& session,
                                          const og::sim::SimEventBatch& batch);
void local_transport_shadow_finish_tick(SessionState& session);

} // namespace og::runtime
