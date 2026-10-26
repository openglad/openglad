/* Imaginations campaign generator.
 *
 * Produces campaigns/imaginations/ (the source tree the build composes
 * into builtin/imaginations.glad): the dream-log campaign — levels kids
 * imagine, sent in and made real, one scenario per idea:
 *   scen 1 "The Raspberry Isle": an island ringed by open sea, the crew
 *     landing on the shores all around, and a moated castle in the
 *     middle whose garrison must be cleared ("we start at the edges of
 *     the island, the sea is all around us, and the enemies are in the
 *     middle"). Exits to 2.
 *   scen 2 "The Castle of Gold": our own castle in a faerie war; gold
 *     taken from its strongrooms wakes ghosts for our side, and the gate
 *     opens for no faerie until the faeries carry all twelve keys.
 *     Exits to 3.
 *   scen 3 "The Asteroid Castle": our castle on an asteroid in black
 *     space, ringed by spikes that catch the raiders; the Big One sits
 *     on the Balloon Rock (a second floor) until he is lured off it and
 *     falls, and the key to the big door hangs under the rock.
 * New ideas append new scens; the walk-out exit of the newest level
 * loops home to 1 until its successor exists.
 *
 * Built on the og::mapgen builder library (the authoritative authoring
 * API; the older tools keep private helper copies to protect committed
 * bytes — a NEW tool must not clone them). SDL-free; headless platform
 * glue, zips a campaign package, mounts it, and self-checks every level
 * by reloading it: exact army counts, text budgets, exit destinations,
 * and the og::mapgen audits (footing, stairs, fall lines, ground
 * reachability from the lead marker, generator spawn egress).
 *
 * Usage: imaginations_mapgen [output-dir]   (default: campaigns/imaginations)
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/mapgen/builders.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/save_data.h>

#include "../campaign_export.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

bool write_pixie_png(const char* filepath, const PixieData& data);
void io_init(int argc, char* argv[]);
void io_exit();
std::string get_user_path();

// --- Headless process globals (same shape as the other mapgen tools). --------
namespace og::runtime {
static SessionState s_mapgen_session{};
thread_local SessionState* current_session = &s_mapgen_session;
std::atomic<SessionState*> primary_session{&s_mapgen_session};
std::atomic<GameplayContext*> primary_game{&s_mapgen_session.game_};
} // namespace og::runtime

void popup_dialog(const char* title, const char* message)
{
    std::fprintf(stderr, "[%s] %s\n", title, message);
}

std::uint32_t random(std::uint32_t x)
{
    static std::uint32_t state = 20260810u;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

namespace imaggen {
namespace {

namespace fs = std::filesystem;

// The embedded pack's id; its hand-authored source lives at
// campaigns/imaginations/packs/imaginations.dreams/ (composed into the
// archive by the build, staged here for the self-checks). Loads after
// "core" — pack replay order is pack-id lexicographic.
constexpr const char* kDreamsPackId = "imaginations.dreams";

int g_errors = 0;

// SCENARIO INFORMATION dialog budget (33 glyphs per briefing line) and the
// level-title field budget (30 bytes).
constexpr std::size_t kBriefingLineBudget = 33;
constexpr std::size_t kTitleBudget = 30;

// Every deterministic decor scatter in this campaign keys on one seed.
constexpr std::uint32_t kScatterSeed = 20260810u;

void fail(const std::string& message)
{
    std::fprintf(stderr, "imaginations_mapgen: ERROR: %s\n", message.c_str());
    ++g_errors;
}

// Deterministic pavement pattern for the moat causeways (PAVEMENT1-3 are
// inert to the autotiler, so the variety is painted in, after smoothing —
// the same recipe as the Wizard's Vale causeways in westlands).
void paint_pavement(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[3] = {PIX_PAVEMENT1, PIX_PAVEMENT2,
                                                  PIX_PAVEMENT3};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            og::mapgen::paint(g, x, y, variants[(x * 7 + y * 13) % 3]);
}

void save_level_files(GameWorld& world, int id, const char* title,
                      const std::vector<std::string>& description,
                      int par_value, int time_bonus_limit)
{
    world.title = title;
    world.par_value = static_cast<short>(par_value);
    world.time_bonus_limit = static_cast<short>(time_bonus_limit);
    if (std::string(title).size() > kTitleBudget)
        fail(std::format("title '{}' overflows the {}-byte budget", title,
                         kTitleBudget));

    og::data::LevelFileMetadata metadata;
    metadata.grid_file = std::format("scen{:04d}", id);
    metadata.generated = true; // provenance mark: this scen is tool output
    for (const std::string& line : description)
    {
        if (line.size() > kBriefingLineBudget)
            fail(std::format("briefing line '{}' overflows the {}-char "
                             "budget", line, kBriefingLineBudget));
        metadata.description.push_back(line);
    }

    const std::string user = get_user_path();
    const std::string fss = user + std::format("temp/scen/scen{}.fss", id);
    og::data::LevelFileIoError err = og::data::LevelFileIoError::None;
    if (!og::data::save_level_scenario_file(world, fss, metadata, &err))
    {
        fail(std::format("failed to write {}", fss));
        return;
    }
    const std::string base = user + "temp/pix/" + metadata.grid_file;
    if (!write_pixie_png((base + ".png").c_str(), world.grid))
        fail(std::format("failed to write {}.png", base));
    // Extra floors by derived name "{grid}_f{N}".
    for (int f = 1; f < world.floor_count(); ++f)
    {
        const std::string p = std::format("{}_f{}.png", base, f);
        if (!write_pixie_png(p.c_str(), world.grid_for_floor(f)))
            fail(std::format("failed to write {}", p));
    }
    // Decor planes by derived name "{grid}_d{N}" for exactly the floors the
    // .fss payload flags as present (valid plane with a nonzero byte).
    for (int f = 0; f < world.floor_count(); ++f)
    {
        const PixieData& dec = world.decor_for_floor(f);
        if (!dec.valid())
            continue;
        const std::size_t cells =
            static_cast<std::size_t>(dec.w) * static_cast<std::size_t>(dec.h);
        bool nonzero = false;
        for (std::size_t c = 0; c < cells && !nonzero; ++c)
            nonzero = dec.data[c] != 0;
        if (!nonzero)
            continue;
        const std::string p = std::format("{}_d{}.png", base, f);
        if (!write_pixie_png(p.c_str(), dec))
            fail(std::format("failed to write {}", p));
    }
    std::printf("imaginations_mapgen: built %d '%s'\n", id, title);
}

// --- Scen 1: THE RASPBERRY ISLE. ---------------------------------------------
// The submitted design, verbatim: a round island in open sea, the crew
// starting spread around the shore ("we start at the edges, at every
// corner"), the foes starting in the middle ("in a castle, near the
// water" — honored twice: the sea rings the island AND a moat rings the
// castle). Kill-all with a walk-out exit at the landing beach.
//
// Geometry, from center (31.5, 31.5) on a 64x64 grid:
//   r >= 28.5        open sea, with four reef islets in the far corners
//   [26.0, 28.5)     the raspberry shore — the speckled landing sand
//   [22.5, 26.0)     sunlit shore meadow (the landing ring, four piers)
//   inside 22.5      the berry fields: bramble groves, copses, boulders
//   rect 17..46      the moat, a square band 3 tiles wide ("the sea in
//   minus 20..43     the middle"), bridged by four paved causeways
//   wall 22..41      the outer bailey: cobbled ring, arrow turrets,
//                    posted garrison
//   wall 27..36      the keep: cobbled court, gate wards, and the
//                    carpet dais at 30..33 where the Sea Wizard waits
//                    between his bodyguards
// Reinforcement waves sleep in the bailey and the court (spawn_delay)
// and wake mid-assault with the WAVE HUD flash. A flight potion near
// the south landing offers the "very magical" alternative — fly the
// moat and the wall face straight into the court — and the reef islets
// hold flight-gated treasure for whoever keeps wings long enough.
void build_raspberry_isle()
{
    LevelRuntimeData level(1, true, &headless_level_data_hooks());
    GameWorld& w = level.world();
    og::mapgen::init_world(w, 1, 64, 64);
    PixieData& g = w.grid;
    const double cx = 31.5;
    const double cy = 31.5;

    // Terrain, inside out (genre tiles only; the autotiler shapes every
    // shore and wall face in smooth_world below).
    og::mapgen::paint_ring(g, cx, cy, 28.5, 100.0, PIX_WATER1); // open sea
    og::mapgen::paint_ring(g, cx, cy, 26.0, 28.5, PIX_DIRT_1);  // landing sand
    og::mapgen::paint_ring(g, cx, cy, 22.5, 26.0, PIX_GRASS_LIGHT_1);
    // Four reef atolls in the deep water, one per compass corner — TREE
    // rings on purpose, with no standable cell: mages and skeletons
    // self-teleport, and any walkable ground off the island would let a
    // pressed foe (the Sea Wizard included — it happened) strand the
    // kill-all across open water.
    for (const auto& islet : {std::pair{8.0, 8.0}, {55.0, 8.0},
                              {8.0, 55.0}, {55.0, 55.0}})
    {
        og::mapgen::paint_ring(g, islet.first, islet.second, 0.0, 2.3,
                               PIX_TREE_M1);
    }
    // Raspberry brambles: three dark-grass groves on the field ring (the
    // shrub scatter below dresses them into berry thickets).
    og::mapgen::paint_rect(g, 12, 18, 17, 22, PIX_GRASS_DARK_1);
    og::mapgen::paint_rect(g, 44, 40, 50, 44, PIX_GRASS_DARK_1);
    og::mapgen::paint_rect(g, 24, 50, 30, 53, PIX_GRASS_DARK_1);
    // The moat: a square band 3 tiles wide around the bailey.
    og::mapgen::paint_rect(g, 17, 17, 46, 46, PIX_WATER1);
    og::mapgen::paint_rect(g, 20, 20, 43, 43, PIX_GRASS1);
    // The castle: outer bailey wall and ring, then the keep and court.
    og::mapgen::paint_rect(g, 22, 22, 41, 41, PIX_WALL2);   // bailey wall
    og::mapgen::paint_rect(g, 23, 23, 40, 40, PIX_COBBLE_1); // bailey ring
    og::mapgen::paint_rect(g, 27, 27, 36, 36, PIX_WALL2);   // keep wall
    og::mapgen::paint_rect(g, 28, 28, 35, 35, PIX_COBBLE_1); // the court
    og::mapgen::paint_rect(g, 30, 30, 33, 33, PIX_CARPET_M); // the dais
    // Gates, two tiles wide at every compass midpoint of both walls.
    // Gates three tiles wide: the hunt AI reaches foes by wall-sliding
    // into openings, and a two-wide mouth on a double-walled castle
    // starves the funnel (unattended sims stalled short of the 6000-tick
    // budget before this widening).
    og::mapgen::paint_rect(g, 30, 22, 32, 22, PIX_COBBLE_1); // bailey north
    og::mapgen::paint_rect(g, 30, 41, 32, 41, PIX_COBBLE_1); // bailey south
    og::mapgen::paint_rect(g, 22, 30, 22, 32, PIX_COBBLE_1); // bailey west
    og::mapgen::paint_rect(g, 41, 30, 41, 32, PIX_COBBLE_1); // bailey east
    og::mapgen::paint_rect(g, 30, 27, 32, 27, PIX_COBBLE_1); // keep north
    og::mapgen::paint_rect(g, 30, 36, 32, 36, PIX_COBBLE_1); // keep south
    og::mapgen::paint_rect(g, 27, 30, 27, 32, PIX_COBBLE_1); // keep west
    og::mapgen::paint_rect(g, 36, 30, 36, 32, PIX_COBBLE_1); // keep east
    // Field copses on the meadow and field rings, all >= 2 tiles clear
    // of every landing marker, pier, grove and causeway.
    og::mapgen::paint_rect(g, 28, 10, 29, 11, PIX_TREE_M1);  // north field
    og::mapgen::paint_rect(g, 37, 12, 38, 13, PIX_TREE_M1);
    og::mapgen::paint_rect(g, 10, 40, 11, 41, PIX_TREE_M1);  // west field
    og::mapgen::paint_rect(g, 52, 24, 53, 25, PIX_TREE_M1);  // east field
    og::mapgen::paint_rect(g, 42, 52, 43, 53, PIX_TREE_M1);  // south field
    og::mapgen::paint_rect(g, 14, 51, 15, 52, PIX_TREE_M1);

    og::mapgen::smooth_world(w);

    // Post-smooth inert tiles: the four moat causeways and the four
    // landing piers running off the beaches into open water.
    // Eight moat crossings: WIDE cardinal causeways plus paved corner
    // plazas. The hunt AI beelines and slides; with a crossing within a
    // short slide of any approach angle, teammates and woken foes FLOW
    // over the moat instead of grinding against the bank in front of
    // the player (the water stays as eight ribbon segments — the
    // submitted "sea in the middle" survives).
    paint_pavement(g, 29, 14, 34, 21); // north causeway
    paint_pavement(g, 29, 42, 34, 49); // south causeway
    paint_pavement(g, 14, 29, 21, 34); // west causeway
    paint_pavement(g, 42, 29, 49, 34); // east causeway
    paint_pavement(g, 17, 17, 22, 22); // corner plazas
    paint_pavement(g, 41, 17, 46, 22);
    paint_pavement(g, 17, 41, 22, 46);
    paint_pavement(g, 41, 41, 46, 46);
    paint_pavement(g, 12, 50, 13, 58); // southwest pier
    paint_pavement(g, 50, 5, 51, 13);  // northeast pier
    paint_pavement(g, 4, 12, 12, 13);  // northwest pier
    paint_pavement(g, 51, 50, 59, 51); // southeast pier

    // --- The garrison (team 1). Every foe is a POSTED guard: the hunt
    // AI (ACT_RANDOM) beelines with no pathfinding, and a moat ring
    // turns distant roamers into walkers jittering at the water's edge.
    // Posts wake at true sight of the charging crew — the submitted
    // design ("WE run at THEM") in engine terms — staged in beats:
    // causeway heads, then the bailey, then the keep, then the throne.
    // ------------------------------------------------------------------
    // Causeway-head sentries, one per compass approach — all bone. The
    // heads are every landing party's FIRST fight, usually one lander
    // alone; the statue-era mix posted orc bruisers on two heads, and a
    // woken orc beats a fresh soldier one-on-one. Skeletons keep the
    // beat (ranged harassment over the approach) and lose the duel the
    // level's contract requires a new team to win. The orcs still live
    // deeper in: the bailey ring, the throne guard, the last wave.
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 31, 13, 1, true);
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 31, 50, 1, true);
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 13, 30, 1, true);
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 50, 30, 1, true);
    // The bailey: posted garrison on opposite ring corners and both
    // gate flanks, plus two immobile arrow turrets watching the north
    // and south gates from the ring's far corners. (The ring held six
    // mobiles when the posts were accidentally frozen statues; with
    // the wake fixed, a causeway fight pulls every in-sight ring guard
    // out through the gate, and six-deep pile-ons wiped fresh landers
    // in the playtest sweeps. Four keeps the ring alive on every side
    // without the grinder.)
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 24, 24, 1, true);
    og::mapgen::place_living(w, FAMILY_ORC, 1, 0, 39, 39, 1, true);
    og::mapgen::place_living(w, FAMILY_ELF, 1, 0, 31, 24, 1, true);
    og::mapgen::place_living(w, FAMILY_ELF, 1, 0, 32, 39, 1, true);
    og::mapgen::place_living(w, FAMILY_TOWER1, 1, 0, 25, 31, 1, true);
    og::mapgen::place_living(w, FAMILY_TOWER1, 1, 0, 38, 32, 1, true);
    // The keep: a bone ward inside three of the four gates — the west
    // gate stands unwarded, a soft way in for a bled crew (the fourth
    // ward joined the same statue-era pile-on budget).
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 31, 29, 1, true);
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 32, 34, 1, true);
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 34, 31, 1, true);
    // The throne: the Sea Wizard between his dais bodyguards.
    // The boss's teleport special is DISABLED (npc_flags bit0): a
    // pressed mage teleports at random, and the one thing worse than a
    // boss without his escape trick is a boss stranded on a reef atoll
    // with the kill-all softlocked behind him.
    // Level 2, down from the statue-era 4: a crew now fights its way
    // through a garrison that fights back and reaches the dais bled;
    // the level-4 bolt grinder ended every playtest run that got there.
    // Two keeps him the strongest mage on the isle (the ward arc keys
    // on that) and beatable by the survivors of an honest assault.
    walker* boss =
        og::mapgen::place_living(w, FAMILY_MAGE, 1, 0, 31, 31, 2,
                                 /*guard=*/true, /*hold_post=*/false,
                                 /*specials_disabled=*/true);
    if (boss != nullptr)
        boss->stats()->name = "Sea Wizard"; // 10 chars: fits the 11-char field
    og::mapgen::place_living(w, FAMILY_ORC, 1, 0, 30, 32, 1, true);
    og::mapgen::place_living(w, FAMILY_ELF, 1, 0, 33, 31, 1, true);
    // Reinforcement waves, dormant behind the walls (WAVE HUD):
    // the bailey relief at tick 500, the throne guard at tick 800. They
    // wake mid-assault, inside the fight the crew is already carrying.
    // Sized for a garrison that FIGHTS: six wave units were budgeted
    // when the posts were accidentally frozen statues; with the wake
    // fixed, the full six landed exactly in a fresh crew's mid-assault
    // collapse window and wiped it in every playtest seed. Three keeps
    // the two-beat wave drama and a clearable dream.
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 26, 26, 1,
                             /*guard=*/true, /*hold_post=*/false,
                             /*specials_disabled=*/false, 500);
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 37, 37, 1,
                             /*guard=*/true, /*hold_post=*/false,
                             /*specials_disabled=*/false, 500);
    og::mapgen::place_living(w, FAMILY_ORC, 1, 0, 34, 29, 1,
                             /*guard=*/true, /*hold_post=*/false,
                             /*specials_disabled=*/false, 800);
    // The wizard's colleges on the field ring, one trickle per art: the
    // mage tower northeast, the bone tent southwest, the elf treehouse
    // by the northwest grove. All on the crew's side of the moat, so
    // their roaming spawns chase over open ground.
    og::mapgen::place_generator(w, FAMILY_TOWER, 1, 0, 43, 11, 1);
    og::mapgen::place_generator(w, FAMILY_TENT, 1, 0, 10, 43, 1);
    og::mapgen::place_generator(w, FAMILY_TREEHOUSE, 1, 0, 19, 11, 1);

    // Hostile posts must WAKE — and they do by construction: hold_post is
    // an explicit place_living parameter now (no team-inferred stamping),
    // and every garrison call above leaves it false, so posts wake at
    // true sight and fight — the submitted design. The immobile
    // watchtowers wake too: on a stationary family ACT_RANDOM turns and
    // fires (walkstep is a facing turn), which is the whole job of an
    // arrow turret. The self-check below still enforces the inversion on
    // the reloaded package: any hostile guard carrying hold-post fails
    // the build.

    // --- The crew (team 0). --------------------------------------------------
    // Sixteen start markers spread around the landing ring — the whole
    // point of the submitted design. Lead FIRST (south beach, facing
    // the causeway); deploy consumes markers in oblist order, so a
    // small crew lands scattered around the island and regroups on the
    // charge, and a full four-player lobby fills every shore. (A
    // clustered southern landing was tried when the garrison learned
    // to fight and measured strictly worse — one blob feeds the whole
    // south garrison at a single causeway funnel, while the scatter
    // splits the defense into local fights the crew can win.)
    og::mapgen::place_start(w, 0, 31, 55); // lead, south landing
    og::mapgen::place_start(w, 0, 31, 7);  // north
    og::mapgen::place_start(w, 0, 55, 31); // east
    og::mapgen::place_start(w, 0, 7, 31);  // west
    og::mapgen::place_start(w, 0, 14, 14); // the four corners of the isle
    og::mapgen::place_start(w, 0, 48, 14);
    og::mapgen::place_start(w, 0, 14, 48);
    og::mapgen::place_start(w, 0, 48, 48);
    og::mapgen::place_start(w, 0, 25, 55); // second-wave beach flanks
    og::mapgen::place_start(w, 0, 38, 55);
    og::mapgen::place_start(w, 0, 25, 7);
    og::mapgen::place_start(w, 0, 38, 7);
    og::mapgen::place_start(w, 0, 7, 25);
    og::mapgen::place_start(w, 0, 7, 38);
    og::mapgen::place_start(w, 0, 55, 25);
    og::mapgen::place_start(w, 0, 55, 38);

    // --- Treasure. -----------------------------------------------------------
    // The magic: potions on the west and east causeway approaches, and
    // a flight potion by the south landing — drink it and cross the
    // moat and the wall face on wings instead of fighting for a gate.
    og::mapgen::place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 16, 33);
    og::mapgen::place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 47, 30);
    og::mapgen::place(w, Order::Treasure, FAMILY_FLIGHT_POTION, 0, 0, 28, 52);
    // Riches hidden in the brambles and out on the pier ends.
    og::mapgen::place(w, Order::Treasure, FAMILY_INVULNERABLE_POTION, 0, 0,
                      14, 20);
    og::mapgen::place(w, Order::Treasure, FAMILY_SPEED_POTION, 0, 0, 48, 42);
    og::mapgen::place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 5, 12);
    og::mapgen::place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 58, 51);
    // Rations along the shore ring and the pier heads.
    og::mapgen::place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 10, 30);
    og::mapgen::place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 53, 33);
    og::mapgen::place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 31, 5);
    og::mapgen::place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 13, 53);
    og::mapgen::place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 50, 10);
    // The wizard's hoard: bailey strongrooms and the court corners.
    og::mapgen::place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 23, 31);
    og::mapgen::place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 40, 32);
    og::mapgen::place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 28, 35);
    og::mapgen::place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 35, 28);
    og::mapgen::place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 28, 28);
    og::mapgen::place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 35, 35);
    // The walk-out exit on the south landing beach, beside the lead
    // marker: clear the isle, then sail on to the next dream (scen 2).
    og::mapgen::place_exit(w, 0, 34, 57, 2);

    // --- Set dressing (after every entity, so scatters keep clearance). -----
    // Torches on both wall rings: corners and gate flanks.
    for (const auto& t : {std::pair{22, 22}, {41, 22}, {22, 41}, {41, 41},
                          {29, 22}, {34, 22}, {29, 41}, {34, 41},
                          {22, 29}, {22, 34}, {41, 29}, {41, 34}})
        og::mapgen::paint_decor(w, 0, t.first, t.second, DECOR_TORCH1);
    for (const auto& t : {std::pair{27, 27}, {36, 27}, {27, 36}, {36, 36},
                          {29, 27}, {34, 27}, {29, 36}, {34, 36}})
        og::mapgen::paint_decor(w, 0, t.first, t.second, DECOR_TORCH1);
    // Braziers on the dais back row behind the wizard.
    og::mapgen::paint_decor(w, 0, 30, 30, DECOR_BRAZIER);
    og::mapgen::paint_decor(w, 0, 33, 30, DECOR_BRAZIER);
    // Old landings: bones where the causeway sentries stand watch.
    og::mapgen::paint_decor(w, 0, 29, 13, DECOR_BONES);
    og::mapgen::paint_decor(w, 0, 34, 50, DECOR_BONES);
    og::mapgen::paint_decor(w, 0, 13, 33, DECOR_BONES);
    og::mapgen::paint_decor(w, 0, 50, 33, DECOR_BONES);
    // The brambles: dense shrub over the three dark-grass groves, light
    // shrub over the whole field, pebbles on the shore meadow, and a
    // sparse boulder scatter across the open field ring.
    og::mapgen::scatter_decor(w, kScatterSeed, 0, 12, 18, 17, 22, 2,
                              DECOR_SHRUB,
                              {og::mapgen::ScatterGround::DarkGrass});
    og::mapgen::scatter_decor(w, kScatterSeed, 0, 44, 40, 50, 44, 2,
                              DECOR_SHRUB,
                              {og::mapgen::ScatterGround::DarkGrass});
    og::mapgen::scatter_decor(w, kScatterSeed, 0, 24, 50, 30, 53, 2,
                              DECOR_SHRUB,
                              {og::mapgen::ScatterGround::DarkGrass});
    og::mapgen::scatter_decor(w, kScatterSeed, 0, 4, 4, 59, 59, 23,
                              DECOR_SHRUB,
                              {og::mapgen::ScatterGround::Grass});
    og::mapgen::scatter_decor(w, kScatterSeed, 0, 4, 4, 59, 59, 13,
                              DECOR_PEBBLES,
                              {og::mapgen::ScatterGround::LightGrass});
    og::mapgen::scatter_boulders(w, kScatterSeed, 0, 4, 4, 59, 59, 41);

    save_level_files(w, 1, "The Raspberry Isle",
                     {
                         "From the dream-log of a young",
                         "commander: an island, the sea",
                         "all around us. We land on the",
                         "shores and charge the middle!",
                         "The Sea Wizard's castle waits",
                         "beyond the moat: breach the",
                         "bailey, storm the keep, and",
                         "take the throne.",
                     },
                     2, 8000);
}

