// Issue #12 (2013): "Openglad crashes while editing a scenario".
//
// The reporter's steps, verbatim: level editor -> new campaign -> make the
// level 30x30 -> place several hundred level-1 clerics along the top -> start
// drawing a wall (the same tile as the level's outer walls) on the bottom row
// near the middle and draw a box -> SIGSEGV in draw_smallHealthBar(w), called
// from walker::draw, from viewscreen::draw_obs, from LevelData::draw, from
// LevelEditorData::draw, with `w` a walker whose fields were garbage.
//
// Mechanism (read off the 2013 sources at 9386730c): `smoother` stored a RAW
// POINTER to the grid buffer, and LevelData::delete_grid / create_new_grid /
// resize_grid all freed or replaced that buffer without re-targeting
// mysmoother -- only the two scenario-LOAD paths ever re-targeted it. So both
// "File > Level > New" and "Level > Details > Map size..." stranded the
// smoother on a freed block. The terrain brush smooths by default and calls
// smoother::smooth() over the 3x3 neighbourhood of every stroke, so each wall
// stroke wrote up to nine tile bytes through the dangling pointer -- into the
// memory the several hundred clerics placed in between had been allocated out
// of. The next redraw then read a clobbered walker and died on the first
// dereference in draw_smallHealthBar.
//
// Fixed on 2026-02-13 by 10c4bb5f ("data: keep smoother synced with grid and
// harden scenario grid loading"), which added mysmoother.reset() to
// delete_grid and mysmoother.set_target(grid) to create_new_grid and
// resize_grid; the target is a std::span today and the write path is
// bounds-checked. These tests pin that invariant (GameWorld::smoothers_in_sync)
// and replay the reporter's scenario twice: once against the draw path
// directly, once through the editor's own event loop.
//
// A smoother borrows TWO pointers, and the same discipline covers both: the
// grid view above, and the RNG its autotile variants are drawn from. The last
// test in this file pins the second one -- LevelRuntimeData::load builds a
// level in a STACK-LOCAL GameWorld and moves its stacked floors out, and until
// this file's commit only the grid span followed them, leaving every loaded
// floor's smoother pointing at a destroyed world's RNG.
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/level_visuals.h>
#include <openglad/interface/render/radar.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/level_editor_state.h>
#include <openglad/platform/game_session.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include "test_input_helpers.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

// From level_editor.cpp
Sint32 level_editor();
// From level_editor.cpp (TESTING, tests/coverage_internal/level_editor_internal.inc):
// the editor's own level, readable only after level_editor() has returned.
LevelRuntimeData* level_editor_testing_level();
bool level_editor_testing_terrain_smoothing();
// From picker_dialogs.cpp (TESTING): queue answers for yes_or_no_prompt().
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
// From level_editor_ui.cpp (TESTING): queue answers for prompt_for_string().
void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);

static inline LevelEditorState& eds() { return *og::runtime::current_session->editor_; }

