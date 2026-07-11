/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <memory>

class PixieData;
class viewscreen;

// Concrete level tile renderer that owns PIX_MAX pixieN tile sprites.
// SDL builds create a LevelRender; headless builds leave renderer_ as nullptr.
class LevelRender {
public:
    LevelRender();
    ~LevelRender();

    LevelRender(const LevelRender&) = delete;
    LevelRender& operator=(const LevelRender&) = delete;

    void init_tiles(PixieData pixdata[]);
    void reset_tiles(PixieData pixdata[]);
    void draw_tile(int tile_index, int x, int y, viewscreen* view,
                   unsigned char alpha = 255);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Factory: create and initialize a LevelRender from pixdata.
std::unique_ptr<LevelRender> create_sdl_level_render(PixieData pixdata[]);
