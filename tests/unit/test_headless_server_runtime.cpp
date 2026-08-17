#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/weather.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/game_context.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/headless_server_runtime.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "test_gameplay_context_scope.h"

namespace {

og::sim::LobbyCharacterSlot make_slot(std::uint8_t slot_index,
                                      std::int32_t guy_id,
                                      const char* name,
                                      std::int8_t family,
                                      std::int16_t team)
{
    og::sim::LobbyCharacterData character;
    character.guy_id = guy_id;
    character.name = name;
    character.family = family;
    character.strength = 10;
    character.dexterity = 11;
    character.constitution = 12;
    character.intelligence = 13;
    character.armor = 14;
    character.level = 3;
    character.teamnum = team;

    return {
        .slot_index = slot_index,
        .character = character,
    };
}

og::sim::LobbyCharacterSlot make_owned_slot(
    std::uint8_t combined_slot_index,
    std::int32_t guy_id,
    const char* name,
    std::int8_t family,
    std::int16_t team,
    std::uint8_t owner_player_index,
    std::uint8_t owner_save_slot)
{
    og::sim::LobbyCharacterSlot slot = make_slot(
        combined_slot_index, guy_id, name, family, team);
    slot.owner_player_index = owner_player_index;
    slot.owner_save_slot = owner_save_slot;
    return slot;
}

walker* find_team_member(GameWorld& world, std::int32_t guy_id)
{
    for (const auto& entry : world.oblist)
    {
        walker* const entity = entry.get();
        if (entity == nullptr || entity->myguy == nullptr)
            continue;
        if (entity->myguy->id == guy_id)
            return entity;
    }
    return nullptr;
}

std::vector<short> start_marker_teams_at(const GameWorld& world,
                                         const walker& hero)
{
    std::vector<short> teams;
    for (const auto& entry : world.oblist)
    {
        const walker* const marker = entry.get();
        if (marker == nullptr || marker->query_order() != Order::Special ||
            marker->family() != FAMILY_RESERVED_TEAM)
        {
            continue;
        }
        if (marker->floor() == hero.floor() &&
            marker->xpos() == hero.xpos() && marker->ypos() == hero.ypos())
        {
            teams.push_back(marker->team_num());
        }
    }
    return teams;
}

// Registers a throwaway scripted campaign picker for one test and restores
// the pack-script registry (and the gameplay context campaign dispatch
// resolves) afterwards — the test_campaign_picker_session fixture approach.
// The chunk name deliberately does NOT start with `packs/`: that prefix
// declares the bytes to the pack-Lua coverage inventory, and this throwaway
// chunk exists nowhere in the repository.
class ScopedSyntheticCampaignPicker
{
public:
    explicit ScopedSyntheticCampaignPicker(const std::string& source)
        : previous_game_(current_game)
        , scripts_(og::script::pack_scripts())
    {
        current_game = nullptr;  // dispatch resolves the shared UI VM
        og::script::register_pack_script(
            {"test.serversync", "serversync/scripts/c.lua", source});
    }

    ~ScopedSyntheticCampaignPicker()
    {
        og::script::clear_pack_scripts();
        for (const og::script::PackScript& script : scripts_)
            og::script::register_pack_script(script);
        current_game = previous_game_;
    }

private:
    GameplayContext* previous_game_;
    std::vector<og::script::PackScript> scripts_;
};

class HeadlessServerRuntimeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        restore_default_settings();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("gladiator"));
        og::runtime::current_session->current_difficulty_ = 1;
    }

    void TearDown() override
    {
        level_data_.reset();
    }

    template <typename Fn>
    auto with_context(Fn&& fn) -> std::invoke_result_t<Fn&>
    {
        if (level_data_ == nullptr)
            throw std::logic_error("level runtime data is not initialized");
        ScopedGameplayContext gameplay(*level_data_, active_save_, events_, cfg);
        GameContext gc;
        gc.rng = &rng_;
        push_test_context(&gc);
        struct PopGuard {
            ~PopGuard() { pop_test_context(); }
        } pop_guard;
        using Result = std::invoke_result_t<Fn&>;
        if constexpr (std::is_void_v<Result>)
        {
            std::forward<Fn>(fn)();
        }
        else
        {
            return std::forward<Fn>(fn)();
        }
    }

    void create_level_runtime_data(short level_id)
    {
        level_data_ = std::make_unique<LevelRuntimeData>(
            level_id,
            true,
            &headless_level_data_hooks());
        level_data_->set_sim_context(
            &active_save_,
            &level_data_->world().enemy_freeze,
            &events_,
            &rng_,
            &cfg);
    }

    void initialize_from_lobby(const og::sim::LobbySaveDataEquivalent& config_save,
                               int difficulty_setting = 1,
                               bool authoritative = true)
    {
        og::server::apply_headless_lobby_game_start_config(active_save_, config_save);
        og::server::copy_headless_server_save_data(checkpoint_save_, active_save_);
        create_level_runtime_data(active_save_.scen_num);
        ASSERT_TRUE(with_context([&] {
            return og::server::load_headless_level_from_save(
                *level_data_,
                active_save_,
                difficulty_setting,
                events_,
                authoritative);
        }));
    }

    SaveData active_save_;
    SaveData checkpoint_save_;
    og::sim::SimEventLog events_;
    FixedRandom rng_{0};
    std::unique_ptr<LevelRuntimeData> level_data_;
};