namespace {

constexpr int kMapW = 30;
constexpr int kMapH = 30;
constexpr int kClerics = 400;

// The reporter's wall box, in grid cells: bottom middle of the 30x30 map.
constexpr int kBoxX0 = 12;
constexpr int kBoxX1 = 20;
constexpr int kBoxY0 = 22;
constexpr int kBoxY1 = 29;

screen* game_screen()
{
    return og::runtime::current_session->myscreen_;
}

// The reporter's level: brand-new campaign level resized to 30x30.
void make_30x30_level()
{
    screen* s = game_screen();
    s->world().delete_objects();
    s->world().create_new_grid();           // editor's File > Level > New: 40x60
    s->world().resize_grid(kMapW, kMapH);   // Level > Details > Map size...
    s->set_level_draw_pos(0, 0);
}

// "several hundred level 1 clerics on the top of the level"
int place_clerics()
{
    screen* s = game_screen();
    int placed = 0;
    for (int i = 0; i < kClerics; ++i)
    {
        walker* w = s->world().add_ob(Order::Living, FAMILY_CLERIC);
        if (w == nullptr)
            break;
        const int col = i % kMapW;
        const int row = i / kMapW;
        w->setxy(col * GRID_SIZE, row * GRID_SIZE);
        w->set_floor(0);
        w->set_team_num(1);
        w->stats()->set_level(1);
        w->set_dead(0);
        // Chip each cleric so draw_small_health_bar() actually paints its bar
        // (a full-HP walker draws none). The 2013 SIGSEGV was on that
        // function's first dereference, so this makes the frame prove the
        // reporter's crash site was entered.
        const float maxhp = w->stats()->max_hitpoints();
        if (maxhp > 0.0f)
            w->stats()->set_hitpoints(
                maxhp * (0.25f + 0.05f * static_cast<float>(i % 10)));
        ++placed;
    }
    return placed;
}

// The wall box outline, in the reporter's drawing order.
std::vector<std::pair<int, int>> wall_box_cells()
{
    std::vector<std::pair<int, int>> stroke;
    for (int x = kBoxX0; x <= kBoxX1; ++x)
    {
        stroke.emplace_back(x, kBoxY1);
        stroke.emplace_back(x, kBoxY0);
    }
    for (int y = kBoxY0; y <= kBoxY1; ++y)
    {
        stroke.emplace_back(kBoxX0, y);
        stroke.emplace_back(kBoxX1, y);
    }
    return stroke;
}

// The test binary is shared, so a cfg override must not outlive the test.
struct CfgOverrideGuard
{
    std::map<std::string, std::map<std::string, std::string>> saved;
    CfgOverrideGuard() : saved(cfg.overrides) {}
    ~CfgOverrideGuard() { cfg.overrides = saved; }
};

// Leave the session's level the way the group's other tests expect to find it.
void restore_shared_level()
{
    screen* s = game_screen();
    s->world().delete_objects();
    s->world().create_new_grid();
    s->set_level_draw_pos(0, 0);
}

void draw_editor_frame(screen* s, radar& myradar)
{
    s->begin_gameplay_frame();
    s->clearbuffer();
    s->viewob[0]->editor_floor_override_ = 0;
    s->viewob[0]->editor_authoring_view_ = true;
    s->draw_level();
    myradar.draw(&s->level_runtime_data());
    s->viewob[0]->editor_floor_override_ = -1;
    s->viewob[0]->editor_authoring_view_ = false;
}

} // namespace

