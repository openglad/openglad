#include <algorithm>
#include <memory>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/io_common.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/platform/game_session.h>
#include "test_save_state_guard.h"
// myscreen is now a macro defined in base.h (via game_session.h)

short load_saved_game(const char *filename, screen *scr);

namespace {

class ScopedGameplayRngOverride
{
public:
    explicit ScopedGameplayRngOverride(IRandom* next)
        : next_(next)
        , previous_(gameplay_rng_override())
        , restore_(previous_)
    {
        set_gameplay_rng_override(&next_);
    }

    ~ScopedGameplayRngOverride()
    {
        if (previous_ != nullptr)
            set_gameplay_rng_override(&restore_);
        else
            set_gameplay_rng_override(nullptr);
    }

    ScopedGameplayRngOverride(const ScopedGameplayRngOverride&) = delete;
    ScopedGameplayRngOverride& operator=(const ScopedGameplayRngOverride&) = delete;

private:
    IRandom* next_ = nullptr;
    IRandom* previous_ = nullptr;
    IRandom* restore_ = nullptr;
};

bool prepare_default_level_load()
{
    restore_default_campaigns();
    restore_default_settings();
#ifdef TESTING
    set_mounted_campaign_for_testing("");
#endif
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    return mount_campaign_package_with_error("org.openglad.gladiator") == CampaignPackageIoError::None;
}

const og::sim::EntitySnapshot* find_entity_snapshot(
    const std::vector<og::sim::EntitySnapshot>& entities,
    std::uint32_t entity_id)
{
    const auto it = std::find_if(
        entities.begin(), entities.end(),
        [entity_id](const og::sim::EntitySnapshot& snapshot) {
            return snapshot.entity_id == entity_id;
        });
    return it == entities.end() ? nullptr : &*it;
}

} // namespace

// Test: Load levels 1-10, covering both version 9 and version 6 scenario formats.
//
// Levels 3, 4, 8 use version 6 format which previously had a buffer overflow
// in the description text reader (tempwidth could exceed the 80-byte oneline
// buffer). This test verifies the fix works.

TEST(LoadLevels, load_multiple_levels) {
    ASSERT_TRUE(prepare_default_level_load()) << "default campaign should be restored and mounted before load test";
    for (int level = 1; level <= 10; level++) {
        trace_clear();

        og::runtime::current_session->myscreen_->save_data.scen_num = static_cast<short>(level);
        og::runtime::current_session->myscreen_->save_data.numplayers = 1;
        og::runtime::current_session->myscreen_->save_data.save("test_level_multi");

        short result = load_saved_game("test_level_multi", og::runtime::current_session->myscreen_);
        (void)result;

        char msg[80];
        snprintf(msg, 80, "level %d should load successfully", level);
        ASSERT_TRUE(trace_contains("game", "level loaded")) << msg;

        // Clean up loaded objects before loading the next level
        og::runtime::current_session->myscreen_->world().delete_objects();
    }
}



// Test: Level data integrity -- verify that loaded level has sensible data
TEST(LoadLevels, level_data_integrity) {
    ASSERT_TRUE(prepare_default_level_load()) << "default campaign should be restored and mounted before integrity test";
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.scen_num = static_cast<short>(1);
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.save("test_level_integrity");

    load_saved_game("test_level_integrity", og::runtime::current_session->myscreen_);

    // Level 1 should have a valid grid
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().grid.valid()) << "level 1 should have a valid grid";

    // Level 1 should have some objects (enemies)
    ASSERT_TRUE(!og::runtime::current_session->myscreen_->world().oblist.empty()) << "level 1 should have objects (enemies/npcs)";

    // Level ID should match what we requested
    ASSERT_EQ(1, og::runtime::current_session->myscreen_->world().id) << "level id should be 1";

    og::runtime::current_session->myscreen_->world().delete_objects();
}

