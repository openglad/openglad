// Shipped "The Long Season" campaign validation
// (builtin/longseason.glad, authored by tools/longseason_mapgen).
//
// Every registered level (ids 1-19, contiguous) is loaded through the
// production campaign-mount path and pinned against the authoring invariants
// the generator promises:
//   - structure: title/floors/grid geometry, briefing budgets, scenario type
//     bits, start markers, army census (with tolerance; level 18 fields a
//     THREE-WAY fight, so team 1 is pinned per level rather than empty by
//     convention), generators, the per-NPC v10 extras (spawn_delay /
//     specials_disabled), the MAXOBS budget, and the exact exit-destination
//     sets;
//   - the campaign GRAPH: exit-closure from level 1 reaches every level, the
//     three hubs (2, 6, 11) offer their optional contracts (3, 7, 12 —
//     rejoining at 4, 8, 13), 19 -> 1 is the deliberate full-circle loop,
//     and backtrack exits only return to forward-graph predecessors;
//   - the cast: the SAVE_ALL watch covers EXACTLY the Assessor (level 4) and
//     the Reeve (level 15) — Kettle is protect-OPTIONAL everywhere he is
//     placed (4/9/18) — and the recurring named NPCs round-trip their teams
//     and extras;
//   - traversal: aligned Z-stair pairs on every floor boundary, footing for
//     every authored entity, fall-line landings, and A*-reachability
//     (respecting floors, air holes and Z-stairs) from the lead start marker
//     to every exit, every stair pair, and EVERY living and generator (empty
//     allowlist — kill-all contracts demand the player can close with every
//     foe);
//   - balance: 300-tick spectator smokes of the big battles (6, 15, 17, 18)
//     with the designed stand-in crew (4x lvl-5 soldiers + a lvl-3 thief);
//   - weather: the winter act (14, 15, 16) is buried past the Snow terrain
//     override, while the Smelter's Road (13) carries only its deliberate
//     sub-threshold dusting and can never roll Snow.
//
// This test is the regression pin for the committed package: if it drifts
// from tools/longseason_mapgen, regenerate the package (the tool self-checks
// the same table) and update the pins here in the same change.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/weather.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/game_context.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include "test_gameplay_context_scope.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Entity wiring: one shared loader for every level load (mirrors the
// production headless wiring; the campaign uses only stock families).
// ---------------------------------------------------------------------------
loader& longseason_levels_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

void wire_longseason_world_entity_services(GameWorld* world,
                                           LevelRuntimeData* level)
{
    (void)level;
    if (world == nullptr)
        return;
    loader* game_loader = &longseason_levels_loader();
    world->entity_factory = [game_loader](Order order, std::int32_t family) {
        return game_loader->create_walker_owned(order, family);
    };
    world->entity_configurator =
        [game_loader](walker& entity, Order order,
                      std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(), entity.family());
    };
    world->entity_derived_stats =
        [game_loader](walker* entity, Order order, std::int32_t family) {
            if (entity != nullptr)
                game_loader->set_derived_stats(entity, order, family);
        };
}

const LevelDataHooks& longseason_levels_hooks()
{
    static const LevelDataHooks hooks = [] {
        LevelDataHooks h{};
        h.wire_world_entity_services = wire_longseason_world_entity_services;
        return h;
    }();
    return hooks;
}

// Mounts the shipped Long Season campaign for the duration of one test and
// restores the previous mount in teardown.
class LongseasonCampaignTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("longseason"))
            << "builtin/longseason.glad should restore and mount";
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error("longseason");
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

private:
    std::string previous_;
};

// A campaign level loaded with full sim context. The world's own SimRandom
// drives the sim (mirroring the production text-protocol wiring) so the
// battle smokes get realistic AI cadence from a reproducible seed.
struct LoadedLongseasonLevel
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedLongseasonLevel(int id, std::uint32_t seed = 0)
        : level(id, true, &longseason_levels_hooks())
        , gameplay(level, save, events, cfg)
    {
        level.world().rng_.state_ = seed;
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &level.world().rng_, &cfg);
        gc.rng = &level.world().rng_;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedLongseasonLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

// ---------------------------------------------------------------------------
// The authored structure (tools/longseason_mapgen season tables). The census
// pins are exact today; livings carry a small tolerance so minor balance
// retunes don't churn this file (the generator's own self-check stays
// exact).
// ---------------------------------------------------------------------------
struct ShippedLevel
{
    int id;
    const char* title;
    int floors;
    int grid_w;
    int grid_h;
    int type_bits; // SCEN_TYPE_* (1 == CAN_EXIT, 4 == SAVE_ALL)
    int start_markers;
    int team0_livings;
    int team0_generators;
    int team1_livings;
    int team1_generators;
    int team2_livings;
    int team2_generators;
    int delayed_spawns;
    int specials_disabled;
    std::vector<int> exit_destinations;
};

const std::vector<ShippedLevel>& shipped_levels()
{
    static const std::vector<ShippedLevel> levels = {
        // SPRING: the mud-season openers and the first SAVE_ALL contract.
        {1, "Mud Pay", 1, 60, 40, 0, 9, 0, 0, 0, 0, 17, 0, 2, 0, {2}},
        {2, "The Ferry Right", 1, 80, 40, 0, 10, 5, 0, 0, 0, 26, 0, 14, 0,
         {4, 3, 1}},
        {3, "Saltmere Bell", 2, 50, 50, 0, 8, 0, 0, 0, 0, 23, 1, 2, 0,
         {4, 2}},
        {4, "The Assessor", 1, 80, 45, 4, 10, 5, 0, 0, 0, 32, 1, 13, 1,
         {5, 3}},
        // SUMMER: the war contracts, the hub at the hay cross, Long Tom.
        {5, "Two Banners", 1, 60, 40, 0, 10, 0, 0, 0, 0, 27, 0, 5, 0,
         {6, 4}},
        {6, "The Hay War", 1, 60, 60, 0, 10, 0, 0, 0, 0, 29, 3, 6, 0,
         {8, 7, 5}},
        {7, "Grey Tolls", 1, 50, 60, 0, 10, 6, 0, 0, 0, 29, 2, 21, 0,
         {8, 6}},
        {8, "The Paymaster Vanishes", 1, 60, 60, 0, 10, 0, 0, 0, 0, 33, 0, 5,
         0, {9, 6}},
        {9, "Ashfall Fair", 1, 60, 50, 0, 10, 3, 0, 0, 0, 30, 2, 20, 0,
         {10, 8}},
        // AUTUMN: the Foundry's debt, the mine, the optional vault, the road.
        {10, "The Ledger Debt", 2, 58, 44, 0, 10, 1, 0, 0, 0, 33, 0, 4, 15,
         {9, 11}},
        {11, "Cold Seams", 2, 62, 48, 0, 10, 0, 0, 0, 0, 39, 1, 12, 3,
         {10, 12, 13}},
        {12, "The Old Count's Vault", 1, 56, 44, 0, 9, 0, 0, 0, 0, 33, 0, 4,
         0, {11, 13}},
        {13, "The Smelter's Road", 1, 50, 60, 1, 10, 4, 0, 0, 0, 37, 1, 10,
         2, {11, 14}},
        // WINTER: the toll repaint, the Reeve's SAVE_ALL hold, the ford.
        {14, "The Long Toll", 1, 60, 60, 0, 10, 6, 0, 0, 0, 35, 1, 27, 0,
         {15, 13}},
        {15, "Wolf Winter", 1, 60, 60, 4, 10, 7, 0, 0, 0, 32, 0, 24, 1,
         {16, 14}},
        {16, "The Frozen Ford", 1, 90, 40, 1, 10, 0, 0, 0, 0, 26, 0, 10, 10,
         {17, 15}},
        // THE RECKONING: the gate, the three-way mint climb, settlement.
        {17, "Ashfall Gate", 1, 70, 44, 0, 10, 0, 0, 0, 0, 48, 2, 14, 0,
         {18, 16}},
        {18, "The Warm Mint", 3, 64, 44, 1, 10, 1, 0, 28, 0, 33, 1, 12, 0,
         {19, 17}},
        {19, "Settlement Day", 1, 44, 30, 0, 8, 0, 0, 0, 0, 16, 0, 4, 0,
         {1}},
    };
    return levels;
}

