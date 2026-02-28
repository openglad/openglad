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

class LevelData;
class walker;
class screen;
class PixieData;

struct LevelDataHooks
{
    using ClearStaleViewControlsFn = void (*)(LevelData* level);
    using DrawFn = void (*)(LevelData* level, screen* screenp);
    using CreateLevelRenderFn = std::unique_ptr<LevelRender> (*)(PixieData pixdata[]);

    ClearStaleViewControlsFn clear_stale_view_controls = nullptr;
    DrawFn draw = nullptr;
    CreateLevelRenderFn create_level_render = nullptr;
};

// Runtime providers expose pre-wired capability tables.
const LevelDataHooks& sdl_level_data_hooks();
const LevelDataHooks& headless_level_data_hooks();