TEST_F(HeadlessServerRuntimeTest,
       lobby_save_handoff_loads_level_and_spawns_headless_team)
{
    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "gladiator";
    lobby_save.scen_num = 1;
    lobby_save.numplayers = 2;
    lobby_save.allied_mode = 0;
    lobby_save.team_list = {
        make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER, 0),
        make_slot(3u, 200, "Guest Guy", FAMILY_ARCHER, 1),
    };

    initialize_from_lobby(lobby_save);

    EXPECT_EQ("gladiator", active_save_.current_campaign);
    EXPECT_EQ(1, active_save_.scen_num);
    EXPECT_EQ(1, active_save_.current_levels[active_save_.current_campaign]);
    EXPECT_EQ(2u, active_save_.numplayers);
    EXPECT_EQ(0, active_save_.allied_mode);
    EXPECT_EQ(2, active_save_.team_size);
    EXPECT_EQ(1, level_data_->world().id);
    EXPECT_EQ(1, level_data_->world().current_scenario);
    EXPECT_TRUE(events_.empty());

    walker* const host = find_team_member(level_data_->world(), 100);
    walker* const guest = find_team_member(level_data_->world(), 200);
    ASSERT_NE(nullptr, host);
    ASSERT_NE(nullptr, guest);
    EXPECT_EQ("Host Guy", host->myguy->name);
    EXPECT_EQ("Guest Guy", guest->myguy->name);
    EXPECT_EQ(0, host->team_num());
    EXPECT_EQ(1, guest->team_num());
    EXPECT_FALSE(host->has_render());
    EXPECT_FALSE(guest->has_render());

    const std::vector<short> host_marker_teams =
        start_marker_teams_at(level_data_->world(), *host);
    ASSERT_FALSE(host_marker_teams.empty());
    for (const short marker_team : host_marker_teams)
        EXPECT_EQ(0, marker_team);

    const std::vector<short> guest_marker_teams =
        start_marker_teams_at(level_data_->world(), *guest);
    for (const short marker_team : guest_marker_teams)
        EXPECT_EQ(1, marker_team)
            << "the authoritative guest must not borrow a red start marker";
}

TEST_F(HeadlessServerRuntimeTest,
       invalid_saved_level_falls_back_to_first_shipped_level)
{
    constexpr short kInvalidLevel = 9999;
    constexpr short kFirstGladiatorLevel = 1;
    const std::string campaign = "gladiator";

    active_save_.current_campaign = campaign;
    active_save_.scen_num = kInvalidLevel;
    active_save_.current_levels[campaign] = kFirstGladiatorLevel;
    create_level_runtime_data(kInvalidLevel);

    testing::internal::CaptureStderr();
    const bool loaded = with_context([&] {
        return og::server::load_headless_level_from_save(
            *level_data_, active_save_, /*difficulty_setting=*/1, events_,
            /*authoritative=*/true);
    });
    const std::string diagnostics = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(loaded) << diagnostics;
    EXPECT_EQ(kFirstGladiatorLevel, active_save_.scen_num);
    EXPECT_EQ(kFirstGladiatorLevel, active_save_.current_levels[campaign]);
    EXPECT_EQ(kFirstGladiatorLevel, level_data_->world().id);
    EXPECT_EQ(kFirstGladiatorLevel, level_data_->world().current_scenario);
    EXPECT_FALSE(level_data_->world().oblist.empty())
        << "the fallback must load the real first shipped level";

    EXPECT_NE(std::string::npos,
              diagnostics.find(
                  "headless_server_level_mismatch "
                  "campaign=gladiator save_level=9999 "
                  "current_level=1"));
    EXPECT_NE(std::string::npos,
              diagnostics.find(
                  "headless_server_level_load_failed level=9999 "
                  "action=fallback_to_1"));
    EXPECT_EQ(std::string::npos,
              diagnostics.find("headless_server_level_fallback_failed"));
}

TEST_F(HeadlessServerRuntimeTest,
       production_callback_installer_syncs_completes_exits_and_withdraws)
{
    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "gladiator";
    lobby_save.scen_num = 1;
    lobby_save.numplayers = 1;
    lobby_save.team_list = {
        make_slot(0u, 100, "Lead", FAMILY_SOLDIER, 0),
    };
    initialize_from_lobby(lobby_save);

    std::shared_ptr<og::sim::InProcessTransport> transport =
        og::sim::InProcessTransport::create_server();
    og::sim::GameServer game_server(
        level_data_->world(), events_, *transport);
    og::server::install_headless_server_callbacks(
        game_server,
        *level_data_,
        active_save_,
        checkpoint_save_,
        /*difficulty_setting=*/1,
        events_);

    ASSERT_TRUE(static_cast<bool>(game_server.on_save_sync));
    ASSERT_TRUE(static_cast<bool>(game_server.on_level_transition));
    ASSERT_TRUE(static_cast<bool>(game_server.on_exit_accepted));
    ASSERT_TRUE(static_cast<bool>(game_server.on_withdraw_accepted));

    level_data_->world().m_score[0] = 41u;
    game_server.on_save_sync();
    EXPECT_EQ(41u, active_save_.m_score[0]);

    level_data_->world().m_score[0] = 7u;
    ASSERT_TRUE(with_context([&] {
        return game_server.on_level_transition(2);
    }));
    EXPECT_EQ(2, active_save_.scen_num);
    EXPECT_EQ(2, checkpoint_save_.scen_num);
    EXPECT_EQ(2, level_data_->world().current_scenario);
    EXPECT_TRUE(active_save_.is_level_completed(1));

    level_data_->world().m_score[0] = 5u;
    ASSERT_TRUE(with_context([&] {
        return game_server.on_exit_accepted(3);
    }));
    EXPECT_EQ(3, active_save_.scen_num);
    EXPECT_EQ(3, checkpoint_save_.scen_num);
    EXPECT_EQ(3, level_data_->world().current_scenario);
    EXPECT_TRUE(active_save_.is_level_completed(2));

    const std::uint32_t checkpoint_cash = checkpoint_save_.m_totalcash[0];
    active_save_.m_totalcash[0] = checkpoint_cash + 999u;
    level_data_->world().m_score[0] = 123u;
    ASSERT_TRUE(with_context([&] {
        return game_server.on_withdraw_accepted(2);
    }));
    EXPECT_EQ(2, active_save_.scen_num);
    EXPECT_EQ(2, checkpoint_save_.scen_num);
    EXPECT_EQ(2, level_data_->world().current_scenario);
    EXPECT_EQ(checkpoint_cash, active_save_.m_totalcash[0]);
    EXPECT_EQ(checkpoint_cash, checkpoint_save_.m_totalcash[0]);
    EXPECT_EQ(0u, active_save_.m_score[0]);
}

