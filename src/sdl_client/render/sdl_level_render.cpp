/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "sdl_level_render.h"
#include <openglad/render/view.h>

void SdlLevelRender::init_tiles(PixieData pixdata[])
{
    for (int i = 0; i < PIX_MAX; i++)
        back[i] = std::make_unique<pixieN>(pixdata[i], 0);

    // Tiles with palette cycling must not use acceleration
    back[PIX_WATER1]->set_accel(0);
    back[PIX_WATER2]->set_accel(0);
    back[PIX_WATER3]->set_accel(0);
    back[PIX_WATERGRASS_LL]->set_accel(0);
    back[PIX_WATERGRASS_LR]->set_accel(0);
    back[PIX_WATERGRASS_UL]->set_accel(0);
    back[PIX_WATERGRASS_UR]->set_accel(0);
    back[PIX_WATERGRASS_U]->set_accel(0);
    back[PIX_WATERGRASS_D]->set_accel(0);
    back[PIX_WATERGRASS_L]->set_accel(0);
    back[PIX_WATERGRASS_R]->set_accel(0);
    back[PIX_GRASSWATER_LL]->set_accel(0);
    back[PIX_GRASSWATER_LR]->set_accel(0);
    back[PIX_GRASSWATER_UL]->set_accel(0);
    back[PIX_GRASSWATER_UR]->set_accel(0);
}

void SdlLevelRender::reset_tiles(PixieData pixdata[])
{
    for (int i = 0; i < PIX_MAX; i++)
        back[i].reset();
}

void SdlLevelRender::draw_tile(int tile_index, int x, int y, viewscreen* view)
{
    if (tile_index >= 0 && tile_index < PIX_MAX && back[tile_index])
        back[tile_index]->draw(static_cast<short>(x), static_cast<short>(y), view);
}

std::unique_ptr<ILevelRender> create_sdl_level_render(PixieData pixdata[])
{
    auto r = std::make_unique<SdlLevelRender>();
    r->init_tiles(pixdata);
    return r;
}
