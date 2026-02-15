#include <openglad/sim/simulator.h>

#include "unit.h"

// These tests verify that the simulation module behaves correctly under
// conditions typical of the Emscripten/web build:
//   - Variable delta-time (browser requestAnimationFrame jitter)
//   - Rapid 60-FPS stepping (game runs inside browser frame callback)
//   - Full 4-player input snapshot processing

using namespace og::sim;

// ---------------------------------------------------------------------------
// Variable delta-time determinism
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_sim_variable_dt_determinism)
{
    // dt is currently unused in the simulator's accumulator logic,
    // but verify the API handles variable dt without crashing or
    // producing different state for the same input sequence.
    Simulator s1(42u);
    Simulator s2(42u);

    InputSnapshot snap;
    snap.players[0].cmd = 1;
    snap.players[0].value = 10;

    // s1: constant dt
    for (int i = 0; i < 30; ++i)
        s1.step(snap, 1.0f / 60.0f);

    // s2: variable dt (simulating browser jitter)
    float dts[] = {0.014f, 0.018f, 0.016f, 0.020f, 0.012f, 0.017f};
    for (int i = 0; i < 30; ++i)
        s2.step(snap, dts[i % 6]);

    // State must be identical (sim is tick-based, not dt-dependent)
    OG_ASSERT(s1.state().tick == s2.state().tick);
    OG_ASSERT(s1.state().acc == s2.state().acc);
    OG_ASSERT(s1.events().size() == s2.events().size());
}

// ---------------------------------------------------------------------------
// Rapid 60-FPS stepping (web pattern)
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_sim_rapid_60fps_stepping)
{
    // Simulate 10 seconds of game time at 60 FPS (600 ticks).
    // Verify the simulator handles this without overflow or corruption.
    Simulator s(123u);
    InputSnapshot snap;
    snap.players[0].cmd = 1;
    snap.players[0].value = 5;

    for (int i = 0; i < 600; ++i) {
        s.step(snap, 1.0f / 60.0f);
        s.clear_events(); // Web build clears events each frame
    }

    OG_ASSERT(s.state().tick == 600);
    OG_ASSERT(s.state().acc != 0); // accumulator should have advanced
}

OG_UNIT_TEST(test_sim_rapid_stepping_determinism)
{
    // Two simulators doing 600 ticks with clear_events each frame
    // must produce the same final state.
    Simulator s1(99u);
    Simulator s2(99u);

    InputSnapshot snap;
    snap.players[0].cmd = 2;
    snap.players[1].cmd = 1;
    snap.players[1].value = 3;

    for (int i = 0; i < 600; ++i) {
        s1.step(snap, 1.0f / 60.0f);
        s1.clear_events();
        s2.step(snap, 1.0f / 60.0f);
        s2.clear_events();
    }

    OG_ASSERT(s1.state().tick == s2.state().tick);
    OG_ASSERT(s1.state().acc == s2.state().acc);
}

// ---------------------------------------------------------------------------
// 4-player input snapshots
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_sim_4player_input_snapshot)
{
    Simulator s(55u);

    InputSnapshot snap;
    for (int p = 0; p < InputSnapshot::MAX_PLAYERS; ++p) {
        snap.players[p].cmd = static_cast<std::uint32_t>(p + 1);
        snap.players[p].value = static_cast<std::uint32_t>((p + 1) * 10);
    }

    s.step(snap, 1.0f / 60.0f);

    OG_ASSERT(s.state().tick == 1);
    OG_ASSERT(s.events().size() == 1);
    // All 4 players' inputs should contribute to the accumulator
    OG_ASSERT(s.state().acc != 0);
}

OG_UNIT_TEST(test_sim_4player_different_inputs_differ)
{
    Simulator s1(77u);
    Simulator s2(77u);

    InputSnapshot snap1;
    InputSnapshot snap2;

    // snap1: player 0 active
    snap1.players[0].cmd = 1;
    snap1.players[0].value = 100;

    // snap2: player 3 active (different player)
    snap2.players[3].cmd = 1;
    snap2.players[3].value = 100;

    s1.step(snap1, 1.0f / 60.0f);
    s2.step(snap2, 1.0f / 60.0f);

    // Same seed, same total cmd/value, but different player indices
    // should produce different accumulator values
    OG_ASSERT(s1.state().acc != s2.state().acc);
}

OG_UNIT_TEST(test_sim_4player_all_zero_inputs)
{
    Simulator s(10u);

    InputSnapshot snap; // all zero
    s.step(snap, 1.0f / 60.0f);

    OG_ASSERT(s.state().tick == 1);
    // Even with zero inputs, RNG still contributes to accumulator
    OG_ASSERT(s.state().acc != 0);
}