// §4.2 LOCAL assembly filter: a benched (deployed == false) save member is
// SKIPPED at spawn time but stays in the save's roster untouched. This is
// the local-session half of the deploy rule — networked rosters arrive
// pre-filtered by the lobby equivalent, local saves keep benched members on
// disk and rely on this spawn guard to keep them out of the level.
TEST_F(HeadlessServerRuntimeTest, spawn_skips_benched_members_but_save_keeps_them)
{
    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "gladiator";
    lobby_save.scen_num = 1;
    lobby_save.numplayers = 1;
    lobby_save.allied_mode = 0;
    lobby_save.team_list = {
        make_slot(0u, 100, "Front", FAMILY_SOLDIER, 0),
        make_slot(1u, 200, "Reserve", FAMILY_ARCHER, 0),
    };

    og::server::apply_headless_lobby_game_start_config(active_save_, lobby_save);
    // Bench the second member directly in the save — the LOCAL shape (the
    // local equivalent deliberately keeps benched members so the company
    // seed never loses them; spawn is where they are filtered).
    ASSERT_NE(nullptr, active_save_.team_list[1]);
    active_save_.team_list[1]->deployed = false;
    og::server::copy_headless_server_save_data(checkpoint_save_, active_save_);
    create_level_runtime_data(active_save_.scen_num);
    ASSERT_TRUE(with_context([&] {
        return og::server::load_headless_level_from_save(
            *level_data_,
            active_save_,
            /*difficulty_setting=*/1,
            events_,
            /*authoritative=*/true);
    }));

    EXPECT_NE(nullptr, find_team_member(level_data_->world(), 100))
        << "the deployed member spawns";
    EXPECT_EQ(nullptr, find_team_member(level_data_->world(), 200))
        << "the benched member must never enter the level";

    // The save's roster is untouched: both members present, flag intact.
    ASSERT_NE(nullptr, active_save_.team_list[0]);
    ASSERT_NE(nullptr, active_save_.team_list[1]);
    EXPECT_TRUE(active_save_.team_list[0]->deployed);
    EXPECT_FALSE(active_save_.team_list[1]->deployed);
    EXPECT_EQ("Reserve", active_save_.team_list[1]->name);
}

TEST_F(HeadlessServerRuntimeTest,
       lobby_start_preserves_distinct_owners_with_same_private_save_slot)
{
    constexpr std::uint8_t host_owner = 2u;
    constexpr std::uint8_t guest_owner = 9u;
    constexpr std::uint8_t private_slot = 0u;

    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "gladiator";
    lobby_save.scen_num = 1;
    lobby_save.numplayers = 2;
    lobby_save.allied_mode = 0;
    lobby_save.team_list = {
        make_owned_slot(0u, 101, "Host Private Zero", FAMILY_SOLDIER, 0,
                        host_owner, private_slot),
        make_owned_slot(1u, 202, "Guest Private Zero", FAMILY_ARCHER, 1,
                        guest_owner, private_slot),
    };

    initialize_from_lobby(lobby_save);

    ASSERT_NE(nullptr, active_save_.team_list[0]);
    ASSERT_NE(nullptr, active_save_.team_list[1]);
    EXPECT_EQ(host_owner,
              active_save_.team_list[0]->owner_player_index);
    EXPECT_EQ(private_slot,
              active_save_.team_list[0]->owner_save_slot);
    EXPECT_EQ(guest_owner,
              active_save_.team_list[1]->owner_player_index);
    EXPECT_EQ(private_slot,
              active_save_.team_list[1]->owner_save_slot);

    walker* const host = find_team_member(level_data_->world(), 101);
    walker* const guest = find_team_member(level_data_->world(), 202);
    ASSERT_NE(nullptr, host);
    ASSERT_NE(nullptr, guest);
    ASSERT_NE(nullptr, host->myguy);
    ASSERT_NE(nullptr, guest->myguy);
    EXPECT_EQ(host_owner, host->myguy->owner_player_index);
    EXPECT_EQ(private_slot, host->myguy->owner_save_slot);
    EXPECT_EQ(guest_owner, guest->myguy->owner_player_index);
    EXPECT_EQ(private_slot, guest->myguy->owner_save_slot);

    const og::sim::WorldSnapshot snapshot = with_context([&] {
        return og::sim::capture_keyframe_snapshot(level_data_->world());
    });
    const auto find_snapshot_guy =
        [&snapshot](std::int32_t guy_id) -> const og::sim::GuySnapshot* {
            for (const og::sim::GuySnapshot& candidate :
                 snapshot.guy_snapshots)
            {
                if (candidate.guy_id == guy_id)
                    return &candidate;
            }
            return nullptr;
        };

    const og::sim::GuySnapshot* const host_snapshot = find_snapshot_guy(101);
    const og::sim::GuySnapshot* const guest_snapshot = find_snapshot_guy(202);
    ASSERT_NE(nullptr, host_snapshot);
    ASSERT_NE(nullptr, guest_snapshot);
    EXPECT_EQ(host_owner, host_snapshot->owner_player_index);
    EXPECT_EQ(private_slot, host_snapshot->owner_save_slot);
    EXPECT_EQ(guest_owner, guest_snapshot->owner_player_index);
    EXPECT_EQ(private_slot, guest_snapshot->owner_save_slot);
}

TEST_F(HeadlessServerRuntimeTest,
       lobby_start_config_defaults_campaign_level_and_ignores_bad_slots)
{
    active_save_.m_score[0] = 12u;
    active_save_.m_totalcash[0] = 34u;
    active_save_.m_totalscore[0] = 56u;

    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "";
    lobby_save.scen_num = -4;
    lobby_save.numplayers = 3;
    lobby_save.allied_mode = 1;
    lobby_save.team_list = {
        make_slot(0u, 101, "Valid", FAMILY_SOLDIER, 0),
        make_slot(static_cast<std::uint8_t>(active_save_.team_list.size()),
                  202,
                  "Out Of Range",
                  FAMILY_ELF,
                  1),
    };

    og::server::apply_headless_lobby_game_start_config(active_save_, lobby_save);

    EXPECT_EQ("gladiator", active_save_.current_campaign);
    EXPECT_EQ(1, active_save_.scen_num);
    EXPECT_EQ(1, active_save_.current_levels[active_save_.current_campaign]);
    EXPECT_EQ(3u, active_save_.numplayers);
    EXPECT_EQ(1, active_save_.allied_mode);
    EXPECT_EQ(1, active_save_.team_size);
    ASSERT_NE(nullptr, active_save_.team_list[0]);
    EXPECT_EQ("Valid", active_save_.team_list[0]->name);
    EXPECT_EQ(nullptr, active_save_.team_list[1]);
    EXPECT_EQ(12u, active_save_.score);
    EXPECT_EQ(34u, active_save_.totalcash);
    EXPECT_EQ(56u, active_save_.totalscore);
}