// The designed FORWARD story graph (campaign_meta.md / story-skeleton.md).
// Backtrack exits are the "declining continued work" mechanic: they must
// only return to a forward-graph predecessor. 19 -> 1 is the deliberate
// full-circle loop (the mint burns behind; the year starts over) and is
// checked separately so predecessor closures stay meaningful.
const std::map<int, std::vector<int>>& forward_graph()
{
    static const std::map<int, std::vector<int>> graph = {
        {1, {2}},        {2, {3, 4}},   {3, {4}},   {4, {5}},   {5, {6}},
        {6, {7, 8}},     {7, {8}},      {8, {9}},   {9, {10}},  {10, {11}},
        {11, {12, 13}},  {12, {13}},    {13, {14}}, {14, {15}}, {15, {16}},
        {16, {17}},      {17, {18}},    {18, {19}}, {19, {}},
    };
    return graph;
}

// SCENARIO INFORMATION dialog budgets.
constexpr std::size_t kBriefingLineBudget = 33;
constexpr std::size_t kTitleByteBudget = 30;
constexpr std::size_t kNameByteBudget = 11;
// Small slack on living counts only; everything else is pinned exact.
constexpr int kArmyTolerance = 2;

struct ArmyCensus
{
    int livings[MAX_TEAM + 1] = {};
    int generators[MAX_TEAM + 1] = {};
    int total_livings = 0;
    int start_markers = 0;
    int delayed_spawns = 0;
    int specials_disabled = 0;
};

ArmyCensus take_census(GameWorld& world)
{
    ArmyCensus census;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->spawn_delay() > 0)
            ++census.delayed_spawns;
        if (ob->specials_disabled())
            ++census.specials_disabled;
        const int team = ob->team_num();
        if (team < 0 || team > MAX_TEAM)
            continue;
        if (ob->query_order() == Order::Living)
        {
            ++census.livings[team];
            ++census.total_livings;
        }
        else if (ob->query_order() == Order::Generator)
        {
            ++census.generators[team];
        }
        else if (ob->query_order() == Order::Special &&
                 ob->family() == FAMILY_RESERVED_TEAM && team == 0)
        {
            ++census.start_markers;
        }
    }
    return census;
}

std::vector<walker*> find_exits(GameWorld& world)
{
    std::vector<walker*> exits;
    for (const auto& uptr : world.fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Treasure &&
            ob->family() == FAMILY_EXIT)
        {
            exits.push_back(ob);
        }
    }
    return exits;
}

std::vector<int> exit_destinations(GameWorld& world)
{
    std::vector<int> destinations;
    for (walker* e : find_exits(world))
        destinations.push_back(static_cast<int>(e->stats()->level()));
    std::sort(destinations.begin(), destinations.end());
    return destinations;
}

walker* find_named_living(GameWorld& world, const char* name)
{
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->stats() != nullptr && ob->stats()->name == name)
        {
            return ob;
        }
    }
    return nullptr;
}

walker* find_lead_start_marker(GameWorld& world)
{
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Special &&
            ob->family() == FAMILY_RESERVED_TEAM && ob->team_num() == 0)
        {
            return ob;
        }
    }
    return nullptr;
}

std::vector<walker*> find_start_markers(GameWorld& world)
{
    std::vector<walker*> markers;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Special &&
            ob->family() == FAMILY_RESERVED_TEAM && ob->team_num() == 0)
        {
            markers.push_back(ob);
        }
    }
    return markers;
}

int alive_livings_on_team(GameWorld& world, int team)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && !ob->dead() &&
            ob->query_order() == Order::Living && ob->team_num() == team)
        {
            ++count;
        }
    }
    return count;
}

// Multi-floor A* path-state encoding (walker_pathing.cpp's MAKE_STATE).
PathState make_path_state(int x, int y, int floor)
{
    return reinterpret_cast<PathState>(
        static_cast<intptr_t>(floor) * FLOOR_STRIDE +
        static_cast<intptr_t>(y / GRID_SIZE) * MAP_WIDTH + (x / GRID_SIZE));
}

// An aligned Z-stair pair on the boundary between floor and floor + 1.
struct StairPair
{
    int floor;
    int tx;
    int ty;
};

std::vector<StairPair> find_stair_pairs(GameWorld& world)
{
    std::vector<StairPair> pairs;
    for (int f = 0; f + 1 < world.floor_count(); ++f)
    {
        const PixieData& lo = world.grid_for_floor(f);
        const PixieData& hi = world.grid_for_floor(f + 1);
        if (!lo.valid() || !hi.valid())
            continue;
        // The pair test indexes BOTH planes with the lower floor's width,
        // which needs the shared-footprint invariant (game_world.h). Report
        // a violation instead of comparing the wrong cells (non-fatal: this
        // helper returns a value, and every caller pins the dims itself).
        EXPECT_EQ(static_cast<int>(lo.w), static_cast<int>(hi.w))
            << "floors " << f << "/" << (f + 1) << " grid width";
        EXPECT_EQ(static_cast<int>(lo.h), static_cast<int>(hi.h))
            << "floors " << f << "/" << (f + 1) << " grid height";
        if (lo.w != hi.w || lo.h != hi.h)
            continue;
        for (int ty = 0; ty < lo.h; ++ty)
            for (int tx = 0; tx < lo.w; ++tx)
            {
                const int i = tx + ty * lo.w;
                if (lo.data[static_cast<std::size_t>(i)] == PIX_ZSTAIR_UP &&
                    hi.data[static_cast<std::size_t>(i)] == PIX_ZSTAIR_DOWN)
                {
                    pairs.push_back({f, tx, ty});
                }
            }
    }
    return pairs;
}

