/* Multiplayer Game Modes campaign generator — Team Deathmatch (300-305).
 *
 * The six arenas grids, byte-copied from the vendored art
 * (data/arenas/pix), with the entity layer re-authored for TDM: exits and
 * stains stripped, the treasure scatter thinned to per-family budgets
 * (obmap discipline — the §2.3 ledger), doors/keys/teleporters and the
 * 20-markers-x-4-teams start layout kept, generators re-leveled per the
 * roster. scen300 converts its four former exit tiles into the speed
 * potions its budget calls for (the source ships none).
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "modes_mapgen.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/level_data_hooks.h>

#include <cstdio>
#include <format>
#include <map>

namespace modesgen {
namespace {

struct TdmSpec
{
    int id;
    const char* title;
    // Treasure family -> keep count (fxlist order); families absent from
    // the map are doomed wholesale (exits, stains, surplus).
    std::map<int, int> keep;
    // Generator stat level override (0 = keep the authored level).
    int generator_level = 0;
    bool speed_at_exits = false; // scen300: former exit tiles become speed
};

std::vector<TdmSpec> tdm_specs()
{
    std::vector<TdmSpec> out;
    out.push_back({300, "Team Deathmatch: THE CIRCLE",
                   {{FAMILY_DRUMSTICK, 24}, {FAMILY_GOLD_BAR, 32},
                    {FAMILY_SILVER_BAR, 10}},
                   0, true});
    out.push_back({301, "Team Deathmatch: BLOODGLADE",
                   {{FAMILY_DRUMSTICK, 80}, {FAMILY_SILVER_BAR, 20},
                    {FAMILY_FLIGHT_POTION, 10}},
                   0, false});
    out.push_back({302, "Team Deathmatch: ARCHIPELAGO",
                   {{FAMILY_DRUMSTICK, 40}, {FAMILY_GOLD_BAR, 30},
                    {FAMILY_SILVER_BAR, 13}, {FAMILY_MAGIC_POTION, 8},
                    {FAMILY_FLIGHT_POTION, 9}},
                   2, false});
    out.push_back({303, "Team Deathmatch: GATEKEEPERS",
                   {{FAMILY_DRUMSTICK, 60}, {FAMILY_GOLD_BAR, 40},
                    {FAMILY_MAGIC_POTION, 11}, {FAMILY_INVIS_POTION, 13},
                    {FAMILY_FLIGHT_POTION, 6}, {FAMILY_KEY, 6}},
                   0, false});
    out.push_back({304, "Team Deathmatch: THE CASTLE",
                   {{FAMILY_DRUMSTICK, 45}, {FAMILY_GOLD_BAR, 12},
                    {FAMILY_SILVER_BAR, 12}, {FAMILY_MAGIC_POTION, 7},
                    {FAMILY_INVIS_POTION, 8}, {FAMILY_SPEED_POTION, 16},
                    {FAMILY_TELEPORTER, 12}},
                   2, false});
    out.push_back({305, "Team Deathmatch: BULLSEYE",
                   {{FAMILY_DRUMSTICK, 66}, {FAMILY_SILVER_BAR, 25},
                    {FAMILY_INVIS_POTION, 17},
                    {FAMILY_INVULNERABLE_POTION, 4},
                    {FAMILY_SPEED_POTION, 8}},
                   2, false});
    return out;
}

void build_one(const TdmSpec& spec, const ExpectedLevel& row)
{
    LevelRuntimeData level(spec.id, true, &headless_level_data_hooks());
    if (!level.load())
    {
        fail(std::format("failed to load vendored arenas scen{}", spec.id));
        return;
    }
    GameWorld& world = level.world();
    std::unique_ptr<walker> probe = make_probe(world);
    if (probe == nullptr)
    {
        fail(std::format("scen{}: no passability probe", spec.id));
        return;
    }

    // Pass 1: the treasure re-dress. Keep the first N PASSABLE ones of
    // each budgeted family in file order (the hand-authored scatter ships
    // a few treasures on impassable tiles); strip exits, stains and every
    // surplus. Impassable shortfalls are made up with extra drumsticks so
    // the per-map total budget stays exact.
    std::vector<walker*> doomed;
    std::vector<walker*> spare_food;
    std::vector<TilePos> exit_tiles;
    std::map<int, int> kept;
    for (const auto& uptr : world.fxlist)
    {
        walker* fx = uptr.get();
        if (fx == nullptr || fx->query_order() != Order::Treasure)
            continue;
        const int family = fx->family();
        const TilePos t{static_cast<short>(fx->xpos() / GRID_SIZE),
                        static_cast<short>(fx->ypos() / GRID_SIZE)};
        if (family == FAMILY_EXIT)
            exit_tiles.push_back(t);
        // The hand-authored scatter is not tile-aligned: probe the
        // treasure's EXACT footing (own position, own footprint) — the
        // same predicate the self-check's footing audit applies.
        const bool footed = world.query_grid_passable(
            static_cast<float>(fx->xpos()), static_cast<float>(fx->ypos()),
            fx, fx->floor());
        const auto budget = spec.keep.find(family);
        if (budget == spec.keep.end() ||
            kept[family] >= budget->second || !footed)
        {
            if (family == FAMILY_DRUMSTICK && footed)
                spare_food.push_back(fx);
            doomed.push_back(fx);
            continue;
        }
        ++kept[family];
    }
    int shortfall = 0;
    for (const auto& [family, want] : spec.keep)
        shortfall += want - kept[family];
    for (walker* fx : doomed)
    {
        if (shortfall > 0 && !spare_food.empty() && fx == spare_food.front())
        {
            spare_food.erase(spare_food.begin());
            --shortfall; // this drumstick covers an impassable slot
            continue;
        }
        world.remove_ob(fx);
    }
    if (shortfall > 0)
        fail(std::format("scen{}: {} budget slots unfilled (not enough "
                         "passable treasures)", spec.id, shortfall));

    // The 20-markers-x-4-teams start layout is kept, but a hand-authored
    // marker on impassable ground is nudged to the nearest free passable
    // tile (deterministic ring search).
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr || ob->query_order() != Order::Special ||
            ob->family() != FAMILY_RESERVED_TEAM)
            continue;
        const TilePos t{static_cast<short>(ob->xpos() / GRID_SIZE),
                        static_cast<short>(ob->ypos() / GRID_SIZE)};
        if (tile_passable(world, probe.get(), t))
            continue;
        bool moved = false;
        for (int r = 1; r <= 6 && !moved; ++r)
            for (int dy = -r; dy <= r && !moved; ++dy)
                for (int dx = -r; dx <= r && !moved; ++dx)
                {
                    if (std::max(std::abs(dx), std::abs(dy)) != r)
                        continue;
                    const TilePos n{static_cast<short>(t.tx + dx),
                                    static_cast<short>(t.ty + dy)};
                    if (n.tx < 0 || n.ty < 0 ||
                        !tile_passable(world, probe.get(), n))
                        continue;
                    ob->setxy(static_cast<short>(n.tx * GRID_SIZE),
                              static_cast<short>(n.ty * GRID_SIZE));
                    std::printf("modes_mapgen: scen%d: marker (%d, %d) "
                                "nudged to (%d, %d)\n", spec.id, t.tx, t.ty,
                                n.tx, n.ty);
                    moved = true;
                }
        if (!moved)
            fail(std::format("scen{}: marker ({}, {}) has no passable "
                             "neighbor", spec.id, t.tx, t.ty));
    }

    // Generators: keep placement and family, re-level per the roster.
    if (spec.generator_level > 0)
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob != nullptr && ob->query_order() == Order::Generator &&
                ob->stats() != nullptr)
                ob->stats()->set_level(
                    static_cast<short>(spec.generator_level));
        }

    // scen300's budget calls for four speed potions the source never
    // shipped: the four former exit tiles take them (same map beats).
    if (spec.speed_at_exits)
        for (const TilePos& t : exit_tiles)
            place_at(world, Order::Treasure, FAMILY_SPEED_POTION, 0, t);

    apply_mode_metadata(world, spec.title, world.par_value);
    if (!save_level(world, row))
        return;
    copy_vendored_pix("arenas", std::format("scen{:04d}.png", spec.id));
    copy_vendored_pix("arenas", std::format("scen{:04d}_d0.png", spec.id));
    std::printf("modes_mapgen: built %d '%s' (arenas re-dress)\n", spec.id,
                spec.title);
}

} // namespace

std::vector<ExpectedLevel> tdm_expectations()
{
    std::vector<ExpectedLevel> out;

    ExpectedLevel circle;
    circle.id = 300;
    circle.mode = ModeKind::Tdm;
    circle.par = 10;
    circle.title = "Team Deathmatch: THE CIRCLE";
    circle.grid_w = 60;
    circle.grid_h = 60;
    circle.team_count = 4;
    circle.markers_per_team = 20;
    circle.treasures = 70;
    circle.time_limit = 7200;
    circle.score_limit = 20;
    circle.decor_cells = 1;
    circle.briefing = {
        "THE OLD PROVING GROUND. EVERY",
        "CHAMPION IN THE BOOK BLED HERE",
        "FIRST. NO WALLS, NO TRICKS,",
        "NO PLACE TO HIDE.",
        "KILLS ALONE FILL THE LEDGER.",
        "FIRST BAND TO THE POSTED TALLY",
        "TAKES THE PURSE.",
        "-- THE GAMESMASTER",
    };
    out.push_back(std::move(circle));

    ExpectedLevel glade;
    glade.id = 301;
    glade.mode = ModeKind::Tdm;
    glade.par = 12;
    glade.title = "Team Deathmatch: BLOODGLADE";
    glade.grid_w = 60;
    glade.grid_h = 60;
    glade.team_count = 4;
    glade.markers_per_team = 20;
    glade.treasures = 110;
    glade.time_limit = 7200;
    glade.score_limit = 20;
    glade.decor_cells = 1;
    glade.briefing = {
        "THE FOREST GAME, CONTENDERS.",
        "THE GLADE HIDES BLADES WELL",
        "AND FEEDS YOU BETTER. EAT,",
        "STALK, STRIKE FROM THE GREEN.",
        "EVERY KILL GOES IN THE BOOK.",
        "FIRST BAND TO THE TALLY WINS.",
        "-- THE GAMESMASTER",
    };
    out.push_back(std::move(glade));

    ExpectedLevel isles;
    isles.id = 302;
    isles.mode = ModeKind::Tdm;
    isles.par = 14;
    isles.title = "Team Deathmatch: ARCHIPELAGO";
    isles.grid_w = 80;
    isles.grid_h = 80;
    isles.team_count = 4;
    isles.markers_per_team = 20;
    isles.generators_per_team[4] = 2;
    isles.treasures = 100;
    isles.spawn_caps = {{4, 8}};
    isles.time_limit = 7200;
    isles.score_limit = 20;
    isles.decor_cells = 26;
    isles.briefing = {
        "ISLANDS AND FORDS, CONTENDERS.",
        "THE CROSSINGS ARE THE KILLING",
        "FIELDS. OLD TENTS ON THE SHOALS",
        "POUR OUT BONES THAT COUNT FOR",
        "NOTHING - ONLY CONTENDERS FILL",
        "THE LEDGER. TAKE FLIGHT IF YOU",
        "DARE THE DEEP WATER.",
        "-- THE GAMESMASTER",
    };
    out.push_back(std::move(isles));

    ExpectedLevel gates;
    gates.id = 303;
    gates.mode = ModeKind::Tdm;
    gates.par = 14;
    gates.title = "Team Deathmatch: GATEKEEPERS";
    gates.grid_w = 80;
    gates.grid_h = 80;
    gates.team_count = 4;
    gates.markers_per_team = 20;
    gates.generators_per_team[5] = 1; // treehouse
    gates.generators_per_team[6] = 1; // tent
    gates.treasures = 136;            // 130 spice + the 6 kept keys
    gates.doors = 15;
    gates.spawn_caps = {{5, 4}, {6, 4}};
    gates.time_limit = 7200;
    // 20 timed out 5/6 sweep runs (20-42 total frags in 10:00): the doors/
    // keys identity caps the frag pace, so the score limit meets it (B3).
    gates.score_limit = 12;
    gates.a_star_waived = true; // arenas heritage: always ran past the limit
    gates.decor_cells = 34;
    gates.briefing = {
        "THE VAULT GAME. DOORS AND KEYS",
        "GUARD A GLUT OF GOLD, AND THE",
        "NARROW HALLS MAKE MURDER EASY.",
        "LOOT IF YOU MUST; THE BOOK",
        "COUNTS ONLY KILLS. MIND THE",
        "WARDENS' TENTS IN THE DEEP.",
        "-- THE GAMESMASTER",
    };
    out.push_back(std::move(gates));

    ExpectedLevel castle;
    castle.id = 304;
    castle.mode = ModeKind::Tdm;
    castle.par = 15;
    castle.title = "Team Deathmatch: THE CASTLE";
    castle.grid_w = 60;
    castle.grid_h = 60;
    castle.team_count = 4;
    castle.markers_per_team = 20;
    castle.generators_per_team[7] = 2;
    castle.treasures = 112; // 100 spice + the 12 kept teleporter pads
    castle.spawn_caps = {{7, 8}};
    castle.time_limit = 7200;
    // 20 timed out 6/6 sweep runs (6-14 total frags in 10:00): the pad-
    // scatter identity makes ~1 frag/min the map's pace, so the score
    // limit meets it (B3).
    castle.score_limit = 10;
    castle.decor_cells = 16;
    // THE CASTLE is the teleporter-ambush map: the four team quadrants and
    // the central towers are pad-served by design (twelve kept teleporters),
    // so ground-A* from the team-0 lead legitimately cannot reach them.
    castle.reachability_exceptions = {
        "at tile (28, 28)", // the paired tower generators, pad-served court
        "at tile (50, 11)", // team 1 lead quadrant
        "at tile (10, 55)", // team 2 lead quadrant
        "at tile (51, 56)", // team 3 lead quadrant
    };
    castle.briefing = {
        "THE CASTLE GAME, CONTENDERS.",
        "TWELVE PADS BLINK YOU ACROSS",
        "THE COURTS - AMBUSH IS THE",
        "HOUSE STYLE. THE OLD TOWERS",
        "STILL RAISE THEIR OWN GUARDS.",
        "KILLS ALONE FILL THE LEDGER.",
        "-- THE GAMESMASTER",
    };
    out.push_back(std::move(castle));

    ExpectedLevel rings;
    rings.id = 305;
    rings.mode = ModeKind::Tdm;
    rings.par = 15;
    rings.title = "Team Deathmatch: BULLSEYE";
    rings.grid_w = 60;
    rings.grid_h = 60;
    rings.team_count = 4;
    rings.markers_per_team = 20;
    rings.generators_per_team[0] = 1;
    rings.generators_per_team[1] = 1;
    rings.generators_per_team[2] = 1;
    rings.generators_per_team[3] = 1;
    rings.treasures = 120;
    rings.spawn_caps = {{0, 4}, {1, 4}, {2, 4}, {3, 4}};
    rings.time_limit = 7200;
    rings.score_limit = 20;
    rings.a_star_waived = true;
    rings.decor_cells = 24;
    rings.briefing = {
        "THE RINGS, CONTENDERS. EVERY",
        "BAND GETS A TENT OF ITS OWN,",
        "POURING BONES INTO THE FRAY.",
        "SPEND THEM AS SHIELDS OR SPEND",
        "THEM AS SPEARS. THE CENTER IS",
        "DEATH AND THE BOOK LOVES IT.",
        "-- THE GAMESMASTER",
    };
    out.push_back(std::move(rings));

    return out;
}

void build_tdm()
{
    if (!mount_vendored_data("arenas"))
        return;
    const std::vector<ExpectedLevel> rows = tdm_expectations();
    for (const TdmSpec& spec : tdm_specs())
    {
        const ExpectedLevel* row = nullptr;
        for (const ExpectedLevel& r : rows)
            if (r.id == spec.id)
                row = &r;
        if (row == nullptr)
        {
            fail(std::format("no expectation row for scen{}", spec.id));
            continue;
        }
        build_one(spec, *row);
    }
    unmount_vendored_data("arenas");
}

} // namespace modesgen