// --- Scen 2: THE CASTLE OF GOLD. ---------------------------------------------
// A yellow castle that is OURS, four strongrooms of gold inside it, and a
// faerie war raging all around. The faeries pour in from the map's edges
// and want in: the gate opens for no faerie until the faeries between them
// carry all twelve keys scattered on the shores. Inside, one of us keeps
// the castle: every gold bar taken from the strongrooms wakes six ghosts
// for our side at the Ghost Tent (the blue circle east of the walls), the
// last bar seven. Nobody teleports into the castle. Two rivers cut the
// faerie shores off the battlefield. Kill-all, then the throne on the hall
// dais is the way out.
//
// Geometry on a 64x64 grid (tiles, inclusive):
//   x0-4 / x59-63 / y0-4 / y59-63   the light-grass faerie shores
//   y5-6 and y57-58                  two rivers, five paved bridges each
//   17..46 x 14..43                  the castle ring, 3 walls thick (a
//                                    faerie blink of 24+2L px cannot hop
//                                    48 px of wall up to level 11)
//   20..43 x 17..40                  the cobbled court; four 8x8
//                                    strongrooms in its corners, each with
//                                    a 3-door row facing the court
//   30..32 x 41..43                  the gate tunnel, 3 doors at y43
//   29..33 x 44..60                  the causeway south over the bridge
// The gold and key beats live in the dream script
// (packs/imaginations.dreams/scripts/gold_castle.lua): the gate doors are
// TEAM 0 (the crew and its ghosts pass their own gate without opening it)
// at key level 30, which no key on the map opens, so only the script drops
// them; the strongroom doors are neutral (team 4) at key 13, the castle key
// lying one step north of the keeper's start.
//
// Exit destination: the throne leads on to scen 3.
constexpr int kCastleOfGoldExit = 3;