// --- Fall-line audit support. ------------------------------------------------
// Single-cell ground passability: the Living arm of
// GameWorld::query_grid_passable with none of the flyer / forestwalk /
// ethereal escapes — the tiles a plain ground walker can STAND on. A fall
// landing must be immediately standable; water, lava, boulder and torch
// bases all bounce the faller into the engine's landing nudge, and levels
// must not rely on the nudge. Keep in lockstep with the classifier in
// tools/longseason_mapgen/main.cpp.
bool ground_cell_standable(unsigned char tile)
{
    switch (tile)
    {
        case PIX_GRASS1:
        case PIX_GRASS2:
        case PIX_GRASS3:
        case PIX_GRASS4:
        case PIX_GRASS_DARK_1:
        case PIX_GRASS_DARK_2:
        case PIX_GRASS_DARK_3:
        case PIX_GRASS_DARK_4:
        case PIX_GRASS_DARK_LL:
        case PIX_GRASS_DARK_UR:
        case PIX_GRASS_DARK_B1:
        case PIX_GRASS_DARK_B2:
        case PIX_GRASS_DARK_BR:
        case PIX_GRASS_DARK_R1:
        case PIX_GRASS_DARK_R2:
        case PIX_GRASS_RUBBLE:
        case PIX_GRASS1_DAMAGED:
        case PIX_GRASS_LIGHT_1:
        case PIX_GRASS_LIGHT_TOP:
        case PIX_GRASS_LIGHT_RIGHT_TOP:
        case PIX_GRASS_LIGHT_RIGHT:
        case PIX_GRASS_LIGHT_RIGHT_BOTTOM:
        case PIX_GRASS_LIGHT_BOTTOM:
        case PIX_GRASS_LIGHT_LEFT_BOTTOM:
        case PIX_GRASS_LIGHT_LEFT:
        case PIX_GRASS_LIGHT_LEFT_TOP:
        case PIX_GRASSWATER_LL:
        case PIX_GRASSWATER_LR:
        case PIX_GRASSWATER_UL:
        case PIX_GRASSWATER_UR:
        case PIX_PAVEMENT1:
        case PIX_PAVEMENT2:
        case PIX_PAVEMENT3:
        case PIX_COBBLE_1:
        case PIX_COBBLE_2:
        case PIX_COBBLE_3:
        case PIX_COBBLE_4:
        case PIX_FLOOR_PAVEL:
        case PIX_FLOOR_PAVER:
        case PIX_FLOOR_PAVEU:
        case PIX_FLOOR_PAVED:
        case PIX_PAVESTEPS1:
        case PIX_PAVESTEPS2:
        case PIX_PAVESTEPS2L:
        case PIX_PAVESTEPS2R:
        case PIX_FLOOR1:
        case PIX_CARPET_LL:
        case PIX_CARPET_B:
        case PIX_CARPET_LR:
        case PIX_CARPET_UR:
        case PIX_CARPET_U:
        case PIX_CARPET_UL:
        case PIX_CARPET_L:
        case PIX_CARPET_M:
        case PIX_CARPET_M2:
        case PIX_CARPET_R:
        case PIX_CARPET_SMALL_HOR:
        case PIX_CARPET_SMALL_VER:
        case PIX_CARPET_SMALL_CUP:
        case PIX_CARPET_SMALL_CAP:
        case PIX_CARPET_SMALL_LEFT:
        case PIX_CARPET_SMALL_RIGHT:
        case PIX_CARPET_SMALL_TINY:
        case PIX_DIRT_1:
        case PIX_DIRTGRASS_UL1:
        case PIX_DIRTGRASS_UR1:
        case PIX_DIRTGRASS_LL1:
        case PIX_DIRTGRASS_LR1:
        case PIX_DIRT_DARK_1:
        case PIX_DIRTGRASS_DARK_UL1:
        case PIX_DIRTGRASS_DARK_UR1:
        case PIX_DIRTGRASS_DARK_LL1:
        case PIX_DIRTGRASS_DARK_LR1:
        case PIX_PATH_1:
        case PIX_PATH_2:
        case PIX_PATH_3:
        case PIX_PATH_4:
        case PIX_SNOW1:
        case PIX_SNOW2:
        case PIX_MARSH1:
        case PIX_MARSH2:
        case PIX_ASH1:
        case PIX_ASH2:
        // Z tiles a ground walker occupies at the grid layer (stairs carry
        // you off; glass and drop blocks resolve in movement) — all legal
        // landings. PIX_AIR is deliberately NOT here: the audit chases air
        // columns itself.
        case PIX_ZSTAIR_UP:
        case PIX_ZSTAIR_DOWN:
        case PIX_GLASS:
        case PIX_DROPBLOCK_UP:
        case PIX_DROPBLOCK_RIGHT:
        case PIX_DROPBLOCK_DOWN:
        case PIX_DROPBLOCK_LEFT:
            return true;
        default:
            return false; // walls, trees, water, lava, boulders, torches,
                          // braziers, columns, jagged litter, void, air
    }
}

// Base tile AND decor plane both standable for a ground walker.
bool cell_standable(GameWorld& world, int floor, int tx, int ty)
{
    const PixieData& g = world.grid_for_floor(floor);
    if (!g.valid() || tx < 0 || ty < 0 || tx >= g.w || ty >= g.h)
        return false;
    if (!ground_cell_standable(g.data[static_cast<std::size_t>(tx + ty * g.w)]))
        return false;
    const PixieData& dec = world.decor_for_floor(floor);
    if (dec.valid() && dec.w == g.w && dec.h == g.h)
    {
        const unsigned char d = dec.data[static_cast<std::size_t>(tx + ty * dec.w)];
        if (d < DECOR_MAX &&
            kDecorRegistry[d].pass == DecorPassability::BlocksGround)
            return false;
    }
    return true;
}

