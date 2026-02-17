/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>

class PixieData;
class viewscreen;

// Abstract interface for level tile rendering.
// SDL builds create an SdlLevelRender that owns back[PIX_MAX] pixieN tiles.
// Headless builds leave renderer_ as nullptr.
class ILevelRender {
public:
    virtual ~ILevelRender() = default;
    virtual void init_tiles(PixieData pixdata[]) = 0;
    virtual void reset_tiles(PixieData pixdata[]) = 0;
    virtual void draw_tile(int tile_index, int x, int y, viewscreen* view) = 0;

protected:
    ILevelRender() = default;
};
