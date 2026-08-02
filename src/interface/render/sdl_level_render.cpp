/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/interface/level_render.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/interface/render/pixie.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>

// LevelRender PIMPL implementation (backed by pixieN tile sprites)
struct LevelRender::Impl {
    std::unique_ptr<pixieN> back[PIX_MAX];
    // Decor cut-out sprites (index 0 transparent), decor id -> pixie; slot 0
    // (DECOR_NONE) stays empty. Never accelerated: the torch/brazier flame
    // pixels live in the cycled ORANGE band (do_cycle IS the flame
    // animation), which an accel surface snapshot would freeze.
    std::unique_ptr<pixie> decor[DECOR_MAX];
};

LevelRender::LevelRender() : impl_(std::make_unique<Impl>()) {}
LevelRender::~LevelRender() = default;

void LevelRender::init_tiles(PixieData pixdata[])
{
    // Initialize a pixie for each background piece
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

void LevelRender::draw_tile(int tile_index, int x, int y, viewscreen* view,
                            unsigned char alpha)
{
    if (tile_index >= 0 && tile_index < PIX_MAX && impl_->back[tile_index])
        impl_->back[tile_index]->draw(static_cast<short>(x), static_cast<short>(y), view,
                                      alpha);
}

void LevelRender::init_decor(PixieData decor_pixdata[])
{
    for (int i = 0; i < DECOR_MAX; i++)
        impl_->decor[i] = decor_pixdata[i].valid()
            ? std::make_unique<pixie>(decor_pixdata[i], 0)
            : nullptr;
}

void LevelRender::draw_decor(int decor_index, int x, int y, viewscreen* view,
                             unsigned char alpha)
{
    if (decor_index <= 0 || decor_index >= DECOR_MAX ||
        !impl_->decor[decor_index])
        return;
    pixie& p = *impl_->decor[decor_index];
    if (alpha >= 255)
    {
        // Transparent sprite blit (walkputbuffer): index-0 pixels leave the
        // base tile visible. The opaque tile path would paint them black.
        p.drawMix(static_cast<short>(x), static_cast<short>(y), view);
        return;
    }
    // Faded/ghosted floor: full-color alpha sprite blit, same clipping as
    // pixie::drawMix. No decor pixel reaches the >=248 team-recolor range
    // (generator self-check), so the teamcolor argument is inert.
    const Sint32 xscreen = static_cast<Sint32>(x - view->topx + view->xloc);
    const Sint32 yscreen = static_cast<Sint32>(y - view->topy + view->yloc);
    og::runtime::current_session->myscreen_->walkputbuffer_alpha(
        xscreen, yscreen, p.sizex, p.sizey,
        view->xloc, view->yloc, view->endx, view->endy,
        {p.bmp_data(), static_cast<size_t>(p.sizex * p.sizey)}, RED, alpha);
}

std::unique_ptr<LevelRender> create_sdl_level_render(PixieData pixdata[])
{
    auto r = std::make_unique<LevelRender>();
    r->init_tiles(pixdata);
    return r;
}
