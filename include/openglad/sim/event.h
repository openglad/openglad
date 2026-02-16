#pragma once

#include <cstdint>
#include <string>

namespace og::sim {

// Event kinds emitted by the simulation layer.
// Runtime/render layers consume these to produce audio, visual FX, and UI updates.
enum class EventKind : std::uint32_t {
    None = 0,
    PlaySound = 4,     // Request sound: a=sound_id, b=0
    Notification = 8,  // Text notification: message in text field
    SetPalette = 11,   // Request palette change: a=0 normal, a=1 blue/freeze
    RequestRedraw = 12,// Force full screen redraw
};

// Simulation event record, pushed into SimEventLog during a tick.
struct Event final {
    std::uint32_t tick = 0;
    EventKind kind = EventKind::None;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::string text;  // Optional text payload for Notification events

    bool operator==(const Event& o) const = default;
};

} // namespace og::sim