TEST_F(HeadlessServerRuntimeTest,
       complete_level_updates_save_and_loads_next_level_for_exit_flow)
{
    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.team_list = {
        make_slot(0u, 100, "Lead", FAMILY_SOLDIER, 0),
    };
    lobby_save.numplayers = 1;
    initialize_from_lobby(lobby_save);

    const std::uint32_t initial_total_cash = active_save_.m_totalcash[0];
    ASSERT_EQ(initial_total_cash, checkpoint_save_.m_totalcash[0]);

    level_data_->world().m_score[0] = 7;
    level_data_->world().time_bonus_limit = 0;
    ASSERT_TRUE(with_context([&] {
        return og::server::complete_headless_level_and_load_next(
            *level_data_,
            active_save_,
            checkpoint_save_,
            1,
            events_,
            2);
    }));

    const std::uint32_t expected_total_cash = initial_total_cash + 14u;
    EXPECT_EQ(2, active_save_.scen_num);
    EXPECT_EQ(2, checkpoint_save_.scen_num);
    EXPECT_TRUE(active_save_.is_level_completed(1));
    EXPECT_TRUE(checkpoint_save_.is_level_completed(1));
    EXPECT_EQ(0u, active_save_.m_score[0]);
    EXPECT_EQ(7u, active_save_.m_totalscore[0]);
    EXPECT_EQ(expected_total_cash, active_save_.m_totalcash[0]);
    EXPECT_EQ(expected_total_cash, checkpoint_save_.m_totalcash[0]);
    EXPECT_EQ(expected_total_cash, active_save_.totalcash);
    EXPECT_EQ(expected_total_cash, checkpoint_save_.totalcash);
    EXPECT_EQ(2, level_data_->world().id);
    EXPECT_EQ(2, level_data_->world().current_scenario);
    EXPECT_EQ(0u, level_data_->world().tick_count_);
    EXPECT_TRUE(events_.empty());

    walker* const lead = find_team_member(level_data_->world(), 100);
    ASSERT_NE(nullptr, lead);
    EXPECT_FALSE(lead->has_render());
}

TEST_F(HeadlessServerRuntimeTest,
       withdraw_restores_checkpoint_state_before_loading_destination_level)
{
    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.team_list = {
        make_slot(0u, 100, "Lead", FAMILY_SOLDIER, 0),
    };
    lobby_save.numplayers = 1;
    initialize_from_lobby(lobby_save);

    level_data_->world().m_score[0] = 5;
    ASSERT_TRUE(with_context([&] {
        return og::server::complete_headless_level_and_load_next(
            *level_data_,
            active_save_,
            checkpoint_save_,
            1,
            events_,
            2);
    }));

    const std::string checkpoint_name = checkpoint_save_.team_list[0]->name;
    const std::uint32_t checkpoint_total_score = checkpoint_save_.m_totalscore[0];
    const std::uint32_t checkpoint_total_cash = checkpoint_save_.m_totalcash[0];

    active_save_.team_list[0]->name = "Corrupted";
    active_save_.m_totalscore[0] = 999u;
    active_save_.m_totalcash[0] = 888u;
    active_save_.m_score[0] = 777u;
    level_data_->world().m_score[0] = 333u;

    ASSERT_TRUE(with_context([&] {
        return og::server::withdraw_headless_level(
            *level_data_,
            active_save_,
            checkpoint_save_,
            1,
            events_,
            1);
    }));

    EXPECT_EQ(1, active_save_.scen_num);
    EXPECT_EQ(1, checkpoint_save_.scen_num);
    EXPECT_TRUE(active_save_.is_level_completed(1));
    ASSERT_NE(nullptr, active_save_.team_list[0]);
    EXPECT_EQ(checkpoint_name, active_save_.team_list[0]->name);
    EXPECT_EQ(checkpoint_total_score, active_save_.m_totalscore[0]);
    EXPECT_EQ(checkpoint_total_cash, active_save_.m_totalcash[0]);
    EXPECT_EQ(0u, active_save_.m_score[0]);
    EXPECT_EQ(1, level_data_->world().id);
    EXPECT_EQ(1, level_data_->world().current_scenario);
    EXPECT_TRUE(events_.empty());

    walker* const lead = find_team_member(level_data_->world(), 100);
    ASSERT_NE(nullptr, lead);
    EXPECT_FALSE(lead->has_render());
}

// The authoritative level-load path rolls the per-level weather; under the
// default-0 nonce the roll is a pure function of (level id, roll sequence):
// each load consumes the next sequence value, so retries roll fresh weather,
// and resetting the sequence reproduces a roll exactly. Level id 2 pins
// Clouds at sequence 0 and None at sequence 1 (tests/unit/test_weather.cpp).
TEST_F(HeadlessServerRuntimeTest,
       authoritative_level_load_rolls_weather_deterministically)
{
    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "gladiator";
    lobby_save.scen_num = 2;
    lobby_save.numplayers = 1;
    lobby_save.team_list = {
        make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER, 0),
    };

    og::set_weather_roll_sequence(0u);
    initialize_from_lobby(lobby_save);
    EXPECT_EQ(WeatherKind::Clouds, level_data_->world().weather())
        << "level id 2 must roll Clouds under nonce 0, sequence 0";

    // A reload RE-ROLLS with the next sequence value — retrying a level is
    // a fresh roll, not a repeat (level id 2 at sequence 1 pins None).
    level_data_->world().set_weather(WeatherKind::Rain);
    ASSERT_TRUE(with_context([&] {
        return og::server::load_headless_level_from_save(
            *level_data_,
            active_save_,
            1,
            events_,
            /*authoritative=*/true);
    }));
    EXPECT_EQ(WeatherKind::None, level_data_->world().weather())
        << "the reload must consume the next sequence (fresh roll)";

    // Resetting the sequence reproduces the original roll exactly.
    og::set_weather_roll_sequence(0u);
    ASSERT_TRUE(with_context([&] {
        return og::server::load_headless_level_from_save(
            *level_data_,
            active_save_,
            1,
            events_,
            /*authoritative=*/true);
    }));
    EXPECT_EQ(WeatherKind::Clouds, level_data_->world().weather())
        << "same (id, nonce, sequence) must reproduce the same kind";
}

