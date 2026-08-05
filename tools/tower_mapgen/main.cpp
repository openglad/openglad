/* The Endless Tower campaign generator.
 *
 * Produces campaigns/tower/ (the source tree the build
 * composes into builtin/tower.glad): the Gate-ONLY package for the
 * Tower Climb mode (tower-triple spec §5.2, D7). Contents EXACTLY:
 * campaign.yaml (`mode: tower`, first_level 700), icon.png, and the
 * authored antechamber scen700 "The Gate" + its grid PNG. The package must
 * NEVER contain scen ids >= 701: mounted campaigns are PREPENDED to the
 * search path and a shipped floor file would shadow — and freeze — every
 * generated run (a unit test on the zip member list enforces this too).
 * Generated floors are written at run time to <user_path>/scen + pix by
 * og::tower::generate_tower_floor_to_user_dir.
 *
 * SDL-free; mirrors tools/concept_mapgen (headless platform glue, zips a
 * campaign package, mounts it, self-checks the level by reloading it).
 *
 * Usage: tower_mapgen [output-dir]   (default: campaigns/tower)
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/tower_constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
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
    static std::uint32_t state = 20260713u;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

namespace towergen {
namespace {

namespace fs = std::filesystem;
int g_errors = 0;

// SCENARIO INFORMATION dialog budget (33 glyphs per briefing line).
constexpr std::size_t kBriefingLineBudget = 33;

void fail(const std::string& message)
{
    std::fprintf(stderr, "tower_mapgen: ERROR: %s\n", message.c_str());
    ++g_errors;
}

PixieData make_grid(int tw, int th, unsigned char fill)
{
    auto* buf = new unsigned char[static_cast<std::size_t>(tw) * th];
    std::fill(buf, buf + static_cast<std::size_t>(tw) * th, fill);
    return PixieData(1, static_cast<unsigned char>(tw),
                     static_cast<unsigned char>(th), buf);
}

void paint(PixieData& g, int tx, int ty, unsigned char tile)
{
    if (tx >= 0 && ty >= 0 && tx < g.w && ty < g.h)
        g.data[tx + ty * g.w] = tile;
}

void paint_rect(PixieData& g, int tx0, int ty0, int tx1, int ty1,
                unsigned char tile)
{
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, tile);
}

walker* place(GameWorld& world, Order order, int family, int team, int tx,
              int ty)
{
    walker* w = (order == Order::Treasure) ? world.add_fx_ob(order, family)
                                           : world.add_ob(order, family);
    if (w == nullptr)
    {
        fail(std::format("could not place order {} family {}",
                         static_cast<int>(order), family));
        return nullptr;
    }
    w->set_floor(0);
    w->setxy(static_cast<short>(tx * GRID_SIZE),
             static_cast<short>(ty * GRID_SIZE));
    w->set_team_num(static_cast<unsigned char>(team));
    w->set_real_team_num(static_cast<unsigned char>(team));
    return w;
}

// --- The Gate (scen 700): a ~20x15 authored antechamber, no foes. ------------
void build_gate()
{
    LevelRuntimeData level(og::kTowerGateLevel, true,
                           &headless_level_data_hooks());
    GameWorld& w = level.world();
    const int tw = 20;
    const int th = 15;
    w.grid = make_grid(tw, th, PIX_PAVEMENT1);
    w.pixmaxx = w.grid.w * GRID_SIZE;
    w.pixmaxy = w.grid.h * GRID_SIZE;
    w.mysmoother.set_target(w.grid);

    // Walls all around; the hall narrows toward the northern stair door.
    paint_rect(w.grid, 0, 0, tw - 1, 0, PIX_H_WALL1);
    paint_rect(w.grid, 0, th - 1, tw - 1, th - 1, PIX_H_WALL1);
    paint_rect(w.grid, 0, 0, 0, th - 1, PIX_H_WALL1);
    paint_rect(w.grid, tw - 1, 0, tw - 1, th - 1, PIX_H_WALL1);
    paint_rect(w.grid, 1, 1, 5, 1, PIX_H_WALL1);
    paint_rect(w.grid, 14, 1, tw - 2, 1, PIX_H_WALL1);

    // A carpet aisle from the muster floor to the doorway.
    paint_rect(w.grid, 9, 2, 10, th - 3, PIX_CARPET_M);
    // Brazier posts flanking the aisle mouth and the door.
    paint(w.grid, 7, 3, PIX_BRAZIER1);
    paint(w.grid, 12, 3, PIX_BRAZIER1);
    paint(w.grid, 7, th - 4, PIX_BRAZIER1);
    paint(w.grid, 12, th - 4, PIX_BRAZIER1);
    // Column pair at the threshold.
    paint(w.grid, 6, 6, PIX_COLUMN1);
    paint(w.grid, 13, 6, PIX_COLUMN1);

    // Ten start markers (lead FIRST — deploy consumes markers in oblist
    // order), 2x2 footprints on a 2-tile grid across the muster floor.
    for (int i = 0; i < 10; ++i)
        place(w, Order::Special, FAMILY_RESERVED_TEAM, 0, 3 + (i % 5) * 3,
              10 + (i / 5) * 2);

    // The single exit: Floor 1. SCEN_TYPE_CAN_EXIT (no foes to clear) +
    // SCEN_TYPE_TOWER (forward sim identity; display-only in v1).
    walker* gate_exit =
        place(w, Order::Treasure, FAMILY_EXIT, 0, 9, 2);
    if (gate_exit != nullptr)
        gate_exit->stats()->set_level(og::kTowerFirstFloorLevel);

    w.type = static_cast<char>(SCEN_TYPE_CAN_EXIT | SCEN_TYPE_TOWER);
    w.title = "The Gate";
    // No free gold: get_time_bonus returns 0 when the limit is <= 0.
    w.time_bonus_limit = 0;
    w.par_value = 0;

    og::data::LevelFileMetadata metadata;
    metadata.grid_file = std::format("scen{:04d}", og::kTowerGateLevel);
    metadata.generated = true; // provenance mark: this scen is tool output
    metadata.description = {
        "The Tower has no end.",
        "Each floor bears its number.",
        "Death ends the climb.",
        "No second lives within.",
    };
    for (const std::string& line : metadata.description)
        if (line.size() > kBriefingLineBudget)
            fail(std::format("briefing line '{}' overflows the {}-char budget",
                             line, kBriefingLineBudget));

    const std::string user = get_user_path();
    const std::string fss =
        user + std::format("temp/scen/scen{}.fss", og::kTowerGateLevel);
    og::data::LevelFileIoError err = og::data::LevelFileIoError::None;
    if (!og::data::save_level_scenario_file(w, fss, metadata, &err))
    {
        fail(std::format("failed to write {}", fss));
        return;
    }
    const std::string png =
        user + "temp/pix/" + metadata.grid_file + ".png";
    if (!write_pixie_png(png.c_str(), w.grid))
        fail(std::format("failed to write {}", png));
    std::printf("tower_mapgen: built %d '%s'\n", og::kTowerGateLevel,
                w.title.c_str());
}

void write_campaign_yaml(const std::string& path)
{
    std::ofstream out(path);
    out << "format_version:  1\n"
        << "title:           The Endless Tower\n"
        << "version:         1\n"
        << "first_level:     700\n"
        << "suggested_power: 1\n"
        << "mode:            tower\n"
        << "authors:         OpenGlad\n"
        << "contributors:    \n"
        << "\n"
        << "description:     |\n"
        << "    An endless, seeded climb. Every\n"
        << "    floor is built when you reach\n"
        << "    it; every run ends. Gold and\n"
        << "    veterans persist between runs —\n"
        << "    how high can your crew climb?\n";
    if (!out)
        fail(std::format("cannot write {}", path));
}

// A 32x32 icon: a tower silhouette with battlements and a lit gate.
void write_icon(const std::string& path)
{
    constexpr int kSize = 32;
    PixieData icon = make_grid(kSize, kSize, 0);
    // The shaft.
    for (int y = 6; y < 30; ++y)
        for (int x = 11; x < 21; ++x)
            paint(icon, x, y, 10);
    // Battlements.
    for (int x = 10; x < 22; x += 4)
        for (int y = 3; y < 6; ++y)
        {
            paint(icon, x, y, 10);
            paint(icon, x + 1, y, 10);
        }
    // The gate.
    for (int y = 24; y < 30; ++y)
        for (int x = 14; x < 18; ++x)
            paint(icon, x, y, 88);
    // Window slits.
    for (int y = 9; y < 21; y += 5)
        for (int x = 15; x < 17; ++x)
            paint(icon, x, y, 88);
    if (!write_pixie_png(path.c_str(), icon))
        fail(std::format("cannot write {}", path));
}

// Reload the packaged Gate and audit it (concept_mapgen self-check shape).
void self_check_gate()
{
    LevelRuntimeData level(og::kTowerGateLevel, true,
                           &headless_level_data_hooks());
    if (!level.load())
    {
        fail("self-check: scen700 failed to load");
        return;
    }
    GameWorld& world = level.world();
    if (world.floor_count() != 1)
        fail(std::format("self-check: Gate floor_count {} != 1",
                         world.floor_count()));
    if (world.title != "The Gate")
        fail(std::format("self-check: title '{}' != 'The Gate'", world.title));
    if ((world.type & GameWorld::TYPE_TOWER) == 0)
        fail("self-check: Gate missing TYPE_TOWER");
    if ((world.type & GameWorld::TYPE_CAN_EXIT_WHENEVER) == 0)
        fail("self-check: Gate missing SCEN_TYPE_CAN_EXIT");
    if (world.time_bonus_limit != 0 || world.par_value != 0)
        fail("self-check: Gate must carry no time bonus / par gold");

    int starts = 0;
    int livings = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->query_order() == Order::Living)
            ++livings;
        else if (ob->query_order() == Order::Special &&
                 ob->family() == FAMILY_RESERVED_TEAM && ob->team_num() == 0)
            ++starts;
    }
    if (starts != 10)
        fail(std::format("self-check: {} start markers, expected 10", starts));
    if (livings != 0)
        fail(std::format("self-check: the Gate must hold no foes, found {}",
                         livings));

    int exits = 0;
    for (const auto& uptr : world.fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Treasure &&
            ob->family() == FAMILY_EXIT)
        {
            ++exits;
            if (ob->stats()->level() != og::kTowerFirstFloorLevel)
                fail(std::format("self-check: exit destination {} != {}",
                                 ob->stats()->level(),
                                 og::kTowerFirstFloorLevel));
        }
    }
    if (exits != 1)
        fail(std::format("self-check: {} exits, expected 1", exits));
}

// D7 shadowing invariant, enforced at build time too: the staged package
// dir must contain scen700 and NOTHING with an id >= 701.
void check_no_floor_members(const std::string& temp_scen_dir)
{
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(temp_scen_dir, ec))
    {
        const std::string name = entry.path().filename().string();
        if (name != std::format("scen{}.fss", og::kTowerGateLevel))
            fail(std::format(
                "package would ship '{}' — only the Gate may be a member",
                name));
    }
}

} // namespace
} // namespace towergen

int main(int argc, char* argv[])
{
    using namespace towergen;
    namespace fs = std::filesystem;

    const std::string out_tree =
        (argc > 1) ? argv[1] : "campaigns/tower";
    const fs::path out_abs = fs::absolute(out_tree);

    fs::path scratch;
    if (const char* preset = std::getenv("OPENGLAD_CONFIG_DIR");
        preset == nullptr || preset[0] == '\0')
    {
        scratch = fs::temp_directory_path() /
                  ("tower_mapgen_" + std::to_string(getpid()));
        fs::create_directories(scratch);
        setenv("OPENGLAD_CONFIG_DIR", scratch.c_str(), 1);
    }

    init_logging();
    io_init(argc, argv);
    if (get_mounted_campaign() != "gladiator")
    {
        std::fprintf(stderr, "tower_mapgen: ERROR: stock campaign not mounted; "
                             "run next to staged assets (build dir)\n");
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

    build_gate();
    check_no_floor_members(user + "temp/scen/");

    const std::string glad_path = user + "campaigns/tower.glad";
    std::remove(glad_path.c_str());
    if (zip_contents_with_error(user + "temp/", glad_path) !=
        ArchiveIoError::None)
        fail(std::format("failed to zip campaign into {}", glad_path));

    if (g_errors == 0)
    {
        if (mount_campaign_package_with_error("tower") !=
            CampaignPackageIoError::None)
        {
            fail("failed to mount the produced campaign");
        }
        else
        {
            self_check_gate();
            (void)unmount_campaign_package_with_error("tower");
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
            std::printf("tower_mapgen: wrote %s\n", out_abs.c_str());
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
        std::fprintf(stderr, "tower_mapgen: FAILED with %d error(s)\n",
                     g_errors);
    return result;
}
