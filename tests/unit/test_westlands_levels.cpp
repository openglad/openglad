// Shipped "War of the Westlands" campaign validation
// (builtin/westlands.glad, authored by tools/westlands_mapgen).
//
// Every registered level (ids 1-17 and 19-26; 18 is the deliberate act gap)
// is loaded through the production campaign-mount path and pinned against
// the authoring invariants the generator promises:
//   - structure: title/floors/grid geometry, briefing budgets, scenario type
//     bits, start markers, army census (with tolerance), generators, the
//     per-NPC v10 extras (spawn_delay / specials_disabled), the MAXOBS
//     budget, and the exact exit-destination sets;
//   - the campaign GRAPH: exit-closure from level 1 reaches every level, the
//     four branches (4, 6, 12, 24) hold, and backtrack exits only return to
//     forward-graph predecessors;
//   - the cast: SAVE_ALL levels carry the named team-0 Bearer, and the
//     recurring named NPCs round-trip their teams and extras;
//   - traversal: aligned Z-stair pairs on every floor boundary, footing for
//     every authored entity, and A*-reachability (respecting floors, air
//     holes and Z-stairs) from the lead start marker to every exit and
//     every stair pair;
//   - balance: 300-tick spectator smokes of the big battles with the
//     designed stand-in crew (4x lvl-5 soldiers + a lvl-3 thief);
//   - weather: the High Pass (level 5) is blizzard country — the terrain
//     override forces WeatherKind::Snow after the roll.
//
// This test is the regression pin for the committed package: if it drifts
// from tools/westlands_mapgen, regenerate the package (the tool self-checks
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
#include <openglad/platform/game_context.h>
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
loader& westlands_levels_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

void wire_westlands_world_entity_services(GameWorld* world,
                                          LevelRuntimeData* level)
{
    (void)level;
    if (world == nullptr)
        return;
    loader* game_loader = &westlands_levels_loader();
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

const LevelDataHooks& westlands_levels_hooks()
{
    static const LevelDataHooks hooks = [] {
        LevelDataHooks h{};
        h.wire_world_entity_services = wire_westlands_world_entity_services;
        return h;
    }();
    return hooks;
}

// Mounts the shipped Westlands campaign for the duration of one test and
// restores the previous mount in teardown.
class WestlandsCampaignTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("westlands"))
            << "builtin/westlands.glad should restore and mount";
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error("westlands");
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

private:
    std::string previous_;
};

// A campaign level loaded with full sim context. The world's own SimRandom
// drives the sim (mirroring the production text-protocol wiring) so the
// battle smokes get realistic AI cadence from a reproducible seed.
struct LoadedWestlandsLevel
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedWestlandsLevel(int id, std::uint32_t seed = 0)
        : level(id, true, &westlands_levels_hooks())
        , gameplay(level, save, events, cfg)
    {
        level.world().rng_.state_ = seed;
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &level.world().rng_, &cfg);
        gc.rng = &level.world().rng_;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedWestlandsLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

// ---------------------------------------------------------------------------
// The authored structure (tools/westlands_mapgen act tables). The census
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
    int type_bits; // SCEN_TYPE_* (5 == CAN_EXIT | SAVE_ALL)
    int start_markers;
    int team0_livings;
    int team0_generators;
    int team2_livings;
    int team2_generators;
    int delayed_spawns;
    int specials_disabled;
    std::vector<int> exit_destinations;
};

