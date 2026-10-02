#ifndef _TEST_MATCH_SEED_H__
#define _TEST_MATCH_SEED_H__

// The one match seed every TESTING harness pins (#338).
//
// Production draws a fresh match seed per round from std::random_device xor
// the steady clock (og::server::draw_match_seed). That seed becomes the whole
// staged world's RNG and its weather sequence, so a test binary that launches
// a level without pinning it simulates a different world on every run, and
// the coverage gate reads a different number from identical binaries.
//
// Each harness main that links match_stage.cpp (integration_main.cpp,
// curses_test_main.cpp, headless_unit_main.cpp) calls
// og::server::set_match_seed_for_testing(kHarnessMatchSeed) right after
// InitGoogleTest. A test that needs a different world scopes its own seed and
// restores THIS one, never nullopt: nullopt is production entropy, and every
// later test in the binary would go random again.
//
// The value is the curses lobby harness's pinned seed (it predates this
// header as kPinnedCursesMatchSeed, chosen when a joiner kept spawning inside
// gladiator/1's guard pack); it is taken by provenance, never picked for the
// lines it happens to cover.

#include <cstdint>

inline constexpr std::uint32_t kHarnessMatchSeed = 0x0C0FFEEDu;

#endif
