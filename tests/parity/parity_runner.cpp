#include "parity_runner.h"

#include "scenario_runtime.h"

#include <openglad/core/irandom.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include <cstdlib>

// `cfg` is the file-scope cfg_store in data/gparser.cpp; declared here so
// the runner can install it into the ScopedGameplayContext. Do NOT
// default-construct a local cfg_store — gameplay queries it during
// load() and tick().
extern cfg_store cfg;

namespace og::parity {

namespace {

// Phase 04 parity-fix: a libc-rand-backed IRandom used to scope the
// gameplay_rng_override around `apply_post_load_spawns`. The branch's
// `walker_rng()` consults `gameplay_rng_override()` first; when the
// override is installed, walker construction reads from libc rand
// (matching master's pre-migration `rand()%10` for path_check_counter).
// The override is removed before the tick loop so combat sites that
// master also reads from `world.rng_` (compute_xp_from_action,
// compute_base_damage) stay on `world.rng_` on both sides.
class LibcRandRandom : public IRandom
{
public:
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0) return 0;
        return static_cast<std::uint32_t>(std::rand()) % max_exclusive;
    }
};

// Test-side ScopedGameplayContext mirror. Cannot reuse the one declared in
// tests/test_gameplay_context_scope.h because including it would pull in the
// rest of the integration-test fixture support; we only need to install the
// current_game pointer for the duration of `level.load()` and the tick loop.
class ScopedGameplayContext
{
public:
    ScopedGameplayContext(GameWorld& world, SaveData& save,
                          og::sim::SimEventLog& events, cfg_store& config)
        : previous_(current_game)
    {
        context_.world      = &world;
        context_.save       = &save;
        context_.sim_events = &events;
        context_.config     = &config;
        current_game        = &context_;
    }

    ~ScopedGameplayContext()
    {
        current_game = previous_;
    }

    ScopedGameplayContext(const ScopedGameplayContext&) = delete;
    ScopedGameplayContext& operator=(const ScopedGameplayContext&) = delete;
    ScopedGameplayContext(ScopedGameplayContext&&) = delete;
    ScopedGameplayContext& operator=(ScopedGameplayContext&&) = delete;

private:
    GameplayContext  context_{};
    GameplayContext* previous_ = nullptr;
};

} // namespace

RunOutcome run_scenario(const ScenarioSpec& spec)
{
    RunOutcome out;

    const int level_id = scenario_level_id(spec.scenario_file);
    LevelRuntimeData level(level_id, /*headless=*/true,
                           &sdl_level_data_hooks());

    SaveData save;
    og::sim::SimEventLog events;

    level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                          &level.world().rng_, &::cfg);

    ScopedGameplayContext ctx(level.world(), save, events, ::cfg);

    // Seed libc rand deterministically. The branch's walker_combat.cpp
    // (the migrated hit-fx ani_type site, see parity-fix above) and
    // master's gameplay both consume `std::rand()` at the same
    // call site; both binaries reset libc state to seed 1 at scenario
    // start so the consumption is byte-equivalent.
    std::srand(1);

    out.loaded = level.load();

    GameWorld& world = level.world();

    // Re-seed RNG AFTER load() — LevelRuntimeData::load reads the world's
    // RNG state during decoration, so the canonical seed must be applied
    // here for both sides to start the tick loop from the same point.
    world.rng_.state_ = spec.rng_seed;

    if (spec.fresh_arena)
        clear_world_entities(world);

    // Scope a libc-rand override around `apply_post_load_spawns` so the
    // branch's `walker_rng()` reads libc rand during walker construction
    // — matching master's pre-migration `rand()%10` for the
    // path_check_counter init. The override is removed immediately
    // after the spawn loop so combat sites that consult
    // `gameplay_rng_override()` during the tick loop (combat_rng's
    // damage/xp callers) stay on `world.rng_` on both sides.
    {
        LibcRandRandom parity_construct_rng;
        IRandom*       parity_construct_slot = &parity_construct_rng;
        set_gameplay_rng_override(&parity_construct_slot);
        apply_post_load_spawns(world, spec);
        set_gameplay_rng_override(nullptr);
    }

    // Phase 03 coverage observation. The schema-v1 dump only carries
    // oblist (without per-walker order) and fxlist, but the coverage
    // gate needs to know which weapon / treasure / generator families
    // were instantiated over the lifetime of the run. Walkers that die
    // mid-run get cleaned out of oblist before end-of-run, so we sample
    // every list at every tick rather than only at the end.
    auto bag_walker = [](const walker* w, CoverageObservation& obs) {
        if (w == nullptr) return;
        const auto family = static_cast<std::int32_t>(w->family());
        switch (w->query_order())
        {
            case Order::Living:    obs.walker_families.insert(family); break;
            case Order::Weapon:    obs.weapon_families.insert(family); break;
            case Order::Treasure:  obs.treasure_families.insert(family); break;
            case Order::Generator: obs.generator_families.insert(family); break;
            case Order::FX:        obs.effect_families.insert(family); break;
            default:               break;
        }
    };
    auto sample_world = [&]() {
        for (const auto& uptr : world.oblist)
            bag_walker(uptr.get(), out.coverage);
        for (const auto& uptr : world.weaplist)
            bag_walker(uptr.get(), out.coverage);
        for (const auto& uptr : world.fxlist)
            bag_walker(uptr.get(), out.coverage);
    };

    sample_world(); // post-spawn snapshot

    ScenarioInputDriver input_driver;
    for (std::uint32_t t = 0; t < spec.tick_budget; ++t)
    {
        apply_inputs_at_tick(world, spec, t, input_driver, &events);
        world.tick();
        sample_world();
        // Phase 02 redo: do NOT break on level_done. Both sides drive the
        // full tick_budget so cadence comparison stays apples-to-apples;
        // level_done is recorded in the schema-v1 dump as a top-level
        // field for divergence reports to look at.
        if (world.level_done != 0 && !out.early_stopped)
        {
            out.early_stopped   = true;
            out.early_stop_tick = world.tick_count_;
        }
    }
    out.ticked = true;
    out.dump   = capture_state_dump(world, &events);

    for (const auto& ev : events.events())
        out.coverage.event_kinds.insert(
            event_kind_symbol(static_cast<std::uint32_t>(ev.kind)));

    return out;
}

} // namespace og::parity
