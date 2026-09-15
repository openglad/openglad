#pragma once

#include <openglad/core/irandom.h>
#include <openglad/gameplay/game_world.h>

// The ONE way a test scripts the simulation RNG.
//
// OpenGlad runs two independent random streams and a test has to say which one
// it means:
//   * the GAMEPLAY stream -- walker construction, combat rolls, autotiling --
//     reached through gameplay_rng_override(); tests install it by handing a
//     GameContext to push_test_context() (see include/openglad/interface/
//     game_context.h), which is what TestGameWorld and the R-fixtures do.
//   * the SIM stream -- GameWorld::rng_, drawn by living::act, act_random,
//     act_guard, walker::death and statistics::try_command -- reached through
//     og::sim::set_sim_random_override(). push_test_context does NOT touch it.
//     This guard is the way in.
//
// The hook is an unconditional gameplay hook (game_world.h), not a TESTING
// seam, so this guard steers draws made inside og_gameplay-compiled code in
// EVERY test binary -- headless unit groups included, where og_gameplay is
// built without -DTESTING.
//
// Guards nest: the constructor keeps whatever ref was installed and the
// destructor puts it back, so an inner guard restores the outer one and the
// outermost restores "no override".
//
// The alternative idiom stays valid: a test that wants the REAL LCG at a known
// point sets `world.rng_.state_ = k` and asserts observable outcomes. Use this
// guard when you want a scripted stream or a recording spy instead.
class ScopedSimRandom
{
public:
    explicit ScopedSimRandom(IRandom* rng)
        : rng_(rng), previous_(og::sim::set_sim_random_override(&rng_))
    {
    }

    ~ScopedSimRandom() { og::sim::set_sim_random_override(previous_); }

    ScopedSimRandom(const ScopedSimRandom&) = delete;
    ScopedSimRandom& operator=(const ScopedSimRandom&) = delete;
    ScopedSimRandom(ScopedSimRandom&&) = delete;
    ScopedSimRandom& operator=(ScopedSimRandom&&) = delete;

private:
    IRandom* rng_;
    IRandom** previous_;
};
