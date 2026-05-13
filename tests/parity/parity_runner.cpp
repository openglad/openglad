#include "parity_runner.h"

#include "scenario_runtime.h"

#include <openglad/core/order.h>
#include <openglad/resources/level_data_hooks.h>

namespace og::parity { const LevelDataHooks& parity_level_data_hooks(); }

#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

// `cfg` is the file-scope cfg_store in data/gparser.cpp; declared here so
// the runner can install it into the ScopedGameplayContext. Do NOT
// default-construct a local cfg_store — gameplay queries it during
// load() and tick().
extern cfg_store cfg;

namespace og::parity {

namespace {

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
    // Use the parity headless hooks (see tests/parity/parity_headless_hooks.cpp)
    // so the branch installs the same no-`attach_render` entity factory the
    // master companion uses via `headless_level_data_hooks`. The SDL hooks
    // would wire `attach_render`, allocating a WalkerRender per spawn — a
    // path the master companion does not exercise, and one that shifts the
    // post-spawn world state in ways that break byte equality.
    LevelRuntimeData level(level_id, /*headless=*/true,
                           &parity_level_data_hooks());

    SaveData save;
    og::sim::SimEventLog events;

    level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                          &level.world().rng_, &::cfg);

    ScopedGameplayContext ctx(level.world(), save, events, ::cfg);

    out.loaded = level.load();

    GameWorld& world = level.world();

    // Re-seed RNG AFTER load() — LevelRuntimeData::load reads the world's
    // RNG state during decoration, so the canonical seed must be applied
    // here for both sides to start the tick loop from the same point.
    world.rng_.state_ = spec.rng_seed;

    if (spec.fresh_arena)
        clear_world_entities(world);

    apply_post_load_spawns(world, spec);

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