void build_castle_of_gold()
{
    LevelRuntimeData level(2, true, &headless_level_data_hooks());
    GameWorld& w = level.world();
    og::mapgen::init_world(w, 1, 64, 64);
    PixieData& g = w.grid;

    // Terrain (genre tiles only; the autotiler shapes every face).
    // The faerie shores, light meadow on every edge.
    og::mapgen::paint_rect(g, 0, 0, 63, 4, PIX_GRASS_LIGHT_1);
    og::mapgen::paint_rect(g, 0, 59, 63, 63, PIX_GRASS_LIGHT_1);
    og::mapgen::paint_rect(g, 0, 7, 4, 56, PIX_GRASS_LIGHT_1);
    og::mapgen::paint_rect(g, 59, 7, 63, 56, PIX_GRASS_LIGHT_1);
    // The two rivers between the shores and the battlefield.
    og::mapgen::paint_rect(g, 0, 5, 63, 6, PIX_WATER1);
    og::mapgen::paint_rect(g, 0, 57, 63, 58, PIX_WATER1);
    // The grove (west) and the Ghost Tent's dark ring (east).
    og::mapgen::paint_rect(g, 3, 18, 8, 23, PIX_GRASS_DARK_1);
    og::mapgen::paint_ring(g, 52.5, 33.5, 0.0, 2.6, PIX_GRASS_DARK_1);
    // The castle: a 3-thick ring and its court.
    og::mapgen::paint_rect(g, 17, 14, 46, 43, PIX_WALL2);
    og::mapgen::paint_rect(g, 20, 17, 43, 40, PIX_COBBLE_1);
    // Four strongrooms in the court corners, yellow sand floors.
    for (const auto& room : {std::pair{20, 17}, {36, 17}, {20, 33}, {36, 33}})
    {
        og::mapgen::paint_rect(g, room.first, room.second, room.first + 7,
                               room.second + 7, PIX_WALL2);
        og::mapgen::paint_rect(g, room.first + 1, room.second + 1,
                               room.first + 6, room.second + 6, PIX_DIRT_1);
    }
    // Door mouths, 3 wide, through each strongroom's court-facing wall:
    // the top rooms open down, the bottom rooms open up.
    og::mapgen::paint_rect(g, 22, 24, 24, 24, PIX_COBBLE_1);
    og::mapgen::paint_rect(g, 39, 24, 41, 24, PIX_COBBLE_1);
    og::mapgen::paint_rect(g, 22, 33, 24, 33, PIX_COBBLE_1);
    og::mapgen::paint_rect(g, 39, 33, 41, 33, PIX_COBBLE_1);
    // The hall dais, where the throne (the exit) stands.
    og::mapgen::paint_rect(g, 29, 26, 33, 30, PIX_CARPET_M);
    // The gate tunnel (3 wide through the 3-thick wall) and the causeway.
    og::mapgen::paint_rect(g, 30, 41, 32, 43, PIX_COBBLE_1);
    og::mapgen::paint_rect(g, 29, 44, 33, 60, PIX_COBBLE_1);
    // The counting house (the yellow box northeast), open to the south.
    og::mapgen::paint_rect(g, 54, 8, 60, 13, PIX_WALL2);
    og::mapgen::paint_rect(g, 55, 9, 59, 12, PIX_DIRT_1);
    og::mapgen::paint_rect(g, 56, 13, 58, 13, PIX_COBBLE_1);

    og::mapgen::smooth_world(w);

    // Post-smooth inert pavement: five bridges over each river, every one
    // >= 4 wide and no river cell more than 10 tiles from one (the beeline
    // AI slides into a crossing), the pad square northeast and the field
    // camp southwest.
    for (int x0 : {6, 20, 29, 40, 54})
    {
        const int x1 = (x0 == 29) ? 33 : x0 + 3;
        paint_pavement(g, x0, 4, x1, 7);
        paint_pavement(g, x0, 56, x1, 59);
    }
    paint_pavement(g, 56, 15, 60, 18);
    paint_pavement(g, 11, 48, 16, 53);

    // --- The faerie war (team 1). Faeries only; every one a roamer that
    // comes to the war — flyers cross the rivers, the castle walls stop
    // them, and the dream script stands a faerie grinding at a wall back
    // down onto a post (the engine's own sight test re-wakes it). Four
    // wake at once; four waves follow from the same shores (WAVE HUD).
    auto faerie = [&](int tx, int ty, int lvl, int delay, const char* name)
    {
        walker* f = og::mapgen::place_living(
            w, FAMILY_FAERIE, 1, 0, tx, ty, lvl, /*guard=*/false,
            /*hold_post=*/false, /*specials_disabled=*/false, delay);
        if (f != nullptr && name != nullptr)
            f->stats()->name = name; // <= 11 chars
    };
    const std::pair<int, int> wave1_spots[] = {
        {12, 2}, {50, 2}, {12, 61}, {50, 61}, {2, 20}, {61, 42}};
    const std::pair<int, int> wave2_spots[] = {
        {18, 2}, {44, 2}, {18, 61}, {44, 61}, {2, 42}, {61, 20}};
    for (const auto& s : {std::pair{31, 2}, {31, 61}, {2, 31}, {61, 31}})
        faerie(s.first, s.second, 1, 0, nullptr);
    for (const auto& s : wave1_spots)
        faerie(s.first, s.second, 1, 400, nullptr);
    for (const auto& s : wave2_spots)
        faerie(s.first, s.second, 1, 900, nullptr);
    for (const auto& s : wave1_spots) // wave 3 crosses the same keys
        faerie(s.first, s.second, 2, 1500, "Fae Knight");
    // The finale: the Fae Queen and her four knights on the south shore.
    faerie(31, 62, 3, 2200, "Fae Queen");
    for (const auto& s : {std::pair{27, 61}, {35, 61}, {23, 60}, {39, 60}})
        faerie(s.first, s.second, 2, 2200, "Fae Knight");

    // --- The crew (team 0): twelve markers, the keeper FIRST, inside by
    // the gate; the rest on the battlefield around the castle.
    og::mapgen::place_start(w, 0, 31, 38); // lead: the castle-keeper
    og::mapgen::place_start(w, 0, 31, 47); // the causeway foot
    og::mapgen::place_start(w, 0, 27, 47);
    og::mapgen::place_start(w, 0, 35, 47);
    og::mapgen::place_start(w, 0, 12, 30); // west field
    og::mapgen::place_start(w, 0, 50, 28); // east field
    og::mapgen::place_start(w, 0, 31, 10); // north
    og::mapgen::place_start(w, 0, 31, 53); // south
    og::mapgen::place_start(w, 0, 12, 12); // the corners
    og::mapgen::place_start(w, 0, 50, 12);
    og::mapgen::place_start(w, 0, 12, 44);
    og::mapgen::place_start(w, 0, 50, 44);

    // --- Doors. The gate: team 0 at key 30 (no such key exists, so only
    // the script opens it). The strongrooms: neutral team 4 at key 13.
    auto door = [&](int tx, int ty, int team, int key)
    {
        walker* d = og::mapgen::place(w, Order::Weapon, FAMILY_DOOR, team, 0,
                                      tx, ty);
        if (d != nullptr)
            d->stats()->set_level(static_cast<short>(key));
    };
    for (int x = 30; x <= 32; ++x)
        door(x, 43, 0, 30);
    for (int x : {22, 23, 24, 39, 40, 41})
    {
        door(x, 24, 4, 13);
        door(x, 33, 4, 13);
    }

    // --- Keys. The castle key (13) one step north of the keeper; the
    // twelve shore keys (1..12), each two tiles inward of a wave spawn on
    // its line to the castle.
    auto treasure = [&](int family, int tx, int ty, int lvl)
    {
        walker* t = og::mapgen::place(w, Order::Treasure, family, 0, 0, tx, ty);
        if (t != nullptr && lvl > 0)
            t->stats()->set_level(static_cast<short>(lvl));
    };
    treasure(FAMILY_KEY, 31, 36, 13);
    {
        const std::pair<int, int> shore_keys[] = {
            {12, 4}, {50, 4}, {12, 59}, {50, 59}, {4, 20}, {59, 42},
            {18, 4}, {44, 4}, {18, 59}, {44, 59}, {4, 42}, {59, 20}};
        int key = 1;
        for (const auto& k : shore_keys)
            treasure(FAMILY_KEY, k.first, k.second, key++);
    }
    // --- Teleporters (pairs in fxlist order): channel 1 joins the pad
    // square to the Ghost Tent, channel 2 the field camp to the grove.
    // None inside the castle.
    treasure(FAMILY_TELEPORTER, 58, 16, 1);
    treasure(FAMILY_TELEPORTER, 52, 33, 1);
    treasure(FAMILY_TELEPORTER, 13, 50, 2);
    treasure(FAMILY_TELEPORTER, 6, 21, 2);

    // --- The castle gold: two bars per strongroom, the ghost engine.
    for (const auto& bar : {std::pair{22, 19}, {25, 22}, {38, 19}, {41, 22},
                            {22, 38}, {25, 35}, {38, 38}, {41, 35}})
        treasure(FAMILY_GOLD_BAR, bar.first, bar.second, 0);
    treasure(FAMILY_MAGIC_POTION, 39, 21, 0);
    treasure(FAMILY_INVULNERABLE_POTION, 23, 36, 0);
    treasure(FAMILY_SPEED_POTION, 40, 37, 0);
    treasure(FAMILY_DRUMSTICK, 28, 32, 0);
    treasure(FAMILY_DRUMSTICK, 35, 32, 0);
    // The counting house banks the classic way (outside the castle).
    treasure(FAMILY_GOLD_BAR, 57, 10, 0);
    treasure(FAMILY_SILVER_BAR, 55, 10, 0);
    treasure(FAMILY_SILVER_BAR, 59, 12, 0);
    treasure(FAMILY_SPEED_POTION, 56, 11, 0);
    // The camp, the grove, the shores, and wings by the tent.
    treasure(FAMILY_DRUMSTICK, 12, 49, 0);
    treasure(FAMILY_DRUMSTICK, 15, 52, 0);
    treasure(FAMILY_MAGIC_POTION, 14, 48, 0);
    treasure(FAMILY_SILVER_BAR, 5, 19, 0);
    treasure(FAMILY_DRUMSTICK, 7, 22, 0);
    for (const auto& r : {std::pair{31, 8}, {31, 54}, {8, 31}, {55, 31}})
        treasure(FAMILY_DRUMSTICK, r.first, r.second, 0);
    treasure(FAMILY_FLIGHT_POTION, 50, 36, 0);
    // The throne on the hall dais is the way out.
    og::mapgen::place_exit(w, 0, 31, 28, kCastleOfGoldExit);

    // --- Set dressing (after every entity, so scatters keep clearance). -----
    // Torches on the ring's wall faces and the strongroom walls (wall
    // tiles only: a torch blocks ground).
    for (int x = 19; x <= 44; x += 4)
    {
        og::mapgen::paint_decor(w, 0, x, 14, DECOR_TORCH1);
        if (x < 29 || x > 33)
            og::mapgen::paint_decor(w, 0, x, 43, DECOR_TORCH1);
    }
    for (int y = 18; y <= 40; y += 4)
    {
        og::mapgen::paint_decor(w, 0, 17, y, DECOR_TORCH1);
        og::mapgen::paint_decor(w, 0, 46, y, DECOR_TORCH1);
    }
    for (const auto& t : {std::pair{20, 20}, {27, 20}, {36, 20}, {43, 20},
                          {20, 37}, {27, 37}, {36, 37}, {43, 37}})
        og::mapgen::paint_decor(w, 0, t.first, t.second, DECOR_TORCH1);
    // Braziers: the dais corners, the gate mouth, the camp, the tent.
    for (const auto& b : {std::pair{29, 26}, {33, 26}, {29, 30}, {33, 30},
                          {29, 44}, {33, 44}, {11, 48}, {16, 48}, {52, 30}})
        og::mapgen::paint_decor(w, 0, b.first, b.second, DECOR_BRAZIER);
    // Two columns flank the causeway, off its funnel.
    og::mapgen::paint_decor(w, 0, 28, 46, DECOR_COLUMN_BOTTOM);
    og::mapgen::paint_decor(w, 0, 34, 46, DECOR_COLUMN_BOTTOM);
    // The Ghost Tent is ringed with bones around its pad.
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx != 0 || dy != 0)
                og::mapgen::paint_decor(w, 0, 52 + dx, 33 + dy, DECOR_BONES);
    og::mapgen::scatter_decor(w, kScatterSeed, 0, 3, 18, 8, 23, 2, DECOR_SHRUB,
                              {og::mapgen::ScatterGround::DarkGrass});
    og::mapgen::scatter_decor(w, kScatterSeed, 0, 50, 31, 55, 36, 3,
                              DECOR_SHRUB,
                              {og::mapgen::ScatterGround::DarkGrass});
    og::mapgen::scatter_decor(w, kScatterSeed, 0, 6, 8, 57, 55, 23,
                              DECOR_SHRUB,
                              {og::mapgen::ScatterGround::Grass});
    og::mapgen::scatter_decor(w, kScatterSeed, 0, 0, 0, 63, 63, 11,
                              DECOR_PEBBLES,
                              {og::mapgen::ScatterGround::LightGrass});
    // Boulders on the open battlefield only — never in the court, on the
    // causeway or on a bridge.
    og::mapgen::scatter_boulders(w, kScatterSeed, 0, 6, 8, 15, 55, 47);
    og::mapgen::scatter_boulders(w, kScatterSeed, 0, 48, 19, 57, 55, 47);
    og::mapgen::scatter_boulders(w, kScatterSeed, 0, 17, 8, 52, 12, 47);
    og::mapgen::scatter_boulders(w, kScatterSeed, 0, 17, 45, 27, 55, 47);
    og::mapgen::scatter_boulders(w, kScatterSeed, 0, 35, 45, 46, 55, 47);

    save_level_files(w, 2, "The Castle of Gold",
                     {
                         "From the dream-log of a young",
                         "commander: a castle of gold,",
                         "and it is OURS. The faeries",
                         "want in: they must find all",
                         "twelve keys first. One of us",
                         "keeps the castle: every gold",
                         "bar wakes six ghosts for our",
                         "side. Beat the faerie war!",
                     },
                     3, 9000);
}

