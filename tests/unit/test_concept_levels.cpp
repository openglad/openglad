// Shipped Concept Playground campaign validation. The five Z-axis demo
// levels (600-604 in builtin/org.openglad.concept.glad) are loaded through
// the production campaign-mount path and pinned against the authoring
// invariants tools/concept_mapgen promises: floor counts and grid geometry,
// the single start marker, the seeded foes, briefing budgets, the exit that
// chains the tour onward (604 loops home to 600), aligned Z-stair pairs on
// the boundaries that have them, and every authored entity standing on
// ground its own footprint can occupy. This test is the regression pin for
// the committed package.
//
// The six epic multifloor war stories that used to ship here as levels
// 605-610 moved to builtin/org.openglad.westlands.glad
// (tools/westlands_mapgen, ids 15/14/8/6/17/7); that package is pinned by
// tests/unit/test_westlands_levels.cpp.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
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

#include <string>

namespace {

// ---------------------------------------------------------------------------
// Entity wiring: one shared loader for every level load (mirrors the
// production headless wiring; the concept levels use only stock families).
// ---------------------------------------------------------------------------
loader& concept_levels_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

void wire_concept_world_entity_services(GameWorld* world, LevelRuntimeData* level)
{
    (void)level;
    if (world == nullptr)
        return;
    loader* game_loader = &concept_levels_loader();
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

const LevelDataHooks& concept_levels_hooks()
{
    static const LevelDataHooks hooks = [] {
        LevelDataHooks h{};
        h.wire_world_entity_services = wire_concept_world_entity_services;
        return h;
    }();
    return hooks;
}

// Mounts the shipped concept campaign for the duration of one test and
// restores the previous mount in teardown.
class ConceptCampaignTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("org.openglad.concept"))
            << "builtin/org.openglad.concept.glad should restore and mount";
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error("org.openglad.concept");
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

private:
    std::string previous_;
};

// A campaign level loaded with full sim context.
struct LoadedConceptLevel
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedConceptLevel(int id)
        : level(id, true, &concept_levels_hooks())
        , gameplay(level, save, events, cfg)
    {
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &rng, &cfg);
        gc.rng = &rng;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedConceptLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

struct ShippedDemoLevel
{
    int id;
    const char* title;
    int floors;
    int grid_w;
    int grid_h;
    int start_markers; // the single player start of each demo
    int team1_livings; // the seeded foes
    int exits;         // the exit that chains the tour onward
    // 601/603 traverse their floors by falling, so they carry no stair pair.
    bool stairs_every_boundary;
};

// The authored rosters (tools/concept_mapgen/main.cpp). Every pin is exact:
// the demos are tiny, deliberate teaching levels.
constexpr ShippedDemoLevel kDemoLevels[] = {
    {600, "Stairs", 2, 24, 18, 1, 1, 1, true},
    {601, "Mind the Gap", 2, 28, 18, 1, 1, 1, false},
    {602, "Glasshouse", 2, 22, 16, 1, 1, 1, true},
    {603, "Drop Zone", 2, 22, 16, 1, 1, 1, false},
    {604, "Arc Range", 2, 30, 16, 1, 2, 1, true},
};

// SCENARIO INFORMATION dialog budget: at most 33 characters per line.
constexpr std::size_t kBriefingLineBudget = 33;

} // namespace

TEST_F(ConceptCampaignTest, demo_levels_round_trip_the_authored_structure)
{
    for (const ShippedDemoLevel& expected : kDemoLevels)
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedConceptLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded) << "level should load from the mounted campaign";
        GameWorld& world = fx.world();

        EXPECT_EQ(expected.title, world.title) << "shipped demo title";
        EXPECT_EQ(expected.floors, world.floor_count()) << "floor count";
        EXPECT_EQ(expected.grid_w, static_cast<int>(world.grid.w));
        EXPECT_EQ(expected.grid_h, static_cast<int>(world.grid.h));
        for (int f = 0; f < world.floor_count(); ++f)
        {
            EXPECT_TRUE(world.grid_for_floor(f).valid())
                << "floor " << f << " grid must round-trip";
        }
        EXPECT_FALSE(fx.level.description.empty()) << "teaching briefing";
        for (const std::string& line : fx.level.description)
        {
            EXPECT_LE(line.size(), kBriefingLineBudget)
                << "briefing line overflows the dialog: '" << line << "'";
        }

        int livings[MAX_TEAM + 1] = {};
        int starts = 0;
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr)
                continue;
            const int team = ob->team_num() & 7;
            if (ob->query_order() == Order::Living)
                ++livings[team];
            else if (ob->query_order() == Order::Special &&
                     ob->family() == FAMILY_RESERVED_TEAM && team == 0)
                ++starts;
        }
        EXPECT_EQ(expected.start_markers, starts) << "the player start";
        EXPECT_EQ(0, livings[0]) << "no placed team-0 livings in the demos";
        EXPECT_EQ(expected.team1_livings, livings[1]) << "the seeded foes";

        int exits = 0;
        for (const auto& uptr : world.fxlist)
        {
            walker* ob = uptr.get();
            if (ob != nullptr && ob->query_order() == Order::Treasure &&
                ob->family() == FAMILY_EXIT)
            {
                ++exits;
            }
        }
        EXPECT_EQ(expected.exits, exits) << "the exit chains the tour onward";
    }
}

// The tour's last stop loops home: 604's exit names 600 as its destination,
// keeping the demo chain inside the demo set now that the epic war stories
// live in the Westlands campaign.
TEST_F(ConceptCampaignTest, arc_range_exit_loops_home_to_stairs)
{
    LoadedConceptLevel fx(604);
    ASSERT_TRUE(fx.loaded);
    walker* exit_ob = nullptr;
    for (const auto& uptr : fx.world().fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Treasure &&
            ob->family() == FAMILY_EXIT)
        {
            exit_ob = ob;
            break;
        }
    }
    ASSERT_NE(nullptr, exit_ob) << "Arc Range ships one exit";
    EXPECT_EQ(600, static_cast<int>(exit_ob->stats()->level()))
        << "the demo tour restarts at Stairs";
}

TEST_F(ConceptCampaignTest, demo_levels_have_stairs_on_flagged_boundaries)
{
    for (const ShippedDemoLevel& expected : kDemoLevels)
    {
        if (!expected.stairs_every_boundary)
            continue;
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedConceptLevel fx(expected.id);
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

TEST_F(ConceptCampaignTest, demo_level_entities_stand_on_passable_ground)
{
    for (const ShippedDemoLevel& expected : kDemoLevels)
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedConceptLevel fx(expected.id);
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
