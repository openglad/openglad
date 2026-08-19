/* og::server::MatchStage — the staged-lobby authoritative world (#218).
 *
 * These are the C4 oracles: restage determinism (byte-identical keyframes
 * for identical inputs, divergence for changed roster/seed), dormancy (a
 * staged world never ticks and its announcement queue never drains), the
 * change-key/debounce discipline, on_load running for real at stage time
 * (spawns visible in the staged world, the latch claimed), classic levels
 * staging without mode init, and the honest Failed shapes.
 */
#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/weather.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/match_stage.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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

og::server::MatchStageInputs make_modes_inputs(std::uint32_t match_seed)
{
    og::server::MatchStageInputs inputs;
    inputs.equivalent.current_campaign = "modes";
    inputs.equivalent.scen_num = 820; // soccer pitch: fills draw bot squads
    inputs.equivalent.numplayers = 1;
    inputs.equivalent.allied_mode = 0;
    inputs.equivalent.team_list = {
        make_slot(0u, 100, "Striker", FAMILY_SOLDIER, 0),
    };
    inputs.difficulty = 1;
    inputs.match_seed = match_seed;
    return inputs;
}

std::vector<std::uint8_t> staged_keyframe_bytes(og::server::MatchStage& stage)
{
    GameWorld* const staged_world = stage.world();
    EXPECT_NE(nullptr, staged_world);
    return og::sim::serialize_snapshot(
        og::sim::peek_keyframe_snapshot(*staged_world));
}

int count_notifications_with_text(const og::sim::SimEventLog& log,
                                  const std::string& text)
{
    int count = 0;
    for (const og::sim::Event& event : log.events())
    {
        if (event.kind == og::sim::EventKind::Notification &&
            event.text == text)
            ++count;
    }
    return count;
}

const walker* find_living_named(const GameWorld& world, const std::string& name)
{
    for (const auto& entry : world.oblist)
    {
        const walker* const entity = entry.get();
        if (entity != nullptr && entity->query_order() == Order::Living &&
            entity->stats() != nullptr && entity->stats()->name == name)
            return entity;
    }
    return nullptr;
}

class MatchStageTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        restore_default_settings();
    }
};