TEST(LoadLevels, completed_non_ctf_level_purges_only_replayable_hostiles)
{
    og::test::ScopedCampaignMountState mount_restore;
    ASSERT_TRUE(prepare_default_level_load());

    og::runtime::GameSession::Config session_config;
    session_config.allocate_screen = true;
    session_config.create_display = false;
    session_config.allocate_prefs = true;
    session_config.install_legacy_globals = false;
    og::runtime::GameSession isolated_session(session_config);
    auto isolated_scope = isolated_session.activate();
    screen* const scr = isolated_session.screen_ptr();
    ASSERT_NE(nullptr, scr);

    SaveData& save = scr->save_data;
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    save.numplayers = 0; // spectator: no roster spawn obscures authored objects
    save.completed_levels[save.current_campaign].insert(save.scen_num);

    ASSERT_EQ(LoadSavedGameError::None,
              load_saved_game_with_error(nullptr, scr));
    ASSERT_EQ(0, scr->world().type & GameWorld::TYPE_SCRIPTED);

    const auto protected_replay_object = [](const walker& object) {
        const Order order = object.query_order();
        const short family = object.family();
        return ((object.team_num() == 0 || object.myguy != nullptr) &&
                order == Order::Living) ||
               (order == Order::Treasure && family == FAMILY_EXIT) ||
               (order == Order::Treasure && family == FAMILY_TELEPORTER);
    };

    std::size_t purged = 0;
    std::size_t protected_alive = 0;
    const auto verify_list = [&](const auto& objects) {
        for (const auto& object : objects)
        {
            ASSERT_NE(nullptr, object);
            if (protected_replay_object(*object))
            {
                if (!object->dead())
                    ++protected_alive;
            }
            else
            {
                EXPECT_TRUE(object->dead())
                    << "completed-level hostile/object must not replay";
                if (object->dead())
                    ++purged;
            }
        }
    };
    verify_list(scr->world().oblist);
    verify_list(scr->world().weaplist);
    verify_list(scr->world().fxlist);

    EXPECT_GT(purged, 0u);
    EXPECT_GT(protected_alive, 0u)
        << "team-zero actors, exits, or teleporters remain replayable";
}



// Test: Loading a nonexistent level falls back to level 1
TEST(LoadLevels, level_fallback) {
    ASSERT_TRUE(prepare_default_level_load()) << "default campaign should be restored and mounted before fallback test";
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.scen_num = 9999;  // This level shouldn't exist
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.save("test_level_fallback");

    load_saved_game("test_level_fallback", og::runtime::current_session->myscreen_);

    // Should have fallen back to level 1
    ASSERT_EQ(1, og::runtime::current_session->myscreen_->world().id) << "nonexistent level should fall back to level 1";

    og::runtime::current_session->myscreen_->world().delete_objects();
}

TEST(LoadLevels, load_advances_world_rng_state)
{
    ASSERT_TRUE(prepare_default_level_load()) << "default campaign should be restored and mounted before rng load test";

    constexpr std::uint32_t seed = 123u;
    LevelRuntimeData level(1, &sdl_level_data_hooks());
    level.world().rng_.state_ = seed;

    GameWorld expected_world(seed);
    expected_world.id = 1;
    sdl_level_data_hooks().wire_world_entity_services(&expected_world, &level);

    og::data::LevelFileMetadata metadata;
    og::data::LevelFileIoError io_error = og::data::LevelFileIoError::None;
    {
        ScopedGameplayRngOverride rng_override(&expected_world.rng_);
        ASSERT_TRUE(og::data::load_level("scen1.fss", expected_world, metadata, &io_error))
            << "scratch scenario load should succeed";
    }

    const std::uint32_t expected_state = expected_world.rng_.state_;
    ASSERT_NE(seed, expected_state) << "scenario load should consume RNG during walker construction";

    ASSERT_TRUE(level.load()) << "level load should succeed";
    ASSERT_EQ(expected_state, level.world().rng_.state_)
        << "world RNG state after load should match the live load-time draws";

    level.world().delete_objects();
    expected_world.delete_objects();
}

