/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/data/level_render.h>
#include <functional>
#include <memory>

class LevelRuntimeData;
class GameWorld;
class walker;
class screen;
class PixieData;
struct EntityFactory;

struct LevelDataHooks
{
    using ClearStaleViewControlsFn = void (*)(LevelRuntimeData* level);
    using DrawFn = void (*)(LevelRuntimeData* level, screen* screenp);
    using CreateLevelRenderFn = std::unique_ptr<LevelRender> (*)(PixieData pixdata[]);
    using CreateEntityFactoryFn = EntityFactory (*)();
    using WireWorldEntityServicesFn = void (*)(GameWorld* world, LevelRuntimeData* level);

    ClearStaleViewControlsFn clear_stale_view_controls = nullptr;
    DrawFn draw = nullptr;
    CreateLevelRenderFn create_level_render = nullptr;
    CreateEntityFactoryFn create_entity_factory = nullptr;
    WireWorldEntityServicesFn wire_world_entity_services = nullptr;
};

// Runtime providers expose pre-wired capability tables.
const LevelDataHooks& sdl_level_data_hooks();
const LevelDataHooks& headless_level_data_hooks();