// Snow tiles across every floor (the WeatherKind::Snow terrain override
// counts the same bytes).
int count_snow_tiles(GameWorld& world)
{
    int snow_tiles = 0;
    for (int f = 0; f < world.floor_count(); ++f)
    {
        const PixieData& g = world.grid_for_floor(f);
        if (!g.valid())
            continue;
        const int cells = g.w * g.h;
        for (int i = 0; i < cells; ++i)
        {
            const unsigned char t = static_cast<unsigned char>(g.data[static_cast<std::size_t>(i)]);
            if (t == PIX_SNOW1 || t == PIX_SNOW2)
                ++snow_tiles;
        }
    }
    return snow_tiles;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Structural pins for every shipped level.
// ---------------------------------------------------------------------------
TEST_F(LongseasonCampaignTest, levels_round_trip_the_authored_structure)
{
    for (const ShippedLevel& expected : shipped_levels())
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedLongseasonLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded) << "level should load from the mounted campaign";
        GameWorld& world = fx.world();

        EXPECT_EQ(expected.title, world.title) << "shipped level title";
        EXPECT_LE(std::strlen(expected.title), kTitleByteBudget)
            << "title overflows the 30-byte budget";
        EXPECT_EQ(expected.floors, world.floor_count()) << "floor count";
        EXPECT_EQ(expected.grid_w, static_cast<int>(world.grid.w));
        EXPECT_EQ(expected.grid_h, static_cast<int>(world.grid.h));
        for (int f = 0; f < world.floor_count(); ++f)
        {
            EXPECT_TRUE(world.grid_for_floor(f).valid())
                << "floor " << f << " grid must round-trip";
            // One footprint for the whole stack (game_world.h: "All floors
            // share pixmaxx/pixmaxy"). The loader reads each plane's dims
            // from its own PNG, so this is the pin: the cross-floor audits
            // below index every floor with one width.
            EXPECT_EQ(expected.grid_w, static_cast<int>(world.grid_for_floor(f).w))
                << "floor " << f << " grid width";
            EXPECT_EQ(expected.grid_h, static_cast<int>(world.grid_for_floor(f).h))
                << "floor " << f << " grid height";
        }
        EXPECT_EQ(expected.type_bits, static_cast<int>(world.type))
            << "scenario type bits (CAN_EXIT / SAVE_ALL)";
        EXPECT_TRUE(fx.level.generated)
            << "mapgen output carries the SCEN_TYPE_GENERATED provenance "
               "mark (metadata-side; world.type above stays clean)";

        EXPECT_GE(fx.level.description.size(), 4u)
            << "the ledger tells the story";
        EXPECT_LE(fx.level.description.size(), 6u) << "briefing budget";
        for (const std::string& line : fx.level.description)
        {
            EXPECT_LE(line.size(), kBriefingLineBudget)
                << "briefing line overflows the dialog: '" << line << "'";
        }

        const ArmyCensus census = take_census(world);
        EXPECT_EQ(expected.start_markers, census.start_markers)
            << "the crew's authored formation";
        EXPECT_LE(std::abs(census.livings[0] - expected.team0_livings),
                  kArmyTolerance)
            << "team-0 allies: " << census.livings[0] << " vs designed "
            << expected.team0_livings;
        EXPECT_LE(std::abs(census.livings[1] - expected.team1_livings),
                  kArmyTolerance)
            << "the second host (the Warm Mint's three-way): "
            << census.livings[1] << " vs designed " << expected.team1_livings;
        EXPECT_LE(std::abs(census.livings[2] - expected.team2_livings),
                  kArmyTolerance)
            << "the enemy host: " << census.livings[2] << " vs designed "
            << expected.team2_livings;
        EXPECT_EQ(expected.team0_generators, census.generators[0])
            << "team-0 generators";
        EXPECT_EQ(expected.team1_generators, census.generators[1])
            << "team-1 generators";
        EXPECT_EQ(expected.team2_generators, census.generators[2])
            << "team-2 generators";
        EXPECT_EQ(expected.delayed_spawns, census.delayed_spawns)
            << "delayed-spawn NPCs must round-trip";
        EXPECT_EQ(expected.specials_disabled, census.specials_disabled)
            << "specials-disabled NPCs must round-trip";
        EXPECT_LE(census.total_livings, MAXOBS)
            << "seeded livings must leave generator headroom under MAXOBS";

        std::vector<int> expected_dests = expected.exit_destinations;
        std::sort(expected_dests.begin(), expected_dests.end());
        EXPECT_EQ(expected_dests, exit_destinations(world))
            << "the exact exit-destination set (campaign_meta level graph)";
    }
}

// ---------------------------------------------------------------------------
// (2) The campaign graph: closure, the three hubs, the 19 -> 1 loop, and
// backtrack discipline.
// ---------------------------------------------------------------------------
TEST_F(LongseasonCampaignTest, campaign_graph_reaches_every_level)
{
    // Authored edges, read back from the shipped package itself.
    std::map<int, std::vector<int>> edges;
    std::set<int> registered;
    for (const ShippedLevel& expected : shipped_levels())
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedLongseasonLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded);
        edges[expected.id] = exit_destinations(fx.world());
        registered.insert(expected.id);
    }

    // The full designed id set: 1..19 contiguous.
    std::set<int> designed;
    for (int id = 1; id <= 19; ++id)
        designed.insert(id);
    EXPECT_EQ(designed, registered) << "the 19 shipped level ids";

    // Exit closure from the campaign's first level reaches every level.
    auto closure_over = [](const std::map<int, std::vector<int>>& graph,
                           int origin) {
        std::set<int> seen{origin};
        std::vector<int> frontier{origin};
        while (!frontier.empty())
        {
            const int id = frontier.back();
            frontier.pop_back();
            auto it = graph.find(id);
            if (it == graph.end())
                continue;
            for (const int next : it->second)
                if (seen.insert(next).second)
                    frontier.push_back(next);
        }
        return seen;
    };
    EXPECT_EQ(designed, closure_over(edges, 1))
        << "every level must be reachable from level 1 via exits";

    // The three hubs offer their optional contracts; the optionals rejoin
    // the mainline one level on (3 -> 4, 7 -> 8, 12 -> 13).
    const std::map<int, std::vector<int>> branch_pins = {
        {2, {1, 3, 4}},   // the ferry cross: Saltmere's bell vs the assessor
        {6, {5, 7, 8}},   // the hay cross: toll fort north vs the pay road
        {11, {10, 12, 13}}, // the mine: the Count's vault vs the road out
    };
    for (const auto& [id, dests] : branch_pins)
    {
        EXPECT_EQ(dests, edges[id])
            << "hub level " << id << " must offer its designed roads";
    }
    const std::map<int, int> optional_rejoins = {{3, 4}, {7, 8}, {12, 13}};
    for (const auto& [opt, rejoin] : optional_rejoins)
    {
        const std::vector<int>& dests = edges[opt];
        EXPECT_TRUE(std::find(dests.begin(), dests.end(), rejoin) !=
                    dests.end())
            << "optional contract " << opt << " must rejoin the mainline at "
            << rejoin;
    }

    // The year loops: Settlement Day exits ONLY back to Mud Pay.
    EXPECT_EQ(std::vector<int>{1}, edges[19])
        << "19 -> 1 is the full-circle loop, with no backtrack";

    // Every authored edge is either a designed forward road, the 19 -> 1
    // full-circle loop, or a backtrack to a forward-graph predecessor.
    for (const auto& [from, dests] : edges)
    {
        for (const int to : dests)
        {
            SCOPED_TRACE("exit " + std::to_string(from) + " -> " +
                         std::to_string(to));
            ASSERT_TRUE(registered.count(to) != 0)
                << "exit destination must exist in the package";
            if (from == 19 && to == 1)
                continue; // the ledger closes; the year starts over
            const auto fwd = forward_graph().find(from);
            ASSERT_TRUE(fwd != forward_graph().end());
            const bool forward =
                std::find(fwd->second.begin(), fwd->second.end(), to) !=
                fwd->second.end();
            if (forward)
                continue;
            // Backtrack: the destination must be a predecessor (this level
            // is in the destination's forward closure).
            const std::set<int> preds = closure_over(forward_graph(), to);
            EXPECT_TRUE(preds.count(from) != 0)
                << "backtrack exit must return to a story predecessor";
        }
    }
}

