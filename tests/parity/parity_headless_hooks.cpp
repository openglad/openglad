// Parity harness — branch-side headless `LevelDataHooks` shim.
//
// The branch's SDL build only links `sdl_level_data_hooks()`; the master
// companion uses `headless_level_data_hooks()` from
// src/platform/text/platform_headless.cpp. The SDL hook installs an
// `EntityFactory::attach_render` callback that constructs a WalkerRender
// per spawned walker — a per-spawn allocation/initialisation path the
// master companion does not exercise, which perturbs the world's
// entity-id counter and post-spawn state in ways that break byte
// equality with the master goldens.
//
// To keep the runner mirrored with `parity_dump_master`, this file
// provides a local headless-style table that builds a `loader` without
// `attach_render` (matching `headless_create_entity_factory`) and wires
// the world's entity factory / configurator / derived-stats through it.
// `parity_runner.cpp` installs this table instead of `sdl_level_data_hooks`.

#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_render.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/level_data_hooks.h>

#include <memory>
#include <mutex>
#include <string>

namespace og::parity {

namespace {

loader* parity_headless_entity_loader()
{
    static auto game_loader = std::make_unique<loader>([] {
        EntityFactory factory;
        // No attach_render: matches headless_create_entity_factory on
        // master. report_error is a no-op; the parity scenarios spawn
        // only known-valid (order, family) pairs.
        factory.report_error = [](const std::string&) {};
        return factory;
    }());
    return game_loader.get();
}

void parity_clear_stale_view_controls(LevelRuntimeData*) {}
void parity_level_data_draw(LevelRuntimeData*, screen*) {}

std::unique_ptr<LevelRender> parity_create_level_render(PixieData[])
{
    return nullptr;
}

EntityFactory parity_create_entity_factory()
{
    EntityFactory factory;
    factory.report_error = [](const std::string&) {};
    return factory;
}

void parity_wire_world_entity_services(GameWorld* world, LevelRuntimeData*)
{
    if (world == nullptr) return;
    loader* game_loader = parity_headless_entity_loader();
    if (game_loader == nullptr) return;

    world->entity_factory =
        [game_loader](Order order, std::int32_t family)
            -> std::unique_ptr<walker> {
        return game_loader->create_walker_owned(order, family);
    };
    world->entity_configurator =
        [game_loader](walker& entity, Order order, std::int32_t family)
            -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(),
                                          entity.family());
    };
    world->entity_derived_stats =
        [game_loader](walker* entity, Order order, std::int32_t family) {
        if (entity == nullptr) return;
        game_loader->set_derived_stats(entity, order, family);
    };
}

const LevelDataHooks kParityHeadlessLevelDataHooks{
    .clear_stale_view_controls = parity_clear_stale_view_controls,
    .draw                      = parity_level_data_draw,
    .create_level_render       = parity_create_level_render,
    .create_entity_factory     = parity_create_entity_factory,
    .wire_world_entity_services = parity_wire_world_entity_services,
};

} // namespace

const LevelDataHooks& parity_level_data_hooks()
{
    return kParityHeadlessLevelDataHooks;
}

} // namespace og::parity