const std::vector<ShippedLevel>& shipped_levels()
{
    static const std::vector<ShippedLevel> levels = {
        {1, "The Quiet Vale", 1, 60, 40, 0, 10, 1, 0, 9, 0, 2, 0, {2}},
        {2, "The Forest Road", 1, 90, 40, 5, 9, 1, 0, 30, 0, 11, 7, {3, 1}},
        // (Content batch 2026-07: the Ford and Refuge waves scaled up to a
        // real flood — the act-1 defenses carry a lvl-6 ally skeleton camp
        // and lvl-9 door-wards respectively; counts moved 19->39 / 23->38,
        // beat ticks and per-walker levels unchanged.)
        {3, "The Last Ford", 1, 70, 45, 4, 10, 7, 1, 39, 2, 30, 1, {4, 2}},
        {4, "The Hidden Refuge", 2, 70, 50, 4, 10, 9, 1, 38, 2, 32, 1,
         {5, 7, 3}},
        {5, "The High Pass", 1, 60, 60, 4, 10, 4, 0, 25, 1, 4, 1, {6, 4}},
        {6, "Under the Mountain", 3, 60, 60, 5, 28, 1, 0, 21, 1, 1, 0, {8, 9}},
        {7, "The Frozen Wall", 3, 80, 45, 0, 25, 2, 1, 37, 2, 0, 0, {5}},
        {8, "The Bridge of Shadow", 2, 70, 40, 4, 17, 4, 0, 33, 1, 0, 2, {10}},
        {9, "The Lost Delve", 2, 60, 50, 4, 10, 1, 0, 37, 0, 4, 1, {8, 6}},
        {10, "The Golden Wood", 2, 70, 50, 4, 12, 8, 4, 40, 1, 14, 1, {11, 8}},
        {11, "The Great River", 1, 60, 80, 4, 10, 4, 1, 38, 2, 4, 1, {12, 10}},
        {12, "The Falls", 2, 80, 50, 4, 8, 4, 0, 40, 2, 20, 1, {13, 19}},
        {13, "The Plains of the Horse-lords", 1, 90, 50, 0, 15, 14, 0, 54, 3,
         9, 0, {14, 12}},
        {14, "The Wizard's Vale", 4, 60, 60, 0, 23, 0, 0, 23, 2, 0, 1,
         {15, 13}},
        {15, "The Deeping Wall", 2, 80, 50, 0, 26, 1, 1, 47, 2, 1, 0,
         {16, 14}},
        {16, "The White City", 3, 90, 50, 0, 17, 18, 1, 80, 5, 7, 0, {17, 15}},
        {17, "The Black Gate", 2, 90, 50, 0, 30, 7, 0, 58, 3, 0, 0, {24, 16}},
        {19, "The Dead Marshes", 1, 90, 50, 5, 10, 1, 0, 45, 4, 6, 1,
         {20, 12}},
        {20, "The Crossroads", 1, 80, 50, 4, 12, 9, 0, 33, 1, 18, 1, {21, 19}},
        {21, "The Pass of the Spider", 1, 90, 40, 5, 9, 1, 0, 28, 1, 0, 2,
         {22, 20}},
        {22, "The Tower of the Moon", 4, 60, 50, 5, 8, 3, 0, 23, 2, 2, 1,
         {23, 21}},
        {23, "The Ash Plains", 1, 90, 50, 5, 9, 3, 0, 57, 3, 6, 1, {24, 22}},
        {24, "The Mountain of Fire", 3, 90, 50, 5, 14, 24, 0, 74, 3, 13, 0,
         {25, 26}},
        {25, "The Scouring", 1, 60, 40, 0, 9, 1, 0, 15, 2, 0, 0, {26, 24}},
        {26, "The Grey Ships", 1, 60, 35, 0, 9, 0, 0, 3, 0, 0, 0, {1}},
    };
    return levels;
}

// The designed FORWARD story graph (design.md). Backtrack exits are the
// withdraw mechanic: they must only return to a forward-graph predecessor.
// 26 -> 1 is the deliberate full-circle loop and is checked separately so
// predecessor closures stay meaningful.
const std::map<int, std::vector<int>>& forward_graph()
{
    static const std::map<int, std::vector<int>> graph = {
        {1, {2}},   {2, {3}},       {3, {4}},   {4, {5, 7}},   {5, {6}},
        {6, {8, 9}}, {7, {5}},      {8, {10}},  {9, {8}},      {10, {11}},
        {11, {12}}, {12, {13, 19}}, {13, {14}}, {14, {15}},    {15, {16}},
        {16, {17}}, {17, {24}},     {19, {20}}, {20, {21}},    {21, {22}},
        {22, {23}}, {23, {24}},     {24, {25, 26}}, {25, {26}}, {26, {}},
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
        for (int ty = 0; ty < lo.h; ++ty)
            for (int tx = 0; tx < lo.w; ++tx)
            {
                const int i = tx + ty * lo.w;
                if (lo.data[i] == PIX_ZSTAIR_UP &&
                    hi.data[i] == PIX_ZSTAIR_DOWN)
                {
                    pairs.push_back({f, tx, ty});
                }
            }
    }
    return pairs;
}

