#pragma once

// Convenience helpers for emitting simulation events from entity code.
//
// These replace direct myscreen->soundp->play_sound() and
// myscreen->viewob[0]->set_display_text() calls with event emission
// through the SimEventLog.

#include <openglad/sim/sim_event_log.h>
#include <openglad/runtime/game_context.h>

#include <cstdint>
#include <string>

namespace og::sim {

// Emit a sound event. The runtime event dispatcher will play the
// sound via the audio subsystem after the simulation tick.
inline void emit_sound(std::uint32_t sound_id)
{
    if (ctx().sim_events)
        ctx().sim_events->push_sound(sound_id);
}

// Emit a text notification event. The runtime event dispatcher will
// display the text via the HUD/viewscreen system after the simulation tick.
inline void emit_notification(const std::string& message)
{
    if (ctx().sim_events)
        ctx().sim_events->push_notification(message);
}

// Emit a generic simulation event.
inline void emit_event(EventKind kind, std::uint32_t a = 0, std::uint32_t b = 0)
{
    if (ctx().sim_events)
        ctx().sim_events->push(kind, a, b);
}

} // namespace og::sim