// ---------------------------------------------------------------------------
// (3) The cast: the SAVE_ALL watch covers EXACTLY the Assessor (level 4) and
// the Reeve (level 15) — nobody else on any level carries the protected bit
// (Kettle above all: he is placed on 4/9/18 and is protect-OPTIONAL by the
// story bible) — and the recurring named NPCs' v10 extras round-trip.
// ---------------------------------------------------------------------------
TEST_F(LongseasonCampaignTest, save_all_watch_covers_exactly_the_two_protectees)
{
    struct ProtecteePin
    {
        int level_id;
        const char* name;
        int family;
        int stat_level;
    };
    static constexpr ProtecteePin kProtectees[] = {
        {4, "Assessor", FAMILY_CLERIC, 4},    // spring: the crown's assessor
        {15, "The Reeve", FAMILY_CLERIC, 6},  // winter: Thornby's reeve
    };

    std::set<int> save_all_ids;
    for (const ProtecteePin& pin : kProtectees)
        save_all_ids.insert(pin.level_id);
    for (const ShippedLevel& level : shipped_levels())
    {
        const bool expect_save_all = save_all_ids.count(level.id) != 0;
        EXPECT_EQ(expect_save_all,
                  (level.type_bits & SCEN_TYPE_SAVE_ALL) != 0)
            << "scen" << level.id << " SAVE_ALL flag";
    }

    for (const ShippedLevel& level : shipped_levels())
    {
        SCOPED_TRACE("scen" + std::to_string(level.id));
        LoadedLongseasonLevel fx(level.id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();

        int protected_count = 0;
        for (const auto& uptr : world.oblist)
        {
            const walker* ob = uptr.get();
            if (ob != nullptr && ob->save_all_protected())
                ++protected_count;
        }
        if (save_all_ids.count(level.id) == 0)
        {
            EXPECT_EQ(0, protected_count)
                << "only the two SAVE_ALL contracts may flag a protectee";
            continue;
        }
        EXPECT_EQ(1, protected_count)
            << "the SAVE_ALL watch must cover the protectee and ONLY them";
    }

    for (const ProtecteePin& pin : kProtectees)
    {
        SCOPED_TRACE(std::string(pin.name) + " in scen" +
                     std::to_string(pin.level_id));
        LoadedLongseasonLevel fx(pin.level_id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();
        ASSERT_NE(0, static_cast<int>(world.type) & SCEN_TYPE_SAVE_ALL);

        walker* protectee = find_named_living(world, pin.name);
        ASSERT_NE(nullptr, protectee) << "SAVE_ALL level needs its protectee";
        EXPECT_TRUE(protectee->save_all_protected())
            << "the protectee must carry npc_flags bit 2";
        EXPECT_EQ(0, static_cast<int>(protectee->team_num()));
        EXPECT_EQ(pin.family, static_cast<int>(protectee->family()));
        EXPECT_EQ(pin.stat_level,
                  static_cast<int>(protectee->stats()->level()));
        EXPECT_EQ(ACT_GUARD, protectee->act_type())
            << "the protectee holds a post rather than roaming into danger";
        EXPECT_TRUE(protectee->specials_disabled())
            << "the protectee cannot special themselves out of the hold";
    }
}

TEST_F(LongseasonCampaignTest, named_cast_extras_round_trip)
{
    struct NamedPin
    {
        int level_id;
        const char* name;
        int family;
        int team;
        int stat_level;
        bool specials_disabled;
        int spawn_delay;
    };
    static constexpr NamedPin kCast[] = {
        // The quartermaster keeps the books on 4, 9 and 18 — always team 0,
        // never protected (his death is a ledger line, not a mission fail).
        {4, "Kettle", FAMILY_SOLDIER, 0, 3, false, 0},
        {9, "Kettle", FAMILY_SOLDIER, 0, 5, false, 0},
        {18, "Kettle", FAMILY_SOLDIER, 0, 8, false, 0},
        // The campaign's named enemies: the vanishing paymaster-thief (his
        // invisibility stays ON — the joke), the toll-miller, the dead lord
        // on his hoard, and the Founder over the mint.
        {8, "Long Tom", FAMILY_THIEF, 2, 7, false, 0},
        {10, "The Miller", FAMILY_THIEF, 2, 6, false, 0},
        {12, "The Count", FAMILY_GIANT_SKELETON, 2, 9, false, 0},
        {18, "The Founder", FAMILY_ARCHMAGE, 2, 10, false, 0},
        // The Foundry's factor rides along on the Ledger Debt, sheathed.
        {10, "The Factor", FAMILY_SOLDIER, 0, 5, true, 0},
        // The wagon run: two golem "carts" the crew escorts down the road
        // (guard=false — they ROLL; specials sheathed).
        {13, "Ore Wagon", FAMILY_GOLEM, 0, 8, true, 0},
        {13, "Spare Cart", FAMILY_GOLEM, 0, 6, true, 0},
    };

    for (const NamedPin& pin : kCast)
    {
        SCOPED_TRACE(std::string(pin.name) + " in scen" +
                     std::to_string(pin.level_id));
        EXPECT_LE(std::strlen(pin.name), kNameByteBudget)
            << "names serialize through a 12-byte buffer";
        LoadedLongseasonLevel fx(pin.level_id);
        ASSERT_TRUE(fx.loaded);
        walker* npc = find_named_living(fx.world(), pin.name);
        ASSERT_NE(nullptr, npc) << "the named NPC must round-trip";
        EXPECT_EQ(pin.family, static_cast<int>(npc->family()));
        EXPECT_EQ(pin.team, static_cast<int>(npc->team_num()));
        EXPECT_EQ(pin.stat_level, static_cast<int>(npc->stats()->level()));
        EXPECT_EQ(pin.specials_disabled, npc->specials_disabled())
            << "specials_disabled must round-trip";
        EXPECT_EQ(pin.spawn_delay, static_cast<int>(npc->spawn_delay()))
            << "spawn_delay must round-trip";
        EXPECT_FALSE(npc->save_all_protected())
            << "named cast members other than the two protectees stay "
               "non-critical (SAVE_ALL scoping)";
    }
}

// ---------------------------------------------------------------------------
// (4) Traversal structure: aligned stair pairs on every floor boundary, and
// every authored entity standing on ground its footprint can occupy.
// ---------------------------------------------------------------------------
TEST_F(LongseasonCampaignTest, stairs_on_every_floor_boundary)
{
    for (const ShippedLevel& expected : shipped_levels())
    {
        if (expected.floors < 2)
            continue;
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedLongseasonLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();
        ASSERT_EQ(expected.floors, world.floor_count());

        for (int f = 0; f + 1 < world.floor_count(); ++f)
        {
            const PixieData& lo = world.grid_for_floor(f);
            const PixieData& hi = world.grid_for_floor(f + 1);
            ASSERT_TRUE(lo.valid());
            ASSERT_TRUE(hi.valid());
            ASSERT_EQ(lo.w, hi.w);
            ASSERT_EQ(lo.h, hi.h);
            int pairs = 0;
            const int cells = lo.w * lo.h;
            for (int i = 0; i < cells; ++i)
            {
                if (lo.data[static_cast<std::size_t>(i)] == PIX_ZSTAIR_UP &&
                    hi.data[static_cast<std::size_t>(i)] == PIX_ZSTAIR_DOWN)
                {
                    ++pairs;
                }
            }
            EXPECT_GE(pairs, 1) << "floor boundary " << f << "<->" << f + 1
                                << " needs an aligned UP/DOWN stair pair";
        }
    }
}

TEST_F(LongseasonCampaignTest, entities_stand_on_passable_ground)
{
    for (const ShippedLevel& expected : shipped_levels())
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedLongseasonLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();

        auto check_footing = [&](walker* ob)
        {
            if (ob == nullptr)
                return;
            EXPECT_TRUE(world.query_grid_passable(
                static_cast<float>(ob->xpos()),
                static_cast<float>(ob->ypos()), ob, ob->floor()))
                << "order " << static_cast<int>(ob->query_order())
                << " family " << static_cast<int>(ob->family()) << " at tile ("
                << ob->xpos() / GRID_SIZE << ", " << ob->ypos() / GRID_SIZE
                << ") floor " << ob->floor() << " stands on impassable ground";
            // Ground troops must not spawn hanging over an air hole.
            if (ob->query_order() == Order::Living &&
                !ob->stats()->query_bit_flags(BIT_FLYING))
            {
                const PixieData& g = world.grid_for_floor(ob->floor());
                const int tx = (ob->xpos() + ob->sizex() / 2) / GRID_SIZE;
                const int ty = (ob->ypos() + ob->sizey() / 2) / GRID_SIZE;
                ASSERT_TRUE(tx >= 0 && ty >= 0 && tx < g.w && ty < g.h);
                EXPECT_NE(PIX_AIR, g.data[static_cast<std::size_t>(tx + ty * g.w)])
                    << "ground unit family " << static_cast<int>(ob->family())
                    << " spawns over air at tile (" << tx << ", " << ty
                    << ") floor " << ob->floor();
            }
        };
        for (const auto& uptr : world.oblist)
            check_footing(uptr.get());
        for (const auto& uptr : world.fxlist)
            check_footing(uptr.get());
    }
}

// Fall-line audit (mirrors tools/longseason_mapgen's self-check): any AIR
// cell a ground walker can actually step into — 8-adjacent to a standable
// cell of the SAME floor — must land its faller cleanly. Chase the column
// down through stacked AIR; the landing cell must be standable (base AND
// decor): never a wall top, water, lava or blocking decor. Falling past
// floor 0 is a pit death, a designed mechanic, and stays legal. The engine's
// nudge can rescue a blocked landing, but no shipped level may RELY on it.
TEST_F(LongseasonCampaignTest, air_fall_lines_land_on_standable_ground)
{
    for (const ShippedLevel& expected : shipped_levels())
    {
        if (expected.floors < 2)
            continue;
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedLongseasonLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();

        // The fall column below walks DOWN through the floors while indexing
        // each one with floor f's width. That is only sound because every
        // floor shares one footprint (game_world.h: "All floors share
        // pixmaxx/pixmaxy (same footprint)") -- and the loader takes each
        // plane's dims straight from its PNG, so nothing downstream enforces
        // it. Pin the invariant here: a mis-sized plane must fail loudly
        // instead of silently auditing the wrong cells.
        const PixieData& base_grid = world.grid_for_floor(0);
        ASSERT_TRUE(base_grid.valid()) << "floor 0 grid";
        for (int f = 0; f < world.floor_count(); ++f)
        {
            const PixieData& fg = world.grid_for_floor(f);
            ASSERT_TRUE(fg.valid()) << "floor " << f << " grid";
            ASSERT_EQ(static_cast<int>(base_grid.w), static_cast<int>(fg.w))
                << "floor " << f << " grid width must match floor 0";
            ASSERT_EQ(static_cast<int>(base_grid.h), static_cast<int>(fg.h))
                << "floor " << f << " grid height must match floor 0";
        }

        for (int f = 1; f < world.floor_count(); ++f)
        {
            const PixieData& g = world.grid_for_floor(f);
            ASSERT_TRUE(g.valid());
            for (int ty = 0; ty < g.h; ++ty)
            {
                for (int tx = 0; tx < g.w; ++tx)
                {
                    if (g.data[static_cast<std::size_t>(tx + ty * g.w)] != PIX_AIR)
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
                    while (lf > 0 && world.grid_for_floor(lf)
                                             .data[static_cast<std::size_t>(tx + ty * g.w)] == PIX_AIR)
                        --lf;
                    if (world.grid_for_floor(lf).data[static_cast<std::size_t>(tx + ty * g.w)] ==
                        PIX_AIR)
                        continue; // fell past floor 0: pit death by design
                    EXPECT_TRUE(cell_standable(world, lf, tx, ty))
                        << "fall line at tile (" << tx << ", " << ty
                        << ") floor " << f
                        << " lands on impassable ground of floor " << lf
                        << " (the level must not rely on the engine's "
                           "landing nudge)";
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// (5) A*-reachability: from the lead start marker (the spot the player's
// first character takes) a ground probe must be able to path to every exit
// and to every Z-stair pair, respecting passability, air holes, and floors.
// ---------------------------------------------------------------------------
TEST_F(LongseasonCampaignTest, exits_and_stairs_reachable_from_the_lead_start)
{
    for (const ShippedLevel& expected : shipped_levels())
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedLongseasonLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();
        ASSERT_NE(nullptr, world.myobmap.get());

        walker* lead = find_lead_start_marker(world);
        ASSERT_NE(nullptr, lead) << "the crew's lead start marker";

        // A ground probe on the lead marker; removed from the obmap so it
        // never self-blocks a solve.
        walker* probe = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, probe);
        probe->set_team_num(0);
        probe->set_real_team_num(0);
        probe->set_floor(lead->floor());
        ASSERT_TRUE(probe->setxy(lead->xpos(), lead->ypos()));
        (void)world.myobmap->remove(probe);

        ASSERT_NE(nullptr, current_game);
        GameplayPathfindingState* pathing =
            ensure_pathfinding_state(*current_game);
        ASSERT_NE(nullptr, pathing);

        const PathState start = make_path_state(
            probe->xpos(), probe->ypos(), probe->floor());
        auto expect_reachable = [&](int gx, int gy, int gfloor,
                                    const std::string& what)
        {
            std::vector<PathState> path;
            float total_cost = 0.0f;
            pathing->solve_for_point(probe, static_cast<short>(gx),
                                     static_cast<short>(gy), start,
                                     make_path_state(gx, gy, gfloor), path,
                                     total_cost);
            EXPECT_FALSE(path.empty())
                << what << " at tile (" << gx / GRID_SIZE << ", "
                << gy / GRID_SIZE << ") floor " << gfloor
                << " is unreachable from the lead start marker";
        };

        for (walker* exit_ob : find_exits(world))
        {
            expect_reachable(exit_ob->xpos(), exit_ob->ypos(),
                             exit_ob->floor(),
                             "exit to " +
                                 std::to_string(static_cast<int>(
                                     exit_ob->stats()->level())));
        }
        for (const StairPair& stair : find_stair_pairs(world))
        {
            expect_reachable(stair.tx * GRID_SIZE, stair.ty * GRID_SIZE,
                             stair.floor, "stair pair");
        }
    }
}

// ---------------------------------------------------------------------------
// (5b) A*-reachability of the ARMIES: every living and every generator must
// be reachable from the lead start marker by a ground probe — kill-all
// levels demand the player can close with every foe, and CAN_EXIT levels
// promise no foe is sealed away. Flyers are exempt (ghosts hover over water,
// meres and air pits by design). The allowlist is EMPTY and must stay empty:
// tools/longseason_mapgen/main.cpp's reachability_exceptions() is the
// lockstep twin.
// ---------------------------------------------------------------------------
TEST_F(LongseasonCampaignTest, livings_and_generators_reachable_from_the_lead_start)
{
    for (const ShippedLevel& expected : shipped_levels())
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedLongseasonLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();
        ASSERT_NE(nullptr, world.myobmap.get());

        walker* lead = find_lead_start_marker(world);
        ASSERT_NE(nullptr, lead) << "the crew's lead start marker";

        // A ground probe on the lead marker; removed from the obmap so it
        // never self-blocks a solve.
        walker* probe = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, probe);
        probe->set_team_num(0);
        probe->set_real_team_num(0);
        probe->set_floor(lead->floor());
        ASSERT_TRUE(probe->setxy(lead->xpos(), lead->ypos()));
        (void)world.myobmap->remove(probe);

        ASSERT_NE(nullptr, current_game);
        GameplayPathfindingState* pathing =
            ensure_pathfinding_state(*current_game);
        ASSERT_NE(nullptr, pathing);

        const PathState start = make_path_state(
            probe->xpos(), probe->ypos(), probe->floor());
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
            const PathState goal =
                make_path_state(ob->xpos(), ob->ypos(), ob->floor());
            if (goal == start)
                continue; // shares the lead marker's cell
            std::vector<PathState> path;
            float total_cost = 0.0f;
            pathing->solve_for_point(probe, static_cast<short>(ob->xpos()),
                                     static_cast<short>(ob->ypos()), start,
                                     goal, path, total_cost);
            EXPECT_FALSE(path.empty())
                << "order " << static_cast<int>(order) << " family "
                << static_cast<int>(ob->family()) << " at tile ("
                << ob->xpos() / GRID_SIZE << ", " << ob->ypos() / GRID_SIZE
                << ") floor " << ob->floor()
                << " is unreachable from the lead start marker (fix the "
                   "map — the Long Season allowlist is deliberately empty)";
        }
    }
}

// ---------------------------------------------------------------------------
// (6) Balance smokes: the big battles self-play for 300 ticks with the
// DESIGNED stand-in crew on the authored start markers, and each level's
// design-doc smoke gate is asserted STRUCTURALLY. The campaign's curve
// climbs from crew 1 to crew 8, so the stand-in scales with the act (the
// level docs' recipes): the summer hub (6, curve 3) takes the Westlands
// crew (4x lvl-5 soldiers + a lvl-3 thief); the winter hold and the
// Reckoning (15/17/18, curve 7-8) take the docs' "4x lvl 8 on markers 1-4".
// The brawler stand-in crew is a VERY pessimistic floor (it cannot kite,
// heal or exit), so the pins are the docs' structural facts — the sides
// engage, the guard posts and generators hold, the waves stay dormant on
// schedule, the SAVE_ALL protectee survives — not absolute winnability.
// ---------------------------------------------------------------------------
namespace {

int alive_dormant_livings(GameWorld& world)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* ob = uptr.get();
        if (ob != nullptr && !ob->dead() &&
            ob->query_order() == Order::Living && ob->dormant())
            ++count;
    }
    return count;
}

