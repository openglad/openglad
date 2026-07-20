#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/weather.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/session_state.h>
#include <openglad/platform/game_context.h>
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

class HeadlessServerRuntimeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        restore_default_settings();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("org.openglad.gladiator"));
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
    lobby_save.current_campaign = "org.openglad.gladiator";
    lobby_save.scen_num = 1;
    lobby_save.numplayers = 2;
    lobby_save.allied_mode = 0;
    lobby_save.team_list = {
        make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER, 0),
        make_slot(3u, 200, "Guest Guy", FAMILY_ARCHER, 1),
    };

    initialize_from_lobby(lobby_save);

    EXPECT_EQ("org.openglad.gladiator", active_save_.current_campaign);
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
}

TEST_F(HeadlessServerRuntimeTest,
       lobby_start_preserves_distinct_owners_with_same_private_save_slot)
{
    constexpr std::uint8_t host_owner = 2u;
    constexpr std::uint8_t guest_owner = 9u;
    constexpr std::uint8_t private_slot = 0u;

    og::sim::LobbySaveDataEquivalent lobby_save;
    lobby_save.current_campaign = "org.openglad.gladiator";
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

    EXPECT_EQ("org.openglad.gladiator", active_save_.current_campaign);
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
    lobby_save.current_campaign = "org.openglad.gladiator";
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
    lobby_save.current_campaign = "org.openglad.gladiator";
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

// §3.4 dropped-field bug class: the server/checkpoint copies must carry the
// GTL v14 company fields — the last-played timestamp explicitly, the per-guy
// deploy flag via the guy deep copies. (The tower fields were the previous
// instance of this bug class; the copy is an explicit field list, so every
// new SaveData member needs a line there AND a pin here.)
TEST(HeadlessServerSaveCopy, copy_carries_v14_company_fields)
{
    SaveData source;
    source.last_played_unix_s = 0x0A0B0C0D0E0F1011LL;
    source.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    source.team_list[0]->name = "BROUGHT";
    source.team_list[0]->deployed = true;
    source.team_list[1] = std::make_unique<guy>(FAMILY_ELF);
    source.team_list[1]->name = "HELDBACK";
    source.team_list[1]->deployed = false;
    source.team_size = 2;

    SaveData destination;
    destination.last_played_unix_s = 42; // must be overwritten, not merged

    og::server::copy_headless_server_save_data(destination, source);

    EXPECT_EQ(0x0A0B0C0D0E0F1011LL, destination.last_played_unix_s)
        << "timestamp must survive the server/checkpoint copy";
    ASSERT_TRUE(destination.team_list[0] != nullptr);
    ASSERT_TRUE(destination.team_list[1] != nullptr);
    EXPECT_NE(destination.team_list[0].get(), source.team_list[0].get())
        << "guys are deep copies, not shared pointers";
    EXPECT_TRUE(destination.team_list[0]->deployed);
    EXPECT_FALSE(destination.team_list[1]->deployed)
        << "the deploy flag rides the guy copy";
    EXPECT_TRUE(destination.team_list[1]->name == "HELDBACK");
    EXPECT_EQ(2, (int)destination.team_size);
}

} // namespace