// The reporter's scenario, end to end, against the real render path:
// LevelRuntimeData::draw -> viewscreen::redraw -> draw_obs -> draw_walker ->
// draw_small_health_bar, redrawn between every wall stroke.
TEST(Issue12EditorWallCrash, wall_box_on_bottom_row_with_hundreds_of_clerics)
{
    trace_clear();
    screen* s = game_screen();
    ASSERT_TRUE(s != nullptr);
    ASSERT_TRUE(s->viewob[0] != nullptr);

    CfgOverrideGuard cfg_guard;
    // The 2013 SIGSEGV was inside draw_smallHealthBar(); today that is
    // draw_small_health_bar(), gated on effects/mini_hp_bar. The throwaway test
    // config dir carries no settings file, so turn the gate on explicitly --
    // without it the reporter's exact crash frame is never entered.
    cfg.apply_override("effects", "mini_hp_bar", "on");
    cfg.apply_override("effects", "damage_numbers", "on");

    make_30x30_level();
    ASSERT_EQ(kMapW, s->world().grid.w);
    ASSERT_EQ(kMapH, s->world().grid.h);
    ASSERT_TRUE(s->world().smoothers_in_sync())
        << "the resize left the smoother on the freed 40x60 buffer";

    ASSERT_EQ(kClerics, place_clerics());

    // The editor owns a radar bound to viewob[0]; it starts once on entry and
    // is update()d after every terrain stroke.
    radar myradar(s->viewob[0].get(), s, 0);
    myradar.force_lower_position = true;
    myradar.start(&s->level_runtime_data());

    // "start drawing a wall ... on the bottom row near the middle. Draw a box
    // from there."
    const std::vector<std::pair<int, int>> stroke = wall_box_cells();
    for (const auto& [gx, gy] : stroke)
    {
        // set_terrain
        PixieData& g = s->world().grid_for_floor(0);
        ASSERT_TRUE(gx >= 0 && gx < g.w && gy >= 0 && gy < g.h);
        g.data[static_cast<std::size_t>(gy * g.w + gx)] =
            static_cast<unsigned char>(PIX_H_WALL1);

        // smooth the 3x3 neighbourhood, exactly as the terrain brush does
        for (int i = gx - 1; i <= gx + 1; ++i)
            for (int j = gy - 1; j <= gy + 1; ++j)
                if (i >= 0 && i < g.w && j >= 0 && j < g.h)
                    s->world().smoother_for_floor(0).smooth(i, j);

        myradar.update(&s->level_runtime_data());

        draw_editor_frame(s, myradar);
    }

    // Every stroked cell must be wall in the LIVE grid. Read it through a
    // PROBE smoother pointed straight at the grid buffer, not through the
    // world's own smoother: the brush writes the tile itself and only SMOOTHS
    // through the world smoother, so a stranded one would not stop the wall
    // from landing -- and reading through it would be the very use-after-free
    // this test exists to catch. Staleness is pinned separately, by
    // smoothers_in_sync() below.
    {
        smoother probe;
        probe.set_target(s->world().grid_for_floor(0));
        for (const auto& [gx, gy] : stroke)
        {
            EXPECT_EQ(TYPE_WALL, probe.query_genre_x_y(gx, gy))
                << "cell " << gx << "," << gy << " is not wall in the live grid";
        }
    }

    // Scroll to the bottom of the map (the reporter was working there) and
    // redraw again, so the off-screen clerics at the top are still iterated.
    s->set_level_draw_pos(0, (kMapH * GRID_SIZE) - 200);
    for (int frame = 0; frame < 10; ++frame)
        draw_editor_frame(s, myradar);

    EXPECT_TRUE(s->world().smoothers_in_sync())
        << "a stroke left the smoother pointing away from the grid";

    // Proof media: the editor's own frame, saved through the game's screenshot
    // path (lands as screenshot<N>.png under OPENGLAD_CONFIG_DIR).
    if (std::getenv("OPENGLAD_ISSUE12_SHOT") != nullptr)
    {
        s->set_level_draw_pos(0, 0);
        draw_editor_frame(s, myradar);
        EXPECT_TRUE(s->save_screenshot()) << "top-of-map frame";
        // Centre the painted wall box.
        s->set_level_draw_pos(110, (kMapH * GRID_SIZE) - 200);
        draw_editor_frame(s, myradar);
        EXPECT_TRUE(s->save_screenshot()) << "bottom-of-map frame";

        // The harness points OPENGLAD_CONFIG_DIR at a throwaway directory and
        // removes it on teardown, so copy the frames out now.
        const char* out = std::getenv("OPENGLAD_ISSUE12_SHOT_DIR");
        const char* home = std::getenv("OPENGLAD_CONFIG_DIR");
        if (out != nullptr && home != nullptr)
        {
            std::error_code ec;
            std::filesystem::create_directories(out, ec);
            for (const auto& p : std::filesystem::directory_iterator(home, ec))
            {
                const std::string name = p.path().filename().string();
                if (name.rfind("screenshot", 0) == 0)
                    std::filesystem::copy_file(
                        p.path(), std::filesystem::path(out) / name,
                        std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
    }

    restore_shared_level();
}

// Root-cause pin. The smoother holds a NON-OWNING view of the grid buffer, so
// every path that frees or replaces a grid has to re-target it. Pinned through
// the public read path (what the terrain brush actually uses) AND through
// GameWorld::smoothers_in_sync(), which answers the same question without
// dereferencing a possibly-freed buffer.
TEST(Issue12EditorWallCrash, smoother_follows_the_grid_across_a_map_resize)
{
    screen* s = game_screen();
    GameWorld& w = s->world();

    w.delete_objects();

    // File > Level > New: a fresh 40x60 grid, and a smoother that sees it.
    w.create_new_grid();
    ASSERT_EQ(40, w.grid.w);
    ASSERT_EQ(60, w.grid.h);
    EXPECT_TRUE(w.smoothers_in_sync()) << "create_new_grid left the smoother stale";
    w.grid.data[static_cast<std::size_t>(59 * 40 + 39)] =
        static_cast<unsigned char>(PIX_CARPET_M);
    EXPECT_EQ(PIX_CARPET_M, w.mysmoother.query_x_y(39, 59))
        << "the smoother is not reading the new grid";

    // A marker in the OLD buffer, at a cell that will be outside the new one.
    // If the smoother is left behind, the read below returns this byte instead
    // of the out-of-range sentinel -- deterministically, not by luck.
    w.grid.data[static_cast<std::size_t>(50 * 40 + 35)] =
        static_cast<unsigned char>(PIX_WATER1);

    // Level > Details > Map size... 30x30.
    w.resize_grid(kMapW, kMapH);
    EXPECT_TRUE(w.smoothers_in_sync()) << "resize_grid left the smoother stale";

    // A marker byte written straight into the new grid must be visible through
    // the smoother: a stale view would read the freed 40x60 buffer instead.
    PixieData& g = w.grid_for_floor(0);
    g.data[static_cast<std::size_t>((kMapH - 1) * g.w + 15)] =
        static_cast<unsigned char>(PIX_CARPET_M);
    EXPECT_EQ(PIX_CARPET_M, w.mysmoother.query_x_y(15, kMapH - 1))
        << "smoother is not reading the resized grid";

    // ... and its bounds must be the new ones: (35, 50) is inside the old
    // 40x60 buffer and outside the new 30x30 one, so it must read as the
    // out-of-range sentinel, not the PIX_WATER1 marker planted above.
    EXPECT_EQ(PIX_GRASS1, w.mysmoother.query_x_y(35, 50))
        << "smoother kept the pre-resize extents";

    // The write path travels the same view: smoothing the bottom-middle cell
    // (the reporter's stroke) must land in the new grid, never past it.
    // A lone carpet tile autotiles deterministically (surround 0 ->
    // PIX_CARPET_SMALL_TINY, no RNG), so the write is observable.
    w.smoother_for_floor(0).smooth(15, kMapH - 1);
    EXPECT_EQ(PIX_CARPET_SMALL_TINY, static_cast<int>(
        g.data[static_cast<std::size_t>((kMapH - 1) * g.w + 15)]))
        << "smooth() did not write through to the resized grid";

    // ... and when the grid goes away entirely the smoother must let go of it.
    w.delete_grid();
    EXPECT_FALSE(w.mysmoother.has_target())
        << "delete_grid freed the buffer but left the smoother pointing at it";
    EXPECT_TRUE(w.smoothers_in_sync());

    restore_shared_level();
}

// Teeth for the pin above, demonstrated without any use-after-free: the same
// public reads DO tell a pre-resize target apart from a re-synced one. On the
// 2013 tree LevelData::resize_grid left the smoother on the first buffer,
// which is the `set_target(big)` state below -- so that pin fails there, and
// passes only once resize_grid re-targets (the `small` state, which is what
// GameWorld::resize_grid does today).
TEST(Issue12EditorWallCrash, smoother_pin_discriminates_a_stale_target)
{
    auto* big_bytes = new unsigned char[40 * 60];
    std::fill_n(big_bytes, 40 * 60, static_cast<unsigned char>(PIX_GRASS1));
    auto* small_bytes = new unsigned char[kMapW * kMapH];
    std::fill_n(small_bytes, kMapW * kMapH,
                static_cast<unsigned char>(PIX_GRASS1));

    PixieData big(1, 40, 60, big_bytes);           // the 40x60 new-level grid
    PixieData small(1, kMapW, kMapH, small_bytes); // after Map size... 30x30
    big.data[static_cast<std::size_t>(50 * 40 + 35)] =
        static_cast<unsigned char>(PIX_WATER1);
    small.data[static_cast<std::size_t>((kMapH - 1) * kMapW + 15)] =
        static_cast<unsigned char>(PIX_CARPET_M);

    smoother sm;
    EXPECT_FALSE(sm.has_target());
    EXPECT_FALSE(sm.targets(big));

    sm.set_target(big);   // 2013: the target resize_grid never updated
    EXPECT_TRUE(sm.has_target());
    EXPECT_TRUE(sm.targets(big));
    EXPECT_FALSE(sm.targets(small)) << "targets() cannot see a stale target";
    EXPECT_NE(PIX_CARPET_M, sm.query_x_y(15, kMapH - 1))
        << "content pin cannot see a stale target";
    EXPECT_NE(PIX_GRASS1, sm.query_x_y(35, 50))
        << "bounds pin cannot see a stale target";

    sm.set_target(small); // today: GameWorld::resize_grid re-targets
    EXPECT_TRUE(sm.targets(small));
    EXPECT_FALSE(sm.targets(big));
    EXPECT_EQ(PIX_CARPET_M, sm.query_x_y(15, kMapH - 1));
    EXPECT_EQ(PIX_GRASS1, sm.query_x_y(35, 50));

    sm.reset();
    EXPECT_FALSE(sm.has_target());
    EXPECT_FALSE(sm.targets(small));
}

// Regression pin for the one documented heap-overflow that lives in this exact
// flow: radar::update() indexes bmp[i + sizex*j] (and reads the grid at the same
// index) using extents recorded at the last sync. The editor restarts the
// minimap after Level > Details > Map size..., but "Resmooth terrain",
// "Clear all terrain" and every terrain-brush stroke call update() directly, so
// update() has to re-derive the extents itself. Without that, a map shrunk from
// the 40x60 new-level default to the reporter's 30x30 makes the very next
// stroke read 1500 bytes past the end of the 900-byte grid buffer.
TEST(Issue12EditorWallCrash, radar_update_resyncs_after_map_shrink)
{
    screen* s = game_screen();
    ASSERT_TRUE(s != nullptr && s->viewob[0] != nullptr);

    s->world().delete_objects();
    s->world().create_new_grid();   // File > Level > New: 40x60
    ASSERT_EQ(40, s->world().grid.w);
    ASSERT_EQ(60, s->world().grid.h);

    radar r(s->viewob[0].get(), s, 0);
    r.force_lower_position = true;
    r.start(&s->level_runtime_data());
    ASSERT_EQ(40, r.sizex);
    ASSERT_EQ(60, r.sizey);
    ASSERT_EQ(static_cast<std::size_t>(40 * 60), r.bmp.size());

    // Level > Details > Map size... 30x30, then hundreds of clerics.
    s->world().resize_grid(kMapW, kMapH);
    ASSERT_EQ(kMapW, s->world().grid.w);
    ASSERT_EQ(kMapH, s->world().grid.h);
    ASSERT_EQ(kClerics, place_clerics());

    // One terrain-brush stroke on the bottom row, near the middle.
    PixieData& g = s->world().grid_for_floor(0);
    g.data[static_cast<std::size_t>((kMapH - 1) * g.w + 15)] =
        static_cast<unsigned char>(PIX_H_WALL1);
    r.update(&s->level_runtime_data());

    EXPECT_EQ(kMapW, r.sizex) << "radar kept its pre-resize width";
    EXPECT_EQ(kMapH, r.sizey) << "radar kept its pre-resize height";
    EXPECT_EQ(static_cast<std::size_t>(kMapW * kMapH), r.bmp.size())
        << "radar bmp not resized with the grid";

    restore_shared_level();
}

namespace {

constexpr int kFloorLoadLevelId = 9430;
constexpr const char* kFloorLoadGridName = "issue12z";

// save_level_to_user_dir() drops the fixture straight into user_path, where
// the editor's own level list finds it. Sweep it back out however this test
// leaves -- a stray scenario there shifts list_levels() for every later test
// in this binary (it broke LevelEditorHelpers' validate-campaign counts once).
struct FloorLoadFixtureCleanup
{
    ~FloorLoadFixtureCleanup()
    {
        const std::string user = get_user_path();
        std::error_code ec;
        std::filesystem::remove(
            user + std::string("scen/scen") + std::to_string(kFloorLoadLevelId) + ".fss", ec);
        std::filesystem::remove(user + "pix/" + kFloorLoadGridName + ".png", ec);
        for (int f = 1; f < 4; ++f)
        {
            std::filesystem::remove(
                user + "pix/" + kFloorLoadGridName + "_f" + std::to_string(f) + ".png", ec);
            std::filesystem::remove(
                user + "pix/" + kFloorLoadGridName + "_d" + std::to_string(f) + ".png", ec);
        }
    }
};

// smooth()'s grass variant table (src/gameplay/smooth.cpp): a plain grass cell
// with plain grass all round is repainted grass_variants[next_random(4)] --
// exactly one RNG draw, which is what makes the borrowed RNG observable.
constexpr int kGrassVariants[4] = {PIX_GRASS1, PIX_GRASS2, PIX_GRASS3, PIX_GRASS4};

void fill_grid(PixieData& g, int value)
{
    std::fill_n(g.data.get(),
                static_cast<std::size_t>(g.w) * static_cast<std::size_t>(g.h),
                static_cast<unsigned char>(value));
}

} // namespace

// The OTHER borrowed pointer, found reviewing the pin above and live on this
// tree until this commit. `smoother` borrows a grid view AND an RNG.
// GameWorld::set_floor_count binds every stacked floor's smoother to that
// world's rng_, but LevelRuntimeData::load builds the level in a STACK-LOCAL
// GameWorld and then moves extra_floors_ out of it (replace_loaded_world_state).
// Only the span was re-pointed, so each moved floor smoother kept the address
// of a GameWorld that dies when load() returns: the first autotile stroke on
// floor >= 1 of any loaded multi-floor level -- terrain brush or "Resmooth
// terrain" -- drew through a dangling pointer. The shipped SDL client installs
// no gameplay RNG override, so nothing masked it there. Same failure family as
// the 2013 crash this file pins, one member over.
TEST(Issue12EditorWallCrash, floor_smoothers_follow_the_live_world_rng_across_a_level_load)
{
    FloorLoadFixtureCleanup fixture_cleanup;

    // A two-floor level on disk, both floors plain grass.
    {
        GameWorld src_world(1);
        src_world.create_new_grid();
        ASSERT_TRUE(src_world.grid.valid());
        fill_grid(src_world.grid, PIX_GRASS1);

        // Exactly what the editor's add_floor() does: grow the stack, then
        // size, fill and re-target the new floor's grid.
        const int gw = src_world.grid.w;
        const int gh = src_world.grid.h;
        src_world.set_floor_count(2);
        const std::size_t cells =
            static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh);
        auto* buf = new unsigned char[cells];
        std::fill_n(buf, cells, static_cast<unsigned char>(PIX_GRASS1));
        src_world.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                                static_cast<unsigned char>(gh), buf);
        src_world.smoother_for_floor(1).set_target(src_world.grid_for_floor(1));
        ASSERT_TRUE(src_world.smoothers_in_sync())
            << "add_floor's own wiring must satisfy the invariant";

        og::data::LevelFileMetadata md;
        md.grid_file = kFloorLoadGridName;
        og::data::LevelFileIoError io_error = og::data::LevelFileIoError::None;
        ASSERT_TRUE(og::data::save_level_to_user_dir(src_world, kFloorLoadLevelId,
                                                     md, &io_error))
            << "could not write the two-floor fixture level";
    }

    LevelRuntimeData dst(kFloorLoadLevelId, true);
    ASSERT_TRUE(dst.load()) << "could not read the two-floor fixture level back";
    GameWorld& w = dst.world();
    ASSERT_EQ(2, w.floor_count()) << "the stacked floor did not survive the round trip";
    ASSERT_TRUE(w.grid_for_floor(1).valid());

    // The pin: both of every smoother's borrowed pointers belong to THIS world.
    // Pointer comparison only -- nothing is dereferenced, so this discriminates
    // without any undefined behaviour of its own.
    ASSERT_TRUE(w.smoothers_in_sync())
        << "a loaded floor smoother is still bound to the loader's throwaway world";

    // ... and the consequence, observed: smoothing a grass cell on floor 1 must
    // draw from THIS world's RNG. Both overrides must be absent, or the draw
    // would be routed away from rng_ and prove nothing.
    ASSERT_EQ(nullptr, gameplay_rng_override())
        << "an earlier test left a gameplay RNG override installed";
    ASSERT_EQ(nullptr, og::sim::sim_random_override())
        << "an earlier test left a sim RNG override installed";

    constexpr std::uint32_t kSeed = 0x1234567u;
    og::sim::SimRandom reference(kSeed);
    const std::uint32_t roll = reference.next(4);

    w.rng_.state_ = kSeed;
    w.smoother_for_floor(1).smooth(5, 5);

    EXPECT_EQ(reference.state_, w.rng_.state_)
        << "the floor smoother drew its grass variant from some other world's RNG";
    const PixieData& upper = w.grid_for_floor(1);
    EXPECT_EQ(kGrassVariants[roll],
              static_cast<int>(upper.data[static_cast<std::size_t>(5 * upper.w + 5)]))
        << "the autotiled grass variant does not match this world's RNG stream";
}

// ---------------------------------------------------------------------------
// The same scenario through the editor's REAL event loop: the reporter drove
// menus and a mouse, and frame 6 of the 2013 stack is LevelEditorData::draw.
// Injected SDL mouse events carry *window* coordinates; the helpers below
// apply the same game->window mapping test_level_editor_interactions.cpp uses,
// so the click table can be written in 320x200 game coordinates.
// ---------------------------------------------------------------------------
namespace {

int game_to_window_x(int gx)
{
    return static_cast<int>(
        ui_canvas_to_window(static_cast<float>(gx), 0.0f).first);
}

int game_to_window_y(int gy)
{
    return static_cast<int>(
        ui_canvas_to_window(0.0f, static_cast<float>(gy)).second);
}

void inject_click_game(int gx, int gy, int delay_ms = 20)
{
    inject_click(game_to_window_x(gx), game_to_window_y(gy), delay_ms);
}

void push_mouse_motion_game(int gx, int gy)
{
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = static_cast<float>(game_to_window_x(gx));
    e.motion.y = static_cast<float>(game_to_window_y(gy));
    SDL_PushEvent(&e);
}

// Clerics: one per grid cell, four rows across the top of the map view. The
// object pane occupies x >= 245 and the mode-button row y in [20,35), so the
// table starts below and right of both.
constexpr int kEditorClericCols = 10;
constexpr int kEditorClericRows = 4;
constexpr int kEditorClerics = kEditorClericCols * kEditorClericRows;
constexpr int kEditorClericX0 = 80;
constexpr int kEditorClericY0 = 40;

// The wall stroke: six cells along one row near the bottom of the map view,
// clear of the pan buttons (x in [3,48), y in [149,194)).
constexpr int kEditorWallCells = 6;
constexpr int kEditorWallX0 = 100;
constexpr int kEditorWallY = 160;

// Wait for a traced state change instead of sleeping past it. Returns false
// on timeout, which the caller turns into a loud test failure -- never a
// longer sleep.
bool wait_for_trace(const char* category, const char* needle, Uint32 timeout_ms)
{
    const Uint64 deadline = SDL_GetTicks() + timeout_ms;
    while (!trace_contains(category, needle))
    {
        if (SDL_GetTicks() >= deadline)
            return false;
        SDL_Delay(2);
    }
    return true;
}

struct EditorIssue12ThreadState
{
    bool started = false;
    bool finished = false;
    bool smoothing_was_on = false;
    // Set when a wait_for_trace() gave up; reported by the test body so a
    // timeout names the step it stalled on.
    std::string stalled_on;
};

int editor_issue12_injector(void* opaque)
{
    og::runtime::ensure_thread_session();
    auto* st = static_cast<EditorIssue12ThreadState*>(opaque);
    st->started = true;

    // Wait for the editor to pin the classic canvas, i.e. to be inside its
    // main loop, rather than guessing with a flat delay.
    if (!wait_for_trace("canvas", "editor_pin_classic", 10000))
    {
        st->stalled_on = "editor entry";
        og::runtime::current_session->myscreen_->world().end = 1;
        return 1;
    }
    SDL_Delay(200);

    // --- File > Level > New (the reporter's "new campaign" level) ----------
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);   // "Discard unsaved changes?"
    inject_click_game(15, 10);   // File
    SDL_Delay(40);
    inject_click_game(15, 45);   // Level >
    SDL_Delay(40);
    inject_click_game(85, 45);   // New
    SDL_Delay(120);

    // --- Level > Details > Map size... -> 30 x 30 -------------------------
    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("30");   // Map Width
    level_editor_testing_prompt_queue_push("30");   // Map Height
    inject_click_game(90, 10);    // Level
    SDL_Delay(40);
    inject_click_game(90, 65);    // Details >
    SDL_Delay(40);
    inject_click_game(200, 65);   // Map size...
    // The resize ends in timed_dialog("Resized map to 30x30"), a blocking
    // three-second poll loop that only a click or key press interrupts.
    // Sleeping past it would race the editor under load (a late dialog eats
    // the mode-switch keys that follow), so wait for the dialog to announce
    // itself -- traced after it clears pending input, so a key sent from here
    // is still pending when it polls -- dismiss it, and wait for it to close.
    // The message also pins WHICH dialog: a failed resize says "Resize
    // canceled." and times out here instead of silently continuing.
    if (!wait_for_trace("dialog", "timed_dialog_open Resized map to 30x30", 15000))
    {
        st->stalled_on = "Map size... dialog never opened";
        og::runtime::current_session->myscreen_->world().end = 1;
        return 1;
    }
    inject_key_press(SDLK_RETURN, 10);   // dismiss it now, don't wait it out
    if (!wait_for_trace("dialog", "timed_dialog_closed Resized map to 30x30", 15000))
    {
        st->stalled_on = "Map size... dialog never closed";
        og::runtime::current_session->myscreen_->world().end = 1;
        return 1;
    }
    picker_testing_yes_or_no_queue_clear();
    level_editor_testing_prompt_queue_clear();

    // --- several hundred level-1 clerics along the top --------------------
    // Object mode with a known brush: object_pane index 5 is Living/cleric,
    // which is pane cell (1,1) with the pane scrolled home.
    inject_key_press(SDLK_T, 10);   // -> Terrain, from any mode
    inject_key_press(SDLK_O, 10);   // Terrain -> Object
    eds().rowsdown = 0;
    SDL_Delay(40);
    inject_click_game(262, 97);     // pane cell (1,1): Living / FAMILY_CLERIC
    SDL_Delay(40);
    for (int row = 0; row < kEditorClericRows; ++row)
        for (int col = 0; col < kEditorClericCols; ++col)
            inject_click_game(kEditorClericX0 + col * GRID_SIZE,
                              kEditorClericY0 + row * GRID_SIZE, 20);
    SDL_Delay(120);

    // --- draw a wall along the bottom -------------------------------------
    // Terrain mode, brush = PIX_H_WALL1 (the tile the map border is drawn
    // with). It is kDefaultBackgrounds[46], i.e. pane cell (2,0) with the
    // terrain pane scrolled down eleven rows.
    inject_key_press(SDLK_T, 10);   // -> Terrain
    eds().rowsdown = 11;
    SDL_Delay(40);
    inject_click_game(278, 88);     // pane cell (2,0): PIX_H_WALL1
    SDL_Delay(40);

    // Smoothing is what wrote through the stale pointer in 2013; it is the
    // brush default, but the brush is a static that earlier tests in this
    // binary can have toggled, so put it back on if needed.
    if (!level_editor_testing_terrain_smoothing())
    {
        inject_click_game(45, 27);  // "Smooth" toggle (Terrain mode chrome)
        SDL_Delay(60);
    }

    // Painting happens while the button is HELD and the editor loop polls the
    // pointer, so press once and walk the row.
    inject_mouse_down(game_to_window_x(kEditorWallX0),
                      game_to_window_y(kEditorWallY));
    SDL_Delay(60);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < kEditorWallCells; ++i)
        {
            push_mouse_motion_game(kEditorWallX0 + i * GRID_SIZE, kEditorWallY);
            SDL_Delay(60);
        }
    }
    inject_mouse_up(game_to_window_x(kEditorWallX0 +
                                     (kEditorWallCells - 1) * GRID_SIZE),
                    game_to_window_y(kEditorWallY));
    SDL_Delay(200);

    st->smoothing_was_on = level_editor_testing_terrain_smoothing();

    og::runtime::current_session->myscreen_->world().end = 1;
    st->finished = true;
    return 0;
}

} // namespace