// Client mirror worlds reuse the same loader but must NOT roll: the kind
// arrives via the world snapshot instead. The load itself still resets any
// stale kind back to None.
TEST_F(HeadlessServerRuntimeTest, mirror_level_load_never_rolls_weather)
{
    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "gladiator";
    lobby_save.scen_num = 2;
    lobby_save.numplayers = 1;
    lobby_save.team_list = {
        make_slot(0u, 100, "Guest Guy", FAMILY_SOLDIER, 0),
    };

    initialize_from_lobby(lobby_save, 1, /*authoritative=*/false);
    EXPECT_EQ(WeatherKind::None, level_data_->world().weather())
        << "mirror loads must leave the weather at the load-reset None";

    // Even a previously synced kind is wiped by the next mirror-side load.
    level_data_->world().set_weather(WeatherKind::Rain);
    ASSERT_TRUE(with_context([&] {
        return og::server::load_headless_level_from_save(
            *level_data_,
            active_save_,
            1,
            events_,
            /*authoritative=*/false);
    }));
    EXPECT_EQ(WeatherKind::None, level_data_->world().weather())
        << "level load must reset the kind; only snapshots set it on mirrors";
}

// Matched-teams I4 ordering (docs/matched-teams-design.md §3.2, amended
// D25/D27): the TROOPS: FAIR sentinel rides the existing SaveData -> world
// chain and is in `world.ctf_requested_strip_scenario_troops` strictly
// BEFORE the first world.tick() runs on_mode_init, with the lobby roster
// already in the oblist — so the mode census can see both. The mode var
// written by on_mode_init (slot 2, mode_match.MATCHED.TARGET) is the proof
// Lua saw the sentinel and censused the roster.
TEST_F(HeadlessServerRuntimeTest,
       matched_sentinel_reaches_world_before_first_tick_and_lua_sees_it)
{
    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "modes";
    lobby_save.scen_num = 302; // shipped TDM level
    lobby_save.numplayers = 2;
    lobby_save.allied_mode = 0;
    lobby_save.ctf_strip_scenario_troops = og::sim::kTroopsMatched;
    lobby_save.team_list = {
        make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER, 0),
        make_slot(3u, 200, "Guest Guy", FAMILY_ARCHER, 1),
    };

    initialize_from_lobby(lobby_save);

    EXPECT_EQ(og::sim::kTroopsMatched,
              active_save_.ctf_strip_scenario_troops)
        << "the start config carries the sentinel into the SaveData";
    GameWorld& world = level_data_->world();
    EXPECT_EQ(og::sim::kTroopsMatched,
              world.ctf_requested_strip_scenario_troops)
        << "sync_world_from_save_data ran before the first tick (I4)";
    EXPECT_EQ(0u, world.tick_count_);
    EXPECT_FALSE(world.mode.active) << "on_mode_init has not run yet";
    EXPECT_EQ(0, world.mode.vars[2]);
    ASSERT_NE(nullptr, find_team_member(world, 100))
        << "the roster is in the oblist before init — censusable (D15)";
    ASSERT_NE(nullptr, find_team_member(world, 200));

    with_context([&] { world.tick(); });

    EXPECT_TRUE(world.mode.active) << "the first tick ran on_mode_init";
    EXPECT_NE(0, world.mode.vars[0]) << "the mode id var is written";
    EXPECT_GT(world.mode.vars[2], 0)
        << "on_mode_init stored the matched census target — Lua saw the "
           "sentinel through og.match_setting before any other tick ran";
}

// The unmatched twin: the identical lobby handoff without the sentinel
// leaves the matched census idle — the target var stays 0 and the flow is
// byte-identical to today's behavior.
TEST_F(HeadlessServerRuntimeTest,
       auto_team_count_lobby_handoff_writes_no_matched_target)
{
    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "modes";
    lobby_save.scen_num = 302;
    lobby_save.numplayers = 2;
    lobby_save.allied_mode = 0;
    lobby_save.ctf_strip_scenario_troops = 0;
    lobby_save.team_list = {
        make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER, 0),
        make_slot(3u, 200, "Guest Guy", FAMILY_ARCHER, 1),
    };

    initialize_from_lobby(lobby_save);

    GameWorld& world = level_data_->world();
    EXPECT_EQ(0, world.ctf_requested_team_count);

    with_context([&] { world.tick(); });

    EXPECT_TRUE(world.mode.active);
    EXPECT_NE(0, world.mode.vars[0]);
    EXPECT_EQ(0, world.mode.vars[2])
        << "no matched request -> the census never stores a target (E1 "
           "posture: byte-identical to Auto)";
}

// Campaign scripting (issue #206) authoritative sync, server twin: the
// og::server sync inside load_headless_level_from_save must REPLACE
// world.campaign_vars with the current campaign's decision book filtered
// to og.register_campaign_hooks' vars list — never merge, never carry an
// unregistered or stale name (docs/campaign-scripting-design.md,
// "campaign_vars replace-not-merge sync"). The screen twin is pinned by
// ScreenExtended.sync_world_from_save_data_replaces_campaign_vars.
TEST_F(HeadlessServerRuntimeTest,
       authoritative_sync_replaces_campaign_vars_with_registered_names_only)
{
    ScopedSyntheticCampaignPicker picker(R"LUA(og.register_campaign_hooks({
  vars = { "watch_paid", "delve_counted" },
  picker_menu = function(page_id)
    return { title = "BOOK" }
  end,
}))LUA");

    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "gladiator";
    lobby_save.scen_num = 1;
    lobby_save.numplayers = 1;
    lobby_save.team_list = {
        make_slot(0u, 100, "Lead", FAMILY_SOLDIER, 0),
    };
    og::server::apply_headless_lobby_game_start_config(active_save_, lobby_save);

    // The decision book: two registered names, one unregistered name, and
    // one entry filed under a DIFFERENT campaign.
    ASSERT_TRUE(active_save_.campaign_state_set("gladiator", "watch_paid", 3));
    ASSERT_TRUE(
        active_save_.campaign_state_set("gladiator", "delve_counted", -2));
    ASSERT_TRUE(
        active_save_.campaign_state_set("gladiator", "not_registered", 9));
    ASSERT_TRUE(active_save_.campaign_state_set("othercamp", "watch_paid", 7));

    create_level_runtime_data(active_save_.scen_num);
    // Pre-pollute the world: a stale foreign var AND a stale value for a
    // registered name — both must be wiped by the clear-then-copy.
    level_data_->world().campaign_vars.emplace_back("stale_var", 5);
    level_data_->world().campaign_vars.emplace_back("watch_paid", 99);

    ASSERT_TRUE(with_context([&] {
        return og::server::load_headless_level_from_save(
            *level_data_,
            active_save_,
            /*difficulty_setting=*/1,
            events_,
            /*authoritative=*/true);
    }));

    // Exactly the registered current-campaign entries, in the book's sorted
    // order: the stale entries are gone (clear), the unregistered and
    // foreign-campaign names never crossed (filter), and the registered
    // value is the save's, not the polluted one (replace, not merge).
    const auto& vars = level_data_->world().campaign_vars;
    ASSERT_EQ(2u, vars.size());
    EXPECT_EQ("delve_counted", vars[0].first);
    EXPECT_EQ(-2, vars[0].second);
    EXPECT_EQ("watch_paid", vars[1].first);
    EXPECT_EQ(3, vars[1].second);
}

