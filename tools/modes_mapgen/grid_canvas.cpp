/* Multiplayer Game Modes campaign generator — grid canvas.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "grid_canvas.h"

#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>

#include <cstring>

namespace modesgen {

Canvas::Canvas(int w, int h)
    : w_(w), h_(h), tiles_(static_cast<size_t>(w) * h),
      decor_(static_cast<size_t>(w) * h, DECOR_NONE)
{
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x)
            set(x, y, grass(x, y));
}

void Canvas::set(int x, int y, unsigned char tile)
{
    if (x >= 0 && x < w_ && y >= 0 && y < h_)
        tiles_[static_cast<size_t>(y) * w_ + x] = tile;
}

void Canvas::set_decor(int x, int y, unsigned char decor_id)
{
    if (x >= 0 && x < w_ && y >= 0 && y < h_)
        decor_[static_cast<size_t>(y) * w_ + x] = decor_id;
}

void Canvas::rect(int x0, int y0, int x1, int y1, unsigned char tile)
{
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            set(x, y, tile);
}

void Canvas::hline(int x0, int x1, int y, unsigned char tile)
{
    rect(x0, y, x1, y, tile);
}

void Canvas::vline(int x, int y0, int y1, unsigned char tile)
{
    rect(x, y0, x, y1, tile);
}

unsigned char Canvas::grass(int x, int y)
{
    switch ((x * 7 + y * 13) % 11)
    {
        case 0: return PIX_GRASS2;
        case 1: return PIX_GRASS3;
        case 2: return PIX_GRASS4;
        default: return PIX_GRASS1;
    }
}

unsigned char Canvas::dark_grass(int x, int y)
{
    switch ((x * 7 + y * 13) % 11)
    {
        case 0: return PIX_GRASS_DARK_2;
        case 1: return PIX_GRASS_DARK_3;
        case 2: return PIX_GRASS_DARK_4;
        default: return PIX_GRASS_DARK_1;
    }
}

unsigned char Canvas::water(int x, int y)
{
    switch ((x * 3 + y * 5) % 7)
    {
        case 0: return PIX_WATER2;
        case 1: return PIX_WATER3;
        default: return PIX_WATER1;
    }
}

unsigned char Canvas::cobble(int x, int y)
{
    switch ((x + y * 3) % 4)
    {
        case 0: return PIX_COBBLE_1;
        case 1: return PIX_COBBLE_2;
        case 2: return PIX_COBBLE_3;
        default: return PIX_COBBLE_4;
    }
}

unsigned char Canvas::boulder_decor(int x, int y)
{
    switch ((x * 5 + y) % 4)
    {
        case 0: return DECOR_BOULDER_1;
        case 1: return DECOR_BOULDER_2;
        case 2: return DECOR_BOULDER_3;
        default: return DECOR_BOULDER_4;
    }
}

unsigned char Canvas::tree(int x, int y)
{
    switch ((x + y * 7) % 4)
    {
        case 0: return PIX_TREE_M1;
        case 1: return PIX_TREE_ML;
        case 2: return PIX_TREE_MR;
        default: return PIX_TREE_T1;
    }
}

void Canvas::grass_rect(int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            set(x, y, grass(x, y));
}

void Canvas::dark_grass_rect(int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            set(x, y, dark_grass(x, y));
}

void Canvas::cobble_rect(int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            set(x, y, cobble(x, y));
}

void Canvas::water_rect(int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            set(x, y, water(x, y));
}

// Cobble disc around a possibly half-tile center: coordinates and radius
// arrive DOUBLED so (29, 27, 7) means center (14.5, 13.5), radius 3.5.
void Canvas::cobble_disc(int cx2, int cy2, int r2)
{
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x)
        {
            const int dx = 2 * x - cx2;
            const int dy = 2 * y - cy2;
            if (dx * dx + dy * dy <= r2 * r2)
                set(x, y, cobble(x, y));
        }
}

void Canvas::carpet_rect(int x0, int y0, int x1, int y1)
{
    rect(x0, y0, x1, y1, PIX_CARPET_M);
}

PaintedLevel Canvas::finish() const
{
    PaintedLevel out;
    out.base.frames = 1;
    out.base.w = static_cast<unsigned char>(w_);
    out.base.h = static_cast<unsigned char>(h_);
    out.base.data = std::make_unique<unsigned char[]>(tiles_.size());
    std::memcpy(out.base.data.get(), tiles_.data(), tiles_.size());
    bool any_decor = false;
    for (const unsigned char d : decor_)
        any_decor = any_decor || d != DECOR_NONE;
    if (any_decor)
    {
        out.decor.frames = 1;
        out.decor.w = static_cast<unsigned char>(w_);
        out.decor.h = static_cast<unsigned char>(h_);
        out.decor.data = std::make_unique<unsigned char[]>(decor_.size());
        std::memcpy(out.decor.data.get(), decor_.data(), decor_.size());
    }
    return out;
}

} // namespace modesgen
