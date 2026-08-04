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
 * audit_generator_spawn_exits is NOT from the westlands tool: it is new
 * (PR #174 playtest ruling §4), and models walker::fire()'s fixed spawn
 * placement rather than the A* the other audits share. See builders.h for
 * its contract.
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

#include <array>
#include <cstddef>
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

std::array<SpawnExit, 8> generator_spawn_exits(int gx, int gy, int gen_w,
                                               int gen_h, int spawn_w,
                                               int spawn_h) noexcept
{
    // walker::fire()'s eight setxy() arms, verbatim.
    const int right = gx + gen_w + 1;
    const int left = gx - spawn_w - 1;
    const int down = gy + gen_h + 1;
    const int up = gy - spawn_h - 1;
    const int mid_x = gx + (gen_w - spawn_w) / 2;
    const int mid_y = gy + (gen_h - spawn_h) / 2;
    return {{{right, mid_y},   // FACE_RIGHT
             {left, mid_y},    // FACE_LEFT
             {mid_x, down},    // FACE_DOWN
             {mid_x, up},      // FACE_UP
             {right, up},      // FACE_UP_RIGHT
             {left, up},       // FACE_UP_LEFT
             {right, down},    // FACE_DOWN_RIGHT
             {left, down}}};   // FACE_DOWN_LEFT
}

namespace {

// A generator's pixel footprint — the durable obstruction the spawn-egress
// audit models (see the builders.h note).
struct GeneratorBox
{
    int x0, y0, x1, y1; // half-open [x0, x1) x [y0, y1)
    int floor;
};

bool box_blocked(const std::vector<GeneratorBox>& bodies, int floor, int x,
                 int y, int w, int h)
{
    for (const GeneratorBox& b : bodies)
        if (b.floor == floor && b.x1 > x && b.x0 < x + w && b.y1 > y &&
            b.y0 < y + h)
            return true;
    return false;
}

// Tile-granular occlusion for the walk-out flood: a generator owns a tile
// when its body covers the tile's CENTER. Generator art is not tile-aligned
// (the bone pile is 50x40, the mage tower 50x58), so an exact box test would
// condemn whole tiles over a two-pixel overhang and invent pockets in
// corridors a spawn walks down every match; conversely a body over the
// center leaves no lane worth the name.
bool tile_body_blocked(const std::vector<GeneratorBox>& bodies, int floor,
                       int tx, int ty)
{
    const int cx = tx * GRID_SIZE + GRID_SIZE / 2;
    const int cy = ty * GRID_SIZE + GRID_SIZE / 2;
    for (const GeneratorBox& b : bodies)
        if (b.floor == floor && cx >= b.x0 && cx < b.x1 && cy >= b.y0 &&
            cy < b.y1)
            return true;
    return false;
}

// The spawn's own body fits at pixel (x, y): terrain (the engine's grid arm
// of query_passable, so a flyer's or an ethereal's escapes apply) plus the
// generator bodies.
bool spawn_body_fits(GameWorld& world, const std::vector<GeneratorBox>& bodies,
                     walker* probe, int floor, int x, int y)
{
    return world.query_grid_passable(static_cast<float>(x),
                                     static_cast<float>(y), probe, floor) &&
           !box_blocked(bodies, floor, x, y, probe->sizex(), probe->sizey());
}

// 8-connected flood from the lead marker's tile over the tiles the spawn can
// stand on, with the engine's no-corner-cutting rule and its positional
// Z-stair edges. Returns a (floor, ty, tx)-indexed reached mask.
std::vector<char> flood_spawn_reach(GameWorld& world,
                                    const std::vector<GeneratorBox>& bodies,
                                    walker* probe, int start_floor,
                                    int start_tx, int start_ty)
{
    const PixieData& g0 = world.grid_for_floor(0);
    const int w = g0.w;
    const int h = g0.h;
    const int floors = world.floor_count();
    std::vector<char> seen(static_cast<std::size_t>(floors) * w * h, 0);
    auto index = [&](int f, int tx, int ty) {
        return static_cast<std::size_t>((f * h + ty) * w + tx);
    };
    auto in_bounds = [&](int f, int tx, int ty) {
        return f >= 0 && f < floors && tx >= 0 && ty >= 0 && tx < w && ty < h;
    };
    auto standable = [&](int f, int tx, int ty) {
        return in_bounds(f, tx, ty) &&
               world.query_grid_passable(static_cast<float>(tx * GRID_SIZE),
                                         static_cast<float>(ty * GRID_SIZE),
                                         probe, f) &&
               !tile_body_blocked(bodies, f, tx, ty);
    };
    if (!in_bounds(start_floor, start_tx, start_ty))
        return seen;

    // The marker cell is seeded unconditionally: the crew stands there by
    // construction, and refusing a start would flood nothing and report
    // every generator sealed.
    std::vector<std::array<int, 3>> stack;
    seen[index(start_floor, start_tx, start_ty)] = 1;
    stack.push_back({start_floor, start_tx, start_ty});
    while (!stack.empty())
    {
        const auto [f, tx, ty] = stack.back();
        stack.pop_back();
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0)
                    continue;
                const int nx = tx + dx;
                const int ny = ty + dy;
                if (!standable(f, nx, ny) || seen[index(f, nx, ny)] != 0)
                    continue;
                // No corner cutting: a full-cell body sweeping a diagonal
                // overlaps both flanking cells.
                if (dx != 0 && dy != 0 &&
                    (!standable(f, tx + dx, ty) || !standable(f, tx, ty + dy)))
                    continue;
                seen[index(f, nx, ny)] = 1;
                stack.push_back({f, nx, ny});
            }
        // Positional Z-stair edge (UP -> f+1, DOWN -> f-1), mirroring the
        // engine graph. Single-floor levels never take it.
        if (floors > 1)
        {
            const unsigned char tile =
                world.grid_for_floor(f).data[tx + ty * w];
            const int nf = (tile == PIX_ZSTAIR_UP)     ? f + 1
                           : (tile == PIX_ZSTAIR_DOWN) ? f - 1
                                                       : f;
            if (nf != f && standable(nf, tx, ty) && seen[index(nf, tx, ty)] == 0)
            {
                seen[index(nf, tx, ty)] = 1;
                stack.push_back({nf, tx, ty});
            }
        }
    }
    return seen;
}

} // namespace