// §3.4 dropped-field bug class: the server/checkpoint copies must carry the
// GTL v14 company fields — the last-played timestamp explicitly, the per-guy
// deploy flag via the guy deep copies. (The tower fields were the previous
// instance of this bug class; the copy is an explicit field list, so every
// new SaveData member needs a line there AND a pin here.)
TEST(HeadlessServerSaveCopy, copy_carries_v14_company_fields)
{
    SaveData source;
    source.last_played_unix_s = 0x0A0B0C0D0E0F1011LL;
    source.cross_control = 1; // session-only v8 setting must ride the copy
    source.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    source.team_list[0]->name = "BROUGHT";
    source.team_list[0]->deployed = true;
    source.team_list[1] = std::make_unique<guy>(FAMILY_ELF);
    source.team_list[1]->name = "HELDBACK";
    source.team_list[1]->deployed = false;
    source.team_size = 2;
    // Campaign scripting decision book (GTL v15): same dropped-field rule —
    // a copy that lost it would read og.campaign_var as 0 on the
    // authoritative sim (curses/dedicated/checkpoint arms).
    ASSERT_TRUE(source.campaign_state_set("gladiator", "watch_paid", 1));

    SaveData destination;
    destination.last_played_unix_s = 42; // must be overwritten, not merged
    destination.cross_control = 0;
    ASSERT_TRUE(destination.campaign_state_set("oldcamp", "stale_key", 9));

    og::server::copy_headless_server_save_data(destination, source);

    EXPECT_EQ(0x0A0B0C0D0E0F1011LL, destination.last_played_unix_s)
        << "timestamp must survive the server/checkpoint copy";
    EXPECT_EQ(1, (int)destination.cross_control)
        << "cross_control must survive the server/checkpoint copy";
    ASSERT_TRUE(destination.team_list[0] != nullptr);
    ASSERT_TRUE(destination.team_list[1] != nullptr);
    EXPECT_NE(destination.team_list[0].get(), source.team_list[0].get())
        << "guys are deep copies, not shared pointers";
    EXPECT_TRUE(destination.team_list[0]->deployed);
    EXPECT_FALSE(destination.team_list[1]->deployed)
        << "the deploy flag rides the guy copy";
    EXPECT_TRUE(destination.team_list[1]->name == "HELDBACK");
    EXPECT_EQ(2, (int)destination.team_size);
    EXPECT_EQ(source.campaign_state, destination.campaign_state)
        << "the decision book must ride the copy, replaced not merged";
    EXPECT_EQ(1, destination.campaign_state_get("gladiator", "watch_paid"));
    EXPECT_EQ(0u, destination.campaign_state.count("oldcamp"))
        << "the copy replaces the destination book, it never merges";
}

// ---------------------------------------------------------------------------
// #207 replay excursion: the completed-level purge and its replay-arm skip.
// Loading a level the save marks completed kills everything except team-0
// livings, exits and teleporters (game.cpp's 2002 "Have we already done this
// scenario?" rule; the headless twin is apply_completed_level_cleanup). The
// replay arm restores the authored census for exactly the armed level.
// ---------------------------------------------------------------------------

struct LiveCensus
{
    int hostile_livings = 0;  // Order::Living, team != 0
    int team0_livings = 0;    // Order::Living, team 0 or carrying a guy
    int generators = 0;
    int exits = 0;
    int teleporters = 0;
    int other_treasure = 0;
    int weapons = 0;  // doors, boulders
    int others = 0;

    bool operator==(const LiveCensus&) const = default;

    int total() const
    {
        return hostile_livings + team0_livings + generators + exits +
            teleporters + other_treasure + weapons + others;
    }
};

template <typename EntityList>
void count_live_entities(const EntityList& entities, LiveCensus& census)
{
    for (const auto& entry : entities)
    {
        const walker* const entity = entry.get();
        if (entity == nullptr || entity->dead())
            continue;
        const Order order = entity->query_order();
        const int family = entity->family();
        if (order == Order::Living)
        {
            if (entity->team_num() == 0 || entity->myguy != nullptr)
                ++census.team0_livings;
            else
                ++census.hostile_livings;
        }
        else if (order == Order::Generator)
            ++census.generators;
        else if (order == Order::Treasure && family == FAMILY_EXIT)
            ++census.exits;
        else if (order == Order::Treasure && family == FAMILY_TELEPORTER)
            ++census.teleporters;
        else if (order == Order::Treasure)
            ++census.other_treasure;
        else if (order == Order::Weapon)
            ++census.weapons;
        else
            ++census.others;
    }
}

LiveCensus live_census(const GameWorld& world)
{
    LiveCensus census;
    count_live_entities(world.oblist, census);
    count_live_entities(world.weaplist, census);
    count_live_entities(world.fxlist, census);
    return census;
}

class ReplayPurgeTest : public HeadlessServerRuntimeTest
{
protected:
    // Mount `campaign` and load `level` through the REAL headless purge
    // path (load_headless_level_from_save), with `save` as the session
    // save. The caller shapes completed_levels / the replay arm first.
    LiveCensus load_and_census(const std::string& campaign, short level)
    {
        EXPECT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error(campaign));
        active_save_.current_campaign = campaign;
        active_save_.scen_num = level;
        active_save_.current_levels[campaign] = level;
        create_level_runtime_data(level);
        const bool loaded = with_context([&] {
            return og::server::load_headless_level_from_save(
                *level_data_, active_save_, /*difficulty_setting=*/1, events_,
                /*authoritative=*/true);
        });
        EXPECT_TRUE(loaded);
        return live_census(level_data_->world());
    }
};