// --- Scen 3: THE ASTEROID CASTLE. --------------------------------------------
// Our castle stands on an asteroid in black space, spikes all around its
// walls. Bad guys ride the space wind (stardust drifts force the snow
// weather) and think the castle is theirs; the spike band catches them.
// The Big One, a golem, sits on the Balloon Rock: a striped deck floating
// on floor 1 over a pit of black space, reached by a 3-wide Z-stair on
// the north ledge. The key to the big door hangs under the rock's mast.
//
// Floor 0, 64x64: the rounded asteroid (rock rim, grass) on PIX_VOID1
// space. West side: the tall castle (x 8..23, y 15..46, walls 2 thick)
// with a 3-wide east gate (y 29..31), the crown V of pavement in the
// north hall, and the big door (three team-4 doors at key 3, row 43)
// sealing the striped vestibule that holds the exit pad. A 2-wide
// jagged spike band hugs the outer wall, broken only by the gate apron:
// jagged ground is too sharp for anyone to walk or fly over, so raiders
// pile up against it and the script's spikes bite them there.
// Yard posts east of the castle, the pen (an ash square with boulder
// faces) in the southeast, craters, three stardust drifts, and the
// Balloon pit (void under the whole deck except the three up-stairs).
// Floor 1: all AIR but the striped 7x7 deck (x 46..52, y 7..13).
//
// The dream script (packs/imaginations.dreams/scripts/asteroid.lua)
// carries the beats: the spikes bite every hostile against the band, the Big
// One never fires while on the rock and wakes when the crew comes within
// nine tiles, then walks off the deck and falls (hurt and slow), and the
// raider waves are narrated as they wake. Exit: the newest level loops
// home to 1.
constexpr int kAsteroidCastleExit = 1;

