// Shipped Concept Playground campaign validation. The five Z-axis demo
// levels (600-604 in builtin/org.openglad.concept.glad) plus the scripted
// boss arena "The Ninefold Court" (605) are loaded through the production
// campaign-mount path and pinned against the authoring invariants
// tools/concept_mapgen promises: floor counts and grid geometry, the start
// markers, the seeded foes and pillar generators, briefing budgets, the
// exit chain (600→…→604→605, with the court looping home to 600), aligned
// Z-stair pairs on the boundaries that have them, and every authored
// entity standing on ground its own footprint can occupy. The court's
// embedded pack (packs/org.openglad.concept.showcase/scripts/court.lua
// inside the .glad) is additionally pinned end to end: registered on
// mount, and its ward/judgment/victory phases driven through real sim
// ticks. This test is the regression pin for the committed package.
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
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
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
#include <vector>

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
    int start_markers;    // the demos deploy one start; the court a full crew
    int team1_livings;    // the seeded foes
    int team1_generators; // the court's four warding pillars
    int exits;            // the exit that chains the tour onward
    // 601/603 traverse their floors by falling, so they carry no stair pair.
    bool stairs_every_boundary;
};

// The authored rosters (tools/concept_mapgen/main.cpp). Every pin is exact:
// the demos are tiny, deliberate teaching levels, and the court is the
// level-scripting showcase.
constexpr ShippedDemoLevel kDemoLevels[] = {
    {600, "Stairs", 2, 24, 18, 1, 1, 0, 1, true},
    {601, "Mind the Gap", 2, 28, 18, 1, 1, 0, 1, false},
    {602, "Glasshouse", 2, 22, 16, 1, 1, 0, 1, true},
    {603, "Drop Zone", 2, 22, 16, 1, 1, 0, 1, false},
    {604, "Arc Range", 2, 30, 16, 1, 2, 0, 1, true},
    {605, "The Ninefold Court", 1, 30, 22, 8, 1, 4, 1, false},
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
        EXPECT_TRUE(fx.level.generated)
            << "mapgen output carries the SCEN_TYPE_GENERATED provenance "
               "mark (metadata-side, never world.type)";
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
        int generators[MAX_TEAM + 1] = {};
        int starts = 0;
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr)
                continue;
            const int team = ob->team_num() & 7;
            if (ob->query_order() == Order::Living)
                ++livings[team];
            else if (ob->query_order() == Order::Generator)
                ++generators[team];
            else if (ob->query_order() == Order::Special &&
                     ob->family() == FAMILY_RESERVED_TEAM && team == 0)
                ++starts;
        }
        EXPECT_EQ(expected.start_markers, starts) << "the player starts";
        EXPECT_EQ(0, livings[0]) << "no placed team-0 livings in the tour";
        EXPECT_EQ(expected.team1_livings, livings[1]) << "the seeded foes";
        EXPECT_EQ(expected.team1_generators, generators[1])
            << "the court's warding pillars";

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

// The tour's tail: Arc Range chains into the scripted showcase (604 → 605),
// and The Ninefold Court loops home to Stairs (605 → 600), keeping the
// whole chain inside the package.
TEST_F(ConceptCampaignTest, tour_tail_chains_through_the_court_and_home)
{
    const struct { int id; int destination; const char* why; } hops[] = {
        {604, 605, "Arc Range hands the tour to the court"},
        {605, 600, "the court loops home to Stairs"},
    };
    for (const auto& hop : hops)
    {
        SCOPED_TRACE("scen" + std::to_string(hop.id));
        LoadedConceptLevel fx(hop.id);
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
        ASSERT_NE(nullptr, exit_ob) << "the level ships one exit";
        EXPECT_EQ(hop.destination, static_cast<int>(exit_ob->stats()->level()))
            << hop.why;
    }
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

// ---------------------------------------------------------------------------
// The Ninefold Court's embedded level script (the scripting showcase).
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kShowcasePackId = "org.openglad.concept.showcase";

std::vector<std::string> drain_notifications(og::sim::SimEventLog& events)
{
    std::vector<std::string> out;
    for (const og::sim::Event& ev : events.drain())
        if (ev.kind == og::sim::EventKind::Notification)
            out.push_back(ev.text);
    return out;
}

bool contains_text(const std::vector<std::string>& lines, const char* needle)
{
    for (const std::string& line : lines)
        if (line.find(needle) != std::string::npos)
            return true;
    return false;
}

} // namespace