TEST(LoadLevels, sdl_level_hooks_create_render_aware_entity_factory)
{
    const LevelDataHooks& hooks = sdl_level_data_hooks();
    ASSERT_NE(nullptr, hooks.create_entity_factory);

    const EntityFactory factory = hooks.create_entity_factory();
    EXPECT_TRUE(static_cast<bool>(factory.attach_render));
    EXPECT_TRUE(static_cast<bool>(factory.report_error));

    walker entity;
    PixieData pixel;
    pixel.frames = 1;
    pixel.w = 1;
    pixel.h = 1;
    pixel.data = std::make_unique<unsigned char[]>(1);
    pixel.data[0] = 1;
    factory.attach_render(entity, pixel);
    EXPECT_TRUE(entity.has_render());

    trace_clear();
    factory.report_error("render-aware factory test error");
    EXPECT_TRUE(trace_contains(
        "popup", "ERROR: render-aware factory test error"));
}

TEST(LoadLevels, compatibility_overload_runs_preparation_and_returns_metadata)
{
    ASSERT_TRUE(prepare_default_level_load());

    GameWorld world(1);
    sdl_level_data_hooks().wire_world_entity_services(&world, nullptr);
    std::string grid_file;
    std::list<std::string> description;
    bool prepared = false;
    og::data::LevelFileIoError error =
        og::data::LevelFileIoError::ParseFailed;

    ASSERT_TRUE(og::data::load_level(
        "scen1.fss",
        world,
        grid_file,
        description,
        [&prepared] { prepared = true; },
        &error));
    EXPECT_TRUE(prepared);
    EXPECT_EQ(og::data::LevelFileIoError::None, error);
    EXPECT_FALSE(grid_file.empty());
    EXPECT_FALSE(description.empty());
    EXPECT_FALSE(world.oblist.empty());
}

TEST(LoadLevels, load_resets_future_entity_ids_to_loaded_world_state)
{
    ASSERT_TRUE(prepare_default_level_load()) << "default campaign should be restored and mounted before entity id load test";

    LevelRuntimeData level(1, &sdl_level_data_hooks());

    for (int i = 0; i < 256; ++i)
    {
        walker* seeded = level.world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, seeded) << "seed entity should be created";
    }
    level.world().delete_objects();

    ASSERT_TRUE(level.load()) << "level load should succeed";

    auto max_entity_id = [&level]() -> std::uint32_t {
        std::uint32_t max_id = 0;
        const auto update = [&max_id](const auto& list) {
            for (const auto& entry : list)
            {
                if (entry)
                    max_id = std::max(max_id, entry->entity_id());
            }
        };

        update(level.world().oblist);
        update(level.world().fxlist);
        update(level.world().weaplist);
        return max_id;
    };

    const std::uint32_t loaded_max_id = max_entity_id();
    ASSERT_GT(loaded_max_id, 0u) << "loaded level should assign entity ids";

    walker* fresh = level.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fresh) << "post-load entity should be created";
    ASSERT_EQ(loaded_max_id + 1, fresh->entity_id())
        << "future ids after load should continue from the loaded world, not discarded pre-load history";

    level.world().delete_objects();
}