// Floor-0 cells inside the castle block (walls and court).
bool in_asteroid_castle(int x, int y)
{
    return x >= 8 && x <= 23 && y >= 15 && y <= 46;
}

void build_asteroid_castle()
{
    LevelRuntimeData level(3, true, &headless_level_data_hooks());
    GameWorld& w = level.world();
    og::mapgen::init_world(w, 2, 64, 64);
    PixieData& g0 = w.grid_for_floor(0);
    PixieData& g1 = w.grid_for_floor(1);

    // --- Floor 0 genre paint: the rock rim and its grass.
    og::mapgen::paint_rect(g0, 3, 4, 60, 59, PIX_DIRT_1);
    og::mapgen::paint_rect(g0, 5, 6, 58, 57, PIX_GRASS1);
    og::mapgen::paint_rect(g0, 28, 26, 33, 34, PIX_GRASS_DARK_1);
    og::mapgen::paint_rect(g0, 36, 16, 40, 19, PIX_GRASS_DARK_1);
    og::mapgen::paint_rect(g0, 46, 45, 53, 52, PIX_GRASS_DARK_1);
    // The castle: walls 2 thick, the court, the east gate (3 wide, on the
    // court's centre row), the big-door row and the vestibule behind it.
    og::mapgen::paint_rect(g0, 8, 15, 23, 46, PIX_WALL2);
    og::mapgen::paint_rect(g0, 10, 17, 21, 42, PIX_COBBLE_1);
    og::mapgen::paint_rect(g0, 22, 29, 23, 31, PIX_COBBLE_1);
    og::mapgen::paint_rect(g0, 14, 43, 16, 43, PIX_COBBLE_1);
    og::mapgen::paint_rect(g0, 14, 44, 16, 45, PIX_COBBLE_1);
    // --- Floor 1 genre paint: the deck.
    og::mapgen::paint_rect(g1, 46, 7, 52, 13, PIX_COBBLE_1);

    og::mapgen::smooth_world(w);

    // --- Floor 0, inert tiles (after smoothing).
    // The spike band: two tiles of jagged ground all around the walls
    // (impassable to walkers and flyers alike).
    for (int y = 13; y <= 48; ++y)
        for (int x = 6; x <= 25; ++x)
            if (!in_asteroid_castle(x, y))
                og::mapgen::paint(
                    g0, x, y,
                    static_cast<unsigned char>(PIX_JAGGED_GROUND_1 +
                                               (x + y) % 4));
    // The gate apron: the band's one spike-free mouth.
    og::mapgen::paint_rect(g0, 24, 29, 25, 31, PIX_PAVEMENT1);
    // The crown V in the north hall.
    for (const auto& v : {std::pair{11, 17}, {20, 17}, {12, 18}, {19, 18},
                          {13, 19}, {18, 19}, {14, 20}, {17, 20}, {15, 21},
                          {16, 21}})
        og::mapgen::paint(g0, v.first, v.second, PIX_PAVEMENT3);
    // The striped vestibule.
    og::mapgen::paint_rect(g0, 14, 44, 16, 44, PIX_PAVEMENT1);
    og::mapgen::paint_rect(g0, 14, 45, 16, 45, PIX_PAVEMENT2);
    // Stardust drifts, the space wind (snow weather past 40 tiles).
    for (const auto& d : {std::pair{28.0, 8.0}, {12.0, 52.0}, {56.0, 52.0}})
        og::mapgen::paint_ring(g0, d.first, d.second, 0.0, 3.2, PIX_SNOW1);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (g0.data[static_cast<std::size_t>(x + y * 64)] == PIX_SNOW1 &&
                (x + y) % 3 == 0)
                og::mapgen::paint(g0, x, y, PIX_SNOW2);
    // Craters (ash: jagged means spikes on this map).
    for (const auto& c : {std::pair{33.0, 10.0}, {36.0, 50.0}, {30.0, 44.0}})
        og::mapgen::paint_ring(g0, c.first, c.second, 0.0, 2.2, PIX_ASH1);
    // The rainbow apron south of the big door.
    {
        static constexpr unsigned char kStripes[3] = {
            PIX_PAVEMENT1, PIX_PAVEMENT2, PIX_PAVEMENT3};
        for (int y = 49; y <= 52; ++y)
            og::mapgen::paint_rect(g0, 13, y, 17, y, kStripes[(y - 49) % 3]);
    }
    // The pen: an ash checker outlined in pavement, no walls.
    for (int y = 45; y <= 52; ++y)
        for (int x = 46; x <= 53; ++x)
            og::mapgen::paint(g0, x, y, ((x + y) % 2) ? PIX_ASH2 : PIX_ASH1);
    for (int x = 45; x <= 54; ++x)
    {
        og::mapgen::paint(g0, x, 44, PIX_PAVEMENT2);
        og::mapgen::paint(g0, x, 53, PIX_PAVEMENT2);
    }
    for (int y = 44; y <= 53; ++y)
    {
        og::mapgen::paint(g0, 45, y, PIX_PAVEMENT2);
        og::mapgen::paint(g0, 54, y, PIX_PAVEMENT2);
    }
    // Space: the void all around, with rounded corners.
    og::mapgen::paint_rect(g0, 0, 0, 63, 3, PIX_VOID1);
    og::mapgen::paint_rect(g0, 0, 60, 63, 63, PIX_VOID1);
    og::mapgen::paint_rect(g0, 0, 4, 2, 59, PIX_VOID1);
    og::mapgen::paint_rect(g0, 61, 4, 63, 59, PIX_VOID1);
    for (const auto& c : {std::pair{3.0, 4.0}, {60.0, 4.0}, {3.0, 59.0},
                          {60.0, 59.0}})
        og::mapgen::paint_ring(g0, c.first, c.second, 0.0, 6.0, PIX_VOID1);
    // The Balloon pit: space under the whole deck but the up-stairs, so
    // nothing ever stands under the Big One.
    og::mapgen::paint_rect(g0, 46, 7, 52, 13, PIX_VOID1);

    // --- Floor 1, inert: sky everywhere, the striped deck.
    og::mapgen::paint_rect(g1, 0, 0, 63, 63, PIX_AIR);
    for (int y = 7; y <= 13; ++y)
        og::mapgen::paint_rect(
            g1, 46, y, 52, y,
            static_cast<unsigned char>((y - 7) % 3 == 0   ? PIX_PAVEMENT1
                                       : (y - 7) % 3 == 1 ? PIX_PAVEMENT2
                                                          : PIX_PAVEMENT3));
    // The 3-wide Z-stair on the north ledge: up on floor 0, down on 1.
    for (int x = 48; x <= 50; ++x)
        og::mapgen::stair_pair(w, 0, x, 7);

    // The space wind needs its drifts: 40+ snow tiles force snow weather.
    {
        int snow = 0;
        for (std::size_t c = 0; c < 64u * 64u; ++c)
            if (g0.data[c] == PIX_SNOW1 || g0.data[c] == PIX_SNOW2)
                ++snow;
        if (snow < 40)
            fail(std::format("scen3: only {} stardust tiles; the snow "
                             "weather needs 40", snow));
    }

    // --- The bad guys (team 1). Yard posts and the pen are POSTED guards
    // (they wake at true sight); the waves roam from the rim and come to
    // the castle, piling up against the spikes or finding the gate.
    auto post = [&](int family, int tx, int ty)
    {
        og::mapgen::place_living(w, family, 1, 0, tx, ty, 1, /*guard=*/true);
    };
    post(FAMILY_SKELETON, 30, 30); // the gate watcher
    post(FAMILY_ORC, 34, 24);
    post(FAMILY_ORC, 34, 36);
    post(FAMILY_ELF, 40, 30);
    post(FAMILY_ARCHER, 40, 20);
    post(FAMILY_SKELETON, 40, 40);
    post(FAMILY_ELF, 44, 16); // the Balloon Rock approach
    post(FAMILY_SKELETON, 49, 48); // the pen
    post(FAMILY_SMALL_SLIME, 47, 51);
    post(FAMILY_SMALL_SLIME, 52, 51);
    // The Big One on the Balloon Rock (floor 1), awake and visible.
    {
        walker* golem = og::mapgen::place_living(w, FAMILY_GOLEM, 1, 1, 47,
                                                 10, 1, /*guard=*/true);
        if (golem != nullptr)
            golem->stats()->name = "Big One";
    }
    auto raider = [&](int family, int tx, int ty, int lvl, int delay)
    {
        return og::mapgen::place_living(w, family, 1, 0, tx, ty, lvl,
                                        /*guard=*/false, /*hold_post=*/false,
                                        /*specials_disabled=*/false, delay);
    };
    // Wave 1, the first gust (east rim).
    raider(FAMILY_ORC, 57, 26, 1, 300);
    raider(FAMILY_ORC, 57, 34, 1, 300);
    raider(FAMILY_FAERIE, 56, 22, 1, 300);
    raider(FAMILY_FAERIE, 56, 38, 1, 300);
    // Wave 2, falling stars (south rim).
    raider(FAMILY_SKELETON, 20, 56, 1, 800);
    raider(FAMILY_SKELETON, 30, 57, 1, 800);
    raider(FAMILY_ELF, 40, 56, 1, 800);
    // Wave 3, the captain's band (southeast rim).
    if (walker* captain = raider(FAMILY_BIG_ORC, 57, 46, 2, 1400))
        captain->stats()->name = "Black Dash";
    raider(FAMILY_ORC, 56, 50, 1, 1400);
    raider(FAMILY_ORC, 58, 42, 1, 1400);
    raider(FAMILY_FAERIE, 54, 56, 1, 1400);

    // --- Team 0: the Purple Man holds the ground beside the east gate.
    if (walker* ally = og::mapgen::place_living(
            w, FAMILY_BARBARIAN, 0, 0, 26, 27, 2, /*guard=*/true,
            /*hold_post=*/true))
        ally->stats()->name = "Purple Man";

    // --- The crew: sixteen markers in the court, the lead first.
    for (const auto& s : {std::pair{15, 30}, {12, 30}, {18, 30}, {15, 27},
                          {15, 33}, {12, 27}, {18, 27}, {12, 33}, {18, 33},
                          {15, 36}, {12, 36}, {18, 36}, {15, 24}, {12, 24},
                          {18, 24}, {15, 39}})
        og::mapgen::place_start(w, 0, s.first, s.second);

    // --- The big door: three neutral doors at key 3.
    for (int x = 14; x <= 16; ++x)
        if (walker* d = og::mapgen::place(w, Order::Weapon, FAMILY_DOOR, 4, 0,
                                          x, 43))
            d->stats()->set_level(3);

    // --- Treasure.
    auto treasure = [&](int family, int floor, int tx, int ty, int lvl)
    {
        walker* t =
            og::mapgen::place(w, Order::Treasure, family, 0, floor, tx, ty);
        if (t != nullptr && lvl > 0)
            t->stats()->set_level(static_cast<short>(lvl));
    };
    treasure(FAMILY_KEY, 1, 51, 10, 3); // under the mast
    for (const auto& b : {std::pair{11, 20}, {20, 20}, {10, 25}, {21, 25},
                          {10, 35}, {21, 35}, {11, 41}, {20, 41}, {49, 46},
                          {46, 49}, {53, 49}})
        treasure(FAMILY_GOLD_BAR, 0, b.first, b.second, 0);
    treasure(FAMILY_INVULNERABLE_POTION, 0, 15, 18, 0); // the crown's heart
    treasure(FAMILY_MAGIC_POTION, 0, 30, 24, 0);
    for (const auto& d : {std::pair{11, 30}, {20, 30}, {30, 36}, {44, 6}})
        treasure(FAMILY_DRUMSTICK, 0, d.first, d.second, 0);
    treasure(FAMILY_SPEED_POTION, 1, 46, 13, 0);
    og::mapgen::place_exit(w, 0, 15, 45, kAsteroidCastleExit);

    // --- Set dressing (last).
    for (const auto& t : {std::pair{8, 15}, {23, 15}, {8, 46}, {23, 46},
                          {22, 28}, {22, 32}, {13, 43}, {17, 43}, {12, 16},
                          {15, 16}, {19, 16}})
        og::mapgen::paint_decor(w, 0, t.first, t.second, DECOR_TORCH1);
    og::mapgen::paint_decor(w, 0, 13, 42, DECOR_BRAZIER);
    og::mapgen::paint_decor(w, 0, 17, 42, DECOR_BRAZIER);
    for (const auto& b : {std::pair{24, 20}, {25, 38}, {15, 13}, {6, 30},
                          {16, 48}})
        og::mapgen::paint_decor(w, 0, b.first, b.second, DECOR_BONES);
    og::mapgen::paint_decor(w, 0, 46, 46, DECOR_BOULDER_1);
    og::mapgen::paint_decor(w, 0, 53, 46, DECOR_BOULDER_2);
    og::mapgen::paint_decor(w, 0, 46, 52, DECOR_BOULDER_3);
    og::mapgen::paint_decor(w, 0, 53, 52, DECOR_BOULDER_4);
    // The mast on the Balloon Rock.
    og::mapgen::paint_decor(w, 1, 52, 8, DECOR_COLUMN_TOP);
    og::mapgen::paint_decor(w, 1, 52, 9, DECOR_COLUMN_BOTTOM);
    // Boulders on the open yard, never in the landing ring around the
    // pit (44..54, 5..15), the pen or the gate apron.
    og::mapgen::scatter_boulders(w, kScatterSeed, 0, 27, 16, 43, 58, 37);
    og::mapgen::scatter_boulders(w, kScatterSeed, 0, 44, 16, 58, 42, 37);
    og::mapgen::scatter_boulders(w, kScatterSeed, 0, 55, 5, 58, 15, 37);
    og::mapgen::scatter_boulders(w, kScatterSeed, 0, 27, 6, 43, 15, 37);
    og::mapgen::scatter_decor(w, kScatterSeed, 0, 0, 0, 63, 63, 9,
                              DECOR_PEBBLES,
                              {og::mapgen::ScatterGround::Dirt});

    save_level_files(w, 3, "The Asteroid Castle",
                     {
                         "From the dream-log of a young",
                         "commander: our castle stands on",
                         "an asteroid, spikes all around.",
                         "Bad guys ride the space wind and",
                         "think it is their castle. The",
                         "spikes catch them. The Big One",
                         "sits on the Balloon Rock. Make",
                         "him fall, take the key, go home.",
                     },
                     3, 9000);
}

