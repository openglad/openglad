// Shipped "War of the Westlands" campaign validation
// (builtin/org.openglad.westlands.glad, authored by tools/westlands_mapgen).
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
                  mount_campaign_package_with_error("org.openglad.westlands"))
            << "builtin/org.openglad.westlands.glad should restore and mount";
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error("org.openglad.westlands");
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
        {2, "The Forest Road", 1, 90, 40, 5, 9, 1, 0, 27, 1, 7, 0, {3, 1}},
        {3, "The Last Ford", 1, 70, 45, 0, 10, 2, 0, 19, 2, 14, 0, {4, 2}},
        {4, "The Hidden Refuge", 2, 70, 50, 0, 10, 4, 0, 23, 2, 20, 0,
         {5, 7, 3}},
        {5, "The High Pass", 1, 60, 60, 0, 10, 0, 0, 25, 1, 4, 0, {6, 4}},
        {6, "Under the Mountain", 3, 60, 60, 1, 28, 1, 0, 14, 1, 5, 0, {8, 9}},
        {7, "The Frozen Wall", 3, 80, 45, 0, 25, 0, 1, 37, 2, 0, 0, {5}},
        {8, "The Bridge of Shadow", 2, 70, 40, 0, 17, 1, 0, 33, 1, 0, 1, {10}},
        {9, "The Lost Delve", 2, 60, 50, 0, 10, 0, 0, 37, 2, 4, 0, {8, 6}},
        {10, "The Golden Wood", 2, 70, 50, 0, 12, 7, 4, 40, 1, 14, 0, {11, 8}},
        {11, "The Great River", 1, 60, 80, 0, 10, 0, 0, 42, 2, 4, 0, {12, 10}},
        {12, "The Falls", 2, 80, 50, 0, 10, 1, 0, 40, 2, 20, 0, {13, 19}},
        {13, "The Plains of the Horse-lords", 1, 90, 50, 0, 15, 14, 0, 54, 3,
         9, 0, {14, 12}},
        {14, "The Wizard's Vale", 4, 60, 60, 0, 23, 0, 0, 23, 2, 0, 1,
         {15, 13}},
        {15, "The Deeping Wall", 2, 80, 50, 0, 26, 1, 1, 47, 2, 1, 0,
         {16, 14}},
        {16, "The White City", 3, 90, 50, 0, 17, 18, 1, 80, 5, 7, 0, {17, 15}},
        {17, "The Black Gate", 2, 90, 50, 0, 30, 1, 0, 58, 3, 0, 0, {24, 16}},
        {19, "The Dead Marshes", 1, 90, 50, 5, 10, 1, 0, 45, 4, 6, 1,
         {20, 12}},
        {20, "The Crossroads", 1, 80, 50, 0, 12, 8, 0, 33, 1, 8, 0, {21, 19}},
        {21, "The Pass of the Spider", 1, 90, 40, 5, 9, 1, 0, 28, 1, 0, 2,
         {22, 20}},
        {22, "The Tower of the Moon", 4, 60, 50, 5, 8, 1, 0, 24, 2, 2, 1,
         {23, 21}},
        {23, "The Ash Plains", 1, 90, 50, 5, 9, 1, 0, 57, 3, 6, 1, {24, 22}},
        {24, "The Mountain of Fire", 3, 90, 50, 5, 14, 22, 0, 74, 3, 9, 0,
         {25, 26}},
        {25, "The Scouring", 1, 60, 40, 0, 9, 1, 0, 15, 2, 2, 0, {26, 24}},
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
    // On the Forest Road the cargo has gone AHEAD: he reaches the hollow at
    // tick 450 (about when a sprinting crew makes the road's end), dormant
    // — and unkillable — until then. Everywhere else he is on the map from
    // tick 0.
    static constexpr BearerPin kBearer[] = {
        {2, 3, false, 450}, {19, 5, true, 0}, {21, 5, true, 0},
        {22, 5, true, 0},   {23, 5, true, 0}, {24, 5, false, 0},
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
        // The Grey Wizard bars the span: his special stays sheathed.
        {8, "Grey Wizard", FAMILY_MAGE, 0, 9, true, 0},
        // The betrayal at the throat, and the brood-mother that won't chase.
        {21, "Sneak", FAMILY_THIEF, 2, 8, true, 0},
        {21, "The Lurker", FAMILY_SLIME, 2, 10, false, 0},
        // The grey one rides again: at dawn on the Wall, late at the finale.
        {15, "White Rider", FAMILY_ARCHMAGE, 0, 10, false, 500},
        {24, "White Rider", FAMILY_ARCHMAGE, 0, 10, false, 900},
        // The muster holds the plain from tick 0 (the briefing's promise).
        {24, "Ranger-King", FAMILY_SOLDIER, 0, 9, false, 0},
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
    }
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