TEST(LoadLevels, capture_snapshot_after_real_level_load_and_live_ticks_matches_world)
{
    ASSERT_TRUE(prepare_default_level_load())
        << "default campaign should be restored and mounted before snapshot integration test";

    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    ASSERT_TRUE(
        og::runtime::current_session->myscreen_->save_data.save("test_level_snapshot_capture"))
        << "save should succeed for snapshot integration test";
    ASSERT_TRUE(load_saved_game("test_level_snapshot_capture",
                                og::runtime::current_session->myscreen_) != 0)
        << "load_saved_game should succeed for snapshot integration test";

    GameWorld& world = og::runtime::current_session->myscreen_->world();
    ASSERT_TRUE(world.grid.valid()) << "loaded level should provide a valid grid";
    ASSERT_FALSE(world.oblist.empty()) << "loaded level should contain entities";

    for (int i = 0; i < 3; ++i)
        world.tick();

    const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(world);

    EXPECT_EQ(world.tick_count_, snapshot.tick_count);
    EXPECT_EQ(world.rng_.state_, snapshot.rng_state);
    EXPECT_EQ(world.level_tick_count(), snapshot.level_tick_count);
    EXPECT_EQ(world.level_done, snapshot.level_done);
    EXPECT_EQ(world.game_ended, snapshot.game_ended);
    EXPECT_EQ(world.next_level, snapshot.next_level);
    EXPECT_EQ(world.ending, snapshot.ending);
    EXPECT_EQ(world.living_count, snapshot.living_count);
    EXPECT_EQ(world.control_hp, snapshot.control_hp);
    EXPECT_EQ(world.oblist.size(), snapshot.oblist.size());
    EXPECT_EQ(world.fxlist.size(), snapshot.fxlist.size());
    EXPECT_EQ(world.weaplist.size(), snapshot.weaplist.size());

    const walker* const first_entity = world.oblist.front().get();
    ASSERT_NE(nullptr, first_entity);
    const og::sim::EntitySnapshot* first_snapshot =
        find_entity_snapshot(snapshot.oblist, first_entity->entity_id());
    ASSERT_NE(nullptr, first_snapshot);
    EXPECT_EQ(first_entity->entity_id(), first_snapshot->entity_id);
    EXPECT_EQ(first_entity->xpos(), first_snapshot->xpos);
    EXPECT_EQ(first_entity->ypos(), first_snapshot->ypos);
    EXPECT_EQ(first_entity->order(), first_snapshot->order);
    EXPECT_EQ(first_entity->family(), first_snapshot->family);
    ASSERT_NE(nullptr, first_entity->stats());
    EXPECT_EQ(first_entity->stats()->hitpoints(), first_snapshot->hitpoints);
    EXPECT_EQ(first_entity->stats()->level(), first_snapshot->level);

    if (first_entity->myguy != nullptr)
    {
        const auto guy_it = std::find_if(
            snapshot.guy_snapshots.begin(), snapshot.guy_snapshots.end(),
            [first_entity](const og::sim::GuySnapshot& guy_snapshot) {
                return guy_snapshot.guy_id == first_entity->myguy->id;
            });
        ASSERT_NE(snapshot.guy_snapshots.end(), guy_it);
        EXPECT_EQ(first_entity->myguy->name, guy_it->name);
    }

    world.delete_objects();
}


// Regression: saved multiplayer teams must map to views by saved team ids,
// not by view index.
TEST(LoadLevels, load_saved_game_maps_views_to_saved_team_ids) {
    ASSERT_TRUE(prepare_default_level_load()) << "default campaign should be restored and mounted before team mapping test";
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 2;
    og::runtime::current_session->myscreen_->save_data.allied_mode = 0;

    auto team1 = std::make_unique<guy>(FAMILY_SOLDIER);
    team1->name = "TEAM1";
    team1->teamnum = 1;
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(team1);

    auto team3 = std::make_unique<guy>(FAMILY_ARCHER);
    team3->name = "TEAM3";
    team3->teamnum = 3;
    og::runtime::current_session->myscreen_->save_data.team_list[1] = std::move(team3);
    og::runtime::current_session->myscreen_->save_data.team_size = 2;

    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.save("test_level_team_mapping")) << "save should succeed for team mapping regression";
    ASSERT_TRUE(load_saved_game("test_level_team_mapping", og::runtime::current_session->myscreen_) != 0) << "load_saved_game should succeed for team mapping regression";

    ASSERT_TRUE(og::runtime::current_session->myscreen_->viewob[0] != nullptr) << "view 0 should exist";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->viewob[1] != nullptr) << "view 1 should exist";
    if (!og::runtime::current_session->myscreen_->viewob[0] || !og::runtime::current_session->myscreen_->viewob[1]) {
        og::runtime::current_session->myscreen_->world().delete_objects();
        return;
    }

    ASSERT_EQ(1, (int)og::runtime::current_session->myscreen_->viewob[0]->my_team) << "view 0 should map to first distinct saved team id";
    ASSERT_EQ(3, (int)og::runtime::current_session->myscreen_->viewob[1]->my_team) << "view 1 should map to second distinct saved team id";

    og::runtime::current_session->myscreen_->world().delete_objects();
}
