/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The headless level-data capability table + entity loader, factored out of
 * src/platform/text/platform_headless.cpp so it can link into the SDL client
 * too. The staged lobby (og::server::MatchStage) builds its authoritative
 * preview worlds with HEADLESS hooks on every platform — including inside the
 * SDL picker — so these must not live beside the text platform's link-time
 * SDL stubs (which conflict with the real SDL implementations). Dual-listed
 * like level_runtime_data.cpp: compiled into the og_interface component AND
 * into every headless binary's source list.
 */
#include <openglad/core/util.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_render.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/level_data_hooks.h>

#include <memory>
#include <mutex>
#include <string>

class LevelRuntimeData;
class screen;

namespace {

// Safe no-op: view controls are an SDL render concern; headless has no views.
void headless_clear_stale_view_controls(LevelRuntimeData*) {}

std::once_flag warn_draw_impl;

void headless_level_data_draw(LevelRuntimeData*, screen*)
{
    std::call_once(warn_draw_impl, [] {
        LogWarn("level_data_draw_impl: not supported in headless mode\n");
    });
}

std::unique_ptr<LevelRender> headless_create_level_render(PixieData[])
{
    static std::once_flag warn_flag;
    std::call_once(warn_flag, []() { Log("Warning: create_level_render not supported in headless mode\n"); });
    return nullptr;
}

EntityFactory headless_create_entity_factory()
{
    EntityFactory factory;
    // Headless mode intentionally leaves attach_render empty.
    factory.report_error = [](const std::string& message) {
        LogError("{}\n", message);
    };
    return factory;
}

// File-local: the SDL platform keeps its own twin in sdl_context_services.cpp
// wired to the SDL loader; this one must never leak past this TU.
void wire_world_with_loader(GameWorld* world, loader* game_loader)
{
    if (world == nullptr || game_loader == nullptr)
        return;

    world->entity_factory = [game_loader](Order order, std::int32_t family) -> std::unique_ptr<walker> {
        return game_loader->create_walker_owned(order, family);
    };

    world->entity_configurator = [game_loader](walker& entity, Order order, std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(), entity.family());
    };

    world->entity_derived_stats = [game_loader](walker* entity, Order order, std::int32_t family) {
        if (entity == nullptr)
            return;
        game_loader->set_derived_stats(entity, order, family);
    };
}

void headless_wire_world_entity_services(GameWorld* world, LevelRuntimeData* level)
{
    (void)level;
    // #162 chokepoint: wiring runs at every headless LevelRuntimeData
    // construction AND load(), so every curses/text/server level build gets
    // a loader that matches the currently mounted campaign. Headless render
    // components hold no pixie borrows (attach_render is empty; walkers copy
    // size/frame scalars at attach), so rebuilding here is always safe.
    headless_entity_loader()->reload_graphics_if_stale();
    wire_world_with_loader(world, headless_entity_loader());
}

const LevelDataHooks kHeadlessLevelDataHooks{
    .clear_stale_view_controls = headless_clear_stale_view_controls,
    .draw = headless_level_data_draw,
    .create_level_render = headless_create_level_render,
    .create_entity_factory = headless_create_entity_factory,
    .wire_world_entity_services = headless_wire_world_entity_services,
};

} // namespace

loader* headless_entity_loader()
{
    static auto game_loader = [] {
        auto entity_loader =
            std::make_unique<loader>(headless_create_entity_factory());
        return entity_loader;
    }();
    return game_loader.get();
}

const LevelDataHooks& headless_level_data_hooks()
{
    return kHeadlessLevelDataHooks;
}