std::vector<std::string> audit_generator_spawn_exits(GameWorld& world)
{
    std::vector<std::string> errors;

    std::vector<walker*> generators;
    std::vector<GeneratorBox> bodies;
    walker* lead = nullptr;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->query_order() == Order::Generator)
        {
            generators.push_back(ob);
            bodies.push_back({ob->xpos(), ob->ypos(),
                              ob->xpos() + ob->sizex(),
                              ob->ypos() + ob->sizey(), ob->floor()});
        }
        else if (lead == nullptr && ob->query_order() == Order::Special &&
                 ob->family() == FAMILY_RESERVED_TEAM && ob->team_num() == 0)
        {
            lead = ob;
        }
    }
    if (generators.empty())
        return errors;
    if (lead == nullptr || current_game == nullptr || world.myobmap == nullptr ||
        !world.grid_for_floor(0).valid())
    {
        errors.push_back("spawn exits: audit needs a lead start marker, an "
                         "installed gameplay context, an obmap and a grid");
        return errors;
    }

    GameWorld* const prev_world = current_game->world;
    current_game->world = &world;
    const int start_tx = lead->xpos() / GRID_SIZE;
    const int start_ty = lead->ypos() / GRID_SIZE;

    for (walker* gen : generators)
    {
        // Probe the family this generator actually pours out (walker.cpp
        // :1164): the spawn's own size sets both the fire() offsets and the
        // footprint that has to fit.
        walker* probe =
            world.add_ob(Order::Living, static_cast<int>(gen->default_weapon()));
        if (probe == nullptr)
        {
            errors.push_back(std::format(
                "spawn exits: generator family {} at tile ({}, {}) floor {}: "
                "could not seed the spawn probe",
                static_cast<int>(gen->family()), gen->xpos() / GRID_SIZE,
                gen->ypos() / GRID_SIZE, gen->floor()));
            continue;
        }
        probe->set_team_num(gen->team_num());
        probe->set_real_team_num(gen->team_num());
        probe->set_floor(gen->floor());
        (void)world.myobmap->remove(probe);

        const std::vector<char> reached = flood_spawn_reach(
            world, bodies, probe, lead->floor(), start_tx, start_ty);
        const PixieData& g0 = world.grid_for_floor(0);
        auto candidate_connected = [&](int x, int y) {
            for (int ty = y / GRID_SIZE; ty <= (y + probe->sizey() - 1) / GRID_SIZE;
                 ++ty)
                for (int tx = x / GRID_SIZE;
                     tx <= (x + probe->sizex() - 1) / GRID_SIZE; ++tx)
                {
                    if (tx < 0 || ty < 0 || tx >= g0.w || ty >= g0.h)
                        continue;
                    if (reached[static_cast<std::size_t>(
                            (gen->floor() * g0.h + ty) * g0.w + tx)] != 0)
                        return true;
                }
            return false;
        };

        const std::array<SpawnExit, 8> exits = generator_spawn_exits(
            gen->xpos(), gen->ypos(), gen->sizex(), gen->sizey(),
            probe->sizex(), probe->sizey());
        std::vector<SpawnExit> usable;
        std::vector<SpawnExit> stranded;
        for (const SpawnExit& e : exits)
        {
            if (!spawn_body_fits(world, bodies, probe, gen->floor(), e.x, e.y))
                continue; // the engine discards this round's spawn
            usable.push_back(e);
            if (!candidate_connected(e.x, e.y))
                stranded.push_back(e);
        }
        const auto anchor = std::format(
            "spawn exits: generator family {} at tile ({}, {}) floor {}",
            static_cast<int>(gen->family()), gen->xpos() / GRID_SIZE,
            gen->ypos() / GRID_SIZE, gen->floor());
        if (usable.empty())
        {
            // Nothing can ever be born here: every one of the eight is
            // walled or body-blocked, so the generator is dead scenery that
            // still burns a cadence roll. Always a bug.
            errors.push_back(
                std::format("{}: no usable spawn exit (all 8 engine spawn "
                            "positions blocked)", anchor));
        }
        else if (stranded.size() == usable.size())
        {
            // Everything it pours out lands off the crew's walking map. On
            // an ordinary level that is the sealed-alcove bug; a level whose
            // generator is served by transport instead of feet (teleporter
            // courts) says so through its own reachability exception, which
            // the caller matches against this same anchor.
            errors.push_back(std::format(
                "{}: every spawn exit is cut off from the lead start marker "
                "({} of 8 usable)", anchor, usable.size()));
        }
        else
        {
            // A working exit AND a stranded one: a deterministic share of
            // the spawns piles up in the closed cell forever.
            for (const SpawnExit& p : stranded)
                errors.push_back(std::format(
                    "{}: spawn pocket at ({}, {})", anchor,
                    p.x / GRID_SIZE, p.y / GRID_SIZE));
        }

        (void)world.remove_ob(probe);
    }

    current_game->world = prev_world;
    return errors;
}

} // namespace og::mapgen
