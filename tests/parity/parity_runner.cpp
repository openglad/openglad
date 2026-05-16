#include "parity_runner.h"

#include "scenario_runtime.h"

#include <algorithm>
#include <set>
#include <string>

#include <openglad/core/order.h>
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
    LevelRuntimeData level(level_id, /*headless=*/true,
                           &sdl_level_data_hooks());

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

    // Phase 04 — splice run-observed weapon families into the final dump.
    // Many projectile weapons live only a handful of ticks (knife/rock hit
    // their target and die within 1-2 ticks of emission), so the
    // end-of-run weaplist may not contain a weapon that was definitively
    // emitted during the run. The coverage sampler (sample_world above)
    // already records every weapon family it observed across all ticks;
    // we surface that observation as synthetic WeaponEntry rows here so
    // `WeaponFamilyEmitted` predicates can evaluate against "did this
    // family fly at any point during the run" rather than only "is this
    // family alive in weaplist at the final tick". The synthetic rows
    // carry id=0 and lifetime=0 so they sort first and do not collide
    // with real weaplist entries (whose entity_id() values start at 1).
    {
        std::set<std::string> existing_family_strings;
        for (const auto& w : out.dump.weapons)
            existing_family_strings.insert(w.family);
        for (std::int32_t fam : out.coverage.weapon_families)
        {
            const std::string sym = family_symbol(fam);
            if (existing_family_strings.count(sym) != 0) continue;
            WeaponEntry synth;
            synth.id       = 0;
            synth.family   = sym;
            synth.team     = 0;
            synth.xpos     = 0;
            synth.ypos     = 0;
            synth.lifetime = 0;
            out.dump.weapons.push_back(std::move(synth));
            existing_family_strings.insert(sym);
        }
        std::sort(out.dump.weapons.begin(), out.dump.weapons.end(),
                  [](const WeaponEntry& a, const WeaponEntry& b) {
                      if (a.family != b.family) return a.family < b.family;
                      return a.id < b.id;
                  });
    }

    // Same splice for effect families — effects also expire quickly and
    // EffectFamilyCount predicates ask "was this FX emitted during the
    // run", not "is it alive in fxlist at the final tick".
    {
        std::set<std::string> existing_effect_strings;
        for (const auto& e : out.dump.effects)
            existing_effect_strings.insert(e.family);
        for (std::int32_t fam : out.coverage.effect_families)
        {
            const std::string sym = family_symbol(fam);
            if (existing_effect_strings.count(sym) != 0) continue;
            EffectEntry synth;
            synth.id       = 0;
            synth.family   = sym;
            synth.xpos     = 0;
            synth.ypos     = 0;
            synth.lifetime = 0;
            out.dump.effects.push_back(std::move(synth));
            existing_effect_strings.insert(sym);
        }
        std::sort(out.dump.effects.begin(), out.dump.effects.end(),
                  [](const EffectEntry& a, const EffectEntry& b) {
                      if (a.family != b.family) return a.family < b.family;
                      return a.id < b.id;
                  });
    }

    for (const auto& ev : events.events())
        out.coverage.event_kinds.insert(
            event_kind_symbol(static_cast<std::uint32_t>(ev.kind)));

    return out;
}

} // namespace og::parity