void write_campaign_yaml(const std::string& path)
{
    std::ofstream out(path);
    out << "format_version:  1\n"
        << "title:           Imaginations\n"
        << "version:         1\n"
        << "first_level:     1\n"
        << "suggested_power: 0\n"
        << "authors:         OpenGlad\n"
        << "contributors:    \n"
        << "\n"
        << "description:     |\n"
        << "    Levels dreamed up by young\n"
        << "    commanders and made real:\n"
        << "    every scenario here began\n"
        << "    as a kid's idea for a\n"
        << "    battle. The dream-log\n"
        << "    grows as new ideas arrive.\n";
    if (!out)
        fail(std::format("cannot write {}", path));
}

// A 32x32 icon: the isle itself — green island with a sand rim in open
// blue sea, the moat ring, a grey keep with a red banner.
void write_icon(const std::string& path)
{
    constexpr int kSize = 32;
    PixieData icon = og::mapgen::make_grid(kSize, kSize, 66); // open sea
    const double cx = 15.5;
    const double cy = 15.5;
    auto ring = [&](double r0, double r1, unsigned char c)
    { og::mapgen::paint_ring(icon, cx, cy, r0, r1, c); };
    ring(13.5, 100.0, 218); // deep water at the rim
    ring(10.5, 12.0, 84);   // the sand rim
    ring(0.0, 10.5, 59);    // island grass
    ring(6.5, 8.0, 66);     // the moat
    // The keep.
    og::mapgen::paint_rect(icon, 12, 12, 19, 19, 22);
    for (int x = 12; x <= 19; x += 2)
        og::mapgen::paint(icon, x, 12, 27); // crenellation
    og::mapgen::paint_rect(icon, 14, 14, 17, 17, 27);
    // Causeway south, and the banner.
    og::mapgen::paint_rect(icon, 15, 20, 16, 23, 84);
    og::mapgen::paint(icon, 15, 10, 15);
    og::mapgen::paint(icon, 15, 11, 15);
    og::mapgen::paint_rect(icon, 16, 10, 17, 10, 41);
    if (!write_pixie_png(path.c_str(), icon))
        fail(std::format("cannot write {}", path));
}