// --- Fall-line audit support (Wave E5). --------------------------------------
// Single-cell ground passability: the Living arm of
// GameWorld::query_grid_passable with none of the flyer / forestwalk /
// ethereal escapes — the tiles a plain ground walker can STAND on. A fall
// landing must be immediately standable; water, lava, boulder and torch
// bases all bounce the faller into the engine's A5 landing nudge, and
// levels must not rely on the nudge. Keep in lockstep with the classifier
// in tools/westlands_mapgen/main.cpp.
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
    if (!ground_cell_standable(g.data[tx + ty * g.w]))
        return false;
    const PixieData& dec = world.decor_for_floor(floor);
    if (dec.valid() && dec.w == g.w && dec.h == g.h)
    {
        const unsigned char d = dec.data[tx + ty * dec.w];
        if (d < DECOR_MAX &&
            kDecorRegistry[d].pass == DecorPassability::BlocksGround)
            return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Structural pins for every shipped level.
// ---------------------------------------------------------------------------
TEST_F(WestlandsCampaignTest, levels_round_trip_the_authored_structure)
{
    for (const ShippedLevel& expected : shipped_levels())
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedWestlandsLevel fx(expected.id);
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
        }
        EXPECT_EQ(expected.type_bits, static_cast<int>(world.type))
            << "scenario type bits (CAN_EXIT / SAVE_ALL)";
        EXPECT_TRUE(fx.level.generated)
            << "mapgen output carries the SCEN_TYPE_GENERATED provenance "
               "mark (metadata-side; world.type above stays clean)";

        EXPECT_GE(fx.level.description.size(), 4u) << "briefing tells the story";
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
        EXPECT_LE(std::abs(census.livings[2] - expected.team2_livings),
                  kArmyTolerance)
            << "the enemy host: " << census.livings[2] << " vs designed "
            << expected.team2_livings;
        EXPECT_EQ(0, census.livings[1]) << "armies are team 2 by convention";
        EXPECT_EQ(0, census.generators[1]);
        EXPECT_EQ(expected.team0_generators, census.generators[0])
            << "team-0 generators";
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
            << "the exact exit-destination set (design.md level graph)";
    }
}

