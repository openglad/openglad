// Shipped Concept Playground campaign validation. The six epic multifloor war
// stories (levels 605-610 in builtin/org.openglad.concept.glad) are loaded
// through the production campaign-mount path and pinned against the authoring
// invariants tools/concept_mapgen promises: floor counts, the team-0 start
// marker formations that deploy the player's crew as the defense, the placed
// team-0 allies (the two Grey Wizards, the defender generators), the team-2
// attacker army sizes, at least one aligned Z-stair pair on every floor
// boundary, and every authored entity standing on ground its own footprint can
// occupy. This test is the regression pin for the levels.

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

struct ExpectedArmy
{
    int livings;
    int generators;
};

struct ShippedEpicLevel
{
    int id;
    const char* title;
    int floors;
    int grid_w;
    int grid_h;
    int start_markers; // the crew's authored deployment formation
    ExpectedArmy team0; // placed allies: named heroes + defender generators
    ExpectedArmy team2; // the attacker horde
};

// The authored rosters (tools/concept_mapgen/main.cpp). Attacker living
// counts get a small tolerance so an intentional rebalance of a rank or two
// does not break the pin; marker, ally, and generator counts and floor
// geometry are exact.
constexpr ShippedEpicLevel kEpicLevels[] = {
    {605, "The Deeping Wall", 2, 80, 50, 26, {1, 1}, {47, 2}},
    {606, "The Wizard's Vale", 4, 60, 60, 23, {0, 0}, {23, 2}},
    {607, "The Bridge of Shadow", 2, 70, 40, 17, {1, 0}, {33, 1}},
    {608, "Under the Mountain", 3, 60, 60, 28, {0, 0}, {21, 1}},
    {609, "The Black Gate", 2, 90, 50, 30, {0, 0}, {58, 3}},
    {610, "The Frozen Wall", 3, 80, 45, 25, {0, 1}, {37, 2}},
};

constexpr int kArmyTolerance = 3;

// SCENARIO INFORMATION dialog budget: at most 33 characters per line.
constexpr std::size_t kBriefingLineBudget = 33;

} // namespace

TEST_F(ConceptCampaignTest, epic_levels_field_crew_markers_against_the_horde)
{
    for (const ShippedEpicLevel& expected : kEpicLevels)
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedConceptLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded) << "level should load from the mounted campaign";
        GameWorld& world = fx.world();

        EXPECT_EQ(expected.title, world.title) << "shipped roster title";
        EXPECT_EQ(expected.floors, world.floor_count()) << "floor count";
        EXPECT_EQ(expected.grid_w, static_cast<int>(world.grid.w));
        EXPECT_EQ(expected.grid_h, static_cast<int>(world.grid.h));
        for (int f = 0; f < world.floor_count(); ++f)
        {
            EXPECT_TRUE(world.grid_for_floor(f).valid())
                << "floor " << f << " grid must round-trip";
        }
        EXPECT_FALSE(fx.level.description.empty()) << "war story briefing";
        for (const std::string& line : fx.level.description)
        {
            EXPECT_LE(line.size(), kBriefingLineBudget)
                << "briefing line overflows the dialog: '" << line << "'";
        }

        int livings[MAX_TEAM + 1] = {};
        int generators[MAX_TEAM + 1] = {};
        int total_livings = 0;
        int starts = 0;
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr)
                continue;
            const int team = ob->team_num() & 7;
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
        EXPECT_EQ(expected.start_markers, starts)
            << "the crew's deployment formation";
        EXPECT_EQ(expected.team0.livings, livings[0])
            << "placed team-0 allies";
        EXPECT_EQ(expected.team0.generators, generators[0])
            << "team 0 (defender-side) generators";
        EXPECT_EQ(0, livings[1]) << "the old team-1 defender army is gone — "
                                    "the player's crew is the defense";
        EXPECT_EQ(0, generators[1]) << "no team-1 generators remain";
        EXPECT_NEAR(expected.team2.livings, livings[2], kArmyTolerance)
            << "attacker army size";
        EXPECT_EQ(expected.team2.generators, generators[2])
            << "attacker generators";
        EXPECT_LE(total_livings, MAXOBS)
            << "seeded armies must fit under the living cap";
        EXPECT_GE(starts, 17) << "room to deploy a full crew in formation";

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
        EXPECT_GE(exits, 1) << "an exit chains the campaign onward";
    }
}

