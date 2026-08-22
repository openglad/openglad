#pragma once

#include <openglad/gameplay/event.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace og::sim {

// Accumulates simulation events during a tick.
//
// Game logic (entity updates, combat, AI) pushes events here instead of
// making direct rendering/audio/UI calls. After the simulation tick,
// the runtime layer drains the events and dispatches them to the
// appropriate subsystems (sound, HUD, visual effects).
//
// This is the key decoupling mechanism: simulation code only knows
// about EventKind values, not SDL, sound objects, or viewscreens.
class SimEventLog final {
public:
    SimEventLog() = default;

    // Push a structured event.
    void push(EventKind kind, std::uint32_t a = 0, std::uint32_t b = 0);

    // Push an event with a text payload.
    void push_with_text(EventKind kind, const std::string& text,
                        std::uint32_t a = 0, std::uint32_t b = 0);

    // Push a notification event with a text message.
    // If duration is non-zero, it overrides the default display time.
    // target_player addresses one global player index; -1 broadcasts.
    void push_notification(const std::string& message, std::uint32_t duration = 0,
                           std::int32_t target_player = -1);

    // Push a sound event.
    void push_sound(std::uint32_t sound_id);

    // Access the accumulated events.
    const std::vector<Event>& events() const { return events_; }

    // Drain all events (returns them and clears the internal buffer).
    std::vector<Event> drain();

    // Append already-stamped events behind whatever is queued, preserving
    // each event's own tick stamp (never restamped to current_tick_). This
    // is the staged-lobby adoption seam: a MatchStage queues its init/on_load
    // announcements at stage time with the tick-1 stamp they carry today, and
    // the launch appends them into the live session's log AFTER the
    // level-start clears, so the first ticked drain delivers them once.
    // Ignores the suppressed_ flag: adoption is an explicit transfer, not a
    // gameplay push.
    void append(std::vector<Event> events);

    // Clear without returning.
    void clear() { events_.clear(); }

    // Check if any events have been accumulated.
    bool empty() const { return events_.empty(); }

    std::size_t size() const { return events_.size(); }
    bool suppressed() const { return suppressed_; }
    void set_suppressed(bool suppressed) { suppressed_ = suppressed; }

    // Current simulation tick (set by the simulation loop each frame).
    std::uint32_t current_tick_ = 0;

private:
    std::vector<Event> events_;
    bool suppressed_ = false;
};

class SimEventLogSuppressGuard
{
public:
    explicit SimEventLogSuppressGuard(SimEventLog& log)
        : log_(log)
        , prev_(log.suppressed())
    {
        log_.set_suppressed(true);
    }

    ~SimEventLogSuppressGuard()
    {
        log_.set_suppressed(prev_);
    }

    SimEventLogSuppressGuard(const SimEventLogSuppressGuard&) = delete;
    SimEventLogSuppressGuard& operator=(const SimEventLogSuppressGuard&) = delete;

private:
    SimEventLog& log_;
    bool prev_ = false;
};

} // namespace og::sim