// ---------------------------------------------------------------------------
// (2) The campaign graph: closure, branches, and backtrack discipline.
// ---------------------------------------------------------------------------
TEST_F(WestlandsCampaignTest, campaign_graph_reaches_every_level)
{
    // Authored edges, read back from the shipped package itself.
    std::map<int, std::vector<int>> edges;
    std::set<int> registered;
    for (const ShippedLevel& expected : shipped_levels())
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedWestlandsLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded);
        edges[expected.id] = exit_destinations(fx.world());
        registered.insert(expected.id);
    }

    // The full designed id set: 1..26 contiguous except the act gap 18.
    std::set<int> designed;
    for (int id = 1; id <= 26; ++id)
        if (id != 18)
            designed.insert(id);
    EXPECT_EQ(designed, registered) << "the 25 shipped level ids";

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

    // The four branch points offer both designed roads.
    const std::map<int, std::vector<int>> branch_pins = {
        {4, {3, 5, 7}},   // high pass vs the northern plea (+ backtrack)
        {6, {8, 9}},      // the bridge vs the delve
        {12, {13, 19}},   // THE GREAT BRANCH: war vs the burden
        {24, {25, 26}},   // the scouring vs sailing straight
    };
    for (const auto& [id, dests] : branch_pins)
    {
        EXPECT_EQ(dests, edges[id])
            << "branch level " << id << " must offer its designed roads";
    }

    // Every authored edge is either a designed forward road, the 26 -> 1
    // full-circle loop, or a backtrack to a forward-graph predecessor.
    for (const auto& [from, dests] : edges)
    {
        for (const int to : dests)
        {
            SCOPED_TRACE("exit " + std::to_string(from) + " -> " +
                         std::to_string(to));
            ASSERT_TRUE(registered.count(to) != 0)
                << "exit destination must exist in the package";
            if (from == 26 && to == 1)
                continue; // the grey ships sail home: full-circle replay
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
// (3) The cast: the Bearer on every SAVE_ALL level, and the recurring named
// NPCs' v10 extras (teams, specials_disabled, spawn_delay) round-trip.
// ---------------------------------------------------------------------------
TEST_F(WestlandsCampaignTest, save_all_levels_guard_the_named_bearer)
{
    struct BearerPin
    {
        int level_id;
        int stat_level;
        bool specials_disabled;
        int spawn_delay;
    };
    // On the Forest Road (Wave E1) the cargo runs WITH the company from
    // tick 0: a NON-guard lvl-4 thief inside the crew's marker wedge, so
    // team-0 AI moves him east with the fighting line (specials sheathed —
    // no bombs in the column). Under the Mountain he catches the company
    // up at tick 600 and keeps the entry hall. Everywhere else he is on
    // the map from tick 0, sheltered behind the crew's marker line (Wave
    // E4: the whole Burden's Road — 2..12 and 19..24 — carries him under
    // SAVE_ALL; only the war-path 13..17 goes without).
    static constexpr BearerPin kBearer[] = {
        {2, 4, true, 0},    {3, 4, true, 0},  {4, 4, true, 0},
        {5, 4, true, 0},    {6, 5, false, 600}, {8, 5, true, 0},
        {9, 5, true, 0},    {10, 5, true, 0}, {11, 5, true, 0},
        {12, 5, true, 0},   {19, 5, true, 0}, {20, 5, true, 0},
        {21, 5, true, 0},   {22, 5, true, 0}, {23, 5, true, 0},
        {24, 5, false, 0},
    };

    std::set<int> save_all_ids;
    for (const BearerPin& pin : kBearer)
        save_all_ids.insert(pin.level_id);
    for (const ShippedLevel& level : shipped_levels())
    {
        const bool expect_save_all = save_all_ids.count(level.id) != 0;
        EXPECT_EQ(expect_save_all,
                  (level.type_bits & SCEN_TYPE_SAVE_ALL) != 0)
            << "scen" << level.id << " SAVE_ALL flag";
    }

    for (const BearerPin& pin : kBearer)
    {
        SCOPED_TRACE("scen" + std::to_string(pin.level_id));
        LoadedWestlandsLevel fx(pin.level_id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();
        ASSERT_NE(0, static_cast<int>(world.type) & SCEN_TYPE_SAVE_ALL)
            << "the Burden's Road levels protect the named cargo";

        walker* bearer = find_named_living(world, "The Bearer");
        ASSERT_NE(nullptr, bearer) << "SAVE_ALL level needs its Bearer";
        EXPECT_EQ(0, static_cast<int>(bearer->team_num()));
        EXPECT_EQ(FAMILY_THIEF, static_cast<int>(bearer->family()));
        EXPECT_EQ(pin.stat_level, static_cast<int>(bearer->stats()->level()));
        EXPECT_EQ(pin.specials_disabled, bearer->specials_disabled());
        EXPECT_EQ(pin.spawn_delay, static_cast<int>(bearer->spawn_delay()))
            << "the cargo's authored arrival tick must round-trip";

        // Wave F2: the Bearer alone carries npc_flags bit 2 ("protected"),
        // scoping the SAVE_ALL watch to him — named allies dying no longer
        // fail the mission. Exactly one protected walker per SAVE_ALL level.
        EXPECT_TRUE(bearer->save_all_protected())
            << "the Bearer must carry the protected flag";
        int protected_count = 0;
        for (const auto& uptr : world.oblist)
        {
            const walker* ob = uptr.get();
            if (ob != nullptr && ob->save_all_protected())
                ++protected_count;
        }
        EXPECT_EQ(1, protected_count)
            << "the SAVE_ALL watch must cover the Bearer and ONLY him";
    }
}

TEST_F(WestlandsCampaignTest, named_cast_extras_round_trip)
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
        // (The Grey Wizard bars the span at scen8 UNNAMED: he is fated to
        // fall there, and scen8 was authored pre-F2, when a named team-0
        // death ended a SAVE_ALL mission — the F2 protected-flag scoping
        // watches the Bearer alone now, but the stand-in ships as-built.
        // See grey_wizard_falls_unnamed below.)
        // The betrayal at the throat, and the brood-mother that won't chase.
        {21, "Sneak", FAMILY_THIEF, 2, 8, true, 0},
        {21, "The Lurker", FAMILY_SLIME, 2, 10, false, 0},
        // The grey one rides again: at dawn on the Wall, late at the finale.
        // (The White Rider is the ARCHMAGE — the campaign's returned grey
        // wizard, not a rank mage; both placements pin the family.)
        {15, "White Rider", FAMILY_ARCHMAGE, 0, 10, false, 500},
        {24, "White Rider", FAMILY_ARCHMAGE, 0, 10, false, 900},
        // The muster holds the plain from tick 0 (the briefing's promise).
        {24, "Ranger-King", FAMILY_SOLDIER, 0, 9, false, 0},
        // The war levels' named orc captains (content batch 2026-07): each
        // names the design doc's existing champion/vanguard post — same
        // family, level and posture as the unnamed post it replaces, so
        // every census and balance pin is untouched.
        {13, "Herd-Reaver", FAMILY_BIG_ORC, 2, 8, false, 0},
        {15, "Ram-Captain", FAMILY_BIG_ORC, 2, 6, false, 0},
        {16, "Breach-Lord", FAMILY_BIG_ORC, 2, 8, false, 0},
        {17, "Gate-Warden", FAMILY_ORC, 2, 8, false, 0},
    };

    for (const NamedPin& pin : kCast)
    {
        SCOPED_TRACE(std::string(pin.name) + " in scen" +
                     std::to_string(pin.level_id));
        EXPECT_LE(std::strlen(pin.name), kNameByteBudget)
            << "names serialize through a 12-byte buffer";
        LoadedWestlandsLevel fx(pin.level_id);
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
            << "named cast members other than the Bearer stay non-critical "
               "(Wave F2 SAVE_ALL scoping)";
    }
}

// The Grey Wizard is FATED to fall at the Bridge of Shadow, and scen8 is a
// SAVE_ALL level (the Bearer crosses it): under the pre-F2 legacy rule the
// mission ended when any named team-0 living died, so his name field stays
// empty — the same deliberate-unnamed rule as the finale's door-wards.
// (Since Wave F2 the protected flag scopes the watch to the Bearer alone,
// so the guard is no longer load-bearing, but the stand-in ships as-built.)
// The briefing still names him; the placed stand-in (lvl-9 team-0 mage,
// guard, special sheathed) must round-trip unnamed.
TEST_F(WestlandsCampaignTest, grey_wizard_falls_unnamed)
{
    LoadedWestlandsLevel fx(8);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    EXPECT_EQ(nullptr, find_named_living(world, "Grey Wizard"))
        << "the as-built package ships the span's stand-in unnamed";

    walker* wizard = nullptr;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->family() == FAMILY_MAGE && ob->team_num() == 0)
        {
            wizard = ob;
            break;
        }
    }
    ASSERT_NE(nullptr, wizard) << "the span must still be barred";
    EXPECT_TRUE(wizard->stats()->name.empty());
    EXPECT_EQ(9, static_cast<int>(wizard->stats()->level()));
    EXPECT_EQ(ACT_GUARD, wizard->act_type()) << "he holds mid-span";
    EXPECT_TRUE(wizard->guard_hold_post())
        << "allied guards ship hold-post (npc_flags bit 1): without it the "
           "wake rule would march him off the span at first sight of the "
           "pursuit";
    EXPECT_TRUE(wizard->specials_disabled()) << "he cannot teleport away";
}

