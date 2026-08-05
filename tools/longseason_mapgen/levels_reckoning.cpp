/* The Long Season — THE RECKONING (levels 17-19): Ashfall Gate (the
 * creditors' camp at the foundry-city's west gate; paint_ash_open dresses
 * the plain), The Warm Mint (the three-floor finale climb; The Founder;
 * the campaign's only team-1 level), and the Settlement Day epilogue (the
 * exit loops the year back to level 1 — next spring, the ledger opens
 * again). Authored against campaigns/longseason/README.md, "Level 17" ..
 * "Level 19".
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "builders.h"

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/level_data_hooks.h>

namespace longseason {
namespace {

// --- New-tile paint helpers (the westlands recipe). --------------------------
// Ash, lava and marsh are genre-inert to the autotiler, so — exactly like
// pavement — their two-variant texture is painted in AFTER smooth_world
// rather than smoothed in, with the same deterministic (x*7 + y*13) picker.
// (paint_ash_open, the open-ground-only variant, lives in main.cpp; these
// unconditional fills are for authored rectangles.)
constexpr unsigned char kAshTiles[2] = {PIX_ASH1, PIX_ASH2};
constexpr unsigned char kLavaTiles[2] = {PIX_LAVA1, PIX_LAVA2};
constexpr unsigned char kMarshTiles[2] = {PIX_MARSH1, PIX_MARSH2};

void paint_variants(PixieData& g, int tx0, int ty0, int tx1, int ty1,
                    const unsigned char (&variants)[2])
{
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 7 + y * 13) % 2]);
}

void paint_ash(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    paint_variants(g, tx0, ty0, tx1, ty1, kAshTiles);
}

void paint_lava(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    paint_variants(g, tx0, ty0, tx1, ty1, kLavaTiles);
}

void paint_marsh(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    paint_variants(g, tx0, ty0, tx1, ty1, kMarshTiles);
}

// 17 ASHFALL GATE (THE RECKONING opens): the foundry-city's west gate.
// Every company the year stiffed is camped on the ash, and the Brass
// Kettle Company fights through its own kind to present its bill first.
// The slag runnels leaking out under the wall are the first sight of the
// melt — the warm coin's source, one level early. Kill-all: the wagon
// corridor is the spine fight, the flank camps commit as the crew passes
// their openings, the second watch folds in at 350, the cut-purses wake
// BEHIND the crew at 700, and the gate ward pair (lvl-9 golems + door
// muscle) is the level's deliberate boss beat.
void build_ashfall_gate(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(17, true, &hooks);
    init_world(level, 1, 70, 44);
    GameWorld& w = level.world();

    // Pre-smooth paints (PIX_WALL2): the city wall with its gate throat
    // carved as a grass placeholder (re-ashed post-smooth), the gatehouse
    // towers framing the approach channel, and the three camps.
    paint_rect(w.grid, 64, 0, 69, 43, PIX_WALL2);   // city wall, east edge
    paint_rect(w.grid, 64, 19, 69, 24, PIX_GRASS1); // gate passage carve
    paint_rect(w.grid, 60, 15, 63, 18, PIX_WALL2);  // north gatehouse tower
    paint_rect(w.grid, 60, 25, 63, 28, PIX_WALL2);  // south gatehouse tower
    // North camp palisade (the horse companies), opening SOUTH.
    paint_rect(w.grid, 16, 5, 28, 5, PIX_WALL2);
    paint_rect(w.grid, 16, 5, 16, 11, PIX_WALL2);
    paint_rect(w.grid, 28, 5, 28, 11, PIX_WALL2);
    // South camp palisade (the hired bows and casters), opening NORTH.
    paint_rect(w.grid, 16, 38, 28, 38, PIX_WALL2);
    paint_rect(w.grid, 16, 32, 16, 38, PIX_WALL2);
    paint_rect(w.grid, 28, 32, 28, 38, PIX_WALL2);
    // Center wagon-camp (the foot companies): two wagon-walls astride the
    // road; corridor interior x 36..44, y 18..25 (8 wide).
    paint_rect(w.grid, 36, 16, 44, 17, PIX_WALL2);
    paint_rect(w.grid, 36, 26, 44, 27, PIX_WALL2);
    smooth_world(w);

    // Post-smooth (all genre-inert paint): ash the whole plain around the
    // palisades, then re-ash the carved gate passage unconditionally.
    paint_ash_open(w.grid, 0, 0, 63, 43);
    paint_ash(w.grid, 64, 19, 69, 24);
    // Slag runnels (impassable to ground — the foundry leaking under its
    // own wall) pin the road band (y 15..28) as the main approach; the
    // flanks cross only at the two 3-wide causeway gaps.
    paint_lava(w.grid, 30, 13, 62, 14); // north runnel
    paint_lava(w.grid, 30, 29, 62, 30); // south runnel
    paint_ash(w.grid, 34, 13, 36, 14);  // north causeway gap
    paint_ash(w.grid, 46, 29, 48, 30);  // south causeway gap
    // The road: west edge to the gate throat.
    paint_path(w.grid, 2, 21, 63, 22);

    // ARMIES (team 2 — the creditor companies + the city's gate wards).
    // (Design-doc deviation, F4 calibration: the camps shed one level
    // rank-for-rank — the doc's corridor wiped the curve-8 crew by tick
    // 300 on every seed and bracket; the gate-ward wall trims 9 -> 8,
    // still the boss beat at curve.)
    // The foot companies hold the wagon corridor — ON POSTS (F4: as
    // roamers the whole corridor charged as one 14-pack; posted, the
    // corridor is swept post by post).
    static constexpr int foot[10][2] = {
        {37, 19}, {39, 19}, {41, 19}, {43, 19}, {37, 24},
        {39, 24}, {41, 24}, {43, 24}, {39, 21}, {41, 22}};
    for (int i = 0; i < 10; ++i)
        place_living(w, FAMILY_SOLDIER, 2, 0, foot[i][0], foot[i][1],
                     3 + (i % 2), true);
    // ...their pickets hold the corridor mouths (start-as-guard)...
    static constexpr int pickets[4][2] = {
        {35, 19}, {35, 24}, {45, 19}, {45, 24}};
    for (const auto& p : pickets)
        place_living(w, FAMILY_ARCHER, 2, 0, p[0], p[1], 3, true);
    // ...the horse companies hold the north camp. (Design-doc deviation,
    // F4 calibration: the doc leaves the camps unguarded so they "stream
    // out once alerted" — every sweep showed ~28 units converging on the
    // corridor fight at once and wiping the curve-8 crew by tick 300.
    // Camp guards still commit when the crew reaches their openings; the
    // battle becomes the doc's three staged fights.)
    static constexpr int horse[6][2] = {
        {18, 6}, {21, 6}, {25, 6}, {18, 9}, {21, 9}, {22, 11}};
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_BARBARIAN, 2, 0, horse[i][0], horse[i][1],
                     4 + (i % 2), true);
    // ...the hired bows and casters fill the south camp...
    static constexpr int bows[6][2] = {
        {18, 33}, {21, 33}, {24, 33}, {18, 36}, {21, 36}, {24, 36}};
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_ELF, 2, 0, bows[i][0], bows[i][1], 3 + (i % 2),
                     true);
    place_living(w, FAMILY_MAGE, 2, 0, 26, 33, 4, true);
    place_living(w, FAMILY_MAGE, 2, 0, 26, 36, 4, true);
    // ...and the city's own gate wards, cast from the warm metal, hold the
    // approach channel and the passage (the level's boss beat).
    // (Design-doc deviation, one row each: the doc anchors the pair at
    // (61,20)/(61,23) assuming 2x2 footprints, but the golem sprite is
    // 48x36 — a 3x3 TILE footprint — and the lower ward would clip the
    // south gatehouse tower at y25. Anchored at (61,19)/(61,22) the pair
    // tiles the 6-row approach channel wall to wall; nothing slips the
    // wards, exactly the doc's stated intent.)
    // The wall STANDS (doc posture kept): roaming it in the F4 sweeps
    // just added six heavies to the corridor fight and wiped the crew.
    // The AI floor parks at this deliberate wall instead of finishing it
    // — the documented calibration trade on this level's kill gate (see
    // campaign_meta); a player must break it regardless.
    place_living(w, FAMILY_GOLEM, 2, 0, 61, 19, 7, true);
    place_living(w, FAMILY_GOLEM, 2, 0, 61, 22, 7, true);
    static constexpr int muscle[4][2] = {
        {58, 16}, {58, 27}, {66, 20}, {66, 23}};
    for (const auto& m : muscle)
        place_living(w, FAMILY_BIG_ORC, 2, 0, m[0], m[1], 5, true);
    // The second watch folds in from the camp rears at tick 350, timed to
    // land while the corridor fight is still live.
    static constexpr int watch[8][2] = {
        {17, 2},  {20, 2},  {23, 2},  {26, 2},
        {17, 41}, {20, 41}, {23, 41}, {26, 41}};
    for (const auto& s : watch)
        place_living(w, FAMILY_SOLDIER, 2, 0, s[0], s[1], 3, false, false, 350);
    // At 700 the cut-purses wake BEHIND the crew, on the crew's own
    // treasure line — the rear is never safe on Settlement's eve.
    static constexpr int cutpurses[6][2] = {
        {10, 10}, {14, 13}, {12, 6}, {10, 34}, {14, 30}, {12, 38}};
    for (const auto& t : cutpurses)
        place_living(w, FAMILY_THIEF, 2, 0, t[0], t[1], 3, false, false, 700);
    // Camp trickles (lvl 1 — trickles, not floods, per the F4 lesson; the
    // doc's lvl-3 pair pushed a lost run's t2 past the <60-at-6000 pin).
    place_generator(w, FAMILY_TENT, 2, 0, 24, 8, 1);   // north camp
    place_generator(w, FAMILY_TOWER, 2, 0, 21, 39, 1); // behind the south wall

    // START MARKERS (lead FIRST): a wedge on the road, point east.
    place_start(w, 0, 6, 21); // LEAD, on the road under the banner
    place_start(w, 0, 4, 18);
    place_start(w, 0, 4, 24);
    place_start(w, 0, 2, 15);
    place_start(w, 0, 2, 21);
    place_start(w, 0, 2, 27);
    place_start(w, 0, 8, 17);
    place_start(w, 0, 8, 25);
    place_start(w, 0, 4, 12);
    place_start(w, 0, 4, 30);

    // TREASURE (every camp has a pay chest; all of it warm).
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 26, 6);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 26, 7);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 17, 11);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 19, 11);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 44, 21);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 44, 22);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 17, 37);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 27, 37);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 17, 32);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 19, 32);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 54, 21);

    // EXITS: through the gate into the Warm Mint, or the road back to the
    // Frozen Ford (withdraw = declining the reckoning).
    place_exit(w, 0, 67, 21, 18);
    place_exit(w, 0, 1, 26, 16);

    // Decor (after armies; blocking decor off all routes): gate braziers
    // at the throat corners, watch-torches flanking the camp openings,
    // cook-fires in the camp corners. The wagon camp gets NO brazier — its
    // corridor is the spine battle lane and stays clean of blocking decor.
    paint_decor(w, 0, 63, 18, DECOR_BRAZIER);
    paint_decor(w, 0, 63, 25, DECOR_BRAZIER);
    paint_decor(w, 0, 17, 12, DECOR_TORCH1);
    paint_decor(w, 0, 27, 12, DECOR_TORCH1);
    paint_decor(w, 0, 17, 31, DECOR_TORCH1);
    paint_decor(w, 0, 27, 31, DECOR_TORCH1);
    paint_decor(w, 0, 26, 10, DECOR_BRAZIER);
    paint_decor(w, 0, 18, 37, DECOR_BRAZIER);
    scatter_boulders(w, 0, 0, 0, 15, 43, 25); // sparse west-margin scree
    // E7-style ambience (non-blocking): old bones on the ash — companies
    // have died at this gate before — thickening in the approach channel;
    // road pebbles; cinder-grit around the camps.
    scatter_decor(w, 0, 0, 0, 59, 43, 21, DECOR_BONES, {ScatterGround::Ash});
    scatter_decor(w, 0, 54, 15, 63, 28, 9, DECOR_BONES, {ScatterGround::Ash});
    scatter_decor(w, 0, 2, 19, 63, 24, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 16, 2, 48, 41, 23, DECOR_PEBBLES,
                  {ScatterGround::Ash});

    w.type = 0; // kill-all, then walk to the gate exit
    save_level_files(w, 17, "Ashfall Gate",
                     {"Ledger, ash season. Every company",
                      "the year stiffed is camped at the",
                      "foundry gates, howling for pay.",
                      "So are we. The line forms behind",
                      "our banner. Collect at the gate;",
                      "the coin inside is still warm."},
                     7, 5500);
}

// 18 THE WARM MINT (THE CAMPAIGN FINALE): the mint has been striking coin
// from the city's own foundations; the vault floor is falling into the
// melt as it is stripped. Three sides: the creditor companies forcing the
// doors (team 1 — this campaign's FIRST and ONLY use of team 1), the
// mint's own (team 2), and the company (team 0). CAN_EXIT: the point is
// the climb to the master ledger on the crucible floor — reading the book
// IS the settlement — while the creditors' war rages below. Kettle is
// placed but protect-OPTIONAL (no SAVE_ALL, no npc_flags bit2 on anyone).
void build_warm_mint(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(18, true, &hooks);
    init_world(level, 3, 64, 44);
    GameWorld& w = level.world();
    PixieData& g0 = w.grid;
    PixieData& g1 = w.grid_for_floor(1);
    PixieData& g2 = w.grid_for_floor(2);

    // All three floors' pre-smooth walls BEFORE the single smooth_world
    // call (westlands convention).
    // Floor 0 — the gatehall and the casting floor: 2-thick outer shell,
    // the gatehall wall and the stair-chamber wall.
    paint_rect(g0, 0, 0, 63, 1, PIX_WALL2);
    paint_rect(g0, 0, 42, 63, 43, PIX_WALL2);
    paint_rect(g0, 0, 0, 1, 43, PIX_WALL2);
    paint_rect(g0, 62, 0, 63, 43, PIX_WALL2);
    paint_rect(g0, 18, 2, 18, 41, PIX_WALL2);
    paint_rect(g0, 52, 2, 52, 41, PIX_WALL2);
    // Floor 1 — the vault floor: same shell, same internal walls.
    paint_rect(g1, 0, 0, 63, 1, PIX_WALL2);
    paint_rect(g1, 0, 42, 63, 43, PIX_WALL2);
    paint_rect(g1, 0, 0, 1, 43, PIX_WALL2);
    paint_rect(g1, 62, 0, 63, 43, PIX_WALL2);
    paint_rect(g1, 18, 2, 18, 41, PIX_WALL2);
    paint_rect(g1, 52, 2, 52, 41, PIX_WALL2);
    // Floor 2 — the crucible floor: open heat-haze void, one deck slab.
    paint_rect(g2, 0, 0, 63, 43, PIX_AIR);
    paint_rect(g2, 3, 13, 60, 30, PIX_WALL2);
    smooth_world(w);

    // Floor 0 post-smooth: pave every interior zone, carve the doors, and
    // cut the casting channels (the runnels the coin is poured down; the
    // cycled-flame band animates them) with their 3-wide bridge gaps.
    paint_pavement(g0, 2, 2, 17, 41);  // gatehall
    paint_pavement(g0, 19, 2, 51, 41); // casting floor
    paint_pavement(g0, 53, 2, 61, 41); // stair chamber
    paint_pavement(g0, 18, 10, 18, 13); // gatehall->casting, north door
    paint_pavement(g0, 18, 26, 18, 29); // gatehall->casting, south door
    paint_pavement(g0, 52, 19, 52, 24); // casting->stair chamber
    paint_lava(g0, 19, 8, 51, 9);   // channel A
    paint_lava(g0, 19, 20, 51, 21); // channel B
    paint_lava(g0, 19, 32, 51, 33); // channel C
    paint_pavement(g0, 34, 8, 36, 9);   // A's bridge gap
    paint_pavement(g0, 26, 20, 28, 21); // B's west gap
    paint_pavement(g0, 44, 20, 46, 21); // B's east gap
    paint_pavement(g0, 34, 32, 36, 33); // C's bridge gap

    // Floor 1 post-smooth: counting rooms, vault hall, east landing;
    // doors; THE COLLAPSE — PIX_AIR holes punched through the vault floor,
    // every hole over OPEN floor-0 pavement (the fall-line rule: a faller
    // lands standing, one floor down, in the middle of the creditors'
    // war); and the melt burn-throughs where the floor is already liquid.
    paint_pavement(g1, 2, 2, 17, 41);  // counting rooms
    paint_pavement(g1, 19, 2, 51, 41); // vault hall
    paint_pavement(g1, 53, 2, 61, 41); // east landing
    paint_pavement(g1, 18, 12, 18, 15); // counting<-vault, north door
    paint_pavement(g1, 18, 28, 18, 31); // counting<-vault, south door
    paint_pavement(g1, 52, 19, 52, 24); // vault<-landing
    paint_rect(g1, 30, 13, 33, 16, PIX_AIR); // H1 -> floor-0 band1, clear
    paint_rect(g1, 40, 24, 43, 27, PIX_AIR); // H2 -> floor-0 band2, clear
    paint_rect(g1, 22, 35, 25, 38, PIX_AIR); // H3 -> floor-0 band3, clear
    paint_rect(g1, 44, 4, 46, 6, PIX_AIR);   // H4 -> floor-0 N band, clear
    paint_lava(g1, 19, 20, 22, 23); // melt pool P1
    paint_lava(g1, 47, 30, 50, 33); // melt pool P2
    paint_lava(g1, 33, 20, 36, 22); // melt pool P3

    // Floor 2 post-smooth: carve the deck interior, pour the crucible
    // itself (north walk y 15..17 and south walk y 26..28, both 3 wide;
    // west landing x 5..13, east dais x 50..58), then the LAVA SEAL RING
    // (the westlands E5 fall-line lesson, applied from the start): the
    // deck's outer edge runs with fire, so NO walkable cell on floor 2 is
    // adjacent to PIX_AIR — the only fall lines in the level are the four
    // authored vault holes. Ground units cannot step off; flyers can cross.
    paint_pavement(g2, 5, 15, 58, 28);
    paint_lava(g2, 14, 18, 49, 25); // the crucible
    paint_lava(g2, 3, 13, 60, 14);  // seal ring, north edge
    paint_lava(g2, 3, 29, 60, 30);  // seal ring, south edge
    paint_lava(g2, 3, 15, 4, 28);   // seal ring, west edge
    paint_lava(g2, 59, 15, 60, 28); // seal ring, east edge

    // ARMIES. Team 1 — the creditor companies, pressing the gatehall
    // doors (hostile to the mint AND to us; the war "rages below").
    // (Design-doc deviation, F4 calibration: the creditor ranks stand AT
    // AND THROUGH the gatehall doors — pre-engaged with the mint — not
    // spread across the gatehall's west half. The doc's shape put all 22
    // of team 1 in the closed gatehall with the crew: their nearest foe
    // was the company, the "war below" never raged, and the curve-8 crew
    // was wiped by tick 300 on every seed and bracket. Queued into the
    // two door fights they grind the furnace line exactly as the fiction
    // says, and the crew joins the war from behind. Both armies also shed
    // one level and the mint's tower drops to lvl 1 — it outbred the
    // three-way so t2 GREW while t1 died.)
    static constexpr int t1_soldiers[10][2] = {
        {14, 10}, {16, 10}, {14, 12}, {16, 12}, {20, 10},
        {14, 26}, {16, 26}, {14, 28}, {16, 28}, {20, 26}};
    for (const auto& s : t1_soldiers)
        place_living(w, FAMILY_SOLDIER, 1, 0, s[0], s[1], 5);
    static constexpr int t1_barbarians[4][2] = {
        {20, 12}, {22, 10}, {20, 28}, {22, 26}};
    for (const auto& b : t1_barbarians)
        place_living(w, FAMILY_BARBARIAN, 1, 0, b[0], b[1], 5);
    static constexpr int t1_archers[4][2] = {
        {12, 10}, {12, 12}, {12, 26}, {12, 28}};
    for (const auto& a : t1_archers)
        place_living(w, FAMILY_ARCHER, 1, 0, a[0], a[1], 2);
    static constexpr int t1_thieves[4][2] = {
        {24, 12}, {40, 16}, {30, 28}, {44, 30}}; // already looting
    for (const auto& t : t1_thieves)
        place_living(w, FAMILY_THIEF, 1, 0, t[0], t[1], 5);
    // More companies force the doors behind us at tick 900 (F4: 600 -> 900
    // and lvl 3 — the push landed inside the escape-viability window and
    // finished the pocket on two of three curve seeds).
    static constexpr int t1_push[6][2] = {
        {3, 3}, {6, 3}, {9, 3}, {3, 39}, {6, 39}, {9, 39}};
    for (const auto& p : t1_push)
        place_living(w, FAMILY_SOLDIER, 1, 0, p[0], p[1], 3, false, false, 900);

    // Team 2 — the mint. The furnace-men work the casting floor...
    // (F4: the furnace-men WORK their furnaces — posted; as roamers they
    // counter-punched west through the doors and finished the pocket.)
    static constexpr int furnace_men[6][2] = {
        {22, 5}, {34, 5}, {46, 5}, {26, 38}, {34, 37}, {46, 37}};
    for (const auto& f : furnace_men)
        place_living(w, FAMILY_BIG_ORC, 2, 0, f[0], f[1], 5, true);
    // ...elementals hold the channel-B bridge gaps...
    place_living(w, FAMILY_FIREELEMENTAL, 2, 0, 26, 20, 6, true);
    place_living(w, FAMILY_FIREELEMENTAL, 2, 0, 45, 20, 6, true);
    // ...golems of warm metal flank the stair...
    place_living(w, FAMILY_GOLEM, 2, 0, 55, 19, 6, true);
    place_living(w, FAMILY_GOLEM, 2, 0, 55, 24, 6, true);
    // ...and the furnace dead wake at 400, as the crew crosses.
    static constexpr int furnace_dead[6][2] = {
        {24, 15}, {36, 15}, {48, 15}, {24, 26}, {36, 26}, {48, 26}};
    for (const auto& d : furnace_dead)
        place_living(w, FAMILY_SKELETON, 2, 0, d[0], d[1], 4, true, false,
                     400);
    // Floor 1: one golem ward per treasure heap, the vault dead among the
    // holes, and the counting-room clerks.
    static constexpr int heap_wards[4][2] = {
        {27, 7}, {46, 16}, {25, 32}, {43, 38}};
    for (const auto& h : heap_wards)
        place_living(w, FAMILY_GOLEM, 2, 1, h[0], h[1], 7, true);
    static constexpr int vault_dead[6][2] = {
        {30, 8}, {38, 10}, {22, 25}, {46, 25}, {30, 34}, {38, 31}};
    for (const auto& v : vault_dead)
        place_living(w, FAMILY_SKELETON, 2, 1, v[0], v[1], 5);
    place_living(w, FAMILY_MAGE, 2, 1, 12, 8, 5, true);
    place_living(w, FAMILY_MAGE, 2, 1, 12, 34, 5, true);
    // Floor 2: the rim duel — mid-walk elementals, ghosts hovering OVER
    // the crucible melt (flyers, legal footing), and The Founder at the
    // book. ("The Founder" is 11 chars — exactly fills the name field.)
    place_living(w, FAMILY_FIREELEMENTAL, 2, 2, 30, 16, 9, true);
    place_living(w, FAMILY_FIREELEMENTAL, 2, 2, 30, 27, 9, true);
    place_living(w, FAMILY_GHOST, 2, 2, 24, 21, 8);
    place_living(w, FAMILY_GHOST, 2, 2, 40, 22, 8);
    place_named_foe(w, FAMILY_ARCHMAGE, 2, 2, 52, 21, 10, "The Founder", true);
    // The mint's tower beside (not under) hole H4 (lvl 1 per the F4
    // three-way note above).
    place_generator(w, FAMILY_TOWER, 2, 1, 47, 4, 1);

    // Team 0: Kettle holds the door pocket — literal ACT_GUARD at the
    // west wall, anchoring whoever falls back. (Design-doc deviation, F4:
    // the doc posts him at (3,33) beside the backtrack exit, but the
    // marker pocket he is written to anchor spans y15-27; mid-pocket he
    // actually absorbs the door-fight spillover the sweeps showed
    // finishing the crew.) Protect-OPTIONAL: his death is a ledger line,
    // not a mission failure.
    place_hero(w, FAMILY_SOLDIER, 0, 3, 21, 8, "Kettle", true);

    // START MARKERS (lead FIRST): the gatehall west pocket, wedge pointing
    // at the doors. The creditors' nearest rank stands at x 11 — the brawl
    // starts immediately but nobody deploys in contact.
    place_start(w, 0, 6, 21); // LEAD
    place_start(w, 0, 4, 18);
    place_start(w, 0, 4, 24);
    place_start(w, 0, 8, 18);
    place_start(w, 0, 8, 24);
    place_start(w, 0, 6, 15);
    place_start(w, 0, 6, 27);
    place_start(w, 0, 4, 15);
    place_start(w, 0, 4, 27);
    place_start(w, 0, 8, 21);

    // TREASURE: the season's pay, heaped where the vault is breaking —
    // four heaps on floor 1, one golem ward each.
    static constexpr int heap_gold[12][2] = {
        {25, 5},  {26, 6},  {27, 5},  // heap 1 (NW)
        {44, 14}, {45, 15}, {46, 14}, // heap 2 (NE)
        {23, 30}, {24, 31}, {25, 30}, // heap 3 (SW)
        {41, 36}, {42, 37}, {43, 36}, // heap 4 (SE)
    };
    for (const auto& hg : heap_gold)
        place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, hg[0], hg[1]);
    static constexpr int heap_silver[4][2] = {
        {25, 7}, {44, 16}, {23, 32}, {41, 38}};
    for (const auto& hs : heap_silver)
        place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 1, hs[0], hs[1]);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 1, 57, 25); // the landing
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 1, 12, 10); // counting room
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 1, 12, 12); // the clerks'
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 1, 12, 30); // suppers
    // Beside the floor-1 stair, for the summit.
    place(w, Order::Treasure, FAMILY_INVULNERABLE_POTION, 0, 1, 7, 19);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 33, 14); // band1

    // EXITS: THE MASTER LEDGER on the dais (3 tiles east of The Founder's
    // guard post — the ledger is read over his body or not at all), and
    // the doors behind us (withdraw = declining to collect).
    place_exit(w, 2, 55, 21, 19);
    place_exit(w, 0, 2, 36, 17);

    // Decor — BRAZIER/FLAME everywhere is the finale's signature: every
    // door, stair and hall is fire-lit. Braziers block ground, so every
    // cell sits on pavement BESIDE (never in) a door lane or bridge gap.
    // Floor 0: gatehall door flanks, casting-floor band corners (the band2
    // pair at y 25, clear of the south door lane y 26..29), stair-chamber
    // corners, the gatehall colonnade the door fights swirl around (crew
    // pocket is west of it at x <= 9), torch lines on the east wall.
    static constexpr int f0_braziers[16][2] = {
        {16, 9},  {16, 14}, {16, 25}, {16, 30}, // gatehall door flanks
        {20, 6},  {50, 6},  {20, 15}, {50, 15}, // casting band corners
        {20, 25}, {50, 25}, {20, 36}, {50, 36},
        {54, 4},  {60, 4},  {54, 39}, {60, 39}}; // stair-chamber corners
    for (const auto& b : f0_braziers)
        paint_decor(w, 0, b[0], b[1], DECOR_BRAZIER);
    static constexpr int f0_columns[4][2] = {
        {10, 8}, {10, 17}, {10, 26}, {10, 35}};
    for (const auto& c : f0_columns)
        paint_decor(w, 0, c[0], c[1], DECOR_COLUMN_BOTTOM);
    static constexpr int f0_torches[4][2] = {
        {61, 10}, {61, 16}, {61, 27}, {61, 33}};
    for (const auto& t : f0_torches)
        paint_decor(w, 0, t[0], t[1], DECOR_TORCH1);
    // Floor 0 ambience: cinder-pebbles over the casting floor; old bones
    // where the companies already died at the doors.
    scatter_decor(w, 0, 19, 2, 51, 41, 17, DECOR_PEBBLES,
                  {ScatterGround::Pavement});
    scatter_decor(w, 0, 2, 2, 17, 41, 19, DECOR_BONES,
                  {ScatterGround::Pavement});
    // Floor 1: braziers at the vault doors and landing corners, torch
    // lines in the counting rooms; spilled coin reads as pebble-glitter,
    // and the mint's burned dead lie among the holes.
    static constexpr int f1_braziers[8][2] = {
        {17, 11}, {17, 16}, {17, 27}, {17, 32},
        {54, 4},  {60, 4},  {54, 39}, {60, 39}};
    for (const auto& b : f1_braziers)
        paint_decor(w, 1, b[0], b[1], DECOR_BRAZIER);
    static constexpr int f1_torches[4][2] = {
        {3, 6}, {3, 12}, {3, 30}, {3, 36}};
    for (const auto& t : f1_torches)
        paint_decor(w, 1, t[0], t[1], DECOR_TORCH1);
    scatter_decor(w, 1, 19, 2, 51, 41, 13, DECOR_PEBBLES,
                  {ScatterGround::Pavement});
    scatter_decor(w, 1, 19, 2, 51, 41, 19, DECOR_BONES,
                  {ScatterGround::Pavement});
    // Floor 2: braziers at the dais corners only — NO decor on the two
    // 3-wide walks (finale fight lanes stay clean). The master ledger's
    // lectern is the exit object itself.
    paint_decor(w, 2, 51, 16, DECOR_BRAZIER);
    paint_decor(w, 2, 51, 27, DECOR_BRAZIER);
    paint_decor(w, 2, 57, 16, DECOR_BRAZIER);
    paint_decor(w, 2, 57, 27, DECOR_BRAZIER);

    // Stairs last (after decor), so no carve or dressing overwrites them.
    // The climb zigzags: east on floor 0, west on floor 1, east again.
    stair_pair(w, 0, 57, 21); // stair chamber UP / floor-1 landing DOWN
    stair_pair(w, 1, 9, 21);  // counting rooms UP / floor-2 landing DOWN

    w.type = SCEN_TYPE_CAN_EXIT; // the point is the climb, not extermination
    save_level_files(w, 18, "The Warm Mint",
                     {"Ledger, last entry this year.",
                      "The mint pays out tonight. Its",
                      "coin was the city's own bones,",
                      "melted and struck. The master",
                      "book sits on the crucible floor.",
                      "Kettle holds the door. Collect."},
                     9, 7000);
}

// 19 SETTLEMENT DAY (EPILOGUE — full circle): back at the Weycombe dikes
// where the year began. Rich or broke, the company clears its OWN dike
// one last time — the granary job again, on the house — then walks into
// winter quarters. The exit loops to level 1: next spring, the ledger
// opens again. A victory lap with a pulse; the last "boss" of the
// campaign is two rats in the grain, on purpose.
void build_settlement_day(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(19, true, &hooks);
    init_world(level, 1, 44, 30);
    GameWorld& w = level.world();

    // Pre-smooth: the north treeline, the floodwater pool (the winter
    // melt), the granary, and the winter quarters (the company's house).
    paint_rect(w.grid, 0, 0, 10, 2, PIX_TREE_M1);
    paint_rect(w.grid, 6, 24, 12, 28, PIX_WATER1);
    paint_rect(w.grid, 18, 4, 26, 10, PIX_WALL2);
    paint_rect(w.grid, 34, 8, 40, 13, PIX_WALL2);
    smooth_world(w);

    // Post-smooth: interiors + doors (pavement over wall makes the
    // doorway), the dike (a worn embankment path, west-east), the lanes,
    // and the marsh belt (walkable bog) parted around the pool.
    paint_pavement(w.grid, 19, 5, 25, 9);   // granary interior
    paint_pavement(w.grid, 22, 10, 22, 10); // granary door
    paint_pavement(w.grid, 35, 9, 39, 12);  // house interior
    paint_pavement(w.grid, 37, 13, 37, 13); // house door
    paint_path(w.grid, 2, 18, 41, 19);      // the dike
    paint_path(w.grid, 22, 11, 22, 17);     // granary lane
    paint_path(w.grid, 37, 14, 37, 17);     // house lane
    paint_marsh(w.grid, 0, 22, 43, 23); // the long fringe under the dike
    paint_marsh(w.grid, 0, 24, 5, 29);  // west of the pool
    paint_marsh(w.grid, 13, 24, 43, 29); // east of the pool

    // ARMIES (team 2 — what the flood left, one last time).
    static constexpr int marsh_slimes[4][2] = {
        {6, 22}, {16, 25}, {26, 23}, {34, 26}};
    for (const auto& s : marsh_slimes)
        place_living(w, FAMILY_MEDIUM_SLIME, 2, 0, s[0], s[1], 3);
    // The dike vermin, as in level 1.
    static constexpr int vermin[6][2] = {
        {12, 18}, {16, 19}, {20, 18}, {28, 18}, {24, 15}, {32, 19}};
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_ORC, 2, 0, vermin[i][0], vermin[i][1],
                     2 + (i % 2));
    // Inside the granary, gnawing the grain (ACT_GUARD door-squatters —
    // the beat waits for the player's walk-in).
    place_living(w, FAMILY_ORC, 2, 0, 20, 7, 3, true);
    place_living(w, FAMILY_ORC, 2, 0, 24, 7, 3, true);
    // The marsh gives one more push at dusk (tick 300 — the HUD's last
    // NEXT WAVE is the year's smallest fight).
    static constexpr int dusk_push[4][2] = {
        {14, 26}, {22, 26}, {30, 26}, {38, 25}};
    for (const auto& d : dusk_push)
        place_living(w, FAMILY_MEDIUM_SLIME, 2, 0, d[0], d[1], 3, false, false,
                     300);

    // START MARKERS (lead FIRST): the crew walks in from the west road,
    // on the dike — the fight starts on the dike, as it did in spring.
    place_start(w, 0, 4, 18); // LEAD, on the dike path
    place_start(w, 0, 2, 15);
    place_start(w, 0, 2, 21);
    place_start(w, 0, 6, 15);
    place_start(w, 0, 6, 21);
    place_start(w, 0, 8, 18);
    place_start(w, 0, 4, 14);
    place_start(w, 0, 4, 21);

    // TREASURE: the granary, stocked this time; the season's pay on the
    // winter-quarters table (what survived the mint); the pool fringe
    // cache, where Weycombe always keeps one.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 21, 6);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 23, 8);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 35, 10);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 39, 11);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 13, 26);

    // EXIT: the winter quarters, INSIDE the house between the pay bars —
    // collecting the season and ending it are the same walk. NO backtrack
    // exit, deliberately: the Warm Mint is burning behind the company and
    // the year ends here; the loop-to-1 exit is the only door.
    place_exit(w, 0, 37, 10, 1);

    // Decor (blocking decor off the dike, lanes, and doors): door torches
    // flanking the lanes, never on them, and a single homecoming brazier
    // by the house door — lit for the crew's return.
    paint_decor(w, 0, 21, 11, DECOR_TORCH1);
    paint_decor(w, 0, 23, 11, DECOR_TORCH1);
    paint_decor(w, 0, 36, 14, DECOR_TORCH1);
    paint_decor(w, 0, 38, 14, DECOR_TORCH1);
    paint_decor(w, 0, 40, 14, DECOR_BRAZIER);
    // Ambience (non-blocking): marsh bones — what the flood left — dike
    // pebbles, hedgerow shrubs under the treeline, sparse north-field
    // stones.
    scatter_decor(w, 0, 0, 22, 43, 29, 17, DECOR_BONES,
                  {ScatterGround::Marsh});
    scatter_decor(w, 0, 2, 16, 41, 21, 9, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 0, 3, 12, 8, 13, DECOR_SHRUB, {ScatterGround::Grass});
    scatter_boulders(w, 0, 12, 0, 43, 3, 27);

    w.type = 0; // kill-all, then walk to the winter-quarters exit
    save_level_files(w, 19, "Settlement Day",
                     {"Ledger, first thaw again. Home.",
                      "The season paid, the book square.",
                      "One warm coin nailed over the",
                      "door, so we remember the year.",
                      "The dike crawls. One last job,",
                      "on the house. Then winter beer."},
                     2, 2500);
}

} // namespace

void build_reckoning(const LevelDataHooks& hooks)
{
    build_ashfall_gate(hooks);
    build_warm_mint(hooks);
    build_settlement_day(hooks);
}

std::vector<ExpectedLevel> reckoning_expectations()
{
    // {id, floors, title, starts, t0 liv/gen, t1 liv/gen, t2 liv/gen,
    //  delayed spawns, specials-disabled, stairs-every-boundary, exit dests}
    return {
        {17, 1, "Ashfall Gate", 10, 0, 0, 0, 0, 48, 2, 14, 0, true, {18, 16}},
        {18, 3, "The Warm Mint", 10, 1, 0, 28, 0, 33, 1, 12, 0, true,
         {19, 17}},
        {19, 1, "Settlement Day", 8, 0, 0, 0, 0, 16, 0, 4, 0, true, {1}},
    };
}

} // namespace longseason