// The flagship (#207, the empty island): the Imaginations campaign ships one
// level whose only exit loops home, so every replay is a load of a completed
// level — and today that load deletes all 19 hostiles, 3 generators and 18
// treasures, leaving one exit pad on an empty island. First pin today's
// truth (the collapse), then the fix: the replay arm restores the authored
// census.
TEST_F(ReplayPurgeTest, imaginations_replay_restores_the_authored_island)
{
    // Authored baseline: a fresh company's first landing.
    const LiveCensus authored = load_and_census("imaginations", 1);
    EXPECT_EQ(19, authored.hostile_livings)
        << "3 elves, 1 mage, 10 skeletons, 3 orcs, 2 towers — all team 1";
    EXPECT_EQ(3, authored.generators) << "tent, mage tower, treehouse";
    EXPECT_EQ(18, authored.other_treasure)
        << "gold, silver, drumsticks, 4 potions";
    EXPECT_EQ(1, authored.exits) << "the loop-home exit pad";
    EXPECT_EQ(0, authored.team0_livings) << "no authored allies";

    // Today's truth: the same load with level 1 completed collapses the
    // island to the exit pad (the 2002 completed-level purge).
    active_save_.reset();
    active_save_.add_level_completed("imaginations", 1);
    const LiveCensus purged = load_and_census("imaginations", 1);
    EXPECT_EQ(0, purged.hostile_livings) << "the purge empties the island";
    EXPECT_EQ(0, purged.generators);
    EXPECT_EQ(0, purged.other_treasure);
    EXPECT_EQ(1, purged.exits) << "exits survive the purge";
    EXPECT_EQ(1, purged.total()) << "one exit pad, nothing else";

    // The fix: the replay arm skips the purge and the island comes back.
    active_save_.reset();
    active_save_.add_level_completed("imaginations", 1);
    active_save_.current_campaign = "imaginations";
    active_save_.scen_num = 1;
    active_save_.arm_replay(1);
    const LiveCensus replayed = load_and_census("imaginations", 1);
    EXPECT_EQ(authored, replayed)
        << "an armed replay loads the authored census, not the empty island";
}

// The classic finally pinned: an UNARMED load of a completed level (the
// VISIT walk-through) keeps the purge — team-0 livings and exits survive,
// everything else dies. Gladiator scen 1: 12 elves + treehouse + 23 stains
// die; the 2 authored team-0 livings and both exits stay.
TEST_F(ReplayPurgeTest, gladiator_visit_keeps_the_classic_purge)
{
    const LiveCensus authored = load_and_census("gladiator", 1);
    EXPECT_EQ(12, authored.hostile_livings);
    EXPECT_EQ(1, authored.generators);
    EXPECT_EQ(2, authored.exits);
    EXPECT_EQ(2, authored.team0_livings) << "the authored soldier + archer";
    EXPECT_EQ(23, authored.other_treasure) << "the scenery stains";

    active_save_.reset();
    active_save_.add_level_completed("gladiator", 1);
    const LiveCensus visited = load_and_census("gladiator", 1);
    EXPECT_EQ(0, visited.hostile_livings);
    EXPECT_EQ(0, visited.generators);
    EXPECT_EQ(0, visited.other_treasure);
    EXPECT_EQ(0, visited.weapons);
    EXPECT_EQ(2, visited.exits) << "exits survive";
    EXPECT_EQ(2, visited.team0_livings) << "authored team-0 allies survive";
}

// A stale arm never skips a purge: arming level 3 and then loading a
// completed level 1 (a VISIT after an abandoned excursion) still purges.
TEST_F(ReplayPurgeTest, stale_arm_for_another_level_still_purges)
{
    active_save_.add_level_completed("gladiator", 1);
    active_save_.current_campaign = "gladiator";
    active_save_.scen_num = 5;
    active_save_.arm_replay(3);
    active_save_.scen_num = 1;  // a later plain write abandons the excursion
    const LiveCensus visited = load_and_census("gladiator", 1);
    EXPECT_EQ(0, visited.hostile_livings) << "the stale arm must not skip";
    EXPECT_EQ(2, visited.exits);
}

// The always-redoable policy is CAMPAIGN metadata, not the level's
// TYPE_SCRIPTED bit: both purge sites skip when campaign_matchup(current)
// == "versus" — the same memoized key that already exempts versus from the
// earned-roads gate and XP accrual (#213). A versus-campaign completed
// load restores WITHOUT any arm (mode objectives and bot squads survive a
// rematch); the paired adventure pins above show a completed load purges
// unless armed. Behavior-preserving: the only shipped bit-carrying levels
// are the versus campaign's (modes') 39, so the bit — still set on this
// level, still meaning "carries scripts" — no longer drives the answer.
TEST_F(ReplayPurgeTest, versus_campaign_completed_load_restores_without_arm)
{
    const LiveCensus authored = load_and_census("modes", 500);
    ASSERT_EQ("versus", og::data::campaign_matchup("modes"))
        << "the policy key: modes is the versus campaign";
    ASSERT_TRUE(level_data_->world().type & GameWorld::TYPE_SCRIPTED)
        << "the bit stays in the .fss; it just no longer keys the purge";
    ASSERT_GT(authored.total(), 0);

    active_save_.reset();
    active_save_.add_level_completed("modes", 500);
    const LiveCensus rematch = load_and_census("modes", 500);
    ASSERT_EQ(0, static_cast<int>(active_save_.replay_level))
        << "no arm anywhere near this load";
    EXPECT_EQ(authored, rematch)
        << "a versus rematch keeps the whole map without any arm";
}