// --- Self-check: reload the packaged level and audit everything. -------------
struct ExpectedLevel
{
    int id;
    int floors;
    const char* title;
    int start_markers;
    int team0_livings;
    int team0_generators;
    int team1_livings;
    int team1_generators;
    int delayed_spawns;
    int specials_disabled;
    std::vector<int> exit_destinations;
};

void self_check_level(const ExpectedLevel& ex, const std::set<int>& registered)
{
    LevelRuntimeData level(ex.id, true, &headless_level_data_hooks());
    if (!level.load())
    {
        fail(std::format("self-check: scen{} failed to load", ex.id));
        return;
    }
    GameWorld& world = level.world();
    if (world.floor_count() != ex.floors)
        fail(std::format("self-check scen{}: floor_count {} != expected {}",
                         ex.id, world.floor_count(), ex.floors));
    if (world.title != ex.title)
        fail(std::format("self-check scen{}: title '{}' != '{}'", ex.id,
                         world.title, ex.title));
    for (const std::string& line : level.description)
        if (line.size() > kBriefingLineBudget)
            fail(std::format("self-check scen{}: briefing line '{}' overflows "
                             "the {}-char budget", ex.id, line,
                             kBriefingLineBudget));

    // Army audit: exact per-team living/generator/marker counts, the v10
    // per-NPC extras round-trip, allied guards hold post, and headroom
    // under the engine's living cap.
    int livings[MAX_TEAM + 1] = {};
    int generators[MAX_TEAM + 1] = {};
    int total_livings = 0;
    int starts = 0;
    int delayed = 0;
    int no_specials = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->spawn_delay() > 0)
            ++delayed;
        if (ob->specials_disabled())
            ++no_specials;
        if (ob->query_order() == Order::Living && ob->team_num() <= 1 &&
            ob->act_type() == ACT_GUARD && ob->team_num() == 0 &&
            !ob->guard_hold_post())
            fail(std::format("self-check scen{}: allied guard (family {}) "
                             "must hold post", ex.id,
                             static_cast<int>(ob->family())));
        // The garrison rule, inverted: a HOSTILE post that holds is a
        // statue — act_guard never wakes it and its undirected fire
        // command never releases, the shipped sit-and-twitch bug. Every
        // enemy guard must carry hold-post OFF so true sight wakes it.
        if (ob->query_order() == Order::Living && ob->team_num() >= 1 &&
            ob->act_type() == ACT_GUARD && ob->guard_hold_post())
            fail(std::format("self-check scen{}: hostile guard (family {}) "
                             "holds post — garrison posts must wake at "
                             "sight", ex.id,
                             static_cast<int>(ob->family())));
        const int team = ob->team_num();
        if (team < 0 || team > MAX_TEAM)
            continue;
        if (ob->query_order() == Order::Living)
        {
            ++livings[team];
            ++total_livings;
        }
        else if (ob->query_order() == Order::Generator)
            ++generators[team];
        else if (ob->query_order() == Order::Special &&
                 ob->family() == FAMILY_RESERVED_TEAM && team == 0)
            ++starts;
    }
    if (starts != ex.start_markers)
        fail(std::format("self-check scen{}: {} start markers, expected {}",
                         ex.id, starts, ex.start_markers));
    if (livings[0] != ex.team0_livings || generators[0] != ex.team0_generators)
        fail(std::format("self-check scen{}: team 0 has {} livings / {} "
                         "generators, expected {} / {}", ex.id, livings[0],
                         generators[0], ex.team0_livings,
                         ex.team0_generators));
    if (livings[1] != ex.team1_livings || generators[1] != ex.team1_generators)
        fail(std::format("self-check scen{}: team 1 has {} livings / {} "
                         "generators, expected {} / {}", ex.id, livings[1],
                         generators[1], ex.team1_livings,
                         ex.team1_generators));
    if (delayed != ex.delayed_spawns || no_specials != ex.specials_disabled)
        fail(std::format("self-check scen{}: {} delayed spawns / {} "
                         "specials-disabled NPCs, expected {} / {}", ex.id,
                         delayed, no_specials, ex.delayed_spawns,
                         ex.specials_disabled));
    if (total_livings > MAXOBS)
        fail(std::format("self-check scen{}: {} seeded livings exceed the "
                         "MAXOBS={} living cap", ex.id, total_livings,
                         MAXOBS));

    // Exit-destination audit against the registered id set.
    std::vector<int> destinations;
    for (const auto& uptr : world.fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Treasure &&
            ob->family() == FAMILY_EXIT)
            destinations.push_back(ob->stats()->level());
    }
    if (destinations != ex.exit_destinations)
        fail(std::format("self-check scen{}: exit-destination set does not "
                         "match the design ({} exits found)", ex.id,
                         destinations.size()));
    for (int dest : destinations)
        if (registered.find(dest) == registered.end())
            fail(std::format("self-check scen{}: exit destination {} not in "
                             "the package", ex.id, dest));

    // The og::mapgen audits: footing, stair pairing/clearance, fall
    // lines, ground reachability of every foe + generator + exit from the
    // lead marker, and generator spawn egress. The audits swap the
    // installed context's world themselves.
    for (const std::string& err : og::mapgen::audit_footing(world))
        fail(std::format("self-check scen{}: {}", ex.id, err));
    for (const std::string& err : og::mapgen::audit_stairs(world))
        fail(std::format("self-check scen{}: {}", ex.id, err));
    for (const std::string& err : og::mapgen::audit_fall_lines(world))
        fail(std::format("self-check scen{}: {}", ex.id, err));
    for (const std::string& err : og::mapgen::audit_reachability(world))
        fail(std::format("self-check scen{}: {}", ex.id, err));
    for (const std::string& err : og::mapgen::audit_generator_spawn_exits(world))
        fail(std::format("self-check scen{}: {}", ex.id, err));
}

// The isle's embedded dream script must actually dispatch, not merely
// ride along in the zip: with the produced campaign mounted, the pack
// script registry must carry it, and one sim tick of scen 1 must run
// isle.lua's on_load — watchtower-ward notification emitted, the ward
// stamped on the Sea Wizard, and zero recorded script errors.
void self_check_isle_script()
{
    bool registered = false;
    for (const og::script::PackScript& ps : og::script::pack_scripts())
        if (ps.pack_id == kDreamsPackId)
            registered = true;
    if (!registered)
    {
        fail("self-check: dreams pack script not registered on mount");
        return;
    }

    LevelRuntimeData level(1, true, &headless_level_data_hooks());
    SaveData save;
    og::sim::SimEventLog events;
    level.set_sim_context(&save, &events, &cfg);
    GameplayContext script_ctx;
    script_ctx.world = &level.world();
    script_ctx.save = &save;
    script_ctx.sim_events = &events;
    script_ctx.config = &cfg;
    GameplayContext* prev = current_game;
    current_game = &script_ctx;

    if (!level.load())
    {
        fail("self-check: scen1 failed to load for the script check");
        current_game = prev;
        return;
    }
    level.world().tick();

    bool ward_announced = false;
    for (const og::sim::Event& ev : events.drain())
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find("watchtowers") != std::string::npos)
            ward_announced = true;
    if (!ward_announced)
        fail("self-check: isle on_load did not announce the watchtower ward");

    walker* boss = nullptr;
    for (const auto& uptr : level.world().oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->family() == FAMILY_MAGE && ob->team_num() == 1)
            boss = ob;
    }
    if (boss == nullptr || !boss->stats()->query_bit_flags(BIT_INVINCIBLE))
        fail("self-check: the Sea Wizard is not warded after on_load");

    for (const og::script::ScriptError& err :
         level.world().scripts().host().errors())
        fail(std::format("self-check: script error at {}: {}", err.where,
                         err.message));

    current_game = prev;
}

// The castle's dream script must dispatch too: one tick of scen 2 runs
// gold_castle.lua's on_load (the gate notification), and a crew member
// taking a castle gold bar wakes exactly six allied "Gold Ghost"s — the
// declared core:gold_bar override, proven live on the packaged level.
void self_check_castle_script()
{
    LevelRuntimeData level(2, true, &headless_level_data_hooks());
    SaveData save;
    og::sim::SimEventLog events;
    level.set_sim_context(&save, &events, &cfg);
    GameplayContext script_ctx;
    script_ctx.world = &level.world();
    script_ctx.save = &save;
    script_ctx.sim_events = &events;
    script_ctx.config = &cfg;
    GameplayContext* prev = current_game;
    current_game = &script_ctx;

    if (!level.load())
    {
        fail("self-check: scen2 failed to load for the script check");
        current_game = prev;
        return;
    }
    GameWorld& world = level.world();
    world.tick();

    bool gate_announced = false;
    for (const og::sim::Event& ev : events.drain())
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find("The gate knows us") != std::string::npos)
            gate_announced = true;
    if (!gate_announced)
        fail("self-check: castle on_load did not announce the gate");

    walker* bar = nullptr;
    for (const auto& uptr : world.fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Treasure &&
            ob->family() == FAMILY_GOLD_BAR && ob->xpos() == 22 * GRID_SIZE &&
            ob->ypos() == 19 * GRID_SIZE)
            bar = ob;
    }
    walker* keeper = world.add_ob(Order::Living, FAMILY_SOLDIER);
    if (bar == nullptr || keeper == nullptr)
    {
        fail("self-check: scen2 castle bar or keeper missing");
        current_game = prev;
        return;
    }
    keeper->set_team_num(0);
    keeper->set_real_team_num(0);
    keeper->set_act_type(ACT_CONTROL);
    bar->eat_me(keeper);
    int ghosts = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->family() == FAMILY_GHOST && ob->team_num() == 0 &&
            ob->stats()->name == "Gold Ghost")
            ++ghosts;
    }
    if (!bar->dead() || ghosts != 6)
        fail(std::format("self-check: a castle bar woke {} ghosts (bar dead "
                         "{}), expected 6", ghosts, bar->dead() ? 1 : 0));

    for (const og::script::ScriptError& err : world.scripts().host().errors())
        fail(std::format("self-check: script error at {}: {}", err.where,
                         err.message));
    current_game = prev;
}

