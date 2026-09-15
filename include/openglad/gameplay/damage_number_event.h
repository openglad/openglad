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

// Authority side: how many numbers one tick's lift may put on the wire.
// serialize_event_batch_message THROWS above a 65535-byte payload
// (world_snapshot.cpp, "payload exceeds 16-bit wire length"), and CI builds
// with VALIDATE_SERIALIZATION=ON make even the local in-process shadow
// serialize, so an unbounded lift is a reachable crash, not a bandwidth
// worry. One DamageNumber event costs 28 bytes on the wire (5 x u32 + a u32
// text length of 0 + c), so 512 x 28 = 14,336 B and the tick keeps >50 KB for
// its other events. 512 numbers in one 1/12 s tick means 256 hits landing at
// once, which play cannot reach: this is a crash guard, never a visible
// limit.
inline constexpr std::size_t kMaxLiftedDamageNumbersPerTick = 512;

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

// Authority side: append one event per queued number of EVERY oblist walker
// — seat-bound owners first, so a player's own pane never loses its numbers
// to a bystander's when the per-tick budget bites, then everyone else
// (spectator/follow panes watch walkers no seat owns) — then clear EVERY list
// unconditionally. Render is the only eraser in the classic code, so on a
// headless authority the lists otherwise grow for the life of the level.
// Numbers past kMaxLiftedDamageNumbersPerTick are dropped and drained.
// fxlist/weaplist attacker copies (walker_specials.cpp do_combat_damage with
// an FX attacker) are a non-goal: they cannot paint — no pane's control is an
// FX walker — and they die with their walker.
void lift_damage_number_events(
    GameWorld& world,
    const std::array<walker*, kMaxGlobalPlayers>& player_controls,
    std::vector<Event>& out);

// Mirror side: push the decoded numbers back onto their owners. Returns how
// many were applied; unknown owners are dropped.
std::size_t apply_damage_number_events(GameWorld& world,
                                       const SimEventBatch& batch);

} // namespace og::sim