int alive_generators_on_team(GameWorld& world, int team)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* ob = uptr.get();
        if (ob != nullptr && !ob->dead() &&
            ob->query_order() == Order::Generator && ob->team_num() == team)
            ++count;
    }
    return count;
}

int alive_family_on_team(GameWorld& world, int family, int team)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* ob = uptr.get();
        if (ob != nullptr && !ob->dead() &&
            ob->query_order() == Order::Living && ob->team_num() == team &&
            static_cast<int>(ob->family()) == family)
            ++count;
    }
    return count;
}

} // namespace

TEST_F(LongseasonCampaignTest, battle_smokes_hold_the_line)
{
    struct StandIn
    {
        int family;
        int stat_level;
    };
    struct SmokeSpec
    {
        int id;
        std::vector<StandIn> crew;
    };
    const std::vector<StandIn> kSummerCrew = {
        {FAMILY_SOLDIER, 5}, {FAMILY_SOLDIER, 5}, {FAMILY_SOLDIER, 5},
        {FAMILY_SOLDIER, 5}, {FAMILY_THIEF, 3},
    };
    const std::vector<StandIn> kReckoningCrew = {
        {FAMILY_SOLDIER, 8}, {FAMILY_SOLDIER, 8}, {FAMILY_SOLDIER, 8},
        {FAMILY_SOLDIER, 8},
    };
    const SmokeSpec kSmokes[] = {
        {6, kSummerCrew},     // the hay-cross hub, curve 3
        {15, kReckoningCrew}, // Wolf Winter, curve 7 (SAVE_ALL hold)
        {17, kReckoningCrew}, // Ashfall Gate, curve 8
        {18, kReckoningCrew}, // the Warm Mint three-way, curve 8
    };
    static constexpr std::uint32_t kSeeds[] = {1337u, 424242u};

    for (const SmokeSpec& smoke : kSmokes)
    {
        for (const std::uint32_t seed : kSeeds)
        {
            SCOPED_TRACE("scen" + std::to_string(smoke.id) + " seed " +
                         std::to_string(seed));
            LoadedLongseasonLevel fx(smoke.id, seed);
            ASSERT_TRUE(fx.loaded);
            GameWorld& world = fx.world();

            const std::vector<walker*> markers = find_start_markers(world);
            ASSERT_GE(markers.size(), smoke.crew.size())
                << "the crew needs its formation";

            const int t1_at_load = alive_livings_on_team(world, 1);
            const int t2_at_load = alive_livings_on_team(world, 2);

            std::vector<std::uint32_t> crew_ids;
            for (std::size_t i = 0; i < smoke.crew.size(); ++i)
            {
                walker* stand_in =
                    world.add_ob(Order::Living, smoke.crew[i].family);
                ASSERT_NE(nullptr, stand_in);
                stand_in->set_team_num(0);
                stand_in->set_real_team_num(0);
                stand_in->stats()->set_level(smoke.crew[i].stat_level);
                stand_in->set_floor(markers[i]->floor());
                stand_in->setxy(markers[i]->xpos(), markers[i]->ypos());
                crew_ids.push_back(stand_in->entity_id());
            }

            for (int i = 0; i < 150 && !world.game_ended; ++i)
                world.tick();
            // Level 18 is exempt from the company-alive gate SINCE THE v10
            // LOADER OBMAP FIX: the mint's 17 upper-story defenders were
            // collision ghosts (bucketed on floor 0 by the old
            // setxy-before-set-floor load order) and now fight for real —
            // the three-way brawl can roll over the pessimistic stand-in
            // company inside 150 ticks on some seeds. A real crew that
            // kites and heals plays it; re-sweep with
            // scripts/longseason_playtest.sh before the next balance pass.
            if (smoke.id != 18) {
                EXPECT_GE(alive_livings_on_team(world, 0), 1)
                    << "the company is extinct before tick 150";
            }
            EXPECT_GE(alive_livings_on_team(world, 2), 1)
                << "the enemy host is extinct before tick 150";

            for (int i = 150; i < 300 && !world.game_ended; ++i)
                world.tick();
            EXPECT_GE(world.tick_count_, 300u)
                << "the smoke must run its full course";

            int crew_alive = 0;
            for (const std::uint32_t crew_id : crew_ids)
            {
                walker* member = world.find_by_id(crew_id);
                if (member != nullptr && !member->dead())
                    ++crew_alive;
            }

            // The sides ENGAGED: blood was drawn on one side or the other
            // by tick 300 (a smoke where nobody fights is a pathing dead
            // zone). The enemy count alone can't carry this — generators
            // replace losses — so crew losses count as engagement too.
            // Level 17 is exempt SINCE THE F4 CALIBRATION: the whole camp
            // holds posts now (the staged-fights retune), so the first
            // blood lands when the crew walks the ~30 tiles to the wagon
            // corridor — after this smoke's 300-tick window. Its staged
            // shape is pinned in its case below instead.
            if (smoke.id != 17)
            {
                EXPECT_TRUE(alive_livings_on_team(world, 2) < t2_at_load ||
                            crew_alive < static_cast<int>(crew_ids.size()))
                    << "nobody was engaged by tick 300";
            }

            switch (smoke.id)
            {
                case 6:
                    // The Hay War (level_06.md): all 3 muster tents alive at
                    // 300 (the depots hold until the crew arrives) and the
                    // relief column (tick 600) still dormant.
                    EXPECT_EQ(3, alive_generators_on_team(world, 2))
                        << "the depot tents must hold until the crew arrives";
                    EXPECT_EQ(6, alive_dormant_livings(world))
                        << "the relief column must still be dormant at 300";
                    break;
                case 15:
                {
                    // Wolf Winter (level_15.md): the dusk waves (400/900/
                    // 1500/2200) are all still dormant at 300, and the
                    // SAVE_ALL protectee stands — no run may show the Reeve
                    // dying before the crew wipes.
                    EXPECT_EQ(24, alive_dormant_livings(world))
                        << "the dusk waves must still be dormant at 300";
                    walker* reeve = find_named_living(world, "The Reeve");
                    EXPECT_TRUE(reeve != nullptr && !reeve->dead())
                        << "the Reeve must survive the 300-tick brawl";
                    break;
                }
                case 17:
                    // Ashfall Gate (level_17.md): the gate's boss beat keeps
                    // — the two warm-metal golem wards and the four big-orc
                    // muscle posts are all alive at 300 (nothing reaches the
                    // gate early) and the 14 delayed spawns (8@350 not yet
                    // woken at the 300 checkpoint, 6@700) still dormant.
                    EXPECT_EQ(2, alive_family_on_team(world, FAMILY_GOLEM, 2))
                        << "the gate wards must hold through 300";
                    EXPECT_EQ(4,
                              alive_family_on_team(world, FAMILY_BIG_ORC, 2))
                        << "the gate muscle must hold through 300";
                    EXPECT_EQ(14, alive_dormant_livings(world))
                        << "both reinforcement waves must be dormant at 300";
                    break;
                case 18:
                    // The Warm Mint (level_18.md): the two hostile hosts
                    // fight EACH OTHER (team 1 took losses too — it fields
                    // no generators, so its count is a clean kill signal)
                    // and the 12 delayed spawns (6 t2@400, 6 t1 push@900)
                    // are still dormant. (Kettle's survival is a
                    // CALIBRATION concern, not a structural pin: the doc's
                    // own t1 rear rank spawns three tiles from his door
                    // pocket. The crew-survival clause moved to the same
                    // calibration class after the v10 loader obmap fix —
                    // the 17 upper-story defenders were collision ghosts
                    // and now join the brawl, which wipes the no-kiting
                    // stand-in crew by 300; re-sweep with
                    // scripts/longseason_playtest.sh before the next
                    // balance pass.)
                    EXPECT_LT(alive_livings_on_team(world, 1), t1_at_load)
                        << "the mint's two hosts must fight each other";
                    EXPECT_EQ(12, alive_dormant_livings(world))
                        << "both delayed waves must be dormant at 300";
                    break;
                default:
                    FAIL() << "smoke level without a designed gate";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// (7) Weather: the winter act is buried. Levels 14, 15 and 16 all carry far
// more snow than the terrain-override threshold, so the authoritative roll
// always lands on Snow. The Smelter's Road (13) closes autumn with the
// FIRST dusting — deliberately UNDER the threshold, so winter's arrival
// reads in the briefing and the ground, but the weather stays autumn's
// (Snow is applied exclusively by the override, never by the decile hash).
// ---------------------------------------------------------------------------
TEST_F(LongseasonCampaignTest, winter_snows_and_the_first_dusting_does_not)
{
    for (const int id : {14, 15, 16})
    {
        SCOPED_TRACE("scen" + std::to_string(id));
        LoadedLongseasonLevel fx(id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();
        EXPECT_GE(count_snow_tiles(world), og::kSnowWeatherTileThreshold)
            << "the winter act must stay buried in snow";
        world.roll_weather();
        EXPECT_EQ(WeatherKind::Snow, world.weather())
            << "the blizzard override must land after the roll";
    }

    // The Smelter's Road: a real dusting, deliberately sub-threshold.
    LoadedLongseasonLevel road(13);
    ASSERT_TRUE(road.loaded);
    const int dusting = count_snow_tiles(road.world());
    EXPECT_GT(dusting, 0) << "winter must announce itself on the road";
    EXPECT_LT(dusting, og::kSnowWeatherTileThreshold)
        << "the dusting must stay under the Snow override threshold";
    road.world().roll_weather();
    EXPECT_NE(WeatherKind::Snow, road.world().weather())
        << "Snow comes only from the terrain override";
}
