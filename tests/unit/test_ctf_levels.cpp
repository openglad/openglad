// Shipped CTF campaign validation. Every level in builtin/org.openglad.ctf.glad
// is loaded through the production campaign-mount path and checked against the
// authoring invariants the generator promises (TYPE_CTF, one flag per team,
// anchor clusters, control points, passable tiles, no exits). The committed
// sprites are validated against read_pixie_file's strict palette/sidecar
// rules, and a deterministic bot match smokes the smallest original arena.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/platform/game_context.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gloader_ctf.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/save_data.h>

#include "test_gameplay_context_scope.h"

#include <memory>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// CTF-aware entity wiring. The default loader created for hook-less
// LevelRuntimeData instances lacks the CTF treasure entries, so the shipped
// levels are loaded through a local hooks table that mirrors the production
// (SDL/headless) wiring: one shared loader with register_ctf_loader_entries.
// ---------------------------------------------------------------------------
loader& ctf_levels_loader()
{
    static loader instance{EntityFactory{}};
    static const bool registered = [] {
        register_ctf_loader_entries(instance);
        return true;
    }();
    (void)registered;
    return instance;
}

void wire_ctf_world_entity_services(GameWorld* world, LevelRuntimeData* level)
{
    (void)level;
    if (world == nullptr)
        return;
    loader* game_loader = &ctf_levels_loader();
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

const LevelDataHooks& ctf_levels_hooks()
{
    static const LevelDataHooks hooks = [] {
        LevelDataHooks h{};
        h.wire_world_entity_services = wire_ctf_world_entity_services;
        return h;
    }();
    return hooks;
}

// Mounts the shipped CTF campaign for the duration of one test and restores
// the previous mount in teardown so sibling tests see an untouched state.
class CtfCampaignTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("org.openglad.ctf"))
            << "builtin/org.openglad.ctf.glad should restore and mount";
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error("org.openglad.ctf");
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

private:
    std::string previous_;
};

// A campaign level loaded with full sim context, ready to tick.
struct LoadedCtfLevel
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedCtfLevel(int id)
        : level(id, true, &ctf_levels_hooks())
        , gameplay(level, save, events, cfg)
    {
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &rng, &cfg);
        gc.rng = &rng;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedCtfLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

std::unique_ptr<walker> make_probe(GameWorld& world)
{
    if (!world.entity_factory)
        return nullptr;
    std::unique_ptr<walker> probe =
        world.entity_factory(Order::Living, FAMILY_SOLDIER);
    if (probe == nullptr)
        return nullptr;
    PixieData square;
    square.frames = 1;
    square.w = 16;
    square.h = 16;
    square.data = std::make_unique<unsigned char[]>(16 * 16);
    probe->set_data(square);
    return probe;
}

bool tile_passable(GameWorld& world, walker* probe, std::int32_t x,
                   std::int32_t y)
{
    return world.query_grid_passable(static_cast<float>(x),
                                     static_cast<float>(y), probe);
}

struct ShippedLevel
{
    int id;
    int team_count;
};

constexpr ShippedLevel kShippedLevels[] = {
    {500, 2}, {501, 4}, {502, 2}, {503, 2}, {504, 2},
    {505, 3}, {506, 2}, {507, 2}, {508, 3}, {509, 4},
};

} // namespace

TEST_F(CtfCampaignTest, every_shipped_level_is_a_valid_ctf_arena)
{
    for (const ShippedLevel& expected : kShippedLevels)
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedCtfLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded) << "level should load from the mounted campaign";
        GameWorld& world = fx.world();

        EXPECT_TRUE(world.type & GameWorld::TYPE_CTF) << "TYPE_CTF bit";
        EXPECT_FALSE(world.type & GameWorld::TYPE_CAN_EXIT_WHENEVER)
            << "CAN_EXIT must be cleared on CTF maps";
        EXPECT_EQ(0u, world.title.rfind("CTF: ", 0))
            << "title should carry the CTF prefix: " << world.title;
        EXPECT_FALSE(fx.level.description.empty())
            << "intro description should explain the mode";

        std::unique_ptr<walker> probe = make_probe(world);
        ASSERT_NE(nullptr, probe);

        int flags_per_team[8] = {};
        int cp_count = 0;
        int exit_count = 0;
        for (const auto& uptr : world.fxlist)
        {
            walker* fx_ob = uptr.get();
            if (fx_ob == nullptr || fx_ob->query_order() != Order::Treasure)
                continue;
            if (fx_ob->family() == og::FAMILY_FLAG)
            {
                flags_per_team[fx_ob->team_num() & 7]++;
                EXPECT_TRUE(tile_passable(world, probe.get(), fx_ob->xpos(),
                                          fx_ob->ypos()))
                    << "flag tile (" << fx_ob->xpos() / GRID_SIZE << ", "
                    << fx_ob->ypos() / GRID_SIZE << ") must be passable";
            }
            else if (fx_ob->family() == og::FAMILY_CTF_POINT)
            {
                ++cp_count;
                EXPECT_TRUE(tile_passable(world, probe.get(), fx_ob->xpos(),
                                          fx_ob->ypos()))
                    << "control point tile must be passable";
            }
            else if (fx_ob->family() == FAMILY_EXIT)
            {
                ++exit_count;
            }
        }

        int anchors_per_team[8] = {};
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr || ob->query_order() != Order::Special ||
                ob->family() != FAMILY_RESERVED_TEAM)
            {
                continue;
            }
            anchors_per_team[ob->team_num() & 7]++;
            EXPECT_TRUE(
                tile_passable(world, probe.get(), ob->xpos(), ob->ypos()))
                << "anchor tile (" << ob->xpos() / GRID_SIZE << ", "
                << ob->ypos() / GRID_SIZE << ") must be passable";
        }

        int flag_teams = 0;
        for (int team = 0; team < 4; ++team)
        {
            if (flags_per_team[team] > 0)
            {
                ++flag_teams;
                EXPECT_EQ(1, flags_per_team[team])
                    << "exactly one flag for team " << team;
                EXPECT_GE(anchors_per_team[team], 8)
                    << "team " << team << " needs an anchor cluster";
                EXPECT_LE(anchors_per_team[team], 16)
                    << "team " << team << " anchor cluster exceeds engine cap";
            }
        }
        EXPECT_EQ(expected.team_count, flag_teams) << "authored flag teams";
        EXPECT_GE(cp_count, 1) << "at least one control point";
        EXPECT_EQ(0, exit_count) << "exits must be stripped";
    }
}

