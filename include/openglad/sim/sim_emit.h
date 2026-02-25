#pragma once

// Convenience helpers for emitting simulation events from entity code.
//
// These replace direct myscreen->soundp->play_sound() and
// myscreen->viewob[0]->set_display_text() calls with event emission
// through the SimEventLog.
//
// Each helper takes a SimEventLog* as its first argument. Entity code
// typically passes og::gameplay::current_game->sim_events. If the pointer is null
// (e.g., entity created outside of a game session), the call is a no-op.

#include <openglad/sim/sim_event_log.h>

#include <cstdint>
#include <string>

namespace og::sim {

// Emit a sound event. The runtime event dispatcher will play the
// sound via the audio subsystem after the simulation tick.
inline void emit_sound(SimEventLog* log, std::uint32_t sound_id)
{
    if (log)
        log->push_sound(sound_id);
}

// Emit a text notification event. The runtime event dispatcher will
// display the text via the HUD/viewscreen system after the simulation tick.
// If duration is non-zero, it overrides the default display time.
inline void emit_notification(SimEventLog* log, const std::string& message, std::uint32_t duration = 0)
{
    if (log)
        log->push_notification(message, duration);
}

// Emit a generic simulation event.
inline void emit_event(SimEventLog* log, EventKind kind, std::uint32_t a = 0, std::uint32_t b = 0)
{
    if (log)
        log->push(kind, a, b);
}

} // namespace og::sim