// ---------------------------------------------------------------------------
// (4) Traversal structure: aligned stair pairs on every floor boundary, and
// every authored entity standing on ground its footprint can occupy.
// ---------------------------------------------------------------------------
TEST_F(WestlandsCampaignTest, stairs_on_every_floor_boundary)
{
    for (const ShippedLevel& expected : shipped_levels())
    {
        if (expected.floors < 2)
            continue;
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedWestlandsLevel fx(expected.id);
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
                if (lo.data[i] == PIX_ZSTAIR_UP &&
                    hi.data[i] == PIX_ZSTAIR_DOWN)
                {
                    ++pairs;
                }
            }
            EXPECT_GE(pairs, 1) << "floor boundary " << f << "<->" << f + 1
                                << " needs an aligned UP/DOWN stair pair";
        }
    }
}

// Stair-clearance audit (B2 tooling rule, docs/z-axis-design.md; mirrors
// tools/westlands_mapgen's self-check): neither a stair-pair cell nor any of
// its 4-neighborhood arrival cells, on EITHER floor of the pair, may hold an
// ACT_GUARD post, a generator, or ground-blocking decor. An immobile blocker
// there used to seal the staircase outright ("the stairs are blocked from
// above so I can't ascend"); the engine's blocked-arrival nudge now rescues
// it at runtime, but no shipped level may RELY on the nudge. Roaming livings
// are fine (they move off; the nudge covers the transient).
TEST_F(WestlandsCampaignTest, stair_cells_and_arrivals_clear_of_immobile_blockers)
{
    static constexpr int kArrivalOffsets[5][2] = {
        {0, 0}, {0, -1}, {-1, 0}, {1, 0}, {0, 1}};
    for (const ShippedLevel& expected : shipped_levels())
    {
        if (expected.floors < 2)
            continue;
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedWestlandsLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();

        auto audit_arrival_cell = [&](int pf, int nx, int ny)
        {
            const PixieData& g = world.grid_for_floor(pf);
            if (!g.valid() || nx < 0 || ny < 0 || nx >= g.w || ny >= g.h)
                return;
            const PixieData& dec = world.decor_for_floor(pf);
            if (dec.valid() && nx < dec.w && ny < dec.h)
            {
                const unsigned char d = dec.data[nx + ny * dec.w];
                EXPECT_FALSE(
                    d < DECOR_MAX &&
                    kDecorRegistry[d].pass == DecorPassability::BlocksGround)
                    << "blocking decor " << static_cast<int>(d)
                    << " on stair cell/arrival (" << nx << ", " << ny
                    << ") floor " << pf;
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
                    (order == Order::Living &&
                     ob->act_type() == ACT_GUARD);
                if (!immobile_post)
                    continue;
                EXPECT_FALSE(ob->xpos() + ob->sizex() > x0 &&
                             ob->xpos() < x0 + GRID_SIZE &&
                             ob->ypos() + ob->sizey() > y0 &&
                             ob->ypos() < y0 + GRID_SIZE)
                    << (order == Order::Generator ? "generator"
                                                  : "ACT_GUARD post")
                    << " (family " << static_cast<int>(ob->family())
                    << ") posted on stair cell/arrival (" << nx << ", " << ny
                    << ") floor " << pf << " would seal the staircase";
            }
        };

        const std::vector<StairPair> pairs = find_stair_pairs(world);
        ASSERT_FALSE(pairs.empty());
        for (const StairPair& stair : pairs)
        {
            for (const auto& off : kArrivalOffsets)
            {
                audit_arrival_cell(stair.floor, stair.tx + off[0],
                                   stair.ty + off[1]);
                audit_arrival_cell(stair.floor + 1, stair.tx + off[0],
                                   stair.ty + off[1]);
            }
        }
    }
}