TEST_F(CtfCampaignTest, shipped_levels_author_no_named_npcs)
{
    for (const ShippedLevel& expected : kShippedLevels)
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedCtfLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded);
        for (const auto& uptr : fx.world().oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr || ob->stats() == nullptr)
                continue;
            EXPECT_FALSE(ob->stats()->query_bit_flags(BIT_NAMED))
                << "named NPC survived the CTF transform";
        }
    }
}

// Deterministic bot-match smoke on the smallest original arena: the level
// activates CTF on its first tick, fields a bot squad per team, and the sim
// survives 300 ticks. Flag steals/respawns depend on the CTF AI director
// (its own phase); this smoke only tracks them informationally, and the
// richer interaction assertion lives with the director's tests.
TEST_F(CtfCampaignTest, first_blood_bot_match_smoke)
{
    LoadedCtfLevel fx(506);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    ASSERT_TRUE(world.type & GameWorld::TYPE_CTF);

    bool flag_left_home = false;
    bool respawn_seen = false;
    for (int i = 0; i < 300 && !world.game_ended; ++i)
    {
        world.tick();
        for (int team = 0; team < 2; ++team)
        {
            if (world.ctf.flags[team].present &&
                world.ctf.flags[team].state != og::sim::CtfFlagState::AtHome)
            {
                flag_left_home = true;
            }
        }
        if (!world.ctf.respawn_queue.empty())
            respawn_seen = true;
    }

    EXPECT_TRUE(world.ctf.init_attempted) << "lazy init must have run";
    ASSERT_TRUE(world.ctf.active) << "two flag teams should activate CTF";
    EXPECT_EQ(2, world.ctf.team_count);
    EXPECT_TRUE(world.ctf.flags[0].present);
    EXPECT_TRUE(world.ctf.flags[1].present);
    EXPECT_GE(world.tick_count_, 300u) << "simulation must have advanced";

    int livings_per_team[2] = {};
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && !ob->dead() &&
            ob->query_order() == Order::Living && ob->team_num() < 2)
        {
            livings_per_team[ob->team_num()]++;
        }
    }
    EXPECT_GE(livings_per_team[0], 1) << "team 0 bot squad";
    EXPECT_GE(livings_per_team[1], 1) << "team 1 bot squad";

    // Informational until the CTF AI director phase lands.
    if (flag_left_home || respawn_seen)
    {
        SUCCEED() << "bots interacted with flags/respawns";
    }
}

// The Lagaren flags author a per-map capture limit (stats level 5) which the
// engine resolves during lazy init.
TEST_F(CtfCampaignTest, lagaren_authors_a_five_capture_limit)
{
    LoadedCtfLevel fx(500);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    ASSERT_TRUE(world.ctf.active);
    EXPECT_EQ(5, world.ctf.capture_limit);
}

// ---------------------------------------------------------------------------
// Committed sprite validation. read_pixie_file enforces indexed 8-bit PNG,
// a 256-entry palette within +/-1 of the engine palette, and sidecar frame
// metadata — a parse here proves the generator's output is engine-loadable.
// ---------------------------------------------------------------------------
TEST(CtfSprites, flag_sprite_has_four_team_tinted_frames)
{
    PixieData flag = read_pixie_file("flag.png");
    ASSERT_TRUE(flag.valid()) << "pix/flag.png must parse (palette + sidecar)";
    EXPECT_EQ(4, flag.frames);
    EXPECT_EQ(10, flag.w);
    EXPECT_EQ(14, flag.h);

    const int frame_size = flag.w * flag.h;
    for (int frame = 0; frame < flag.frames; ++frame)
    {
        int team_band = 0;
        int transparent = 0;
        for (int i = 0; i < frame_size; ++i)
        {
            const unsigned char p = flag.data[frame * frame_size + i];
            if (p >= 248)
                ++team_band;
            if (p == 0)
                ++transparent;
        }
        EXPECT_GT(team_band, 0) << "frame " << frame
                                << " cloth must use the 248-255 team band";
        EXPECT_GT(transparent, 0) << "frame " << frame
                                  << " must keep transparent background";
    }
}

TEST(CtfSprites, control_point_sprite_is_a_single_team_ringed_pad)
{
    PixieData pad = read_pixie_file("ctfpoint.png");
    ASSERT_TRUE(pad.valid()) << "pix/ctfpoint.png must parse";
    EXPECT_EQ(1, pad.frames);
    EXPECT_EQ(16, pad.w);
    EXPECT_EQ(16, pad.h);

    int team_band = 0;
    int neutral = 0;
    for (int i = 0; i < pad.w * pad.h; ++i)
    {
        const unsigned char p = pad.data[i];
        if (p >= 248)
            ++team_band;
        else if (p != 0)
            ++neutral;
    }
    EXPECT_GT(team_band, 0) << "ring must team-tint";
    EXPECT_GT(neutral, 0) << "center must stay neutral";
}