// The two named team-0 heroes carry the v10 per-NPC extras through the
// shipped package: 607's Grey Wizard holds mid-span with his teleport
// disabled; 605's Grey Wizard (the archmage) arrives ~500 ticks in on the
// horde's east flank, dormant until then.
TEST_F(ConceptCampaignTest, grey_wizards_guard_the_span_and_arrive_at_dawn)
{
    auto find_grey_wizard = [](GameWorld& world) -> walker* {
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob != nullptr && ob->query_order() == Order::Living &&
                (ob->team_num() & 7) == 0 &&
                ob->stats()->name == "Grey Wizard")
            {
                return ob;
            }
        }
        return nullptr;
    };

    {
        SCOPED_TRACE("scen607");
        LoadedConceptLevel fx(607);
        ASSERT_TRUE(fx.loaded);
        walker* wizard = find_grey_wizard(fx.world());
        ASSERT_NE(nullptr, wizard) << "the Grey Wizard holds the bridge";
        EXPECT_EQ(FAMILY_MAGE, static_cast<int>(wizard->family()));
        EXPECT_EQ(9, static_cast<int>(wizard->stats()->level()));
        // He is authored ACT_GUARD and the .fss carries that command byte,
        // but the level loader applies the family-default act type (the
        // command byte is read and dropped — long-standing engine behavior),
        // so there is no runtime act_type pin here.
        EXPECT_TRUE(wizard->specials_disabled())
            << "he cannot teleport away — he shall not pass";
        EXPECT_EQ(0, static_cast<int>(wizard->spawn_delay()));
        EXPECT_FALSE(wizard->dormant()) << "he is there from the first tick";
        EXPECT_EQ(1, static_cast<int>(wizard->floor())) << "mid-span, floor 1";
    }

    {
        SCOPED_TRACE("scen605");
        LoadedConceptLevel fx(605);
        ASSERT_TRUE(fx.loaded);
        walker* wizard = find_grey_wizard(fx.world());
        ASSERT_NE(nullptr, wizard) << "the Grey Wizard rides at dawn";
        EXPECT_EQ(FAMILY_ARCHMAGE, static_cast<int>(wizard->family()));
        EXPECT_EQ(10, static_cast<int>(wizard->stats()->level()));
        EXPECT_GT(wizard->spawn_delay(), 0) << "he arrives late, by design";
        EXPECT_TRUE(wizard->dormant())
            << "a delayed spawn loads dormant until its tick comes";
        EXPECT_FALSE(wizard->specials_disabled())
            << "the archmage keeps his powers";
        EXPECT_EQ(0, static_cast<int>(wizard->floor()));
        EXPECT_GE(wizard->xpos() / GRID_SIZE, 70)
            << "he appears at the EAST edge of the southern field";
    }
}

// Deterministic battle smoke: the wall siege (605) and the four-story tower
// assault (606, cross-floor AI) must survive 300 ticks with the attacker army
// still fielding troops. A stand-in team-0 soldier is deployed onto the lead
// start marker — the same spot a player's first character would take — so the
// horde has someone to hate; and 605's dormant Grey Wizard must still be
// waiting for his tick (spawn_delay ~500 >> 300).
TEST_F(ConceptCampaignTest, epic_battles_run_headless)
{
    for (const int id : {605, 606})
    {
        SCOPED_TRACE("scen" + std::to_string(id));
        LoadedConceptLevel fx(id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();

        walker* lead_marker = nullptr;
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob != nullptr && ob->query_order() == Order::Special &&
                ob->family() == FAMILY_RESERVED_TEAM)
            {
                lead_marker = ob;
                break;
            }
        }
        ASSERT_NE(nullptr, lead_marker) << "the crew's lead start marker";
        walker* stand_in = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, stand_in);
        stand_in->set_team_num(0);
        stand_in->set_real_team_num(0);
        stand_in->stats()->set_level(5);
        stand_in->set_floor(lead_marker->floor());
        stand_in->setxy(lead_marker->xpos(), lead_marker->ypos());

        for (int i = 0; i < 300 && !world.game_ended; ++i)
            world.tick();
        EXPECT_GE(world.tick_count_, 300u) << "simulation must have advanced";

        int attackers_alive = 0;
        walker* archmage = nullptr;
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr)
                continue;
            if (!ob->dead() && ob->query_order() == Order::Living &&
                (ob->team_num() & 7) == 2)
            {
                ++attackers_alive;
            }
            if (ob->query_order() == Order::Living &&
                ob->family() == FAMILY_ARCHMAGE && (ob->team_num() & 7) == 0)
            {
                archmage = ob;
            }
        }
        EXPECT_GE(attackers_alive, 1) << "the horde wiped out instantly";
        if (id == 605)
        {
            ASSERT_NE(nullptr, archmage) << "the delayed Grey Wizard persists";
            EXPECT_TRUE(archmage->dormant())
                << "at tick 300 the dawn (tick ~900) has not yet come";
            EXPECT_FALSE(archmage->dead());
        }
    }
}

TEST_F(ConceptCampaignTest, epic_levels_have_stairs_on_every_floor_boundary)
{
    for (const ShippedEpicLevel& expected : kEpicLevels)
    {
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

TEST_F(ConceptCampaignTest, epic_level_entities_stand_on_passable_ground)
{
    for (const ShippedEpicLevel& expected : kEpicLevels)
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