TEST_F(WestlandsCampaignTest, entities_stand_on_passable_ground)
{
    for (const ShippedLevel& expected : shipped_levels())
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedWestlandsLevel fx(expected.id);
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
                EXPECT_NE(PIX_AIR, g.data[tx + ty * g.w])
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

// Fall-line audit (Wave E5, mirrors tools/westlands_mapgen's self-check):
// any AIR cell a ground walker can actually step into — 8-adjacent to a
// standable cell of the SAME floor — must land its faller cleanly. Chase
// the column down through stacked AIR; the landing cell must be standable
// (base AND decor): never a wall top, water, lava or blocking decor.
// Falling past floor 0 is a pit death, a designed mechanic, and stays
// legal. The engine's A5 nudge can rescue a blocked landing, but no
// shipped level may RELY on the nudge.
TEST_F(WestlandsCampaignTest, air_fall_lines_land_on_standable_ground)
{
    for (const ShippedLevel& expected : shipped_levels())
    {
        if (expected.floors < 2)
            continue;
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedWestlandsLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();

        for (int f = 1; f < world.floor_count(); ++f)
        {
            const PixieData& g = world.grid_for_floor(f);
            ASSERT_TRUE(g.valid());
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
                    while (lf > 0 && world.grid_for_floor(lf)
                                             .data[tx + ty * g.w] == PIX_AIR)
                        --lf;
                    if (world.grid_for_floor(lf).data[tx + ty * g.w] ==
                        PIX_AIR)
                        continue; // fell past floor 0: pit death by design
                    EXPECT_TRUE(cell_standable(world, lf, tx, ty))
                        << "fall line at tile (" << tx << ", " << ty
                        << ") floor " << f
                        << " lands on impassable ground of floor " << lf
                        << " (the level must not rely on the engine's "
                           "landing nudge)";
                    // Fall-DEPTH bound (fall damage, mirrors the mapgen
                    // fall-depth audit): no shipped fall line may drop more
                    // than 4 stories — the knee of the 50% damage cap.
                    EXPECT_LE(f - lf, 4)
                        << "fall line at tile (" << tx << ", " << ty
                        << ") floor " << f << " drops " << (f - lf)
                        << " stories to floor " << lf
                        << " — deeper than the 4-story fall-damage cap knee";
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
TEST_F(WestlandsCampaignTest, exits_and_stairs_reachable_from_the_lead_start)
{
    for (const ShippedLevel& expected : shipped_levels())
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedWestlandsLevel fx(expected.id);
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
// (5b) A*-reachability of the ARMIES (Wave E3, the trapped-orc rule): every
// living and every generator must be reachable from the lead start marker by
// a ground probe — kill-all levels demand the player can close with every
// foe, and CAN_EXIT levels promise no foe is sealed away. Flyers are exempt
// (ghosts hover over lava, meres and air pits by design). Any DELIBERATE
// ground exception needs an entry here WITH a comment, kept in lockstep with
// tools/westlands_mapgen/main.cpp's reachability_exceptions().
// ---------------------------------------------------------------------------
namespace {

struct ReachabilityException
{
    int id;
    int floor;
    int tx;
    int ty;
};

const std::vector<ReachabilityException>& reachability_exceptions()
{
    static const std::vector<ReachabilityException> exceptions = {
        // (none: every ground living and generator in the shipped package
        //  is reachable from the lead start marker.)
    };
    return exceptions;
}

bool reachability_exception_allowed(int id, int floor, int tx, int ty)
{
    for (const ReachabilityException& e : reachability_exceptions())
        if (e.id == id && e.floor == floor && e.tx == tx && e.ty == ty)
            return true;
    return false;
}

} // namespace

TEST_F(WestlandsCampaignTest, livings_and_generators_reachable_from_the_lead_start)
{
    for (const ShippedLevel& expected : shipped_levels())
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedWestlandsLevel fx(expected.id);
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
            const int tx = ob->xpos() / GRID_SIZE;
            const int ty = ob->ypos() / GRID_SIZE;
            if (reachability_exception_allowed(expected.id, ob->floor(), tx,
                                               ty))
                continue;
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
                << static_cast<int>(ob->family()) << " at tile (" << tx
                << ", " << ty << ") floor " << ob->floor()
                << " is unreachable from the lead start marker (fix the "
                   "map or add a reachability-allowlist entry)";
        }
    }
}

// ---------------------------------------------------------------------------
// (6) Balance smokes: the big battles self-play for 300 ticks with the
// designed stand-in crew (4x lvl-5 soldiers + a lvl-3 thief on the authored
// start markers). Neither side may be extinct at tick 150, and the crew must
// not be wiped by tick 300.
// ---------------------------------------------------------------------------
TEST_F(WestlandsCampaignTest, battle_smokes_hold_the_line)
{
    static constexpr int kSmokeLevels[] = {3, 15, 16, 17, 24};
    static constexpr std::uint32_t kSeeds[] = {1337u, 424242u};

    for (const int id : kSmokeLevels)
    {
        for (const std::uint32_t seed : kSeeds)
        {
            SCOPED_TRACE("scen" + std::to_string(id) + " seed " +
                         std::to_string(seed));
            LoadedWestlandsLevel fx(id, seed);
            ASSERT_TRUE(fx.loaded);
            GameWorld& world = fx.world();

            const std::vector<walker*> markers = find_start_markers(world);
            ASSERT_GE(markers.size(), 5u) << "the crew needs its formation";

            struct StandIn
            {
                int family;
                int stat_level;
            };
            static constexpr StandIn kCrew[] = {
                {FAMILY_SOLDIER, 5}, {FAMILY_SOLDIER, 5}, {FAMILY_SOLDIER, 5},
                {FAMILY_SOLDIER, 5}, {FAMILY_THIEF, 3},
            };
            std::vector<std::uint32_t> crew_ids;
            for (std::size_t i = 0; i < std::size(kCrew); ++i)
            {
                walker* stand_in =
                    world.add_ob(Order::Living, kCrew[i].family);
                ASSERT_NE(nullptr, stand_in);
                stand_in->set_team_num(0);
                stand_in->set_real_team_num(0);
                stand_in->stats()->set_level(kCrew[i].stat_level);
                stand_in->set_floor(markers[i]->floor());
                stand_in->setxy(markers[i]->xpos(), markers[i]->ypos());
                crew_ids.push_back(stand_in->entity_id());
            }

            for (int i = 0; i < 150 && !world.game_ended; ++i)
                world.tick();
            EXPECT_GE(alive_livings_on_team(world, 0), 1)
                << "the defense is extinct before tick 150";
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
            EXPECT_GE(crew_alive, 1)
                << "the stand-in crew was wiped out by tick 300";

            // SAVE_ALL smoke levels: the named cargo must also survive the
            // brawl (the design's Bearer balance guardrail — his death
            // insta-fails the mission for the player).
            if ((static_cast<int>(world.type) & SCEN_TYPE_SAVE_ALL) != 0)
            {
                walker* bearer = find_named_living(world, "The Bearer");
                EXPECT_TRUE(bearer != nullptr && !bearer->dead())
                    << "the Bearer must survive the 300-tick brawl";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// (7) Weather: the High Pass is blizzard country. Its grid carries far more
// snow than the terrain-override threshold, so the authoritative roll always
// lands on Snow — and a snowless level can never roll it (Snow is applied
// exclusively by the override, never by the decile hash).
// ---------------------------------------------------------------------------
TEST_F(WestlandsCampaignTest, high_pass_always_snows)
{
    LoadedWestlandsLevel fx(5);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();

    int snow_tiles = 0;
    for (int f = 0; f < world.floor_count(); ++f)
    {
        const PixieData& g = world.grid_for_floor(f);
        ASSERT_TRUE(g.valid());
        const int cells = g.w * g.h;
        for (int i = 0; i < cells; ++i)
        {
            const unsigned char t = static_cast<unsigned char>(g.data[i]);
            if (t == PIX_SNOW1 || t == PIX_SNOW2)
                ++snow_tiles;
        }
    }
    EXPECT_GE(snow_tiles, og::kSnowWeatherTileThreshold)
        << "the High Pass must stay buried in snow";

    world.roll_weather();
    EXPECT_EQ(WeatherKind::Snow, world.weather())
        << "the blizzard override must land after the roll";

    // Control: the snowless Quiet Vale can never roll Snow.
    LoadedWestlandsLevel vale(1);
    ASSERT_TRUE(vale.loaded);
    vale.world().roll_weather();
    EXPECT_NE(WeatherKind::Snow, vale.world().weather())
        << "Snow comes only from the terrain override";
}

// ---------------------------------------------------------------------------
// (A11) Authored guards: the shipped package carries 184 Living GUARD command
// bytes (the mapgen's place_living(guard=true) posts). The loader now honors
// them, so garrison sentries hold their posts instead of roaming. Since the
// guard wake rule (walker.cpp act_guard, 2026-07-11) the package authors a
// per-guard policy: ENEMY guards ship wake-enabled ambush posts
// (guard_hold_post()==false — they hunt once a foe walks into genuine
// sight), allied guards ship hold-post sentries. Pin the per-level counts
// for two representative levels and prove the enemy posts still hold when
// nothing is sighted: with the crew absent (scen14 authors no team-0
// livings) act_guard never acquires a foe, so no wake fires and every guard
// must sit at its authored spot through a real sim run.
// ---------------------------------------------------------------------------
TEST_F(WestlandsCampaignTest, authored_guards_load_as_guards_and_hold_post)
{
    // scen14 "The Wizard's Vale": 23 tower-garrison guards, team 2 only.
    LoadedWestlandsLevel fx(14, /*seed=*/42);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();

    std::vector<walker*> guards;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->act_type() == ACT_GUARD)
            guards.push_back(ob);
    }
    ASSERT_EQ(23u, guards.size())
        << "scen14 authors 23 Living GUARD commands (mapgen guard posts)";
    for (std::size_t i = 0; i < guards.size(); ++i)
    {
        EXPECT_EQ(2, static_cast<int>(guards[i]->team_num()))
            << "guard " << i << ": scen14's garrison is team 2 only";
        EXPECT_FALSE(guards[i]->guard_hold_post())
            << "guard " << i
            << ": enemy guards ship wake-enabled (ambush posts)";
    }

    std::vector<std::pair<float, float>> posts;
    posts.reserve(guards.size());
    for (const walker* g : guards)
        posts.emplace_back(g->xpos(), g->ypos());

    for (int t = 0; t < 60; ++t)
        world.tick();
    ASSERT_GE(world.level_tick_count(), 60u) << "the sim must actually run";

    for (std::size_t i = 0; i < guards.size(); ++i)
    {
        if (guards[i]->dead())
            continue; // (defensive; no foes exist on this run)
        EXPECT_EQ(posts[i].first, guards[i]->xpos())
            << "guard " << i << " left its post";
        EXPECT_EQ(posts[i].second, guards[i]->ypos())
            << "guard " << i << " left its post";
        EXPECT_EQ(ACT_GUARD, guards[i]->act_type())
            << "guard " << i
            << " woke with no foe on the level — no wake without a genuine "
               "sighting";
    }

    // Cross-check a second level's authored count: scen2 "The Forest Road"
    // posts 19 team-2 wolf-pocket guards across the ambushes (Wave E1: the
    // Bearer is deliberately NOT a guard there — he runs east with the
    // crew's fighting line instead of holding a post the pursuit would
    // overrun).
    LoadedWestlandsLevel road(2);
    ASSERT_TRUE(road.loaded);
    int road_guards = 0;
    int road_wakers = 0;
    for (const auto& uptr : road.world().oblist)
    {
        const walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->act_type() == ACT_GUARD)
        {
            ++road_guards;
            if (!ob->guard_hold_post())
                ++road_wakers;
        }
    }
    EXPECT_EQ(19, road_guards);
    EXPECT_EQ(19, road_wakers)
        << "all 19 wolf-pocket guards are team-2 ambushers: wake-enabled";
    walker* road_bearer = find_named_living(road.world(), "The Bearer");
    ASSERT_NE(nullptr, road_bearer);
    EXPECT_NE(ACT_GUARD, road_bearer->act_type())
        << "the Forest Road Bearer must RUN with the crew, not hold a post";
}
