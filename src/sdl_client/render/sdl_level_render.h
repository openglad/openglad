/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/data/level_render.h>
#include <openglad/data/pixie_data.h>
#include <openglad/render/pixien.h>
#include <openglad/legacy/pixdefs.h>
#include <memory>

// SDL implementation of ILevelRender that owns PIX_MAX pixieN tile sprites.
class SdlLevelRender final : public ILevelRender {
public:
    std::unique_ptr<pixieN> back[PIX_MAX];

    void init_tiles(PixieData pixdata[]) override;
    void reset_tiles(PixieData pixdata[]) override;
    void draw_tile(int tile_index, int x, int y, viewscreen* view) override;
};

// Factory: create and initialize an SdlLevelRender from pixdata.
std::unique_ptr<ILevelRender> create_sdl_level_render(PixieData pixdata[]);