// The .glad carries packs/org.openglad.concept.showcase/scripts/court.lua;
// campaign packs follow campaign mounts (test_campaign_packs.cpp pins the
// mechanism, this pins the shipped payload).
TEST_F(ConceptCampaignTest, court_embedded_pack_registers_on_mount)
{
    bool registered = false;
    for (const auto& script : og::script::pack_scripts())
        if (script.pack_id == kShowcasePackId)
            registered = true;
    EXPECT_TRUE(registered)
        << "mounting the concept campaign must register the embedded "
           "showcase pack's court.lua";
}

// Drives the court's whole scripted arc through real sim ticks: the ward
// stamped and announced at load, the pillar ledger, the ward failing with
// the last pillar, the ninefold judgment pulse on the 300-tick anchor, and
// the per-entity victory hook on the Magistrate's death. The engine's own
// win logic is untouched by the script, so this is pure decoration — but
// it is decoration the package promises, end to end.
TEST_F(ConceptCampaignTest, court_script_runs_the_ninefold_fight)
{
    LoadedConceptLevel fx(605);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();

    walker* boss = nullptr;
    std::vector<walker*> pillars;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->query_order() == Order::Living &&
            ob->family() == FAMILY_ARCHMAGE)
            boss = ob;
        else if (ob->query_order() == Order::Generator)
            pillars.push_back(ob);
        else if (ob->query_order() == Order::Special &&
                 ob->family() == FAMILY_RESERVED_TEAM)
        {
            // Production never ticks with live start markers (the crew
            // deploy consumes them); mirror that here.
            ob->set_dead(1);
        }
    }
    ASSERT_NE(nullptr, boss) << "the Magistrate holds the bench";
    ASSERT_EQ(4u, pillars.size()) << "four warding pillars";
    EXPECT_EQ("Magistrate", boss->stats()->name) << "named boss round-trips";
    EXPECT_EQ(ACT_GUARD, boss->act_type()) << "he sits the bench";
    EXPECT_TRUE(boss->guard_hold_post()) << "hold-post keeps him on the dais";
    EXPECT_FALSE(boss->stats()->query_bit_flags(BIT_INVINCIBLE))
        << "the ward is script-stamped, not level data";

    // Tick 1: on_load stamps the ward and announces the gimmick.
    world.tick();
    std::vector<std::string> lines = drain_notifications(fx.events);
    EXPECT_TRUE(contains_text(lines, "wards hold"))
        << "on_load announces the gimmick";
    EXPECT_TRUE(boss->stats()->query_bit_flags(BIT_INVINCIBLE))
        << "the Magistrate is warded while pillars stand";

    // One pillar falls. Generator deaths dispatch on_entity_death, so the
    // announcement lands during death() itself -- no tick has to elapse and
    // the script keeps no pillar ledger of its own.
    pillars[0]->set_dead(1);
    pillars[0]->death();
    lines = drain_notifications(fx.events);
    EXPECT_TRUE(contains_text(lines, "A pillar falls: 3 wards remain."))
        << "pillar fall is event-driven, not polled";
    EXPECT_TRUE(boss->stats()->query_bit_flags(BIT_INVINCIBLE));

    // The rest fall: the ward breaks on the last one, again immediately.
    for (std::size_t i = 1; i < pillars.size(); ++i)
    {
        pillars[i]->set_dead(1);
        pillars[i]->death();
    }
    lines = drain_notifications(fx.events);
    EXPECT_TRUE(contains_text(lines, "The wards fail"))
        << "last pillar drops the ward";
    EXPECT_FALSE(boss->stats()->query_bit_flags(BIT_INVINCIBLE))
        << "the Magistrate stands exposed";
    world.tick();

    // Let the pillar-death explosions burn out, then discard their events.
    while (world.level_tick_count() < 30u)
        world.tick();
    (void)fx.events.drain();

    // The judgment phase: a ninefold ring on the 300-tick anchor. The
    // explosions deal their damage on animation end, so give the pulse a
    // short settle window and count the DamageTile events it leaves.
    while (world.level_tick_count() < 300u)
        world.tick();
    lines = drain_notifications(fx.events);
    EXPECT_TRUE(contains_text(lines, "The Court passes judgment!"))
        << "the pulse announces itself on the 300-tick anchor";
    for (int settle = 0; settle < 40; ++settle)
        world.tick();
    int strikes = 0;
    for (const og::sim::Event& ev : fx.events.drain())
        if (ev.kind == og::sim::EventKind::DamageTile)
            ++strikes;
    EXPECT_EQ(9, strikes) << "the Court judges ninefold: eight ring "
                             "strikes and the center";

    // The Magistrate falls: the per-entity on_death hook (consumed on
    // fire) plays the victory beat.
    boss->set_dead(1);
    boss->death();
    lines = drain_notifications(fx.events);
    EXPECT_TRUE(contains_text(lines, "The Magistrate falls!"))
        << "victory fanfare";
    EXPECT_TRUE(contains_text(lines, "The Ninefold Court is broken."))
        << "victory notification";

    // The whole arc must have run without a single script error.
    for (const og::script::ScriptError& err :
         world.scripts().host().errors())
        ADD_FAILURE() << "script error at " << err.where << ": "
                      << err.message;
}