// The match-id latch: identical inputs => byte-identical staged keyframe,
// even after staging something else in between AND after the process-global
// weather roll sequence moved underneath (the stage re-pins it per restage).
// A changed roster diverges the bytes; each restage moves the generation by
// exactly one.
TEST_F(MatchStageTest, identical_inputs_restage_is_byte_identical)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    const og::server::MatchStageInputs inputs_a = make_modes_inputs(1001u);

    stage.observe_inputs(inputs_a, /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    EXPECT_EQ(1u, stage.stage_generation());
    const std::vector<std::uint8_t> bytes_a = staged_keyframe_bytes(stage);

    // A changed roster is a changed world.
    og::server::MatchStageInputs inputs_b = inputs_a;
    inputs_b.equivalent.team_list.push_back(
        make_slot(1u, 200, "Winger", FAMILY_ELF, 1));
    stage.observe_inputs(inputs_b, /*now_ms=*/1'000);
    stage.maintain(/*now_ms=*/1'000 + og::server::kStageDebounceMs);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    EXPECT_EQ(2u, stage.stage_generation());
    const std::vector<std::uint8_t> bytes_b = staged_keyframe_bytes(stage);
    EXPECT_NE(bytes_a, bytes_b)
        << "a changed roster must change the staged world";

    // Perturb the process-global weather sequence the way unrelated level
    // loads would; the restage must still reproduce A exactly.
    (void)og::next_weather_roll_sequence();
    (void)og::next_weather_roll_sequence();
    (void)og::next_weather_roll_sequence();

    stage.observe_inputs(inputs_a, /*now_ms=*/2'000);
    stage.maintain(/*now_ms=*/2'000 + og::server::kStageDebounceMs);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    EXPECT_EQ(3u, stage.stage_generation());
    const std::vector<std::uint8_t> bytes_a_again = staged_keyframe_bytes(stage);
    EXPECT_EQ(bytes_a, bytes_a_again)
        << "identical inputs must restage byte-identically";
}

// #235 made a stage-time fact: the bot-squad permutation code is drawn from
// the pinned stream during the staged init and latched into replicated
// mode var 6 — so the squads the preview shows ARE the squads the launch
// plays, and a different match seed rolls a different (pinned) permutation.
TEST_F(MatchStageTest, seed_change_diverges_the_latched_squad_code)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});

    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    ASSERT_NE(nullptr, stage.world());
    ASSERT_TRUE(stage.world()->mode.active)
        << "the soccer pitch must activate at stage time";
    const std::int32_t squad_code_1001 = stage.world()->mode.vars[6];

    stage.observe_inputs(make_modes_inputs(4242u), /*now_ms=*/1'000);
    stage.maintain(/*now_ms=*/1'000 + og::server::kStageDebounceMs);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    ASSERT_TRUE(stage.world()->mode.active);
    const std::int32_t squad_code_4242 = stage.world()->mode.vars[6];

    // Exact pinned draws (the basketball squad-seed pin's discipline): the
    // values are pure functions of the match seed through the staged load.
    EXPECT_EQ(61, squad_code_1001);
    EXPECT_EQ(20, squad_code_4242);
    EXPECT_NE(squad_code_1001, squad_code_4242)
        << "a fresh match seed must roll a fresh squad permutation";

    // Same seed again: the same latched code (no preview flicker).
    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/2'000);
    stage.maintain(/*now_ms=*/2'000 + og::server::kStageDebounceMs);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    EXPECT_EQ(squad_code_1001, stage.world()->mode.vars[6]);
}

// Dormancy + on_load-for-real, on the reporter's Westlands shape: the staged
// world contains the on_load SPAWNS (the paid Watch, visible to the preview
// at tick 0), its ANNOUNCEMENT is queued exactly once with the tick-1 stamp,
// nothing drains or ticks under maintain(), a restage re-queues exactly one
// copy, and the on_load latch is claimed (a re-dispatch attempt is a no-op).
TEST_F(MatchStageTest, westlands_on_load_spawns_and_announces_once_dormant)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("westlands"));

    SaveData host_save;
    host_save.current_campaign = "westlands";
    host_save.scen_num = 15;
    ASSERT_TRUE(host_save.campaign_state_set("westlands", "watch_paid", 900));

    og::server::MatchStage stage({
        .networked = true,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &host_save,
    });

    og::server::MatchStageInputs inputs;
    inputs.equivalent.current_campaign = "westlands";
    inputs.equivalent.scen_num = 15;
    inputs.equivalent.numplayers = 1;
    inputs.equivalent.team_list = {
        make_slot(0u, 100, "Host", FAMILY_SOLDIER, 0),
    };
    inputs.difficulty = 1;
    inputs.match_seed = 7u;

    stage.observe_inputs(inputs, /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    GameWorld* const staged_world = stage.world();
    ASSERT_NE(nullptr, staged_world);

    // The on_load spawn is IN the staged world, before any tick.
    EXPECT_EQ(0u, staged_world->tick_count_);
    EXPECT_EQ(0u, staged_world->level_tick_count());
    EXPECT_NE(nullptr, find_living_named(*staged_world, "Wall-Warden"))
        << "the paid Watch musters in the staged world (#239 Westlands case)";

    // The announcement is queued exactly once, stamped tick 1 (the stamp it
    // carries when init runs lazily at the first tick).
    const std::string ledger_line = "The Watch remembers its wages.";
    ASSERT_EQ(1, count_notifications_with_text(stage.staged_events(),
                                               ledger_line));
    for (const og::sim::Event& event : stage.staged_events().events())
    {
        if (event.text == ledger_line)
        {
            EXPECT_EQ(1u, event.tick)
                << "staged announcements carry the tick-1 stamp";
        }
    }

    // Dormancy: maintain() on a clean stage neither ticks nor drains.
    const std::uint32_t generation = stage.stage_generation();
    for (std::uint64_t at = 1'000; at <= 5'000; at += 1'000)
        stage.maintain(at);
    EXPECT_EQ(generation, stage.stage_generation());
    EXPECT_EQ(0u, staged_world->tick_count_);
    EXPECT_EQ(1, count_notifications_with_text(stage.staged_events(),
                                               ledger_line));

    // The latch was consumed by running the real dispatch: a second delivery
    // attempt is a no-op (no duplicate spawn wave, no duplicate line).
    staged_world->run_pending_level_on_load();
    EXPECT_EQ(1, count_notifications_with_text(stage.staged_events(),
                                               ledger_line));

    // A restage queues exactly one copy again — never zero, never two.
    og::server::MatchStageInputs moved = inputs;
    moved.match_seed = 8u;
    stage.observe_inputs(moved, /*now_ms=*/10'000);
    stage.maintain(/*now_ms=*/10'000 + og::server::kStageDebounceMs);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    EXPECT_EQ(1, count_notifications_with_text(stage.staged_events(),
                                               ledger_line));
    EXPECT_NE(nullptr, find_living_named(*stage.world(), "Wall-Warden"));
}

// A classic (non-scripted) level stages without mode init: the roster spawns,
// the world is dormant, and the mode latches stay untouched so the launch's
// lazy arm keeps its meaning for classic play.
TEST_F(MatchStageTest, classic_level_stages_without_mode_init)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    og::server::MatchStage stage({.networked = false});

    og::server::MatchStageInputs inputs;
    inputs.equivalent.current_campaign = "gladiator";
    inputs.equivalent.scen_num = 1;
    inputs.equivalent.numplayers = 2;
    inputs.equivalent.allied_mode = 0;
    inputs.equivalent.team_list = {
        make_slot(0u, 100, "Lead", FAMILY_SOLDIER, 0),
        make_slot(3u, 200, "Guest", FAMILY_ARCHER, 1),
    };
    inputs.difficulty = 1;
    inputs.match_seed = 99u;

    stage.observe_inputs(inputs, /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    GameWorld* const staged_world = stage.world();
    ASSERT_NE(nullptr, staged_world);

    EXPECT_EQ(0, staged_world->type & GameWorld::TYPE_SCRIPTED);
    EXPECT_FALSE(staged_world->mode.init_attempted)
        << "classic levels never attempt mode init at stage time";
    EXPECT_FALSE(staged_world->mode.active);
    EXPECT_EQ(0u, staged_world->tick_count_);
    EXPECT_EQ(1, staged_world->id);
    EXPECT_EQ(2, stage.staged_save().team_size);

    int roster_walkers = 0;
    for (const auto& entry : staged_world->oblist)
    {
        const walker* const entity = entry.get();
        if (entity != nullptr && entity->myguy != nullptr)
            ++roster_walkers;
    }
    EXPECT_EQ(2, roster_walkers)
        << "both deployed lobby slots must spawn into the staged world";
}

// The change key coalesces: N knob writes inside one debounce window rebuild
// ONCE (trailing edge); an identical key never restages; GO's ensure_current
// forces a synchronous rebuild of a dirty stage so a stale stage can never
// launch.
TEST_F(MatchStageTest, debounce_coalesces_and_ensure_current_forces)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    og::server::MatchStage stage({.networked = true});

    og::server::MatchStageInputs inputs;
    inputs.equivalent.current_campaign = "gladiator";
    inputs.equivalent.scen_num = 1;
    inputs.equivalent.numplayers = 1;
    inputs.equivalent.team_list = {
        make_slot(0u, 100, "Lead", FAMILY_SOLDIER, 0),
    };
    inputs.difficulty = 1;
    inputs.match_seed = 5u;

    stage.observe_inputs(inputs, /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    ASSERT_EQ(1u, stage.stage_generation());

    // Five knob writes 50 ms apart: all coalesce behind the trailing edge.
    for (int knob = 1; knob <= 5; ++knob)
    {
        inputs.equivalent.ctf_capture_limit = static_cast<std::int16_t>(knob);
        stage.observe_inputs(
            inputs, /*now_ms=*/1'000 + static_cast<std::uint64_t>(knob) * 50);
        stage.maintain(/*now_ms=*/1'000 + static_cast<std::uint64_t>(knob) * 50);
    }
    EXPECT_EQ(1u, stage.stage_generation())
        << "knob cycling must not rebuild inside the debounce window";
    EXPECT_TRUE(stage.dirty());

    // Still inside the window measured from the LAST write (1250): no build.
    stage.maintain(/*now_ms=*/1'250 + og::server::kStageDebounceMs - 1);
    EXPECT_EQ(1u, stage.stage_generation());

    stage.maintain(/*now_ms=*/1'250 + og::server::kStageDebounceMs);
    EXPECT_EQ(2u, stage.stage_generation())
        << "the trailing edge rebuilds exactly once for the whole burst";
    EXPECT_FALSE(stage.dirty());

    // An identical key is a no-op forever.
    stage.observe_inputs(inputs, /*now_ms=*/50'000);
    stage.maintain(/*now_ms=*/120'000);
    EXPECT_EQ(2u, stage.stage_generation());

    // GO forces a dirty stage synchronously.
    inputs.equivalent.ctf_capture_limit = 9;
    stage.observe_inputs(inputs, /*now_ms=*/200'000);
    EXPECT_TRUE(stage.dirty());
    EXPECT_TRUE(stage.ensure_current(/*now_ms=*/200'001));
    EXPECT_EQ(3u, stage.stage_generation());
    EXPECT_FALSE(stage.dirty());
    EXPECT_EQ(og::server::StageStatus::Staged, stage.status());
}

// Failed shapes are honest: an unloadable campaign yields Failed + an error
// string + a null world, GO is refused, and a corrected key recovers through
// the ordinary debounce path.
TEST_F(MatchStageTest, failed_stage_reports_and_recovers)
{
    og::server::MatchStage stage({.networked = true});

    og::server::MatchStageInputs inputs;
    inputs.equivalent.current_campaign = "missing-stage-campaign";
    inputs.equivalent.scen_num = 1;
    inputs.equivalent.numplayers = 1;
    inputs.equivalent.team_list = {
        make_slot(0u, 100, "Lead", FAMILY_SOLDIER, 0),
    };
    inputs.difficulty = 1;
    inputs.match_seed = 5u;

    testing::internal::CaptureStderr();
    stage.observe_inputs(inputs, /*now_ms=*/0);
    const std::string diagnostics = testing::internal::GetCapturedStderr();

    EXPECT_EQ(og::server::StageStatus::Failed, stage.status());
    EXPECT_EQ("staged level load failed", stage.error());
    EXPECT_EQ(nullptr, stage.world());
    EXPECT_EQ(1u, stage.stage_generation())
        << "a failed stage is a generation too — the preview must notice";
    EXPECT_FALSE(stage.ensure_current(/*now_ms=*/1))
        << "GO must be refused while the stage is Failed";
    EXPECT_NE(std::string::npos,
              diagnostics.find("headless_server_campaign_load_failed"));

    // The corrected key recovers through the ordinary restage.
    inputs.equivalent.current_campaign = "gladiator";
    stage.observe_inputs(inputs, /*now_ms=*/1'000);
    stage.maintain(/*now_ms=*/1'000 + og::server::kStageDebounceMs);
    EXPECT_EQ(og::server::StageStatus::Staged, stage.status());
    EXPECT_TRUE(stage.error().empty());
    ASSERT_NE(nullptr, stage.world());
    EXPECT_EQ(2u, stage.stage_generation());
    EXPECT_TRUE(stage.ensure_current(/*now_ms=*/2'000));
}

// take_events is the adoption seam: it drains the staged queue (stamps
// preserved for SimEventLog::append) and leaves the stage empty of
// announcements without disturbing the staged world.
TEST_F(MatchStageTest, take_events_drains_the_staged_queue_once)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("westlands"));

    SaveData host_save;
    host_save.current_campaign = "westlands";
    host_save.scen_num = 15;
    ASSERT_TRUE(host_save.campaign_state_set("westlands", "watch_paid", 900));

    og::server::MatchStage stage({
        .networked = false,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &host_save,
    });

    og::server::MatchStageInputs inputs;
    inputs.equivalent.current_campaign = "westlands";
    inputs.equivalent.scen_num = 15;
    inputs.equivalent.numplayers = 1;
    inputs.equivalent.team_list = {
        make_slot(0u, 100, "Host", FAMILY_SOLDIER, 0),
    };
    inputs.difficulty = 1;
    inputs.match_seed = 7u;

    stage.observe_inputs(inputs, /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());

    const std::string ledger_line = "The Watch remembers its wages.";
    const std::vector<og::sim::Event> taken = stage.take_events();
    int taken_ledger_lines = 0;
    for (const og::sim::Event& event : taken)
    {
        if (event.kind == og::sim::EventKind::Notification &&
            event.text == ledger_line)
        {
            ++taken_ledger_lines;
            EXPECT_EQ(1u, event.tick);
        }
    }
    EXPECT_EQ(1, taken_ledger_lines);
    EXPECT_TRUE(stage.staged_events().empty())
        << "take_events must leave the staged queue empty";
    EXPECT_NE(nullptr, stage.world())
        << "taking the events must not disturb the staged world";
}

// --- C6: owner staging drive + staged-pair delivery (#218) ------------------

class RecordingTransport final : public og::sim::ITransport
{
public:
    void send(og::sim::PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override
    {
        sent_.push_back({peer_id, std::vector<std::uint8_t>(data, data + len)});
    }

    std::vector<og::sim::ReceivedMessage> poll() override { return {}; }
    void accept_connections() override {}
    void disconnect(og::sim::PeerId) override {}
    [[nodiscard]] std::vector<og::sim::PeerId> connected_peers() const override
    {
        return peers_;
    }

    void set_peers(std::vector<og::sim::PeerId> peers)
    {
        peers_ = std::move(peers);
    }
    [[nodiscard]] const std::vector<og::sim::ReceivedMessage>& sent() const
    {
        return sent_;
    }

private:
    std::vector<og::sim::PeerId> peers_;
    std::vector<og::sim::ReceivedMessage> sent_;
};

// The owner poll driver: the first key broadcasts one generation-stamped
// setup+keyframe pair to every peer; an unchanged key sends nothing; a knob
// change coalesces through the trailing-edge debounce into exactly one more
// restage and one more pair.
TEST_F(MatchStageTest, drive_lobby_stage_broadcasts_one_pair_per_restage)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    og::server::StageBroadcastState broadcast;
    RecordingTransport transport;
    transport.set_peers({7u, 9u});

    og::server::MatchStageInputs inputs = make_modes_inputs(1001u);
    og::server::drive_lobby_stage(stage, inputs, /*now_ms=*/0, &transport,
                                  broadcast);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    EXPECT_EQ(1u, stage.stage_generation());
    // ITransport's default broadcast fans out per connected peer: 2 peers x
    // (setup + keyframe) = 4 sends, setup before keyframe for each peer.
    ASSERT_EQ(4u, transport.sent().size());
    const auto first_setup =
        og::sim::decode_received_message(transport.sent()[0]);
    ASSERT_EQ(og::sim::TypedReceivedMessageKind::StagedMatchSetup,
              first_setup.kind);
    EXPECT_EQ(1u, first_setup.staged_match_setup->stage_generation);
    EXPECT_EQ(stage.staged_setup_bytes(),
              first_setup.staged_match_setup->setup_bytes);
    const auto first_keyframe =
        og::sim::decode_received_message(transport.sent()[2]);
    ASSERT_EQ(og::sim::TypedReceivedMessageKind::StagedMatchKeyframe,
              first_keyframe.kind);
    EXPECT_EQ(1u, first_keyframe.staged_match_keyframe->stage_generation);
    EXPECT_EQ(stage.staged_keyframe_bytes(),
              first_keyframe.staged_match_keyframe->snapshot_bytes);

    // Unchanged key: nothing restages, nothing is sent.
    og::server::drive_lobby_stage(stage, inputs, /*now_ms=*/50, &transport,
                                  broadcast);
    EXPECT_EQ(1u, stage.stage_generation());
    EXPECT_EQ(4u, transport.sent().size());

    // Knob churn coalesces: two capture-limit writes inside the window are
    // one restage and one broadcast pair, on the trailing edge.
    inputs.equivalent.ctf_capture_limit = 3;
    og::server::drive_lobby_stage(stage, inputs, /*now_ms=*/100, &transport,
                                  broadcast);
    inputs.equivalent.ctf_capture_limit = 5;
    og::server::drive_lobby_stage(stage, inputs, /*now_ms=*/200, &transport,
                                  broadcast);
    EXPECT_EQ(1u, stage.stage_generation());
    EXPECT_EQ(4u, transport.sent().size());
    og::server::drive_lobby_stage(
        stage, inputs, /*now_ms=*/200 + og::server::kStageDebounceMs - 1,
        &transport, broadcast);
    EXPECT_EQ(1u, stage.stage_generation()) << "trailing edge not reached";
    og::server::drive_lobby_stage(
        stage, inputs, /*now_ms=*/200 + og::server::kStageDebounceMs,
        &transport, broadcast);
    EXPECT_EQ(2u, stage.stage_generation());
    ASSERT_EQ(8u, transport.sent().size());
    const auto second_setup =
        og::sim::decode_received_message(transport.sent()[4]);
    ASSERT_EQ(og::sim::TypedReceivedMessageKind::StagedMatchSetup,
              second_setup.kind);
    EXPECT_EQ(2u, second_setup.staged_match_setup->stage_generation);
}

// A peer that connects between restages (a spectator connect changes no
// stage input) gets the CURRENT pair per-peer — and only that peer does.
TEST_F(MatchStageTest, drive_lobby_stage_serves_catchup_pair_to_new_peer)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    og::server::StageBroadcastState broadcast;
    RecordingTransport transport;
    transport.set_peers({7u});

    const og::server::MatchStageInputs inputs = make_modes_inputs(1001u);
    og::server::drive_lobby_stage(stage, inputs, /*now_ms=*/0, &transport,
                                  broadcast);
    ASSERT_EQ(2u, transport.sent().size()) << "one pair to the lone peer";

    transport.set_peers({7u, 9u});
    og::server::drive_lobby_stage(stage, inputs, /*now_ms=*/10, &transport,
                                  broadcast);
    ASSERT_EQ(4u, transport.sent().size());
    EXPECT_EQ(9u, transport.sent()[2].peer_id)
        << "catch-up goes to the NEW peer only";
    EXPECT_EQ(9u, transport.sent()[3].peer_id);
    const auto catchup_setup =
        og::sim::decode_received_message(transport.sent()[2]);
    ASSERT_EQ(og::sim::TypedReceivedMessageKind::StagedMatchSetup,
              catchup_setup.kind);
    EXPECT_EQ(stage.stage_generation(),
              catchup_setup.staged_match_setup->stage_generation);
    const auto catchup_keyframe =
        og::sim::decode_received_message(transport.sent()[3]);
    ASSERT_EQ(og::sim::TypedReceivedMessageKind::StagedMatchKeyframe,
              catchup_keyframe.kind);

    // Once known, the peer never receives the same generation again.
    og::server::drive_lobby_stage(stage, inputs, /*now_ms=*/20, &transport,
                                  broadcast);
    EXPECT_EQ(4u, transport.sent().size());
}

// The cached wire pair IS the staged world: the setup carries the level
// metadata + roster (controlled ids deliberately empty — the launch server
// still sends per-seat setups) and the keyframe bytes equal a fresh Peek
// serialization of the staged world.
TEST_F(MatchStageTest, staged_wire_pair_matches_the_staged_world)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    ASSERT_NE(nullptr, stage.world());

    const auto setup =
        og::sim::deserialize_initial_setup_message(stage.staged_setup_bytes());
    ASSERT_TRUE(setup.has_value());
    EXPECT_EQ(stage.world()->id, setup->level_id);
    EXPECT_EQ(stage.world()->pixmaxx, setup->pixmaxx);
    EXPECT_EQ(stage.world()->pixmaxy, setup->pixmaxy);
    EXPECT_EQ(820, setup->current_scenario);
    bool found_striker = false;
    for (const og::sim::InitialSetupGuyData& guy_data : setup->guys)
        found_striker = found_striker || guy_data.name == "Striker";
    EXPECT_TRUE(found_striker) << "the roster rides the staged setup";
    for (const std::uint32_t controlled : setup->controlled_entity_ids)
        EXPECT_EQ(0u, controlled);

    EXPECT_EQ(staged_keyframe_bytes(stage), stage.staged_keyframe_bytes())
        << "the cached keyframe must be byte-identical to a fresh Peek";
    const og::sim::WorldSnapshot inner =
        og::sim::deserialize_snapshot(stage.staged_keyframe_bytes());
    EXPECT_EQ(0u, inner.tick_count);
    EXPECT_EQ(0u, inner.level_tick_count);
}

// C7: launch == preview, as a byte identity. Adopt the staged world into a
// fresh destination (the SDL shadow's shape: content transfer through
// replace_loaded_world_state + the explicit carries) and the adopted tick-0
// keyframe serializes BYTE-IDENTICAL to the staged pair the preview shows.
TEST_F(MatchStageTest, adopted_world_keyframe_is_byte_identical_to_preview)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    const std::vector<std::uint8_t> preview_bytes =
        stage.staged_keyframe_bytes();
    ASSERT_FALSE(preview_bytes.empty());
    const std::vector<og::sim::Event> staged_events =
        std::vector<og::sim::Event>(stage.staged_events().events());
    ASSERT_FALSE(staged_events.empty())
        << "the staged soccer init queues announcements";

    // The destination: a fresh headless level runtime + save, bracketed by
    // its own context exactly like the shadow's server session.
    LevelRuntimeData dst_level(820, /*headless=*/true,
                               &headless_level_data_hooks());
    SaveData dst_save;
    GameWorld& dst = dst_level.world();
    og::sim::SimEventLog dst_events;
    IRandom* dst_rng = &dst.rng_;
    bool dst_active = false;
    GameplayContext dst_ctx;
    dst_ctx.world = &dst;
    dst_ctx.save = &dst_save;
    dst_ctx.sim_events = &dst_events;
    dst_ctx.config = &cfg;
    dst_ctx.session_rng_ref = &dst_rng;
    dst_ctx.gameplay_active_ref = &dst_active;
    GameplayContext* const previous_context = current_game;
    current_game = &dst_ctx;

    // The shadow's order: prep-clear (tick 0 + reset_level_progress, which
    // re-arms the on_load latch) BEFORE the adopt (which claims it back).
    dst.tick_count_ = 0;
    dst.reset_level_progress();
    ASSERT_TRUE(og::server::adopt_staged_world(dst_level, dst_save, stage));
    dst_events.append(stage.take_events());
    stage.dispose();
    current_game = previous_context;

    const std::vector<std::uint8_t> adopted_bytes =
        og::sim::serialize_snapshot(og::sim::peek_keyframe_snapshot(dst));
    EXPECT_EQ(preview_bytes, adopted_bytes)
        << "adoption must not perturb a single replicated byte";

    // The explicit carries the snapshot cannot prove: the on_load latch is
    // claimed (the first tick must not re-run on_load) and the staged
    // announcements landed in the live log with their tick-1 stamps.
    EXPECT_FALSE(dst.owes_level_on_load())
        << "the adopter claims the on_load latch truthfully";
    EXPECT_EQ(staged_events.size(), dst_events.events().size());
    for (const og::sim::Event& event : dst_events.events())
        EXPECT_EQ(1u, event.tick);

    // The staged save became the session save.
    EXPECT_EQ(820, dst_save.scen_num);
    EXPECT_EQ("modes", dst_save.current_campaign);
    bool found_striker = false;
    for (const auto& member : dst_save.team_list)
        found_striker = found_striker ||
            (member != nullptr && member->name == "Striker");
    EXPECT_TRUE(found_striker);

    // Nothing staged after the adopt+dispose; a fresh key restages cleanly.
    EXPECT_EQ(og::server::StageStatus::Empty, stage.status());
}

// mark_failed (the owner's change-key throw funnel): repeat failures do not
// churn the generation; the key is poisoned so a lobby that recovers back to
// the LAST GOOD key restages instead of no-oping; a Failed stage delivers
// nothing.
TEST_F(MatchStageTest, mark_failed_poisons_key_and_recovers_on_identical_key)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    og::server::StageBroadcastState broadcast;
    RecordingTransport transport;
    transport.set_peers({7u});

    const og::server::MatchStageInputs inputs = make_modes_inputs(1001u);
    og::server::drive_lobby_stage(stage, inputs, /*now_ms=*/0, &transport,
                                  broadcast);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    ASSERT_EQ(2u, transport.sent().size());

    testing::internal::CaptureStderr();
    stage.mark_failed("combined roster exceeds the lobby cap");
    (void)testing::internal::GetCapturedStderr();
    EXPECT_EQ(og::server::StageStatus::Failed, stage.status());
    EXPECT_EQ("combined roster exceeds the lobby cap", stage.error());
    EXPECT_EQ(nullptr, stage.world());
    EXPECT_EQ(2u, stage.stage_generation());
    EXPECT_TRUE(stage.staged_setup_bytes().empty());
    EXPECT_TRUE(stage.staged_keyframe_bytes().empty());

    // The owner re-marks the same failure every poll while the roster stays
    // over the cap: no generation churn.
    stage.mark_failed("combined roster exceeds the lobby cap");
    EXPECT_EQ(2u, stage.stage_generation());

    // A Failed stage never puts anything on the wire.
    og::server::deliver_staged_pair(stage, &transport, broadcast);
    EXPECT_EQ(2u, transport.sent().size());

    // Recovery back to EXACTLY the last good key must restage (the throwing
    // build never reached observe_inputs, so the stored key is stale-good).
    og::server::drive_lobby_stage(stage, inputs, /*now_ms=*/1'000, &transport,
                                  broadcast);
    EXPECT_EQ(og::server::StageStatus::Failed, stage.status())
        << "recovery waits out the debounce";
    og::server::drive_lobby_stage(stage, inputs,
                                  /*now_ms=*/1'000 + og::server::kStageDebounceMs,
                                  &transport, broadcast);
    EXPECT_EQ(og::server::StageStatus::Staged, stage.status());
    EXPECT_EQ(3u, stage.stage_generation());
    EXPECT_EQ(4u, transport.sent().size())
        << "the recovered stage broadcasts one fresh pair";
}

} // namespace
