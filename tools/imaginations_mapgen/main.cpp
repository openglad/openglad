/* Imaginations campaign generator.
 *
 * Produces campaigns/imaginations/ (the source tree the build composes
 * into builtin/imaginations.glad): the dream-log campaign — levels kids
 * imagine, sent in and made real, one scenario per idea. Ships ONE level
 * today, scen 1 "The Raspberry Isle": an island ringed by open sea, the
 * crew landing on the shores all around, and a moated castle in the
 * middle whose garrison must be cleared. The layout is a faithful build
 * of the first submitted idea (spoken aloud, briefing-voice preserved in
 * the level text): "we start at the edges of the island, the sea is all
 * around us, and the enemies are in the middle." New ideas append new
 * scens; the walk-out exit of the newest level loops home to 1 until its
 * successor exists.
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
// and wake mid-assault with the NEXT WAVE flash. A flight potion near
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
        og::mapgen::place_living(w, FAMILY_MAGE, 1, 0, 31, 31, 2, true,
                                 true);
    if (boss != nullptr)
        boss->stats()->name = "Sea Wizard"; // 10 chars: fits the 11-char field
    og::mapgen::place_living(w, FAMILY_ORC, 1, 0, 30, 32, 1, true);
    og::mapgen::place_living(w, FAMILY_ELF, 1, 0, 33, 31, 1, true);
    // Reinforcement waves, dormant behind the walls (NEXT WAVE HUD):
    // the bailey relief at tick 500, the throne guard at tick 800. They
    // wake mid-assault, inside the fight the crew is already carrying.
    // Sized for a garrison that FIGHTS: six wave units were budgeted
    // when the posts were accidentally frozen statues; with the wake
    // fixed, the full six landed exactly in a fresh crew's mid-assault
    // collapse window and wiped it in every playtest seed. Three keeps
    // the two-beat wave drama and a clearable dream.
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 26, 26, 1, true,
                             false, 500);
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 37, 37, 1, true,
                             false, 500);
    og::mapgen::place_living(w, FAMILY_ORC, 1, 0, 34, 29, 1, true,
                             false, 800);
    // The wizard's colleges on the field ring, one trickle per art: the
    // mage tower northeast, the bone tent southwest, the elf treehouse
    // by the northwest grove. All on the crew's side of the moat, so
    // their roaming spawns chase over open ground.
    og::mapgen::place_generator(w, FAMILY_TOWER, 1, 0, 43, 11, 1);
    og::mapgen::place_generator(w, FAMILY_TENT, 1, 0, 10, 43, 1);
    og::mapgen::place_generator(w, FAMILY_TREEHOUSE, 1, 0, 19, 11, 1);

    // Hostile posts must WAKE. og::mapgen::place_living stamps hold-post
    // on every team<=1 guard (the shared builders treat teams 0/1 as
    // allied escorts that must never leave their chokepoint), but on
    // this isle team 1 IS the enemy garrison. A hold-post guard never
    // converts to the hunt AI — walker::act_guard skips the wake — and
    // its parting COMMAND_FIRE carries no direction, so the facing gate
    // eats every shot: the shipped castle sat in the open, pivoting at
    // a crew two tiles away and doing literally nothing. Clear the flag
    // on the whole garrison (the builder documents this exact per-caller
    // override): posts wake at true sight and fight, the submitted
    // design. The immobile watchtowers wake too — on a stationary
    // family ACT_RANDOM turns and fires (walkstep is a facing turn),
    // which is the whole job of an arrow turret; hold-post would lock
    // its aim to the one facing the fire command defaults to.
    for (const auto& uptr : w.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->team_num() == 1 && ob->act_type() == ACT_GUARD)
            ob->set_guard_hold_post(false);
    }

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
    // marker: clear the isle, then sail home. Destination 1 = itself —
    // the loop-home rule for the newest level of a growing campaign.
    og::mapgen::place_exit(w, 0, 34, 57, 1);

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
    FixedRandom script_rng{0};
    level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                          &script_rng, &cfg);
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
            const std::set<int> registered = {1};
            const ExpectedLevel expectations[] = {
                // {id, floors, title, starts, t0 liv/gen, t1 liv/gen,
                //  delayed, specials-disabled, exit destinations}
                {1, 1, "The Raspberry Isle", 16, 0, 0, 19, 3, 3, 1, {1}},
            };
            for (const ExpectedLevel& e : expectations)
                self_check_level(e, registered);
            self_check_isle_script();
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