TEST(Issue12EditorWallCrash, editor_event_loop_new_resize_clerics_and_wall_box)
{
    trace_clear();
    og::runtime::current_session->myscreen_->world().end = 0;

    CfgOverrideGuard cfg_guard;
    cfg.apply_override("effects", "mini_hp_bar", "on");

    EditorIssue12ThreadState st;
    SDL_Thread* thread =
        SDL_CreateThread(editor_issue12_injector, "issue12_editor", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    (void)level_editor();

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    og::runtime::current_session->myscreen_->world().end = 0;
    picker_testing_yes_or_no_queue_clear();
    level_editor_testing_prompt_queue_clear();

    ASSERT_TRUE(st.started) << "injector thread should have started";
    ASSERT_EQ("", st.stalled_on) << "injector timed out waiting for the editor";
    ASSERT_TRUE(st.finished) << "injector never reached the end of the script";
    ASSERT_EQ(0, thread_result);
    EXPECT_TRUE(st.smoothing_was_on)
        << "the wall strokes must smooth -- that is what wrote through the "
           "stale grid pointer in 2013";

    // Everything below reads the editor's OWN level, which only exists now
    // that level_editor() has returned.
    LevelRuntimeData* level = level_editor_testing_level();
    ASSERT_TRUE(level != nullptr);
    GameWorld& w = level->world();

    ASSERT_EQ(kMapW, w.grid.w) << "Map size... did not resize the level";
    ASSERT_EQ(kMapH, w.grid.h);

    int clerics = 0;
    for (const auto& ob : w.oblist)
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->family() == FAMILY_CLERIC)
            ++clerics;
    ASSERT_EQ(kEditorClerics, clerics)
        << "every cleric click should have placed one cleric";

    // The wall really landed on the map, in the cells the pointer walked.
    // (Same arithmetic the editor's paint path uses: game coords -> level
    // pixels -> grid cell, snapped to the grid.) Read through a probe
    // smoother aimed at the grid buffer, so this says "the tile is in the
    // live grid" and nothing about the editor smoother's target -- that is
    // smoothers_in_sync()'s job, below.
    smoother probe;
    probe.set_target(w.grid_for_floor(0));
    const int topx = level->level_visuals().topx;
    const int topy = level->level_visuals().topy;
    const int xloc = og::runtime::current_session->myscreen_->viewob[0]->xloc;
    const int yloc = og::runtime::current_session->myscreen_->viewob[0]->yloc;
    const int cell_y = ((kEditorWallY + topy - yloc) -
                        ((kEditorWallY + topy - yloc) % GRID_SIZE)) / GRID_SIZE;
    for (int i = 0; i < kEditorWallCells; ++i)
    {
        const int px = kEditorWallX0 + i * GRID_SIZE + topx - xloc;
        const int cell_x = (px - (px % GRID_SIZE)) / GRID_SIZE;
        EXPECT_EQ(TYPE_WALL, probe.query_genre_x_y(cell_x, cell_y))
            << "grid cell " << cell_x << "," << cell_y << " is not wall";
    }

    EXPECT_TRUE(w.smoothers_in_sync())
        << "the editor left its smoother pointing away from the level grid";
}
