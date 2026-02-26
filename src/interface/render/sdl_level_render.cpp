/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/resources/level_render.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/render/view.h>

// LevelRender PIMPL implementation (backed by pixieN tile sprites)
struct LevelRender::Impl {
    std::unique_ptr<pixieN> back[PIX_MAX];
};

LevelRender::LevelRender() : impl_(std::make_unique<Impl>()) {}
LevelRender::~LevelRender() = default;

void LevelRender::init_tiles(PixieData pixdata[])
{
    for (int i = 0; i < PIX_MAX; i++)
        impl_->back[i] = std::make_unique<pixieN>(pixdata[i], 0);

    // Tiles with palette cycling must not use acceleration
    impl_->back[PIX_WATER1]->set_accel(0);
    impl_->back[PIX_WATER2]->set_accel(0);
    impl_->back[PIX_WATER3]->set_accel(0);
    impl_->back[PIX_WATERGRASS_LL]->set_accel(0);
    impl_->back[PIX_WATERGRASS_LR]->set_accel(0);
    impl_->back[PIX_WATERGRASS_UL]->set_accel(0);
    impl_->back[PIX_WATERGRASS_UR]->set_accel(0);
    impl_->back[PIX_WATERGRASS_U]->set_accel(0);
    impl_->back[PIX_WATERGRASS_D]->set_accel(0);
    impl_->back[PIX_WATERGRASS_L]->set_accel(0);
    impl_->back[PIX_WATERGRASS_R]->set_accel(0);
    impl_->back[PIX_GRASSWATER_LL]->set_accel(0);
    impl_->back[PIX_GRASSWATER_LR]->set_accel(0);
    impl_->back[PIX_GRASSWATER_UL]->set_accel(0);
    impl_->back[PIX_GRASSWATER_UR]->set_accel(0);
}

void LevelRender::reset_tiles(PixieData pixdata[])
{
    (void)pixdata;
    for (int i = 0; i < PIX_MAX; i++)
        impl_->back[i].reset();
}

void LevelRender::draw_tile(int tile_index, int x, int y, viewscreen* view)
{
    if (tile_index >= 0 && tile_index < PIX_MAX && impl_->back[tile_index])
        impl_->back[tile_index]->draw(static_cast<short>(x), static_cast<short>(y), view);
}

std::unique_ptr<LevelRender> create_sdl_level_render(PixieData pixdata[])
{
    auto r = std::make_unique<LevelRender>();
    r->init_tiles(pixdata);
    return r;
}
