/* Multiplayer Game Modes campaign generator — grid canvas.
 *
 * The BASE + DECOR painting helper the new-mode maps are drawn with,
 * carried over from tools/ctf_mapgen/grid_painters.cpp (which stays
 * untouched until the absorbed packages are deleted). Deterministic
 * texture pickers keep regenerated grids byte-identical.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/gameplay/pixie_data.h>

#include <vector>

namespace modesgen {

// A painted level: the BASE grid plus an optional DECOR plane (BASE +
// DECOR tile layering, .fss v11). `decor` is invalid when the painter
// placed no decor.
struct PaintedLevel
{
    PixieData base;
    PixieData decor;
};

class Canvas
{
public:
    Canvas(int w, int h);

    int w() const { return w_; }
    int h() const { return h_; }

    void set(int x, int y, unsigned char tile);
    void set_decor(int x, int y, unsigned char decor_id);
    void rect(int x0, int y0, int x1, int y1, unsigned char tile);
    void hline(int x0, int x1, int y, unsigned char tile);
    void vline(int x, int y0, int y1, unsigned char tile);

    // Deterministic texture pickers (ctf_mapgen hashes, byte-compatible).
    static unsigned char grass(int x, int y);
    static unsigned char dark_grass(int x, int y);
    static unsigned char water(int x, int y);
    static unsigned char cobble(int x, int y);
    static unsigned char boulder_decor(int x, int y);
    static unsigned char tree(int x, int y);

    void grass_rect(int x0, int y0, int x1, int y1);
    void dark_grass_rect(int x0, int y0, int x1, int y1);
    void cobble_rect(int x0, int y0, int x1, int y1);
    void water_rect(int x0, int y0, int x1, int y1);
    void cobble_disc(int cx2, int cy2, int r2);
    void carpet_rect(int x0, int y0, int x1, int y1);

    PaintedLevel finish() const;

private:
    int w_;
    int h_;
    std::vector<unsigned char> tiles_;
    std::vector<unsigned char> decor_;
};

} // namespace modesgen
