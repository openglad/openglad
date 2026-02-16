#pragma once

#include <openglad/sim/event.h>

#include <cstdint>
#include <vector>

// Forward declarations — SimWorld works with these types but doesn't own them
struct LevelData;
struct SaveData;
namespace og::sim { class SimEventLog; }

namespace og::sim {

// Simple LCG random number generator owned by SimWorld.
// Given the same seed, produces the same sequence — making the
// simulation fully deterministic and reproducible.
class SimRandom final {
public:
    explicit SimRandom(std::uint32_t seed = 0) : state_(seed) {}

    std::uint32_t next(std::uint32_t max_exclusive) {
        if (max_exclusive == 0) return 0;
        // LCG: same constants as glibc
        state_ = state_ * 1103515245u + 12345u;
        return (state_ >> 16) % max_exclusive;
    }

    void reset(std::uint32_t seed) { state_ = seed; }
    std::uint32_t state() const { return state_; }

private:
    std::uint32_t state_;
};

// Result of a single simulation tick.
struct TickResult {
    // Level completion status after this tick:
    //   0 = foes remain
    //   1 = no foes, but exits exist
    //   2 = no foes and no exits (auto-advance)
    short level_done = 0;

    // True if the game ended during this tick (victory, defeat, or abort).
    bool game_ended = false;

    // Next level index if the game ended with a level transition.
    short next_level = -1;

    // Ending type (0=win, 1=loss, SCEN_TYPE_SAVE_ALL=save-all failure)
    short ending = 0;
};

// SimWorld: deterministic simulation tick logic.
//
// Extracts the core game logic from screen::act() into a pure simulation
// interface. Given the current level state and entity lists, it produces
// a TickResult and emits events via SimEventLog.
//
// The rendering layer (screen/viewscreen) reads the resulting state
// for display. Sound and UI notifications are dispatched from the
// accumulated events rather than being called directly during the tick.
//
// Design note: SimWorld does NOT own entities or level data. It operates
// on the existing screen/LevelData/SaveData structures via references.
// This allows incremental migration: screen::act() delegates to
// SimWorld::tick() while keeping the same data structures.
//
// SimWorld owns its own deterministic PRNG (SimRandom). The game seed
// is passed at construction time. This makes the simulation layer fully
// reproducible given the same seed — no runtime dependencies needed.
class SimWorld final {
public:
    explicit SimWorld(std::uint32_t seed = 0) : rng_(seed) {}

    // Run one simulation tick.
    //
    // This executes all entity act() calls, handles dead entity cleanup,
    // checks level completion, and emits events. It mirrors the logic
    // previously embedded in screen::act().
    //
    // Parameters:
    //   level        - Level data (entity lists, grid, etc.)
    //   save         - Save data (team info)
    //   enemy_freeze - Freeze counter (decremented each tick)
    //   end          - Game-end flag
    //   events       - Event log to push simulation events into
    //
    // Returns a TickResult describing what happened during this tick.
    TickResult tick(LevelData& level, SaveData& save,
                    std::int32_t& enemy_freeze, char end,
                    SimEventLog& events);

    // Accumulated tick counter.
    std::uint32_t tick_count() const { return tick_count_; }

    // Access the sim-layer RNG (for testing / seeding).
    SimRandom& rng() { return rng_; }
    const SimRandom& rng() const { return rng_; }

private:
    std::uint32_t tick_count_ = 0;
    SimRandom rng_;
};

} // namespace og::sim