// Design point 7 (the dedicated server): a host level-set onto a level the
// SERVER's save marks completed arms the replay — the networked table
// re-fights a restored level, and the origin remembers the table's real
// campaign position for the win fold to restore.
TEST_F(ReplayPurgeTest, lobby_game_start_onto_completed_level_arms_replay)
{
    // The server's persistent save: levels 1 and 2 beaten, cursor at 3.
    active_save_.current_campaign = "gladiator";
    active_save_.add_level_completed("gladiator", 1);
    active_save_.add_level_completed("gladiator", 2);
    active_save_.scen_num = 3;

    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "gladiator";
    lobby_save.scen_num = 1;  // the host re-picks a beaten level
    lobby_save.numplayers = 1;
    lobby_save.team_list = {
        make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER, 0),
    };
    initialize_from_lobby(lobby_save);

    EXPECT_TRUE(active_save_.replay_armed_for(1))
        << "a completed-level set on the server save arms the replay";
    EXPECT_EQ(3, active_save_.replay_origin)
        << "origin = the table's campaign position before the set";
    const LiveCensus census = live_census(level_data_->world());
    EXPECT_EQ(12, census.hostile_livings)
        << "the networked table re-fights the restored level";

    // The forward flow stays untouched: a start onto an UNCOMPLETED level
    // never arms.
    og::sim::LobbySaveDataEquivalent forward_save = lobby_save;
    forward_save.scen_num = 3;
    og::server::apply_headless_lobby_game_start_config(active_save_,
                                                       forward_save);
    EXPECT_EQ(0, active_save_.replay_level)
        << "an uncompleted-level start clears/never sets the arm";
}

// V5 Option A, point 3 (the trap), both directions. The adopt rule above
// exists for sessions that cannot read the hosting player's intent — the
// dedicated server and networked joiners. A PLAYER-seeded session (the
// curses host, seeded from its own company save) carries explicit intent
// instead: armed = REPLAY, unarmed + completed = VISIT. SeededIntent must
// never auto-arm, or every hosted VISIT silently becomes a replay.
TEST_F(HeadlessServerRuntimeTest, seeded_intent_never_auto_arms_a_visit)
{
    // The host's VISIT posture: level 1 cleared, cursor parked on it by a
    // plain write, no arm.
    active_save_.current_campaign = "gladiator";
    active_save_.add_level_completed("gladiator", 1);
    active_save_.scen_num = 1;

    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "gladiator";
    lobby_save.scen_num = 1;
    lobby_save.numplayers = 1;
    lobby_save.team_list = {
        make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER, 0),
    };
    og::server::apply_headless_lobby_game_start_config(
        active_save_, lobby_save,
        og::server::LobbyStartReplayArm::SeededIntent);

    EXPECT_EQ(0, static_cast<int>(active_save_.replay_level))
        << "a player-seeded session must not adopt the completed landing";

    // The explicit adopt twin on the same shape: the dedicated direction
    // still arms (the default parameter is this policy — the production
    // server_main call keeps it without naming it).
    SaveData dedicated;
    dedicated.current_campaign = "gladiator";
    dedicated.add_level_completed("gladiator", 1);
    dedicated.scen_num = 3;
    og::server::apply_headless_lobby_game_start_config(
        dedicated, lobby_save,
        og::server::LobbyStartReplayArm::AdoptCompletedLanding);
    EXPECT_TRUE(dedicated.replay_armed_for(1))
        << "the dedicated server keeps auto-arming";
    EXPECT_EQ(3, static_cast<int>(dedicated.replay_origin));
}

// SeededIntent keeps the player's own arm when it covers the negotiated
// level — the REPLAY press reaches the authoritative load with its origin.
TEST_F(HeadlessServerRuntimeTest, seeded_intent_carries_the_players_arm)
{
    active_save_.current_campaign = "gladiator";
    active_save_.add_level_completed("gladiator", 1);
    active_save_.scen_num = 3;
    active_save_.arm_replay(1);

    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "gladiator";
    lobby_save.scen_num = 1;
    lobby_save.numplayers = 1;
    lobby_save.team_list = {
        make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER, 0),
    };
    og::server::apply_headless_lobby_game_start_config(
        active_save_, lobby_save,
        og::server::LobbyStartReplayArm::SeededIntent);

    EXPECT_TRUE(active_save_.replay_armed_for(1));
    EXPECT_EQ(3, static_cast<int>(active_save_.replay_origin))
        << "the origin survives into the session";
}

// A stale seeded arm dies at the config apply: an arm for another level or
// another campaign never rides into the negotiated round.
TEST_F(HeadlessServerRuntimeTest, seeded_intent_clears_a_stale_arm)
{
    // Armed for level 2, but the lobby negotiated level 1 (also cleared):
    // the arm is stale AND the landing must still not auto-arm.
    active_save_.current_campaign = "gladiator";
    active_save_.add_level_completed("gladiator", 1);
    active_save_.add_level_completed("gladiator", 2);
    active_save_.scen_num = 5;
    active_save_.arm_replay(2);

    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "gladiator";
    lobby_save.scen_num = 1;
    lobby_save.numplayers = 1;
    lobby_save.team_list = {
        make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER, 0),
    };
    og::server::apply_headless_lobby_game_start_config(
        active_save_, lobby_save,
        og::server::LobbyStartReplayArm::SeededIntent);
    EXPECT_EQ(0, static_cast<int>(active_save_.replay_level))
        << "a stale arm dies; the completed landing stays a VISIT";

    // Campaign switch: an arm covering the level NUMBER of another
    // campaign has no origin here — cleared too.
    SaveData switched;
    switched.current_campaign = "imaginations";
    switched.add_level_completed("imaginations", 1);
    switched.scen_num = 1;
    switched.arm_replay(1);
    og::server::apply_headless_lobby_game_start_config(
        switched, lobby_save,
        og::server::LobbyStartReplayArm::SeededIntent);
    EXPECT_EQ(0, static_cast<int>(switched.replay_level))
        << "no origin may cross campaigns";
}

// The arm rides the server/checkpoint copies (the dropped-field pattern):
// curses builds its authoritative session save through this copy, so a
// dropped pair would purge the armed replay on the curses client.
TEST_F(HeadlessServerRuntimeTest, server_save_copy_carries_the_replay_arm)
{
    SaveData source;
    source.current_campaign = "gladiator";
    source.scen_num = 4;
    source.arm_replay(2);

    SaveData destination;
    og::server::copy_headless_server_save_data(destination, source);
    EXPECT_EQ(2, destination.replay_level);
    EXPECT_EQ(4, destination.replay_origin);
}

} // namespace
