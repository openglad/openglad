/* CTF campaign generator — original map grid painters.
 *
 * Four hand-designed arenas painted programmatically with the engine's
 * tile vocabulary (include/openglad/core/pixdefs.h). Layouts follow the
 * design briefs: FIRST BLOOD (tight 2-team duel), RIVER RUN (2 teams
 * across a bridged river), TRIAD (3-team radial), CROSSFIRE (4-team
 * pinwheel with exact 90-degree rotational symmetry).
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "ctf_mapgen.h"

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ctfgen {

namespace {

class Canvas
{
public:
    Canvas(int w, int h)
        : w_(w), h_(h), tiles_(static_cast<size_t>(w) * h),
          decor_(static_cast<size_t>(w) * h, DECOR_NONE)
    {
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x)
                set(x, y, grass(x, y));
    }

    int w() const { return w_; }
    int h() const { return h_; }

    void set(int x, int y, unsigned char tile)
    {
        if (x >= 0 && x < w_ && y >= 0 && y < h_)
            tiles_[static_cast<size_t>(y) * w_ + x] = tile;
    }

    // DECOR plane authoring (core/decordefs.h): the object sits ON the base
    // tile, which keeps whatever ground the canvas already painted there.
    void set_decor(int x, int y, unsigned char decor_id)
    {
        if (x >= 0 && x < w_ && y >= 0 && y < h_)
            decor_[static_cast<size_t>(y) * w_ + x] = decor_id;
    }

    void rect(int x0, int y0, int x1, int y1, unsigned char tile)
    {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                set(x, y, tile);
    }

    void hline(int x0, int x1, int y, unsigned char tile)
    {
        rect(x0, y, x1, y, tile);
    }

    void vline(int x, int y0, int y1, unsigned char tile)
    {
        rect(x, y0, x, y1, tile);
    }

    // Deterministic texture pickers.
    static unsigned char grass(int x, int y)
    {
        switch ((x * 7 + y * 13) % 11)
        {
            case 0: return PIX_GRASS2;
            case 1: return PIX_GRASS3;
            case 2: return PIX_GRASS4;
            default: return PIX_GRASS1;
        }
    }

    static unsigned char water(int x, int y)
    {
        switch ((x * 3 + y * 5) % 7)
        {
            case 0: return PIX_WATER2;
            case 1: return PIX_WATER3;
            default: return PIX_WATER1;
        }
    }

    static unsigned char cobble(int x, int y)
    {
        switch ((x + y * 3) % 4)
        {
            case 0: return PIX_COBBLE_1;
            case 1: return PIX_COBBLE_2;
            case 2: return PIX_COBBLE_3;
            default: return PIX_COBBLE_4;
        }
    }

    // Boulder cover is DECOR over the canvas grass (same variant hash the
    // legacy combined-tile picker used, so regenerated maps keep the exact
    // rock pattern).
    static unsigned char boulder_decor(int x, int y)
    {
        switch ((x * 5 + y) % 4)
        {
            case 0: return DECOR_BOULDER_1;
            case 1: return DECOR_BOULDER_2;
            case 2: return DECOR_BOULDER_3;
            default: return DECOR_BOULDER_4;
        }
    }

    static unsigned char tree(int x, int y)
    {
        switch ((x + y * 7) % 4)
        {
            case 0: return PIX_TREE_M1;
            case 1: return PIX_TREE_ML;
            case 2: return PIX_TREE_MR;
            default: return PIX_TREE_T1;
        }
    }

    void cobble_rect(int x0, int y0, int x1, int y1)
    {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                set(x, y, cobble(x, y));
    }

    void water_rect(int x0, int y0, int x1, int y1)
    {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                set(x, y, water(x, y));
    }

    void carpet_pad(int cx, int cy)
    {
        rect(cx - 1, cy - 1, cx + 1, cy + 1, PIX_CARPET_M);
    }

    PaintedLevel finish() const
    {
        PaintedLevel out;
        out.base.frames = 1;
        out.base.w = static_cast<unsigned char>(w_);
        out.base.h = static_cast<unsigned char>(h_);
        out.base.data = std::make_unique<unsigned char[]>(tiles_.size());
        std::memcpy(out.base.data.get(), tiles_.data(), tiles_.size());
        // Emit the decor plane only when something was placed: an all-zero
        // plane is "no decor" to the .fss v11 writer, and an invalid plane
        // keeps decor-free maps byte-identical to their pre-decor form.
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

private:
    int w_;
    int h_;
    std::vector<unsigned char> tiles_;
    std::vector<unsigned char> decor_;
};

} // namespace

// ---------------------------------------------------------------------------
// 500 FIRST BLOOD — 40x30, 2 teams, E/W mirror. Open halves split by a
// central wall with three gaps; the middle gap opens into a cobble plaza
// holding the control point. Flag pads ringed by trees for cover.
// ---------------------------------------------------------------------------
PaintedLevel paint_first_blood()
{
    Canvas c(40, 30);

    // Central wall with north gap (y 4-6), plaza opening (y 12-18),
    // and south gap (y 23-25).
    for (int y = 0; y < 30; ++y)
    {
        const bool gap = (y >= 4 && y <= 6) || (y >= 12 && y <= 18) ||
                         (y >= 23 && y <= 25);
        if (!gap)
            c.set(20, y, PIX_H_WALL1);
    }

    // Central plaza around the wall opening.
    c.cobble_rect(17, 12, 23, 18);

    // Flag pads with tree cover, mirrored E/W (mirror x = 39 - x).
    for (int side = 0; side < 2; ++side)
    {
        auto mx = [side](int x) { return side ? 39 - x : x; };
        c.carpet_pad(mx(5), 15);
        c.set(mx(3), 13, Canvas::tree(mx(3), 13));
        c.set(mx(7), 13, Canvas::tree(mx(7), 13));
        c.set(mx(3), 17, Canvas::tree(mx(3), 17));
        c.set(mx(7), 17, Canvas::tree(mx(7), 17));
        // Mid-field boulder cover on each lane.
        c.set_decor(mx(12), 8, Canvas::boulder_decor(mx(12), 8));
        c.set_decor(mx(13), 8, Canvas::boulder_decor(mx(13), 8));
        c.set_decor(mx(12), 21, Canvas::boulder_decor(mx(12), 21));
        c.set_decor(mx(13), 21, Canvas::boulder_decor(mx(13), 21));
    }

    return c.finish();
}

// ---------------------------------------------------------------------------
// 504 RIVER RUN — 60x40, 2 teams, N/S mirror. A four-tile river crossed by
// two plank bridges and a wide stone bridge over a cobble island (CP).
// C-shaped boulder windbreaks shelter the flag pads; trees line the banks.
// ---------------------------------------------------------------------------
PaintedLevel paint_river_run()
{
    Canvas c(60, 40);

    // The river (rows 18-21) and the stone island (slightly taller).
    c.water_rect(0, 18, 59, 21);
    c.cobble_rect(26, 17, 33, 22);                  // center island
    for (int y = 18; y <= 21; ++y)
    {
        c.set(9, y, PIX_PATH_1);                    // west plank bridge
        c.set(10, y, PIX_PATH_2);
        c.set(49, y, PIX_PATH_1);                   // east plank bridge
        c.set(50, y, PIX_PATH_2);
    }

    for (int side = 0; side < 2; ++side)
    {
        auto my = [side](int y) { return side ? 39 - y : y; };
        // Flag pad inside a C-shaped windbreak that opens toward the river.
        c.carpet_pad(29, my(6));
        for (int x = 26; x <= 32; ++x)
            c.set_decor(x, my(3), Canvas::boulder_decor(x, my(3)));
        for (int y = 4; y <= 7; ++y)
        {
            c.set_decor(26, my(y), Canvas::boulder_decor(26, my(y)));
            c.set_decor(32, my(y), Canvas::boulder_decor(32, my(y)));
        }
        // Bank tree clusters for ambushes (kept off the bridges).
        for (int x = 2; x < 58; x += 5)
        {
            if ((x >= 7 && x <= 12) || (x >= 24 && x <= 35) ||
                (x >= 47 && x <= 52))
            {
                continue;
            }
            c.set(x, my(15), Canvas::tree(x, my(15)));
            c.set(x + 1, my(16), Canvas::tree(x + 1, my(16)));
        }
    }

    return c.finish();
}

// ---------------------------------------------------------------------------
// 505 TRIAD — 51x51, 3 teams placed 120 degrees apart around the center.
// Walled base pockets with two entrances (one facing the center, one a rear
// sally door on the rim), spoke lanes to a columned central plaza, and
// forest belts with drumstick orchards on the direct base-to-base paths.
// ---------------------------------------------------------------------------
PaintedLevel paint_triad()
{
    Canvas c(51, 51);
    const int cx = 25, cy = 25;
    const int bases[3][2] = {{25, 7}, {41, 34}, {9, 34}};

    // Forest fringe along the map border.
    for (int i = 0; i < 51; ++i)
    {
        c.set(i, 0, Canvas::tree(i, 0));
        c.set(i, 50, Canvas::tree(i, 50));
        c.set(0, i, Canvas::tree(0, i));
        c.set(50, i, Canvas::tree(50, i));
    }

    // Central plaza: cobble disc with a column ring.
    for (int y = cy - 5; y <= cy + 5; ++y)
        for (int x = cx - 5; x <= cx + 5; ++x)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= 25)
                c.set(x, y, Canvas::cobble(x, y));
    const int cols[6][2] = {{31, 25}, {28, 30}, {22, 30},
                            {19, 25}, {22, 20}, {28, 20}};
    for (auto& p : cols)
        c.set(p[0], p[1], ((p[0] + p[1]) % 2) ? PIX_COLUMN1 : PIX_COLUMN2);

    // Forest belts between neighboring bases with a dirt path through and
    // small clearings at the midpoints (the drumstick orchards).
    for (int i = 0; i < 3; ++i)
    {
        const int j = (i + 1) % 3;
        const int steps = 24;
        for (int s = 0; s <= steps; ++s)
        {
            const int x = bases[i][0] + (bases[j][0] - bases[i][0]) * s / steps;
            const int y = bases[i][1] + (bases[j][1] - bases[i][1]) * s / steps;
            // Trees flank the connecting line; the line itself stays dirt.
            c.set(x, y, PIX_DIRT_1);
            if (s % 3 == 1 && s > 4 && s < steps - 4)
            {
                c.set(x + 2, y + 1, Canvas::tree(x + 2, y + 1));
                c.set(x - 2, y - 1, Canvas::tree(x - 2, y - 1));
            }
        }
    }

    // Base pockets: 9x9 wall ring, gap toward the center plus a rear sally
    // door opposite it, carpet flag pad inside.
    for (auto& b : bases)
    {
        const int bx = b[0], by = b[1];
        for (int x = bx - 4; x <= bx + 4; ++x)
        {
            c.set(x, by - 4, PIX_H_WALL1);
            c.set(x, by + 4, PIX_H_WALL1);
        }
        for (int y = by - 4; y <= by + 4; ++y)
        {
            c.set(bx - 4, y, PIX_H_WALL1);
            c.set(bx + 4, y, PIX_H_WALL1);
        }

        // Gap on the side facing the map center.
        const int dx = cx - bx, dy = cy - by;
        int gap_side; // 0=N 1=E 2=S 3=W
        if (std::abs(dx) >= std::abs(dy))
            gap_side = (dx > 0) ? 1 : 3;
        else
            gap_side = (dy > 0) ? 2 : 0;
        // Rear sally door: opposite the plaza gap, identical for every
        // pocket by construction (the 120-degree layout cannot give all
        // three pockets a same-handed neighbor door on a square grid).
        const int second = (gap_side + 2) % 4;
        for (int side : {gap_side, second})
        {
            for (int k = -1; k <= 1; ++k)
            {
                switch (side)
                {
                    case 0: c.set(bx + k, by - 4, Canvas::grass(bx + k, by - 4)); break;
                    case 1: c.set(bx + 4, by + k, Canvas::grass(bx + 4, by + k)); break;
                    case 2: c.set(bx + k, by + 4, Canvas::grass(bx + k, by + 4)); break;
                    default: c.set(bx - 4, by + k, Canvas::grass(bx - 4, by + k)); break;
                }
            }
        }
        c.carpet_pad(bx, by);

        // Spoke lane from the pocket toward the plaza.
        const int steps = 20;
        for (int s = 5; s <= steps - 6; ++s)
        {
            const int x = bx + (cx - bx) * s / steps;
            const int y = by + (cy - by) * s / steps;
            c.set(x, y, PIX_PATH_1);
            c.set(x + 1, y, PIX_PATH_2);
        }
    }

    return c.finish();
}

// ---------------------------------------------------------------------------
// 509 CROSSFIRE — 60x60, 4 teams, exact 90-degree (pinwheel) symmetry:
// every feature painted for the NW quadrant is replicated with
// (x, y) -> (59 - y, x). Corner bases behind L-walls that open onto a fast
// clockwise boulevard, narrow counter-clockwise boulder alleys, and a big
// columned center plaza around the control point.
// ---------------------------------------------------------------------------
PaintedLevel paint_crossfire()
{
    Canvas c(60, 60);

    struct Feature
    {
        int x, y;
        unsigned char tile;
        bool textured; // re-pick texture per rotated position
    };
    std::vector<Feature> f;
    auto put = [&f](int x, int y, unsigned char tile, bool textured = false) {
        f.push_back({x, y, tile, textured});
    };

    // --- NW quadrant features ---
    // Base pad at (10,10).
    for (int y = 9; y <= 11; ++y)
        for (int x = 9; x <= 11; ++x)
            put(x, y, PIX_CARPET_M);
    // L-wall shielding the base toward the center, openings to the north
    // boulevard (clockwise) and the west alley (counter-clockwise).
    for (int y = 5; y <= 15; ++y)
        if (y < 6 || y > 8)
            put(15, y, PIX_H_WALL1);
    for (int x = 5; x <= 14; ++x)
        if (x < 6 || x > 8)
            put(x, 15, PIX_H_WALL1);
    // Pinwheel arm: boulder bar making the CCW alley narrow.
    for (int y = 19; y <= 27; ++y)
        put(19, y, 0, true); // boulders (textured)
    // Cover dots on the boulevard.
    put(30, 8, 0, true);
    put(31, 8, 0, true);
    put(44, 17, 0, true);

    // Apply with 4 rotations: (x, y) -> (59 - y, x). Textured features are
    // the boulders — now DECOR over the canvas grass, re-picked per rotated
    // position exactly as the legacy base-tile texture was.
    for (const auto& feat : f)
    {
        int x = feat.x, y = feat.y;
        for (int r = 0; r < 4; ++r)
        {
            if (feat.textured)
                c.set_decor(x, y, Canvas::boulder_decor(x, y));
            else
                c.set(x, y, feat.tile);
            const int nx = 59 - y;
            const int ny = x;
            x = nx;
            y = ny;
        }
    }

    // Center plaza (rot4-symmetric by construction).
    c.cobble_rect(25, 25, 34, 34);
    c.set(25, 25, PIX_COLUMN1);
    c.set(34, 25, PIX_COLUMN2);
    c.set(34, 34, PIX_COLUMN1);
    c.set(25, 34, PIX_COLUMN2);

    return c.finish();
}

// ---------------------------------------------------------------------------
// Original-map specs: dressing, spice, and intro text.
// ---------------------------------------------------------------------------
std::vector<OriginalSpec> original_specs()
{
    std::vector<OriginalSpec> out;

    {
        OriginalSpec s;
        s.out_id = 500;
        s.title = "CTF: FIRST BLOOD";
        s.paint = paint_first_blood;
        s.par_value = 4;
        s.dress.team_count = 2;
        s.dress.base = {TilePos{5, 15}, TilePos{34, 15}, TilePos{}, TilePos{}};
        s.dress.cps = {TilePos{20, 15}};
        s.dress.anchors_per_team = 12;
        s.dress.spice = {
            {FAMILY_SPEED_POTION, 0, {20, 5}, 1},
            {FAMILY_SPEED_POTION, 0, {20, 24}, 1},
            {FAMILY_INVIS_POTION, 0, {20, 16}, 1},
            {FAMILY_DRUMSTICK, 0, {2, 13}, 1},
            {FAMILY_DRUMSTICK, 0, {2, 17}, 1},
            {FAMILY_DRUMSTICK, 0, {37, 13}, 1},
            {FAMILY_DRUMSTICK, 0, {37, 17}, 1},
        };
        s.description = {
            "CAPTURE THE FLAG: A TIGHT",
            "DUELING GROUND. STEAL THE ENEMY",
            "BANNER AND CARRY IT BACK TO",
            "YOUR OWN. THREE GAPS CROSS THE",
            "WALL: FAST LANES NORTH AND",
            "SOUTH, A PLAZA IN THE MIDDLE.",
            "HOLD THE PLAZA WAYPOINT FOR",
            "FASTER REINFORCEMENTS.",
        };
        out.push_back(std::move(s));
    }

    {
        OriginalSpec s;
        s.out_id = 504;
        s.title = "CTF: RIVER RUN";
        s.paint = paint_river_run;
        s.par_value = 5;
        s.dress.team_count = 2;
        s.dress.base = {TilePos{29, 6}, TilePos{29, 33}, TilePos{}, TilePos{}};
        s.dress.cps = {TilePos{29, 19}};
        s.dress.anchors_per_team = 12;
        s.dress.spice = {
            {FAMILY_SPEED_POTION, 0, {9, 19}, 1},
            {FAMILY_SPEED_POTION, 0, {50, 20}, 1},
            {FAMILY_INVIS_POTION, 0, {31, 20}, 1},
            {FAMILY_DRUMSTICK, 0, {26, 2}, 1},
            {FAMILY_DRUMSTICK, 0, {32, 2}, 1},
            {FAMILY_DRUMSTICK, 0, {26, 37}, 1},
            {FAMILY_DRUMSTICK, 0, {32, 37}, 1},
        };
        s.description = {
            "CAPTURE THE FLAG ACROSS THE",
            "RIVER. TWO PLANK BRIDGES FLANK",
            "A WIDE STONE CROSSING; THE",
            "ISLAND WAYPOINT SITS IN ITS",
            "MIDDLE. A CARRIER WHO FALLS",
            "OVER WATER SENDS THE FLAG",
            "STRAIGHT HOME, SO FLIERS GAMBLE",
            "WHILE BRIDGES ARE SAFE.",
        };
        out.push_back(std::move(s));
    }

    {
        OriginalSpec s;
        s.out_id = 505;
        s.title = "CTF: TRIAD";
        s.paint = paint_triad;
        s.par_value = 6;
        s.dress.team_count = 3;
        s.dress.base = {TilePos{25, 7}, TilePos{41, 34}, TilePos{9, 34},
                        TilePos{}};
        s.dress.cps = {TilePos{25, 25}};
        s.dress.anchors_per_team = 12;
        s.dress.anchor_shift = 0; // walled pockets: keep anchors inside
        s.dress.spice = {
            {FAMILY_INVIS_POTION, 0, {25, 26}, 1},
            {FAMILY_DRUMSTICK, 0, {33, 20}, 1},
            {FAMILY_DRUMSTICK, 0, {25, 34}, 1},
            {FAMILY_DRUMSTICK, 0, {17, 20}, 1},
            {FAMILY_SPEED_POTION, 0, {25, 16}, 1},
            {FAMILY_SPEED_POTION, 0, {33, 30}, 1},
            {FAMILY_SPEED_POTION, 0, {17, 30}, 1},
        };
        s.description = {
            "THREE-WAY CAPTURE THE FLAG.",
            "EVERY POCKET HAS TWO DOORS:",
            "ONE FACES THE PLAZA, ONE IS A",
            "REAR SALLY DOOR. ROUND THE RIM",
            "TO STRIKE FROM BEHIND - BUT",
            "STEALING FROM ONE RIVAL BARES",
            "YOUR BACK TO THE OTHER. THE",
            "PLAZA WAYPOINT BREAKS",
            "STALEMATES.",
        };
        out.push_back(std::move(s));
    }

    {
        OriginalSpec s;
        s.out_id = 509;
        s.title = "CTF: CROSSFIRE";
        s.paint = paint_crossfire;
        s.par_value = 6;
        s.dress.team_count = 4;
        s.dress.flag_level = 5; // the finale plays to five captures
        s.dress.base = {TilePos{10, 10}, TilePos{49, 10}, TilePos{49, 49},
                        TilePos{10, 49}};
        s.dress.cps = {TilePos{29, 29}};
        s.dress.anchors_per_team = 12;
        s.dress.anchor_shift = 0; // walled pockets: keep anchors inside
        s.dress.spice = {
            {FAMILY_INVIS_POTION, 0, {30, 30}, 1},
            {FAMILY_SPEED_POTION, 0, {29, 22}, 1},
            {FAMILY_SPEED_POTION, 0, {37, 29}, 1},
            {FAMILY_SPEED_POTION, 0, {30, 37}, 1},
            {FAMILY_SPEED_POTION, 0, {22, 30}, 1},
            {FAMILY_DRUMSTICK, 0, {10, 29}, 1},
            {FAMILY_DRUMSTICK, 0, {29, 10}, 1},
            {FAMILY_DRUMSTICK, 0, {49, 30}, 1},
            {FAMILY_DRUMSTICK, 0, {30, 49}, 1},
        };
        s.description = {
            "FOUR-TEAM CAPTURE THE FLAG",
            "PINWHEEL. THE CLOCKWISE",
            "BOULEVARD IS FAST AND OPEN; THE",
            "COUNTER-CLOCKWISE ALLEYS HIDE",
            "BEHIND BOULDERS. RAID FAST,",
            "SNEAK HOME. THE PLAZA WAYPOINT",
            "SPEEDS YOUR RESPAWNS - IF YOU",
            "CAN KEEP IT. FIRST TO FIVE",
            "CAPTURES TAKES THE FIELD.",
        };
        out.push_back(std::move(s));
    }

    return out;
}

} // namespace ctfgen
