#pragma once

// Floating damage/heal numbers on the wire.
//
// The 2013 rising-number overlay (Jonathan Dearborn, 5a3dc5ea/235fc660) is
// pushed by the SIM into walker::damage_numbers, but every client since the
// networking merge (#106) renders a MIRROR world that never ticks the sim, so
// nothing ever pushed a number there. These helpers lift the authoritative
// lists onto the existing sim event batch at broadcast time and re-apply them
// on the mirror.
//
// Lifting in GameServer rather than emitting at the sim push sites is what
// keeps the parity goldens byte-identical: the parity harness ticks GameWorld
// directly and dumps the SimEventLog the sim wrote, so a sim-emitted kind
// would move every combat golden. The server already synthesizes display
// events at this exact seam (SetPalette/RequestRedraw/SetEnd).

#include <openglad/gameplay/event.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/walker.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class GameWorld;

namespace og::sim {

struct SimEventBatch;

// The mirror never ticks the sim, so its only writer is
// apply_damage_number_events(). Cap each list so a hostile server cannot grow
// a client's overlay without bound; the oldest entries are dropped.
inline constexpr std::size_t kMaxMirrorDamageNumbers = 64;

struct DecodedDamageNumber
{
    std::uint32_t owner_entity_id = 0u;
    walker::DamageNumber number{0.0f, 0.0f, 0.0f, 0u, 0u};
};

// a = owner entity id, b = (uint16 x << 16) | uint16 y in WORLD pixels,
// c = (colour << 24) | value as unsigned 16.8 fixed point, tick =
// created_tick. Every field saturates rather than wrapping. target_player
// stays -1: the per-pane filtering is a render-side rule
// (walker_draw.cpp, "only seen by the people they affect").
Event encode_damage_number_event(std::uint32_t owner_entity_id,
                                 const walker::DamageNumber& number);

// nullopt unless event.kind == EventKind::DamageNumber.
std::optional<DecodedDamageNumber> decode_damage_number_event(
    const Event& event);

// Authority side: append one event per queued number of every walker bound to
// a player seat, then clear EVERY walker's list unconditionally. Render is the
// only eraser in the classic code, so on a headless authority the lists
// otherwise grow for the life of the level; unbound AI lists are drained but
// never sent.
void lift_damage_number_events(
    GameWorld& world,
    const std::array<walker*, kMaxGlobalPlayers>& player_controls,
    std::vector<Event>& out);

// Mirror side: push the decoded numbers back onto their owners. Returns how
// many were applied; unknown owners are dropped.
std::size_t apply_damage_number_events(GameWorld& world,
                                       const SimEventBatch& batch);

} // namespace og::sim