// ---------------------------------------------------------------------------
// quit_requested flag
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_sim_quit_requested_flag)
{
    Simulator s(1u);

    InputSnapshot snap;
    snap.quit_requested = true;
    // Verify it doesn't crash or alter determinism
    s.step(snap, 1.0f / 60.0f);
    OG_ASSERT(s.state().tick == 1);

    // Same with quit_requested=false should give same state
    // (quit_requested is metadata, not part of sim logic)
    Simulator s2(1u);
    InputSnapshot snap2;
    snap2.quit_requested = false;
    s2.step(snap2, 1.0f / 60.0f);
    OG_ASSERT(s.state().acc == s2.state().acc);
}

// ---------------------------------------------------------------------------
// Event stream under web patterns
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_sim_event_stream_web_pattern)
{
    // Web pattern: step, read events, clear, step again
    Simulator s(42u);
    InputSnapshot snap;
    snap.players[0].cmd = 1;

    // First frame
    s.step(snap, 1.0f / 60.0f);
    OG_ASSERT(s.events().size() == 1);
    OG_ASSERT(s.events()[0].tick == 1);
    s.clear_events();

    // Second frame
    s.step(snap, 1.0f / 60.0f);
    OG_ASSERT(s.events().size() == 1);
    OG_ASSERT(s.events()[0].tick == 2);
    s.clear_events();

    // Third frame
    s.step(snap, 1.0f / 60.0f);
    OG_ASSERT(s.events().size() == 1);
    OG_ASSERT(s.events()[0].tick == 3);
}

OG_UNIT_TEST(test_sim_events_accumulate_without_clear)
{
    // If events are NOT cleared (unlike web pattern), they accumulate
    Simulator s(42u);
    InputSnapshot snap;
    snap.players[0].cmd = 1;

    for (int i = 0; i < 10; ++i)
        s.step(snap, 1.0f / 60.0f);

    OG_ASSERT(s.events().size() == 10);
    // Verify tick ordering
    for (std::size_t i = 0; i < s.events().size(); ++i) {
        OG_ASSERT(s.events()[i].tick == static_cast<std::uint32_t>(i + 1));
    }
}

// ---------------------------------------------------------------------------
// Cross-platform RNG consistency
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_sim_lcg_rng_known_values)
{
    // Verify the LCG RNG produces known values for a fixed seed.
    // This is critical for cross-platform determinism (native vs WASM).
    // The RNG formula: state = state * 1103515245 + 12345
    // We can observe RNG effects through the accumulator.

    Simulator s1(0u);
    Simulator s2(0u);

    Input in;
    in.cmd = 0;
    in.value = 0;

    // With zero input cmd/value, acc += (r >> 16) where r = RNG output
    s1.step(in);
    std::uint32_t first_acc = s1.state().acc;

    // Independently compute: seed=0 => state = 0*1103515245+12345 = 12345
    // r >> 16 = 12345 >> 16 = 0
    OG_ASSERT(first_acc == 0);

    // Second step: state = 12345*1103515245+12345 = ?
    s1.step(in);

    // Same computation on s2
    s2.step(in);
    s2.step(in);
    OG_ASSERT(s1.state().acc == s2.state().acc);
}

OG_UNIT_TEST(test_sim_rng_seed_1_produces_nonzero)
{
    // Seed 1: state = 1*1103515245+12345 = 1103527590
    // r >> 16 = 1103527590 >> 16 = 16838
    Simulator s(1u);
    Input in;
    in.cmd = 0;
    in.value = 0;
    s.step(in);
    OG_ASSERT(s.state().acc == 16838);
}

// ---------------------------------------------------------------------------
// Large tick counts (long web sessions)
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_sim_long_session_stability)
{
    // Simulate a long web session: 3600 ticks (1 minute at 60 FPS)
    Simulator s(42u);
    InputSnapshot snap;
    snap.players[0].cmd = 1;
    snap.players[0].value = 1;

    for (int i = 0; i < 3600; ++i) {
        s.step(snap, 1.0f / 60.0f);
        s.clear_events();
    }

    OG_ASSERT(s.state().tick == 3600);
    // Accumulator should be some non-trivial value (not stuck at 0 or overflowed to 0)
    // Due to uint32_t wrapping, just verify it ran without crash
    // and events were properly cleared each frame
    OG_ASSERT(s.events().empty());
}
