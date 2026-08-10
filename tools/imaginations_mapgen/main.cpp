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
// Geometry, all from center (20.5, 20.5) on a 42x42 grid:
//   r >= 16.5        open sea
//   [13.5, 16.5)     sunlit shore meadow (the landing ring)
//   [10.5, 13.5)     island grass (the approach ring)
//   [8.0, 10.5)      the moat ("the sea in the middle")
//   wall rect 15..26 the castle, cobbled court inside, carpet dais at
//                    19..22 where the Sea Wizard waits
// Four paved causeways cross the moat at the compass points into four
// 2-tile gates. A flight potion near the south landing offers the "very
// magical" alternative: fly the moat and the wall face straight into
// the court.
void build_raspberry_isle()
{
    LevelRuntimeData level(1, true, &headless_level_data_hooks());
    GameWorld& w = level.world();
    og::mapgen::init_world(w, 1, 42, 42);
    PixieData& g = w.grid;
    const double cx = 20.5;
    const double cy = 20.5;

    // Terrain, inside out (genre tiles only; the autotiler shapes every
    // shore and wall face in smooth_world below).
    og::mapgen::paint_ring(g, cx, cy, 16.5, 100.0, PIX_WATER1); // open sea
    og::mapgen::paint_ring(g, cx, cy, 13.5, 16.5, PIX_GRASS_LIGHT_1);
    og::mapgen::paint_ring(g, cx, cy, 8.0, 10.5, PIX_WATER1);   // the moat
    og::mapgen::paint_rect(g, 15, 15, 26, 26, PIX_WALL2);       // castle wall
    og::mapgen::paint_rect(g, 16, 16, 25, 25, PIX_COBBLE_1);    // the court
    og::mapgen::paint_rect(g, 19, 19, 22, 22, PIX_CARPET_M);    // the dais
    // Four 2-tile gates at the compass midpoints of the wall.
    og::mapgen::paint_rect(g, 20, 15, 21, 15, PIX_COBBLE_1); // north gate
    og::mapgen::paint_rect(g, 20, 26, 21, 26, PIX_COBBLE_1); // south gate
    og::mapgen::paint_rect(g, 15, 20, 15, 21, PIX_COBBLE_1); // west gate
    og::mapgen::paint_rect(g, 26, 20, 26, 21, PIX_COBBLE_1); // east gate

    og::mapgen::smooth_world(w);

    // The causeways cross the moat into the gates — pavement is
    // autotiler-inert, so they go in after the smooth and stay crisp.
    paint_pavement(g, 20, 9, 21, 14);  // north
    paint_pavement(g, 20, 27, 21, 32); // south
    paint_pavement(g, 9, 20, 14, 21);  // west
    paint_pavement(g, 27, 20, 32, 21); // east

    // --- The garrison (team 1). ---------------------------------------------
    // The Sea Wizard on the dais: an ambush post (guard wake policy) — he
    // holds the keep until the crew breaches the court, then fights.
    walker* boss =
        og::mapgen::place_living(w, FAMILY_MAGE, 1, 0, 20, 20, 4, true);
    if (boss != nullptr)
        boss->stats()->name = "Sea Wizard"; // 10 chars: fits the 11-char field
    // A bone ward just inside each gate, posted (they wake one by one as
    // the crew reaches their gate — four small fights, not one big one).
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 20, 17, 1, true);
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 21, 24, 1, true);
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 17, 21, 1, true);
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 24, 20, 1, true);
    // Two court roamers that spill out over the causeways to meet the
    // charge halfway — the submitted design says to run at them at once.
    og::mapgen::place_living(w, FAMILY_ORC, 1, 0, 18, 22, 1);
    og::mapgen::place_living(w, FAMILY_ELF, 1, 0, 23, 18, 1);
    // Two shore patrols on the approach ring for first contact.
    og::mapgen::place_living(w, FAMILY_SKELETON, 1, 0, 12, 28, 1);
    og::mapgen::place_living(w, FAMILY_ORC, 1, 0, 29, 13, 1);
    // The wizard's college: a mage tower on the northeast meadow, its
    // slow level-1 trickle marching for the causeways. Open ground on
    // every side (the spawn-egress audit holds it to that).
    og::mapgen::place_generator(w, FAMILY_TOWER, 1, 0, 29, 10, 1);

    // --- The crew (team 0). --------------------------------------------------
    // Twelve start markers spread around the landing ring — the whole
    // point of the submitted design. Lead FIRST (south beach, facing the
    // causeway); deploy consumes markers in oblist order, so a small crew
    // lands scattered around the island and regroups on the charge.
    og::mapgen::place_start(w, 0, 20, 33); // lead, south landing
    og::mapgen::place_start(w, 0, 20, 7);  // north
    og::mapgen::place_start(w, 0, 33, 20); // east
    og::mapgen::place_start(w, 0, 7, 20);  // west
    og::mapgen::place_start(w, 0, 11, 11); // the four corners of the isle
    og::mapgen::place_start(w, 0, 30, 11);
    og::mapgen::place_start(w, 0, 11, 30);
    og::mapgen::place_start(w, 0, 30, 30);
    og::mapgen::place_start(w, 0, 17, 34); // second-wave beach flanks
    og::mapgen::place_start(w, 0, 24, 34);
    og::mapgen::place_start(w, 0, 17, 6);
    og::mapgen::place_start(w, 0, 24, 6);

    // --- Treasure. -----------------------------------------------------------
    // The magic: potions on the west and east approaches, and a flight
    // potion by the south landing — drink it and cross the moat and the
    // wall face on wings instead of fighting through a gate.
    og::mapgen::place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 12, 21);
    og::mapgen::place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 29, 20);
    og::mapgen::place(w, Order::Treasure, FAMILY_FLIGHT_POTION, 0, 0, 17, 31);
    // Rations for the long walk around the moat.
    og::mapgen::place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 10, 20);
    og::mapgen::place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 31, 21);
    og::mapgen::place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 20, 5);
    // The wizard's hoard, in the four corners of the court.
    og::mapgen::place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 17, 17);
    og::mapgen::place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 24, 24);
    og::mapgen::place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 17, 24);
    og::mapgen::place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 24, 17);
    // The walk-out exit on the south landing beach, beside the lead
    // marker: clear the isle, then sail home. Destination 1 = itself —
    // the loop-home rule for the newest level of a growing campaign.
    og::mapgen::place_exit(w, 0, 23, 35, 1);

    // --- Set dressing (after every entity, so scatters keep clearance). -----
    // Torches on the wall corners and flanking every gate; braziers on
    // the dais back row behind the wizard. All on blocking bases or the
    // dais back rank — off every fight lane.
    for (const auto& t : {std::pair{15, 15}, {26, 15}, {15, 26}, {26, 26},
                          {18, 15}, {23, 15}, {18, 26}, {23, 26}})
        og::mapgen::paint_decor(w, 0, t.first, t.second, DECOR_TORCH1);
    for (const auto& t : {std::pair{15, 18}, {15, 23}, {26, 18}, {26, 23}})
        og::mapgen::paint_decor(w, 0, t.first, t.second, DECOR_TORCH1);
    og::mapgen::paint_decor(w, 0, 19, 19, DECOR_BRAZIER);
    og::mapgen::paint_decor(w, 0, 22, 19, DECOR_BRAZIER);
    // Old landings: bones where the shore patrols walk.
    og::mapgen::paint_decor(w, 0, 13, 26, DECOR_BONES);
    og::mapgen::paint_decor(w, 0, 28, 15, DECOR_BONES);
    // Non-blocking ambience over the whole isle: shrubs on the approach
    // grass, pebbles on the shore meadow.
    og::mapgen::scatter_decor(w, kScatterSeed, 0, 4, 4, 37, 37, 17,
                              DECOR_SHRUB,
                              {og::mapgen::ScatterGround::Grass});
    og::mapgen::scatter_decor(w, kScatterSeed, 0, 4, 4, 37, 37, 11,
                              DECOR_PEBBLES,
                              {og::mapgen::ScatterGround::LightGrass});

    save_level_files(w, 1, "The Raspberry Isle",
                     {
                         "From the dream-log of a young",
                         "commander: an island, the sea",
                         "all around us. We land on the",
                         "shores and charge the middle!",
                         "The Sea Wizard holds the keep",
                         "beyond the moat. Storm it!",
                     },
                     2, 5000);
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
                {1, 1, "The Raspberry Isle", 12, 0, 0, 9, 1, 0, 0, {1}},
            };
            for (const ExpectedLevel& e : expectations)
                self_check_level(e, registered);
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
