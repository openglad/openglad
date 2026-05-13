// Parity harness — branch-side LevelDataHooks table mirroring the master
// companion's `headless_level_data_hooks` (defined in
// src/platform/text/platform_headless.cpp, but only linked into the
// openglad_text headless client). The og_test_parity binary links the
// SDL game library, so the production `sdl_level_data_hooks` is the
// only LevelDataHooks symbol available to it. That table installs an
// `EntityFactory::attach_render` callback that allocates a per-walker
// WalkerRender during `add_ob`; `parity_dump_master` runs without any
// `attach_render` callback (see master's
// `headless_create_entity_factory` in platform_headless.cpp), so
// branch dumps produced under `sdl_level_data_hooks` are structurally
// incompatible with the master goldens.
//
// This file provides a `parity_level_data_hooks()` LevelDataHooks
// table whose entity factory mirrors master's headless factory: no
// `attach_render`, no `report_error` popup. `parity_runner.cpp`
// installs this table instead of the SDL one so that the branch
// runner and the master companion install the same entity factory
// in `world.entity_factory` and dump from the same starting state.

#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_render.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/level_data_hooks.h>

#include <memory>
#include <string>

namespace og::parity {

namespace {

loader* parity_headless_entity_loader()
{
    static auto game_loader = std::make_unique<loader>([] {
        EntityFactory factory;
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
