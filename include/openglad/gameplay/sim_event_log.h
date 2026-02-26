#pragma once

#include <openglad/gameplay/event.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
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
    void push(EventKind kind, std::uint32_t a = 0, std::uint32_t b = 0,
              const std::string& text = "");

    // Push a notification event with a text message.
    // If duration is non-zero, it overrides the default display time.
    void push_notification(const std::string& message, std::uint32_t duration = 0);

    // Push a sound event.
    void push_sound(std::uint32_t sound_id);

    // Access the accumulated events.
    const std::vector<Event>& events() const { return events_; }

    // Drain all events (returns them and clears the internal buffer).
    std::vector<Event> drain();

    // Clear without returning.
    void clear() { events_.clear(); }

    // Check if any events have been accumulated.
    bool empty() const { return events_.empty(); }

    std::size_t size() const { return events_.size(); }

    // Current simulation tick (set by the simulation loop each frame).
    std::uint32_t current_tick_ = 0;

private:
#ifndef NDEBUG
    void assert_thread_ownership_() const;
    mutable std::thread::id owner_thread_{};
    mutable bool owner_thread_initialized_ = false;
#endif
    std::vector<Event> events_;
};

} // namespace og::sim
