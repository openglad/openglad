// Emscripten/WASM test: verifies core game simulation logic runs correctly
// when compiled to WebAssembly and executed via Node.js.
//
// Build: emcc -std=c++20 -I../../include -o test_wasm_sim.js test_wasm_sim.cpp ../../src/sim/simulator.cpp
// Run:   node test_wasm_sim.js

#include <openglad/sim/simulator.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

static int g_pass = 0;
static int g_fail = 0;

#define WASM_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

// Test 1: Simulator determinism (same seed + same inputs => identical state)
void test_determinism()
{
    og::sim::Simulator s1(123u);
    og::sim::Simulator s2(123u);

    for (std::uint32_t i = 0; i < 100; ++i) {
        og::sim::InputSnapshot snap;
        snap.players[0].cmd = i % 3;
        snap.players[0].value = i * 11u;
        s1.step(snap, 1.0f / 60.0f);
        s2.step(snap, 1.0f / 60.0f);
    }

    WASM_ASSERT(s1.state().tick == s2.state().tick, "tick determinism");
    WASM_ASSERT(s1.state().acc == s2.state().acc, "acc determinism");
    WASM_ASSERT(s1.state().tick == 100, "expected 100 ticks");
    std::printf("  test_determinism: OK\n");
}

// Test 2: Known RNG value (seed 1 must produce specific accumulator)
void test_rng_cross_platform()
{
    og::sim::Simulator s(1u);
    og::sim::Input in;
    in.cmd = 0;
    in.value = 0;
    s.step(in);
    // LCG: state = 1*1103515245+12345 = 1103527590, r>>16 = 16838
    WASM_ASSERT(s.state().acc == 16838, "RNG known value matches native");
    std::printf("  test_rng_cross_platform: OK (acc=%u)\n", s.state().acc);
}

// Test 3: 4-player snapshot
void test_4player_snapshot()
{
    og::sim::Simulator s(42u);
    og::sim::InputSnapshot snap;
    for (int p = 0; p < og::sim::InputSnapshot::MAX_PLAYERS; ++p) {
        snap.players[p].cmd = static_cast<std::uint32_t>(p + 1);
        snap.players[p].value = static_cast<std::uint32_t>((p + 1) * 10);
    }
    s.step(snap, 1.0f / 60.0f);
    WASM_ASSERT(s.state().tick == 1, "4-player tick");
    WASM_ASSERT(s.state().acc != 0, "4-player acc non-zero");
    std::printf("  test_4player_snapshot: OK\n");
}

// Test 4: Event stream
void test_event_stream()
{
    og::sim::Simulator s(99u);
    og::sim::InputSnapshot snap;
    snap.players[0].cmd = 1;

    for (int i = 0; i < 10; ++i) {
        s.step(snap, 1.0f / 60.0f);
        s.clear_events();
    }

    WASM_ASSERT(s.state().tick == 10, "event stream 10 ticks");
    WASM_ASSERT(s.events().empty(), "events cleared");
    std::printf("  test_event_stream: OK\n");
}

// Test 5: Long session (1 minute at 60fps = 3600 ticks)
void test_long_session()
{
    og::sim::Simulator s(77u);
    og::sim::InputSnapshot snap;
    snap.players[0].cmd = 1;
    snap.players[0].value = 1;

    for (int i = 0; i < 3600; ++i) {
        s.step(snap, 1.0f / 60.0f);
        s.clear_events();
    }

    WASM_ASSERT(s.state().tick == 3600, "long session tick count");
    std::printf("  test_long_session: OK (3600 ticks)\n");
}

// Test 6: Different seeds produce different results
void test_different_seeds()
{
    og::sim::Simulator s1(1u);
    og::sim::Simulator s2(2u);

    og::sim::InputSnapshot snap;
    snap.players[0].cmd = 5;
    for (int i = 0; i < 20; ++i) {
        s1.step(snap, 1.0f / 60.0f);
        s2.step(snap, 1.0f / 60.0f);
    }

    WASM_ASSERT(s1.state().acc != s2.state().acc, "different seeds differ");
    std::printf("  test_different_seeds: OK\n");
}

// Test 7: Verify uint32_t sizes (important for WASM compatibility)
void test_type_sizes()
{
    WASM_ASSERT(sizeof(std::uint32_t) == 4, "uint32_t is 4 bytes");
    WASM_ASSERT(sizeof(og::sim::Event) >= 16, "Event is at least 16 bytes");
    WASM_ASSERT(sizeof(og::sim::State) >= 12, "State is at least 12 bytes");
    std::printf("  test_type_sizes: OK (uint32_t=%zu, Event=%zu, State=%zu)\n",
                sizeof(std::uint32_t), sizeof(og::sim::Event), sizeof(og::sim::State));
}

int main()
{
    std::printf("=== WASM Sim Tests ===\n");

    test_determinism();
    test_rng_cross_platform();
    test_4player_snapshot();
    test_event_stream();
    test_long_session();
    test_different_seeds();
    test_type_sizes();

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
