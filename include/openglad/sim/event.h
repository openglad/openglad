#pragma once

#include <cstdint>
#include <string>

namespace og::sim {

// Event kinds emitted by the simulation layer.
// Runtime/render layers consume these to produce audio, visual FX, and UI updates.
enum class EventKind : std::uint32_t {
    None = 0,
    Damage = 1,        // Entity took damage: a=target_id, b=amount
    Death = 2,         // Entity died: a=entity_id, b=killer_id
    Spawn = 3,         // Entity spawned: a=entity_id, b=family
    PlaySound = 4,     // Request sound: a=sound_id, b=0
    SpawnFx = 5,       // Spawn visual effect: a=fx_type, b=position_packed
    TextPopup = 6,     // Display text popup: a=text_id, b=duration
    LevelComplete = 7, // Level completed: a=level_id, b=score
    Notification = 8,  // Text notification: message in text field
    LevelLost = 9,     // Level failed: a=reason
    EntityHeal = 10,   // Entity healed: a=target_id, b=amount
};

// Minimal deterministic event format for headless simulation tests.
// This is intentionally POD-ish so event streams can be compared byte-for-byte.
struct Event final {
    std::uint32_t tick = 0;
    EventKind kind = EventKind::None;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::string text;  // Optional text payload for Notification events

    bool operator==(const Event& o) const = default;
};

} // namespace og::sim
