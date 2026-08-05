/* Multiplayer Game Modes campaign generator — CTF (500-509).
 *
 * The ten proven CTF maps, kept as-is from the vendored layouts
 * (data/ctf): grids byte-copied, entity dressing (flags, control points,
 * anchor clusters, doors, keys, spice, par values, capture limits)
 * reproduced through a load/save round-trip. The only content changes are
 * the type byte (0x20 scripted — the Lua CTF port owns these levels), the
 * re-voiced Gamesmaster briefings, and the allied-guard rule (team-0/1
 * ACT_GUARD livings gain the hold-post bit the guard wake policy
 * requires; scen501's garrison predates the rule).
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
#include <filesystem>
#include <format>

namespace modesgen {
namespace {

void build_one(const ExpectedLevel& row)
{
    LevelRuntimeData level(row.id, true, &headless_level_data_hooks());
    if (!level.load())
    {
        fail(std::format("failed to load vendored ctf scen{}", row.id));
        return;
    }
    GameWorld& world = level.world();
    if (world.title != row.title)
        fail(std::format("scen{}: vendored title '{}' != expected '{}'",
                         row.id, world.title, row.title));

    // Allied-guard rule (guard wake policy): a team-0/1 ACT_GUARD living
    // must hold post or the wake rule marches it off its station.
    int hold_post_fixes = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr || ob->query_order() != Order::Living)
            continue;
        if (ob->team_num() <= 1 && ob->act_type() == ACT_GUARD &&
            !ob->guard_hold_post())
        {
            ob->set_guard_hold_post(true);
            ++hold_post_fixes;
        }
    }
    if (hold_post_fixes > 0)
        std::printf("modes_mapgen: scen%d: hold-post set on %d allied "
                    "guard(s)\n", row.id, hold_post_fixes);

    apply_mode_metadata(world, row.title, world.par_value);
    if (!save_level(world, row))
        return;
    copy_vendored_pix("ctf", std::format("scen{:04d}.png", row.id));
    const std::filesystem::path decor =
        std::filesystem::path("tools/modes_mapgen/data/ctf/pix") /
        std::format("scen{:04d}_d0.png", row.id);
    if (std::filesystem::exists(decor))
        copy_vendored_pix("ctf", std::format("scen{:04d}_d0.png", row.id));
    std::printf("modes_mapgen: built %d '%s' (ctf keep-as-is)\n", row.id,
                row.title);
}

// item_pads: hand-transcribed from the vendored layouts (the maps are
// loaded, not built, so the row cannot author them) — the mapgen
// self-check pins the multiset against the world's live food/potion
// treasures exactly, and prints the paste-ready list on any drift.
// Interval 300 (25 s): sustain both offense and defense over the 20-min
// cap.
ExpectedLevel ctf_row(int id, const char* title, int par, int w, int h,
                      int teams, int treasures, int doors, int other_weapons,
                      int livings, int cps, int decor_cells,
                      std::vector<ItemPad> item_pads,
                      std::vector<std::string> briefing)
{
    ExpectedLevel row;
    row.id = id;
    row.mode = ModeKind::Ctf;
    row.title = title;
    row.par = par;
    row.grid_w = w;
    row.grid_h = h;
    row.team_count = teams;
    row.markers_per_team = 12;
    row.flags = teams;
    row.control_points = cps;
    row.authored_livings = livings;
    row.treasures = treasures;
    row.doors = doors;
    row.other_weapons = other_weapons;
    row.item_pads = std::move(item_pads);
    row.item_interval = 300;
    row.time_limit = 14400;
    row.score_limit = 0; // the flag stat level carries the capture limit
    row.decor_cells = decor_cells;
    row.briefing = std::move(briefing);
    return row;
}

} // namespace

std::vector<ExpectedLevel> ctf_expectations()
{
    std::vector<ExpectedLevel> out;

    out.push_back(ctf_row(500, "CTF: FIRST BLOOD", 4, 40, 30, 2, 7, 0, 0, 0, 1,
                          8,
                          {
                              {FAMILY_DRUMSTICK, {1, 12}},
                              {FAMILY_DRUMSTICK, {36, 12}},
                              {FAMILY_DRUMSTICK, {2, 17}},
                              {FAMILY_DRUMSTICK, {37, 17}},
                              {FAMILY_INVIS_POTION, {20, 16}},
                              {FAMILY_SPEED_POTION, {20, 5}},
                              {FAMILY_SPEED_POTION, {20, 24}},
                          },
                          {
                              "CONTENDERS, THE BANNER GAME.",
                              "STEAL THEIR FLAG AND RUN IT",
                              "HOME WHILE YOURS STILL STANDS.",
                              "THREE GAPS CROSS THE WALL:",
                              "FAST LANES NORTH AND SOUTH,",
                              "A PLAZA IN THE MIDDLE. HOLD",
                              "THE PLAZA POST AND YOUR FALLEN",
                              "RETURN TWICE AS FAST.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ctf_row(501, "CTF: A BORDER FORT", 4, 30, 30, 2, 25, 5, 0, 5,
                          1, 4,
                          {
                              {FAMILY_DRUMSTICK, {7, 10}},
                              {FAMILY_DRUMSTICK, {7, 10}},
                              {FAMILY_DRUMSTICK, {7, 10}},
                              {FAMILY_DRUMSTICK, {7, 10}},
                              {FAMILY_DRUMSTICK, {8, 10}},
                              {FAMILY_DRUMSTICK, {8, 10}},
                              {FAMILY_DRUMSTICK, {9, 10}},
                              {FAMILY_DRUMSTICK, {7, 11}},
                              {FAMILY_SPEED_POTION, {6, 12}},
                          },
                          {
                              "THE BANNER GAME AT THE BORDER.",
                              "THE GARRISON KEEPS ITS FLAG IN",
                              "THE COURTYARD; THE BESIEGERS",
                              "RAISE THEIRS ON THE PLAIN.",
                              "THE GATEHOUSE POST DECIDES WHO",
                              "DICTATES THE SIEGE. A KEY OPENS",
                              "THE SIDE DOOR, FOR THE SLY.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ctf_row(502, "CTF: CASTLE CORNER", 4, 30, 40, 2, 16, 0, 0, 0,
                          1, 10,
                          {
                              {FAMILY_DRUMSTICK, {14, 0}},
                              {FAMILY_DRUMSTICK, {21, 0}},
                              {FAMILY_DRUMSTICK, {25, 18}},
                              {FAMILY_DRUMSTICK, {26, 18}},
                              {FAMILY_DRUMSTICK, {25, 19}},
                              {FAMILY_DRUMSTICK, {27, 35}},
                              {FAMILY_DRUMSTICK, {27, 36}},
                              {FAMILY_DRUMSTICK, {27, 36}},
                              {FAMILY_SPEED_POTION, {28, 19}},
                          },
                          {
                              "BANNERS AT THE CASTLE CORNER.",
                              "ONE FLIES IN THE LONG HALL,",
                              "ONE IN THE MUSTERING YARD.",
                              "ALL ROADS MEET AT THE HALL",
                              "MOUTH - HOLD THAT POST AND",
                              "THE CASTLE PAYS ITS TAX TO",
                              "YOUR PAGE OF THE BOOK.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ctf_row(503, "CTF: THE OUTPOST", 5, 40, 60, 2, 52, 5, 0, 0, 1,
                          10,
                          {
                              {FAMILY_DRUMSTICK, {14, 14}},
                              {FAMILY_DRUMSTICK, {23, 14}},
                              {FAMILY_DRUMSTICK, {14, 15}},
                              {FAMILY_DRUMSTICK, {23, 15}},
                              {FAMILY_DRUMSTICK, {14, 16}},
                              {FAMILY_DRUMSTICK, {23, 16}},
                              {FAMILY_DRUMSTICK, {14, 17}},
                              {FAMILY_DRUMSTICK, {23, 17}},
                              {FAMILY_MAGIC_POTION, {15, 18}},
                              {FAMILY_MAGIC_POTION, {22, 18}},
                              {FAMILY_INVIS_POTION, {15, 17}},
                              {FAMILY_INVIS_POTION, {22, 17}},
                          },
                          {
                              "THE LONE OUTPOST PLAYS HOST.",
                              "ONE BANNER WAITS IN A WALLED",
                              "YARD WITH A SINGLE SOUTH GATE;",
                              "THE OTHER MUSTERS ON THE FIELD.",
                              "RING ROADS FLANK EVERY WALL.",
                              "THE GATE POST DECIDES WHO KEEPS",
                              "THE DOOR. A KEY OPENS THE KEEP.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ctf_row(504, "CTF: RIVER RUN", 5, 60, 40, 2, 7, 0, 0, 0, 1,
                          30,
                          {
                              {FAMILY_DRUMSTICK, {26, 2}},
                              {FAMILY_DRUMSTICK, {32, 2}},
                              {FAMILY_DRUMSTICK, {26, 37}},
                              {FAMILY_DRUMSTICK, {32, 37}},
                              {FAMILY_INVIS_POTION, {31, 20}},
                              {FAMILY_SPEED_POTION, {9, 19}},
                              {FAMILY_SPEED_POTION, {50, 20}},
                          },
                          {
                              "THE RIVER GAME, CONTENDERS.",
                              "TWO PLANK BRIDGES FLANK A WIDE",
                              "STONE CROSSING; THE ISLAND",
                              "POST SITS IN ITS MIDDLE. DROP",
                              "A BANNER IN THE DRINK AND IT",
                              "FLIES STRAIGHT HOME - BRIDGES",
                              "ARE SAFE, FLIERS GAMBLE.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ctf_row(505, "CTF: TRIAD", 6, 51, 51, 3, 7, 0, 0, 0, 1, 0,
                          {
                              {FAMILY_DRUMSTICK, {17, 20}},
                              {FAMILY_DRUMSTICK, {33, 20}},
                              {FAMILY_DRUMSTICK, {25, 34}},
                              {FAMILY_INVIS_POTION, {25, 26}},
                              {FAMILY_SPEED_POTION, {25, 16}},
                              {FAMILY_SPEED_POTION, {17, 30}},
                              {FAMILY_SPEED_POTION, {33, 30}},
                          },
                          {
                              "THREE BANDS, THREE BANNERS.",
                              "EVERY POCKET HAS TWO DOORS:",
                              "ONE FACES THE PLAZA, ONE IS A",
                              "REAR SALLY DOOR. STEAL FROM",
                              "ONE RIVAL AND YOU BARE YOUR",
                              "BACK TO THE OTHER. THE PLAZA",
                              "POST BREAKS STALEMATES.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ctf_row(506, "CTF: THE UNDERPASS", 5, 60, 20, 2, 52, 3, 16, 0,
                          1, 0,
                          {
                              {FAMILY_DRUMSTICK, {44, 14}},
                              {FAMILY_DRUMSTICK, {44, 14}},
                              {FAMILY_DRUMSTICK, {46, 15}},
                              {FAMILY_DRUMSTICK, {46, 15}},
                              {FAMILY_DRUMSTICK, {46, 15}},
                              {FAMILY_DRUMSTICK, {46, 15}},
                              {FAMILY_DRUMSTICK, {46, 15}},
                              {FAMILY_DRUMSTICK, {42, 16}},
                              {FAMILY_DRUMSTICK, {42, 16}},
                              {FAMILY_DRUMSTICK, {46, 16}},
                              {FAMILY_DRUMSTICK, {46, 16}},
                              {FAMILY_DRUMSTICK, {46, 16}},
                          },
                          {
                              "ONE TUNNEL JOINS THE CAMPS,",
                              "AND EVERY BANNER RUN WADES",
                              "THROUGH THE ENEMY RESPAWN",
                              "STREAM. THE POST AT THE",
                              "NARROWS HALVES YOUR WAIT.",
                              "A KEY OPENS A TREASURE DOOR",
                              "OFF THE TUNNEL, FOR THE BOLD.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ctf_row(507, "CTF: DUNGEON OF STARS", 6, 70, 70, 4, 57, 22, 0,
                          0, 1, 12,
                          {
                              {FAMILY_DRUMSTICK, {21, 27}},
                              {FAMILY_DRUMSTICK, {21, 27}},
                              {FAMILY_DRUMSTICK, {22, 28}},
                              {FAMILY_DRUMSTICK, {22, 28}},
                              {FAMILY_DRUMSTICK, {22, 28}},
                              {FAMILY_DRUMSTICK, {24, 28}},
                              {FAMILY_DRUMSTICK, {24, 28}},
                              {FAMILY_DRUMSTICK, {44, 28}},
                              {FAMILY_DRUMSTICK, {44, 28}},
                              {FAMILY_DRUMSTICK, {46, 28}},
                              {FAMILY_DRUMSTICK, {47, 28}},
                              {FAMILY_DRUMSTICK, {47, 28}},
                              {FAMILY_DRUMSTICK, {47, 28}},
                              {FAMILY_DRUMSTICK, {23, 29}},
                              {FAMILY_DRUMSTICK, {23, 29}},
                              {FAMILY_DRUMSTICK, {45, 29}},
                          },
                          {
                              "FOUR BANNERS IN THE DEEP.",
                              "EACH CREW CAMPS BESIDE AN",
                              "ANCIENT TELEPORTER - RIDE THEM",
                              "FOR WILD ESCAPES WITH STOLEN",
                              "CLOTH. THE STAR CHAMBER POST",
                              "COMMANDS THE CROSSROADS.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ctf_row(508, "CTF: CENTWHEIT MANOR", 6, 50, 50, 3, 17, 0, 0,
                          0, 2, 9,
                          {
                              {FAMILY_DRUMSTICK, {2, 44}},
                              {FAMILY_DRUMSTICK, {28, 46}},
                              {FAMILY_DRUMSTICK, {36, 46}},
                              {FAMILY_DRUMSTICK, {5, 48}},
                              {FAMILY_DRUMSTICK, {34, 48}},
                              {FAMILY_INVIS_POTION, {18, 1}},
                              {FAMILY_SPEED_POTION, {0, 39}},
                              {FAMILY_SPEED_POTION, {5, 40}},
                          },
                          {
                              "THREE CREWS BRAWL ACROSS THE",
                              "MANOR GROUNDS. ONE BANNER ON",
                              "THE WEST LAWN, ONE IN THE",
                              "GREAT HALL, ONE IN THE SOUTH",
                              "WING. TWO POSTS - THE ARTERY",
                              "AND THE EAST HALL. CLAIM BOTH",
                              "AND RULE THE HOUSE.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ctf_row(509, "CTF: CROSSFIRE", 6, 60, 60, 4, 9, 0, 0, 0, 1,
                          48,
                          {
                              {FAMILY_DRUMSTICK, {29, 10}},
                              {FAMILY_DRUMSTICK, {10, 29}},
                              {FAMILY_DRUMSTICK, {49, 30}},
                              {FAMILY_DRUMSTICK, {30, 49}},
                              {FAMILY_INVIS_POTION, {30, 30}},
                              {FAMILY_SPEED_POTION, {29, 22}},
                              {FAMILY_SPEED_POTION, {37, 29}},
                              {FAMILY_SPEED_POTION, {22, 30}},
                              {FAMILY_SPEED_POTION, {30, 37}},
                          },
                          {
                              "THE PINWHEEL FINALE. THE",
                              "CLOCKWISE BOULEVARD IS FAST",
                              "AND OPEN; THE COUNTER ALLEYS",
                              "HIDE BEHIND BOULDERS. RAID",
                              "FAST, SNEAK HOME. FIVE",
                              "CAPTURES TAKE THE FIELD -",
                              "THE BOOK DEMANDS A SHOW.",
                              "-- THE GAMESMASTER",
                          }));

    return out;
}

void build_ctf()
{
    if (!mount_vendored_data("ctf"))
        return;
    for (const ExpectedLevel& row : ctf_expectations())
        build_one(row);
    unmount_vendored_data("ctf");
}

} // namespace modesgen
