#pragma once

// Convenience helpers for emitting simulation events from entity code.
//
// These replace direct myscreen->soundp->play_sound() and
// myscreen->viewob[0]->set_display_text() calls with event emission
// through the SimEventLog.
//
// Each helper takes a SimEventLog* as its first argument. Entity code
// accesses the log via current_game->sim_events. If the pointer is null
// (e.g., entity created outside of a game session), the call is a no-op.

#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/sim_event_log.h>

#include <cstdint>
#include <string>

namespace og::sim {

// Emit an unplaced sound event: a cue every machine plays (the YO, a mode's
// or a level script's announcement). The runtime event dispatcher will play
// the sound via the audio subsystem after the simulation tick. An in-world
// sound goes through emit_positional_sound instead.
// target_player addresses one global player index (walker::user()); -1
// broadcasts. A cue that speaks to a single seat passes its seat here so the
// clang follows the line.
inline void emit_sound(SimEventLog* log, std::uint32_t sound_id,
                       std::int32_t target_player = -1)
{
    if (log)
        log->push_sound(sound_id, target_player);
}

// The packed box of the walker a sound comes from (sound_source.h), or 0 for
// no walker: an unplaced sound, heard by every machine.
std::uint32_t sound_source_of(const walker* source);

// Emit an in-world sound placed at `source`: each machine plays it only while
// one of its own views shows the source, as the old game played it only on
// screen (#468). It asks no visibility test here, so a caller that already
// asked one (the death cry, whose coin is drawn only when it is heard) does
// not ask twice.
inline void emit_sound_at(SimEventLog* log, const walker* source,
                          std::uint32_t sound_id)
{
    if (log)
        log->push_sound(sound_id, -1, sound_source_of(source));
}

// Emit an in-world sound placed at `source`, after the visibility test a test
// driver may install (the parity recorder installs the old game's 320 x 200
// view; play installs none and leaves the choice to each machine's views).
inline void emit_positional_sound(SimEventLog* log, const walker* source,
                                  std::uint32_t sound_id)
{
    if (current_game && current_game->positional_sound_visible &&
        !current_game->positional_sound_visible(
            source, sound_id, current_game->positional_sound_visible_user))
        return;
    emit_sound_at(log, source, sound_id);
}

// Emit a text notification event. The runtime event dispatcher will
// display the text via the HUD/viewscreen system after the simulation tick.
// If duration is non-zero, it overrides the default display time.
// target_player addresses a single global player index (walker::user());
// -1 broadcasts to every view.
inline void emit_notification(SimEventLog* log, const std::string& message,
                              std::uint32_t duration = 0,
                              std::int32_t target_player = -1)
{
    if (log)
        log->push_notification(message, duration, target_player);
}

// Emit a generic simulation event.
inline void emit_event(SimEventLog* log, EventKind kind, std::uint32_t a = 0, std::uint32_t b = 0)
{
    if (log)
        log->push(kind, a, b);
}

// Emit a generic simulation event with a text payload.
inline void emit_event_text(SimEventLog* log, EventKind kind,
                            const std::string& text,
                            std::uint32_t a = 0, std::uint32_t b = 0)
{
    if (log)
        log->push_with_text(kind, text, a, b);
}

} // namespace og::sim
