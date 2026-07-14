/* Deterministic level audits (og::mapgen) — see builders.h.
 *
 * COPIED from tools/westlands_mapgen/main.cpp self_check_level
 * (stair alignment :1119-1134, stair clearance :1146-1216, fall lines
 * :1218-1261, footing :1263-1316, A*-reachability :887-894 + :1318-1398)
 * with the WP-4 adaptations: each audit is a standalone function over a
 * GameWorld returning failure strings (no fail() global / error counter),
 * the fall-line audit additionally enforces a maximum fall DEPTH (tower
 * recipe: <= 4 stories), the reachability audit also proves every EXIT
 * reachable and removes its probe again (the tool leaked it into a
 * discarded scratch level; a library must leave the entity lists exactly
 * as found), and there is no per-level exception allowlist (generated
 * content rerolls instead of excusing).
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/mapgen/builders.h>

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace og::mapgen {

namespace {

// Multi-floor A* path-state encoding (walker_pathing.cpp's MAKE_STATE).
PathState make_path_state(int x, int y, int floor)
{
    return reinterpret_cast<PathState>(
        static_cast<intptr_t>(floor) * FLOOR_STRIDE +
        static_cast<intptr_t>(y / GRID_SIZE) * MAP_WIDTH + (x / GRID_SIZE));
}

} // namespace

// Footing audit: every authored entity stands on a tile of its own floor
// that its own footprint can occupy, and no ground troop spawns over air or
// on BlocksGround decor.
std::vector<std::string> audit_footing(GameWorld& world)
{
    std::vector<std::string> errors;
    auto check_footing = [&](walker* ob)
    {
        if (ob == nullptr)
            return;
        if (!world.query_grid_passable(static_cast<float>(ob->xpos()),
                                       static_cast<float>(ob->ypos()), ob,
                                       ob->floor()))
        {
            errors.push_back(std::format(
                "footing: order {} family {} at tile ({}, {}) floor {} "
                "stands on impassable ground",
                static_cast<int>(ob->query_order()),
                static_cast<int>(ob->family()),
                ob->xpos() / GRID_SIZE, ob->ypos() / GRID_SIZE, ob->floor()));
        }
        if (ob->query_order() == Order::Living &&
            !ob->stats()->query_bit_flags(BIT_FLYING))
        {
            const PixieData& g = world.grid_for_floor(ob->floor());
            const int tx = (ob->xpos() + ob->sizex() / 2) / GRID_SIZE;
            const int ty = (ob->ypos() + ob->sizey() / 2) / GRID_SIZE;
            if (tx >= 0 && ty >= 0 && tx < g.w && ty < g.h &&
                g.data[tx + ty * g.w] == PIX_AIR)
            {
                errors.push_back(std::format(
                    "footing: ground unit family {} at tile ({}, {}) floor "
                    "{} spawns over air",
                    static_cast<int>(ob->family()), tx, ty, ob->floor()));
            }
            // No ground footprint on BlocksGround decor: the scatters keep
            // entity clearance and hand-placed decor must not pin troops.
            const PixieData& dec = world.decor_for_floor(ob->floor());
            if (dec.valid() && tx >= 0 && ty >= 0 && tx < dec.w && ty < dec.h)
            {
                const unsigned char d = dec.data[tx + ty * dec.w];
                if (d < DECOR_MAX &&
                    kDecorRegistry[d].pass == DecorPassability::BlocksGround)
                {
                    errors.push_back(std::format(
                        "footing: ground unit family {} at tile ({}, {}) "
                        "floor {} spawns on blocking decor {}",
                        static_cast<int>(ob->family()), tx, ty, ob->floor(),
                        d));
                }
            }
        }
    };
    for (const auto& uptr : world.oblist)
        check_footing(uptr.get());
    for (const auto& uptr : world.fxlist)
        check_footing(uptr.get());
    return errors;
}

// Stair audit: each floor boundary reachable through at least one
// vertically-aligned UP/DOWN pair, and no stair sealed by an immobile post.
std::vector<std::string> audit_stairs(GameWorld& world,
                                      bool require_every_boundary)
{
    std::vector<std::string> errors;

    if (require_every_boundary)
    {
        for (int f = 0; f + 1 < world.floor_count(); ++f)
        {
            const PixieData& lo = world.grid_for_floor(f);
            const PixieData& hi = world.grid_for_floor(f + 1);
            if (!lo.valid() || !hi.valid())
            {
                errors.push_back(std::format(
                    "stairs: invalid grid on floor boundary {}<->{}", f,
                    f + 1));
                continue;
            }
            int pairs = 0;
            const int cells = lo.w * lo.h;
            for (int i = 0; i < cells; ++i)
                if (lo.data[i] == PIX_ZSTAIR_UP &&
                    hi.data[i] == PIX_ZSTAIR_DOWN)
                    ++pairs;
            if (pairs < 1)
                errors.push_back(std::format(
                    "stairs: no aligned stair pair on floor boundary {}<->{}",
                    f, f + 1));
        }
    }

    // Stair-clearance audit (B2 tooling rule, docs/z-axis-design.md): the
    // engine's blocked-arrival nudge rescues a blocked stair at runtime, but
    // no shipped level may RELY on it — and before the nudge an IMMOBILE
    // blocker (an ACT_GUARD post, a generator, or ground-blocking decor)
    // sitting on a stair cell seals the staircase outright. Rule: neither
    // the stair-pair cell nor any of its 4-neighborhood arrival cells, on
    // EITHER floor of the pair, may hold an ACT_GUARD post, a generator, or
    // BlocksGround decor. Roaming livings are fine (they move off; the
    // nudge covers the transient).
    static constexpr int kArrivalOffsets[5][2] = {
        {0, 0}, {0, -1}, {-1, 0}, {1, 0}, {0, 1}};
    auto audit_arrival_cell = [&](int pf, int nx, int ny)
    {
        const PixieData& g = world.grid_for_floor(pf);
        if (!g.valid() || nx < 0 || ny < 0 || nx >= g.w || ny >= g.h)
            return;
        const PixieData& dec = world.decor_for_floor(pf);
        if (dec.valid() && nx < dec.w && ny < dec.h)
        {
            const unsigned char d = dec.data[nx + ny * dec.w];
            if (d < DECOR_MAX &&
                kDecorRegistry[d].pass == DecorPassability::BlocksGround)
            {
                errors.push_back(std::format(
                    "stairs: blocking decor {} on stair cell/arrival "
                    "({}, {}) floor {}", d, nx, ny, pf));
            }
        }
        const int x0 = nx * GRID_SIZE;
        const int y0 = ny * GRID_SIZE;
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr || ob->floor() != pf)
                continue;
            const Order order = ob->query_order();
            const bool immobile_post =
                order == Order::Generator ||
                (order == Order::Living && ob->act_type() == ACT_GUARD);
            if (!immobile_post)
                continue;
            if (ob->xpos() + ob->sizex() > x0 &&
                ob->xpos() < x0 + GRID_SIZE &&
                ob->ypos() + ob->sizey() > y0 &&
                ob->ypos() < y0 + GRID_SIZE)
            {
                errors.push_back(std::format(
                    "stairs: {} (family {}) posted on stair cell/arrival "
                    "({}, {}) floor {} would seal the staircase",
                    order == Order::Generator ? "generator"
                                              : "ACT_GUARD post",
                    static_cast<int>(ob->family()), nx, ny, pf));
            }
        }
    };
    for (int f = 0; f + 1 < world.floor_count(); ++f)
    {
        const PixieData& lo = world.grid_for_floor(f);
        const PixieData& hi = world.grid_for_floor(f + 1);
        if (!lo.valid() || !hi.valid())
            continue;
        for (int ty = 0; ty < lo.h; ++ty)
            for (int tx = 0; tx < lo.w; ++tx)
            {
                const int i = tx + ty * lo.w;
                if (lo.data[i] != PIX_ZSTAIR_UP ||
                    hi.data[i] != PIX_ZSTAIR_DOWN)
                    continue;
                for (const auto& off : kArrivalOffsets)
                {
                    audit_arrival_cell(f, tx + off[0], ty + off[1]);
                    audit_arrival_cell(f + 1, tx + off[0], ty + off[1]);
                }
            }
    }
    return errors;
}

// Fall-line audit: any AIR cell a ground walker can actually step into —
// 8-adjacent to a standable cell of the SAME floor — must land its faller
// cleanly. Chase the column down through stacked AIR; the landing cell must
// be standable (base AND decor): never a wall top, water, lava or blocking
// decor — and the drop must not exceed max_fall_depth stories. Falling past
// floor 0 is a pit death, a designed mechanic, and stays legal. The
// engine's A5 nudge can rescue a blocked landing, but no level may RELY on
// the nudge.
std::vector<std::string> audit_fall_lines(GameWorld& world,
                                          int max_fall_depth)
{
    std::vector<std::string> errors;
    for (int f = 1; f < world.floor_count(); ++f)
    {
        const PixieData& g = world.grid_for_floor(f);
        if (!g.valid())
            continue;
        for (int ty = 0; ty < g.h; ++ty)
        {
            for (int tx = 0; tx < g.w; ++tx)
            {
                if (g.data[tx + ty * g.w] != PIX_AIR)
                    continue;
                bool fall_entry = false;
                for (int dy = -1; dy <= 1 && !fall_entry; ++dy)
                    for (int dx = -1; dx <= 1 && !fall_entry; ++dx)
                        if ((dx != 0 || dy != 0) &&
                            cell_standable(world, f, tx + dx, ty + dy))
                            fall_entry = true;
                if (!fall_entry)
                    continue; // open sky no walker can step into
                int lf = f - 1;
                while (lf > 0 &&
                       world.grid_for_floor(lf).data[tx + ty * g.w] == PIX_AIR)
                    --lf;
                if (world.grid_for_floor(lf).data[tx + ty * g.w] == PIX_AIR)
                    continue; // fell past floor 0: pit death by design
                if (!cell_standable(world, lf, tx, ty))
                {
                    errors.push_back(std::format(
                        "fall line at tile ({}, {}) floor {} lands on "
                        "impassable ground of floor {} (the level must not "
                        "rely on the engine's landing nudge)",
                        tx, ty, f, lf));
                }
                else if (f - lf > max_fall_depth)
                {
                    errors.push_back(std::format(
                        "fall line at tile ({}, {}) floor {} drops {} "
                        "stories to floor {} (max fall depth {})",
                        tx, ty, f, f - lf, lf, max_fall_depth));
                }
            }
        }
    }
    return errors;
}

// Reachability audit: every non-flying living, every generator and every
// exit must be A*-reachable from the crew's lead start marker by a ground
// probe, respecting passability, air holes, lava and Z-stairs. Flyers are
// exempt (ghosts hover over lava, meres and air pits by design).
std::vector<std::string> audit_reachability(GameWorld& world)
{
    std::vector<std::string> errors;

    walker* lead = nullptr;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Special &&
            ob->family() == FAMILY_RESERVED_TEAM && ob->team_num() == 0)
        {
            lead = ob;
            break;
        }
    }
    if (lead == nullptr || current_game == nullptr ||
        world.myobmap == nullptr)
    {
        errors.push_back("reachability: audit needs a lead start marker, an "
                         "installed gameplay context and an obmap");
        return errors;
    }

    GameWorld* const prev_world = current_game->world;
    current_game->world = &world;
    // A ground probe on the lead marker; removed from the obmap so it never
    // self-blocks a solve, and erased from the oblist again before
    // returning (the library must leave the entity lists exactly as found).
    walker* probe = world.add_ob(Order::Living, FAMILY_SOLDIER);
    if (probe == nullptr)
    {
        errors.push_back("reachability: could not seed the probe");
        current_game->world = prev_world;
        return errors;
    }
    probe->set_team_num(0);
    probe->set_real_team_num(0);
    probe->set_floor(lead->floor());
    probe->setxy(lead->xpos(), lead->ypos());
    (void)world.myobmap->remove(probe);
    GameplayPathfindingState* pathing =
        ensure_pathfinding_state(*current_game);
    const PathState start =
        make_path_state(probe->xpos(), probe->ypos(), probe->floor());

    auto check_reachable = [&](walker* ob)
    {
        const int tx = ob->xpos() / GRID_SIZE;
        const int ty = ob->ypos() / GRID_SIZE;
        const PathState goal =
            make_path_state(ob->xpos(), ob->ypos(), ob->floor());
        if (goal == start)
            return;
        std::vector<PathState> path;
        float total_cost = 0.0f;
        pathing->solve_for_point(probe, static_cast<short>(ob->xpos()),
                                 static_cast<short>(ob->ypos()), start, goal,
                                 path, total_cost);
        if (path.empty())
        {
            errors.push_back(std::format(
                "reachability: order {} family {} at tile ({}, {}) floor {} "
                "is unreachable from the lead start marker",
                static_cast<int>(ob->query_order()),
                static_cast<int>(ob->family()), tx, ty, ob->floor()));
        }
    };

    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr || ob == probe)
            continue;
        const Order order = ob->query_order();
        if (order != Order::Living && order != Order::Generator)
            continue;
        if (order == Order::Living &&
            ob->stats()->query_bit_flags(BIT_FLYING))
        {
            continue; // flyers cross lava/water/pits by design
        }
        check_reachable(ob);
    }
    // Exits too (tower recipe step 11): a floor whose staircase-out cannot
    // be walked to is unwinnable however clean its armies are.
    for (const auto& uptr : world.fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Treasure &&
            ob->family() == FAMILY_EXIT)
            check_reachable(ob);
    }

    // Erase the probe again (remove_ob also restores living_count; ~walker
    // deregisters from the owning world's obmap — already removed above,
    // which remove() tolerates).
    (void)world.remove_ob(probe);
    current_game->world = prev_world;
    return errors;
}

} // namespace og::mapgen