// The court's fourth scripted rule, and the one the fight test above never
// reaches because it breaks the pillars before they can work: every third
// generator spawn is promoted to an Adjutant through the generator
// `customize_spawn` hook. "Every third" is derived from the spawn's sim
// entity id (cookbook R6 forbids a mutable counter in a family hook), so the
// promotion is deterministic and identical on every peer.
//
// This drives it the way the game does — real ticks, real generator fire —
// rather than calling the hook by hand, because what is worth pinning is that
// the pillars' spawns arrive already promoted.
TEST_F(ConceptCampaignTest, court_pillars_promote_every_third_spawn)
{
    LoadedConceptLevel fx(605);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();

    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        // Production never ticks with live start markers.
        if (ob != nullptr && ob->query_order() == Order::Special &&
            ob->family() == FAMILY_RESERVED_TEAM)
            ob->set_dead(1);
    }

    // Let the pillars work. Generators fire on their own cadence, so this
    // runs until the first promotion lands rather than guessing a tick count.
    walker* adjutant = nullptr;
    int spawns = 0;
    for (unsigned t = 0; t < 900u && adjutant == nullptr; ++t)
    {
        world.tick();
        spawns = 0;
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr || ob->dead() != 0 ||
                ob->query_order() != Order::Living || ob->team_num() == 0)
                continue;
            if (ob->family() == FAMILY_ARCHMAGE)
                continue;  // the Magistrate is authored, not spawned
            ++spawns;
            if (ob->stats()->name == std::string("Adjutant"))
                adjutant = ob;
        }
    }
    ASSERT_GT(spawns, 0) << "the pillars must actually raise something";
    ASSERT_NE(nullptr, adjutant)
        << "no spawn was promoted in 900 ticks: customize_spawn never fired, "
           "or the derived every-third rule stopped selecting anything";

    // The writ of office: extra rank, a speed bonus, a hardier body healed to
    // its new maximum, and a heavier blow.
    EXPECT_GE(adjutant->stats()->level(), 4)
        << "promotion adds three levels to the generator's own roll (>=1)";
    EXPECT_GT(adjutant->stepsize(), 0.0f);
    EXPECT_FLOAT_EQ(adjutant->stats()->max_hitpoints(),
                    adjutant->stats()->hitpoints())
        << "the promotion heals to the new maximum, so it is visible";

    // Striking one from the rolls is announced — the other half of the level
    // death hook, which only an Adjutant can reach.
    (void)fx.events.drain();
    adjutant->set_dead(1);
    adjutant->death();
    const std::vector<std::string> lines = drain_notifications(fx.events);
    EXPECT_TRUE(contains_text(lines, "An Adjutant is struck from the rolls."))
        << "the level death hook recognises its own promotions by name";

    for (const og::script::ScriptError& err :
         world.scripts().host().errors())
        ADD_FAILURE() << "script error at " << err.where << ": "
                      << err.message;
}