// The asteroid's dream script, end to end on the packaged level: one tick
// runs asteroid.lua's on_load (every hostile under the seven-tile sight
// cap, the Big One on the rock at sight 0 and still posted); a hostile on
// the spike band loses hitpoints; a crew member three tiles from the deck
// wakes the Big One, who walks off the rock, falls and lands crippled;
// and the stairs branch (a golem arriving on floor 0 over the deck)
// names him the Stomper.
namespace asteroid_check {
walker* find_golem(GameWorld& world)
{
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            !ob->dead() && ob->family() == FAMILY_GOLEM && ob->team_num() == 1)
            return ob;
    }
    return nullptr;
}
} // namespace asteroid_check

void self_check_asteroid_script()
{
    using asteroid_check::find_golem;
    for (int pass = 0; pass < 2; ++pass)
    {
        LevelRuntimeData level(3, true, &headless_level_data_hooks());
        SaveData save;
        og::sim::SimEventLog events;
        level.set_sim_context(&save, &events, &cfg);
        GameplayContext script_ctx;
        script_ctx.world = &level.world();
        script_ctx.save = &save;
        script_ctx.sim_events = &events;
        script_ctx.config = &cfg;
        GameplayContext* prev = current_game;
        current_game = &script_ctx;
        if (!level.load())
        {
            fail("self-check: scen3 failed to load for the script check");
            current_game = prev;
            return;
        }
        GameWorld& world = level.world();
        world.tick();
        walker* golem = find_golem(world);
        if (golem == nullptr)
        {
            fail("self-check: scen3 has no Big One");
            current_game = prev;
            return;
        }
        if (pass == 0)
        {
            for (const auto& uptr : world.oblist)
            {
                walker* ob = uptr.get();
                if (ob != nullptr && ob->query_order() == Order::Living &&
                    ob->team_num() == 1 && ob->lineofsight() > 7)
                    fail("self-check: a scen3 hostile escaped the sight cap");
            }
            if (golem->lineofsight() != 0 || golem->act_type() != ACT_GUARD ||
                golem->floor() != 1)
                fail(std::format("self-check: the Big One on the rock has "
                                 "sight {} act {} floor {}, expected 0/guard/1",
                                 golem->lineofsight(),
                                 static_cast<int>(golem->act_type()),
                                 golem->floor()));
            // The spikes: the gate watcher set down against the band.
            walker* watcher = nullptr;
            for (const auto& uptr : world.oblist)
            {
                walker* ob = uptr.get();
                if (ob != nullptr && ob->query_order() == Order::Living &&
                    ob->family() == FAMILY_SKELETON &&
                    ob->xpos() == 30 * GRID_SIZE && ob->ypos() == 30 * GRID_SIZE)
                    watcher = ob;
            }
            if (watcher == nullptr)
            {
                fail("self-check: scen3 gate watcher missing");
                current_game = prev;
                return;
            }
            const float hp0 = watcher->stats()->hitpoints();
            for (int t = 0; t < 30; ++t)
            {
                watcher->setxy(static_cast<short>(5 * GRID_SIZE),
                               static_cast<short>(25 * GRID_SIZE));
                world.tick();
            }
            if (!(watcher->stats()->hitpoints() < hp0))
                fail("self-check: the spike band did not bite a raider "
                     "up against it");
            // The fall: a crew member at the pit's west edge wakes him.
            walker* lure = world.add_ob(Order::Living, FAMILY_SOLDIER);
            if (lure == nullptr)
            {
                fail("self-check: scen3 lure refused");
                current_game = prev;
                return;
            }
            lure->set_team_num(0);
            lure->set_real_team_num(0);
            lure->set_act_type(ACT_SIT);
            lure->set_floor(0);
            lure->setxy(static_cast<short>(44 * GRID_SIZE),
                        static_cast<short>(12 * GRID_SIZE));
            lure->stats()->set_max_hitpoints(30000.0f);
            lure->stats()->set_hitpoints(30000.0f);
            world.tick();
            if (golem->act_type() != ACT_RANDOM)
                fail("self-check: the Big One did not wake for a lure "
                     "three tiles from the deck");
            // Up to five seconds to walk off the deck and land.
            for (int t = 0; t < 60 && golem->stats()->name == "Big One"; ++t)
                world.tick();
            // Landed this tick: hp cut to 90 (one regen tick may follow).
            if (golem->floor() != 0 || golem->stats()->name != "Fallen One" ||
                golem->stats()->hitpoints() > 92.0f ||
                golem->stats()->hitpoints() < 89.0f ||
                golem->stepsize() != 4.0f || golem->lineofsight() != 7)
                fail(std::format("self-check: the Big One did not fall "
                                 "crippled (floor {}, name '{}', hp {}, "
                                 "step {}, sight {})", golem->floor(),
                                 golem->stats()->name,
                                 golem->stats()->hitpoints(),
                                 golem->stepsize(), golem->lineofsight()));
        }
        else
        {
            // The stairs branch: arriving on floor 0 with the deck above.
            golem->change_floor(0);
            golem->setxy(static_cast<short>(48 * GRID_SIZE),
                         static_cast<short>(7 * GRID_SIZE));
            world.tick();
            if (golem->stats()->name != "Stomper" ||
                golem->stats()->hitpoints() > 182.0f ||
                golem->stats()->hitpoints() < 179.0f ||
                golem->stepsize() != 6.0f || golem->lineofsight() != 7)
                fail(std::format("self-check: the stairs branch left name "
                                 "'{}', hp {}, step {}, sight {}",
                                 golem->stats()->name,
                                 golem->stats()->hitpoints(),
                                 golem->stepsize(), golem->lineofsight()));
        }
        for (const og::script::ScriptError& err :
             world.scripts().host().errors())
            fail(std::format("self-check: scen3 script error at {}: {}",
                             err.where, err.message));
        current_game = prev;
    }
}

} // namespace
} // namespace imaggen

int main(int argc, char* argv[])
{
    using namespace imaggen;
    namespace fs = std::filesystem;

    const std::string out_tree =
        (argc > 1) ? argv[1] : "campaigns/imaginations";
    const fs::path out_abs = fs::absolute(out_tree);

    fs::path scratch;
    if (const char* preset = std::getenv("OPENGLAD_CONFIG_DIR");
        preset == nullptr || preset[0] == '\0')
    {
        scratch = fs::temp_directory_path() /
                  ("imaginations_mapgen_" + std::to_string(getpid()));
        fs::create_directories(scratch);
        setenv("OPENGLAD_CONFIG_DIR", scratch.c_str(), 1);
    }

    init_logging();
    io_init(argc, argv);
    if (get_mounted_campaign() != "gladiator")
    {
        std::fprintf(stderr, "imaginations_mapgen: ERROR: stock campaign not "
                             "mounted; run next to staged assets (build dir)\n");
        io_exit();
        return 1;
    }
    cfg.load_settings();
    init_all_registries();

    og::runtime::SessionState& session = og::runtime::s_mapgen_session;
    static FixedRandom mapgen_rng{0};
    static GameWorld fallback_world(0);
    static SaveData fallback_save;
    session.ctx_.rng = &mapgen_rng;
    session.game_.world = &fallback_world;
    session.game_.save = &fallback_save;
    session.game_.sim_events = session.ctx_.sim_events.get();
    session.game_.config = &cfg;
    session.game_.session_rng_ref = &session.ctx_.rng;
    session.game_.gameplay_active_ref = &session.gameplay_active_;
    current_game = &session.game_;

    const std::string user = get_user_path();
    cleanup_unpacked_campaign();
    create_dir(user + "temp/");
    create_dir(user + "temp/scen/");
    create_dir(user + "temp/pix/");
    write_campaign_yaml(user + "temp/campaign.yaml");
    write_icon(user + "temp/icon.png");
    if (!og::toolexport::stage_pack_tree(
            "campaigns/imaginations/packs/imaginations.dreams",
            user + "temp/", kDreamsPackId))
        fail("failed to stage the embedded dreams pack");

    build_raspberry_isle();
    build_castle_of_gold();
    build_asteroid_castle();

    const std::string glad_path = user + "campaigns/imaginations.glad";
    std::remove(glad_path.c_str());
    if (zip_contents_with_error(user + "temp/", glad_path) !=
        ArchiveIoError::None)
        fail(std::format("failed to zip campaign into {}", glad_path));

    if (g_errors == 0)
    {
        if (mount_campaign_package_with_error("imaginations") !=
            CampaignPackageIoError::None)
        {
            fail("failed to mount the produced campaign");
        }
        else
        {
            const std::set<int> registered = {1, 2, 3};
            const ExpectedLevel expectations[] = {
                // {id, floors, title, starts, t0 liv/gen, t1 liv/gen,
                //  delayed, specials-disabled, exit destinations}
                {1, 1, "The Raspberry Isle", 16, 0, 0, 19, 3, 3, 1, {2}},
                {2, 1, "The Castle of Gold", 12, 0, 0, 27, 0, 23, 0,
                 {kCastleOfGoldExit}},
                {3, 2, "The Asteroid Castle", 16, 1, 0, 22, 0, 11, 0,
                 {kAsteroidCastleExit}},
            };
            for (const ExpectedLevel& e : expectations)
                self_check_level(e, registered);
            self_check_isle_script();
            self_check_castle_script();
            self_check_asteroid_script();
            (void)unmount_campaign_package_with_error("imaginations");
        }
    }

    int result = 1;
    if (g_errors == 0)
    {
        if (!og::toolexport::export_campaign_tree(user + "temp/", out_abs))
            fail(std::format("failed to export the campaign tree to {}",
                             out_abs.string()));
        else
        {
            std::printf("imaginations_mapgen: wrote %s\n", out_abs.c_str());
            result = 0;
        }
    }

    cleanup_unpacked_campaign();
    io_exit();
    if (!scratch.empty())
    {
        std::error_code ec;
        fs::remove_all(scratch, ec);
    }
    if (result != 0)
        std::fprintf(stderr, "imaginations_mapgen: FAILED with %d error(s)\n",
                     g_errors);
    return result;
}
