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
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/match_stage.h>

#include <algorithm>
#include <cstdint>
#include <set>
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

namespace {

// The court.lua shape, as a fixture probe: the level's on_load spawns a
// marked target and registers its on_death through og.set_entity_hooks. The
// hook banks an exact value in a spare mode var; the target's entity id is
// banked beside it so the test can find the walker after adoption. (This
// stages a CLASSIC level, so the whole shared var band is free.)
constexpr const char* kAdoptHookProbeLua =
    "og.register_level_hooks(1, {\n"
    "  on_load = function(level)\n"
    "    local ent = og.add_ob('living', og.family_id('living', "
    "'core:soldier'))\n"
    "    ent:setxy(160, 160)\n"
    "    ent:set_team_num(1)\n"
    "    og.set_entity_hooks(ent, { on_death = function(e, killer, team)\n"
    "      og.mode_set(48, 31337)\n"
    "    end })\n"
    "    og.mode_set(47, og.entity_id(ent))\n"
    "  end,\n"
    "})\n";

struct AdoptHookProbeScript
{
    AdoptHookProbeScript()
    {
        og::script::register_pack_script(
            {"test.staged", "zz_adopt_hook_probe.lua", kAdoptHookProbeLua});
    }
    ~AdoptHookProbeScript()
    {
        og::script::register_pack_script(
            {"test.staged", "zz_adopt_hook_probe.lua", ""});
    }
};

}  // namespace

// Staged-lobby review finding: the staged on_load's DYNAMIC VM state —
// per-entity hooks registered with og.set_entity_hooks (the court.lua boss
// shape), module-local writes — lives in the stage world's WorldScripts VM
// and nowhere else. The SDL shadow adopts by content transfer
// (replace_loaded_world_state), which used to leave the destination to
// lazily build a FRESH VM with an empty entity-hook table, while the claimed
// on_load latch (correctly) prevented re-registration — so killing the
// hooked entity dispatched nothing and the level's scripted victory logic
// silently never fired. Adoption must MOVE the staged VM with the content;
// the VM binds its world through the thread-local context at dispatch time,
// so the moved VM serves the adopted world.
TEST_F(MatchStageTest, adoption_carries_on_load_entity_hooks_into_the_vm)
{
    // Mount FIRST: pack (re)installation clears every registered script
    // (packs.cpp clear_pack_scripts), so the probe must register after it.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    AdoptHookProbeScript probe;

    og::server::MatchStage stage({.networked = false});
    og::server::MatchStageInputs inputs;
    inputs.equivalent.current_campaign = "gladiator";
    inputs.equivalent.scen_num = 1;
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

    // The probe's on_load ran at stage time: target banked, hook armed but
    // not yet fired.
    EXPECT_EQ(1, staged_world->id);
    ASSERT_TRUE(staged_world->scripts().host().errors().empty())
        << staged_world->scripts().host().errors().back().message;
    const std::int32_t target_id = staged_world->mode.vars[47];
    ASSERT_NE(0, target_id) << "the on_load probe banked its target id";
    ASSERT_EQ(0, staged_world->mode.vars[48]);

    // The SDL shadow's adoption shape (the byte-identity test's fixture).
    LevelRuntimeData dst_level(1, /*headless=*/true,
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
    dst.tick_count_ = 0;
    dst.reset_level_progress();
    ASSERT_TRUE(og::server::adopt_staged_world(dst_level, dst_save, stage));
    dst_events.append(stage.take_events());
    stage.dispose();

    // The adopted world must NOT re-run on_load (the latch is claimed) —
    // the registration made at stage time is the only copy in existence.
    EXPECT_FALSE(dst.owes_level_on_load());
    EXPECT_EQ(target_id, dst.mode.vars[47]);
    EXPECT_EQ(0, dst.mode.vars[48]);

    // Kill the hooked entity IN THE ADOPTED WORLD: the staged on_death must
    // dispatch (per-entity hooks live in the VM registry, keyed by the
    // entity ids the content transfer preserved).
    walker* const target =
        dst.find_by_id(static_cast<std::uint32_t>(target_id));
    ASSERT_NE(nullptr, target);
    target->set_dead(1);
    target->death();
    EXPECT_TRUE(dst.scripts().host().errors().empty())
        << (dst.scripts().host().errors().empty()
                ? std::string()
                : dst.scripts().host().errors().back().message);
    current_game = previous_context;

    EXPECT_EQ(31337, dst.mode.vars[48])
        << "the staged og.set_entity_hooks on_death must dispatch in the "
           "adopted world (the VM moves with the content)";
}

// Staged-lobby review finding: the change key omitted the campaign-state
// inputs a reactive level's on_load reads live (og.campaign_var — the #206
// decision book). A mid-lobby og.campaign_state_set write (a base-camp zone
// action, persisted into the live host save through
// company_autosave_after_mutation) left the owner's key bytes unchanged, so
// the stage stayed clean-by-key and GO adopted the PRE-decision world.
// observe_inputs now stamps a host-save digest into the key every poll, and
// ensure_current re-checks it at GO — the Westlands watch_paid shape both
// ways.
TEST_F(MatchStageTest, mid_lobby_campaign_state_write_restages_at_go)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("westlands"));

    SaveData host_save;
    host_save.current_campaign = "westlands";
    host_save.scen_num = 15;

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

    // The debt is UNPAID when the lobby opens: no Watch in the stage.
    stage.observe_inputs(inputs, /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    ASSERT_EQ(nullptr, find_living_named(*stage.world(), "Wall-Warden"));
    const std::uint32_t unpaid_generation = stage.stage_generation();

    // Mid-lobby, the base camp pays the Watch — a write into the LIVE host
    // save, with every lobby knob/roster byte unchanged.
    ASSERT_TRUE(host_save.campaign_state_set("westlands", "watch_paid", 900));

    // The next owner poll (identical owner key) must dirty the stage, and
    // the debounced restage must reflect the decision.
    stage.observe_inputs(inputs, /*now_ms=*/1'000);
    stage.maintain(/*now_ms=*/1'000 + og::server::kStageDebounceMs);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    EXPECT_NE(unpaid_generation, stage.stage_generation())
        << "a campaign-state write must move the change key";
    EXPECT_NE(nullptr, find_living_named(*stage.world(), "Wall-Warden"))
        << "the restaged world must muster the paid Watch";

    // Revoke the payment right before GO — no poll in between. GO's
    // ensure_current must catch the moved digest and restage synchronously;
    // the ADOPTED world reflects the latest decision.
    ASSERT_TRUE(host_save.campaign_state_set("westlands", "watch_paid", 0));
    ASSERT_TRUE(stage.ensure_current(/*now_ms=*/10'000));

    LevelRuntimeData dst_level(15, /*headless=*/true,
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
    dst.tick_count_ = 0;
    dst.reset_level_progress();
    ASSERT_TRUE(og::server::adopt_staged_world(dst_level, dst_save, stage));
    dst_events.append(stage.take_events());
    stage.dispose();
    current_game = previous_context;

    EXPECT_EQ(nullptr, find_living_named(dst, "Wall-Warden"))
        << "GO must adopt the post-decision world, never the stale stage";
    EXPECT_EQ(0, dst_save.campaign_state_get("westlands", "watch_paid"))
        << "the adopted session save carries the latest decision book";
}

// C8: object handoff (the dedicated server / curses sessions). take() moves
// the staged LevelRuntimeData and the tick-1-stamped announcements out; the
// staged world OBJECT is the live server world. The stage is left Empty and
// a second take yields nothing.
TEST_F(MatchStageTest, take_hands_over_the_staged_level_and_events_once)
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
    inputs.equivalent = og::server::build_local_save_equivalent(host_save);
    inputs.equivalent.numplayers = 1;
    inputs.equivalent.team_list = {
        make_slot(0u, 100, "Host", FAMILY_SOLDIER, 0),
    };
    inputs.difficulty = 1;
    inputs.match_seed = 7u;
    stage.observe_inputs(inputs, /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    const GameWorld* const staged_world = stage.world();
    ASSERT_NE(nullptr, staged_world);

    // The caller's contract: copy the save out FIRST, then take.
    SaveData session_save;
    og::server::copy_headless_server_save_data(session_save,
                                               stage.staged_save());
    EXPECT_EQ(15, session_save.scen_num);

    og::server::MatchStage::TakenStage taken = stage.take();
    ASSERT_NE(nullptr, taken.level);
    EXPECT_EQ(staged_world, &taken.level->world())
        << "handoff moves the OBJECT — no rebuild, no snapshot transfer";
    EXPECT_FALSE(taken.level->world().oblist.empty());
    EXPECT_FALSE(taken.level->world().owes_level_on_load())
        << "the staged world ran on_load; the object carries the real latch";
    int ledger_lines = 0;
    for (const og::sim::Event& event : taken.events)
    {
        if (event.kind == og::sim::EventKind::Notification &&
            event.text == "The Watch remembers its wages.")
        {
            ++ledger_lines;
            EXPECT_EQ(1u, event.tick);
        }
    }
    EXPECT_EQ(1, ledger_lines);

    EXPECT_EQ(og::server::StageStatus::Empty, stage.status());
    EXPECT_EQ(nullptr, stage.world());
    og::server::MatchStage::TakenStage second = stage.take();
    EXPECT_EQ(nullptr, second.level);
    EXPECT_TRUE(second.events.empty());
}

// The staged pipeline is the LOCAL sessions' assembly filter too: a local
// equivalent keeps benched members (build_local_save_equivalent), the apply
// preserves their deploy flags, and spawn skips them — a benched fighter
// must never march into the staged world (the force-deploy regression).
TEST_F(MatchStageTest, local_equivalent_keeps_benched_members_out_of_the_world)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    SaveData company_save;
    company_save.current_campaign = "gladiator";
    company_save.scen_num = 1;
    company_save.numplayers = 1;
    company_save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    company_save.team_list[0]->id = 100;
    company_save.team_list[0]->name = "Marching";
    company_save.team_list[0]->level = 3;
    company_save.team_list[0]->deployed = true;
    company_save.team_list[1] = std::make_unique<guy>(FAMILY_ARCHER);
    company_save.team_list[1]->id = 200;
    company_save.team_list[1]->name = "Benched";
    company_save.team_list[1]->level = 3;
    company_save.team_list[1]->deployed = false;
    company_save.team_size = 2;

    og::server::MatchStageInputs inputs;
    inputs.equivalent = og::server::build_local_save_equivalent(company_save);
    inputs.difficulty = 1;
    inputs.match_seed = 11u;
    ASSERT_EQ(2u, inputs.equivalent.team_list.size())
        << "the local equivalent keeps the benched slot";
    EXPECT_FALSE(inputs.equivalent.team_list[1].deployed);

    og::server::MatchStage stage({
        .networked = false,
        .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
        .host_company_save = &company_save,
    });
    stage.observe_inputs(inputs, /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());

    const auto find_guy_walker = [](const GameWorld& world,
                                    const std::string& name) -> const walker* {
        for (const auto& entry : world.oblist)
        {
            const walker* const entity = entry.get();
            if (entity != nullptr && entity->myguy != nullptr &&
                entity->myguy->name == name)
                return entity;
        }
        return nullptr;
    };
    EXPECT_NE(nullptr, find_guy_walker(*stage.world(), "Marching"));
    EXPECT_EQ(nullptr, find_guy_walker(*stage.world(), "Benched"))
        << "a benched member must never spawn into the staged world";
    // ...but STAYS in the staged save (the local seed rule): the session
    // copy keeps the whole company.
    ASSERT_NE(nullptr, stage.staged_save().team_list[1]);
    EXPECT_FALSE(stage.staged_save().team_list[1]->deployed);
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

// --- C9: the joiner's staged preview mirror ---------------------------------

og::sim::StagedMatchSetupMessage make_setup_message(
    const og::server::MatchStage& stage)
{
    return {.stage_generation = stage.stage_generation(),
            .setup_bytes = stage.staged_setup_bytes()};
}

og::sim::StagedMatchKeyframeMessage make_keyframe_message(
    const og::server::MatchStage& stage)
{
    return {.stage_generation = stage.stage_generation(),
            .snapshot_bytes = stage.staged_keyframe_bytes()};
}

// The networked-exactness oracle at the byte level: a mirror healed from the
// owner's broadcast pair re-serializes the IDENTICAL tick-0 keyframe — the
// joiner's preview world IS the host's staged world, not a re-derivation.
TEST_F(MatchStageTest, mirror_heals_byte_identical_from_the_staged_pair)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    ASSERT_NE(nullptr, stage.world());

    og::server::StagedPreviewMirror mirror;
    EXPECT_EQ(og::server::MirrorStatus::Empty, mirror.status());
    mirror.receive_setup(make_setup_message(stage));
    mirror.receive_keyframe(make_keyframe_message(stage));
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/0);

    ASSERT_EQ(og::server::MirrorStatus::Staged, mirror.status());
    ASSERT_NE(nullptr, mirror.world());
    EXPECT_EQ(stage.stage_generation(), mirror.applied_generation());
    EXPECT_EQ(1u, mirror.refresh_serial());

    GameWorld& mirrored = *mirror.world();
    EXPECT_EQ(stage.world()->id, mirrored.id);
    EXPECT_EQ(stage.world()->title, mirrored.title);
    EXPECT_EQ(0u, mirrored.tick_count_) << "mirrors never tick";
    EXPECT_TRUE(mirrored.mode.active)
        << "the replicated mode block carries the staged init";
    EXPECT_EQ(stage.world()->mode.vars[6], mirrored.mode.vars[6])
        << "the latched squad code reaches the mirror (#235)";
    EXPECT_EQ(og::sim::serialize_snapshot(
                  og::sim::peek_keyframe_snapshot(mirrored)),
              stage.staged_keyframe_bytes())
        << "the mirror world must re-serialize byte-identical to the pair";

    // Late-open retention: the same wire bytes stay readable for a preview
    // pane opened long after the broadcast.
    std::uint32_t retained_generation = 0;
    const std::vector<std::uint8_t>* retained_setup = nullptr;
    const std::vector<std::uint8_t>* retained_keyframe = nullptr;
    ASSERT_TRUE(mirror.retained_pair(retained_generation, retained_setup,
                                     retained_keyframe));
    EXPECT_EQ(stage.stage_generation(), retained_generation);
    ASSERT_NE(nullptr, retained_setup);
    ASSERT_NE(nullptr, retained_keyframe);
    EXPECT_EQ(stage.staged_setup_bytes(), *retained_setup);
    EXPECT_EQ(stage.staged_keyframe_bytes(), *retained_keyframe);
}

// Generation pairing: a keyframe that does not match the last received
// setup's generation is dropped; a setup for a NEW generation invalidates the
// previous pair's keyframe while the last APPLIED world keeps presenting
// until the fresh pair completes (no flicker to Empty mid-restage).
TEST_F(MatchStageTest, mirror_drops_generation_mismatched_keyframes)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    ASSERT_EQ(1u, stage.stage_generation());

    og::server::StagedPreviewMirror mirror;
    mirror.receive_setup(make_setup_message(stage));

    // A stale keyframe (previous generation) must not pair with this setup.
    og::sim::StagedMatchKeyframeMessage stale = make_keyframe_message(stage);
    stale.stage_generation = 0u;
    mirror.receive_keyframe(stale);
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/0);
    EXPECT_EQ(og::server::MirrorStatus::Empty, mirror.status());
    EXPECT_EQ(nullptr, mirror.world());
    EXPECT_EQ(0u, mirror.refresh_serial())
        << "an incomplete pair is not an apply attempt";

    // The matching keyframe completes the pair.
    mirror.receive_keyframe(make_keyframe_message(stage));
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/0);
    ASSERT_EQ(og::server::MirrorStatus::Staged, mirror.status());
    EXPECT_EQ(1u, mirror.applied_generation());

    // A restage's setup (generation 2) arrives; its keyframe is still in
    // flight. The applied generation-1 world keeps presenting, and a stale
    // generation-1 keyframe re-delivery cannot pair with the new setup.
    og::server::MatchStageInputs moved = make_modes_inputs(1001u);
    moved.equivalent.team_list.push_back(
        make_slot(1u, 200, "Winger", FAMILY_ELF, 1));
    stage.observe_inputs(moved, /*now_ms=*/1'000);
    stage.maintain(/*now_ms=*/1'000 + og::server::kStageDebounceMs);
    ASSERT_EQ(2u, stage.stage_generation());

    mirror.receive_setup(make_setup_message(stage));
    og::sim::StagedMatchKeyframeMessage old_generation =
        make_keyframe_message(stage);
    old_generation.stage_generation = 1u;
    mirror.receive_keyframe(old_generation);
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/2'000);
    EXPECT_EQ(og::server::MirrorStatus::Staged, mirror.status());
    EXPECT_EQ(1u, mirror.applied_generation())
        << "the generation-1 world presents until the fresh pair completes";

    mirror.receive_keyframe(make_keyframe_message(stage));
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/2'000);
    ASSERT_EQ(og::server::MirrorStatus::Staged, mirror.status());
    EXPECT_EQ(2u, mirror.applied_generation());
    ASSERT_NE(nullptr, mirror.world());
    EXPECT_EQ(og::sim::serialize_snapshot(
                  og::sim::peek_keyframe_snapshot(*mirror.world())),
              stage.staged_keyframe_bytes());
}

// C10: the staged report carries the COMBINED lobby roster with no
// marshaling — both slots' companies are spawned walkers in the staged
// world, so the census labels both teams COMPANY with exact counts (the
// old lobby_roster_team_counts marshaling is gone with the plan arm).
TEST_F(MatchStageTest, staged_report_carries_the_combined_roster)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    og::server::MatchStageInputs inputs = make_modes_inputs(1001u);
    inputs.equivalent.team_list.push_back(
        make_slot(1u, 200, "Winger", FAMILY_ELF, 1));
    inputs.equivalent.team_list.push_back(
        make_slot(2u, 201, "Keeper", FAMILY_ARCHER, 1));
    stage.observe_inputs(inputs, /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());

    SaveData display_save;
    display_save.my_team = 0;
    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(
            stage.world(), og::ui::StagePreviewStatus::Staged, display_save,
            nullptr);
    EXPECT_TRUE(report.staged);
    EXPECT_EQ("SOCCER", report.mode_name);
    EXPECT_EQ(og::ui::ScenarioFill::Company, report.team_fill[0]);
    EXPECT_EQ(1, report.team_fill_count[0]);
    EXPECT_EQ(og::ui::ScenarioFill::Company, report.team_fill[1]);
    EXPECT_EQ(2, report.team_fill_count[1])
        << "a remote seat's two deployed fighters census exactly";

    // The row loop groups company fighters by (team, family, level) — the
    // shared roster rows every client renders identically.
    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    bool soldier_row = false;
    bool elf_row = false;
    bool archer_row = false;
    for (const std::string& line : lines)
    {
        soldier_row = soldier_row || line == "  1x SOLDIER Lv 3";
        elf_row = elf_row || line == "  1x ELF Lv 3";
        archer_row = archer_row || line == "  1x ARCHER Lv 3";
    }
    EXPECT_TRUE(soldier_row) << "the host company rides the staged rows";
    EXPECT_TRUE(elf_row) << "the joiner company rides the staged rows";
    EXPECT_TRUE(archer_row);
}

// C10: the networked-exactness oracle at the LINE level — a mirror healed
// from the owner's broadcast pair builds the IDENTICAL report, line for
// line, so the host pane and every joiner pane say the same thing.
TEST_F(MatchStageTest, mirror_report_equals_host_report_line_for_line)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());

    og::server::StagedPreviewMirror mirror;
    mirror.receive_setup(make_setup_message(stage));
    mirror.receive_keyframe(make_keyframe_message(stage));
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/0);
    ASSERT_EQ(og::server::MirrorStatus::Staged, mirror.status());

    SaveData display_save;
    const std::vector<std::string> host_lines =
        og::ui::format_scenario_report_lines(
            og::ui::build_scenario_roster_report(
                stage.world(), og::ui::StagePreviewStatus::Staged,
                display_save, nullptr));
    const std::vector<std::string> mirror_lines =
        og::ui::format_scenario_report_lines(
            og::ui::build_scenario_roster_report(
                mirror.world(), og::ui::StagePreviewStatus::Staged,
                display_save, nullptr));
    ASSERT_FALSE(host_lines.empty());
    EXPECT_TRUE(std::find(host_lines.begin(), host_lines.end(),
                          "  RED TEAM  ACTIVE - COMPANY (1)") !=
                host_lines.end())
        << "the host report reads the staged census";
    EXPECT_EQ(host_lines, mirror_lines)
        << "host pane and joiner pane must say the identical thing";
}

// Apply-failure honesty: an undecodable pair reports Unavailable (never a
// stale or half-applied world), a level this machine cannot load locally
// reports Unavailable, and the campaign catching up retries the RETAINED
// pair immediately — the owner never has to resend.
TEST_F(MatchStageTest, mirror_apply_failure_is_honest_and_retries)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());

    og::server::StagedPreviewMirror mirror;

    // Undecodable setup bytes: honest Unavailable, refresh serial moves so a
    // keyed preview repaints into the degradation line.
    testing::internal::CaptureStderr();
    mirror.receive_setup({.stage_generation = stage.stage_generation(),
                          .setup_bytes = {0x01u, 0x02u, 0x03u}});
    mirror.receive_keyframe(make_keyframe_message(stage));
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/0);
    (void)testing::internal::GetCapturedStderr();
    EXPECT_EQ(og::server::MirrorStatus::Unavailable, mirror.status());
    EXPECT_EQ("staged setup decode failed", mirror.error());
    EXPECT_EQ(nullptr, mirror.world());
    EXPECT_EQ(1u, mirror.refresh_serial());

    // The good pair is a fresh apply attempt for the same generation (the
    // owner's catch-up resend shape): receive_setup for the same generation
    // re-opens the pair.
    mirror.receive_setup(make_setup_message(stage));
    mirror.receive_keyframe(make_keyframe_message(stage));
    // The failed attempt recorded campaign "modes" at now_ms=0; the good
    // bytes re-arm the attempt directly (attempted_pending_ cleared).
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/1);
    ASSERT_EQ(og::server::MirrorStatus::Staged, mirror.status());
    EXPECT_EQ(2u, mirror.refresh_serial());

    // A pair whose level this client cannot load locally (wrong campaign
    // mounted/synced): honest Unavailable — the first-level fallback must
    // not masquerade as the staged level — then the campaign catch-up
    // retries the retained pair immediately and heals.
    og::server::StagedPreviewMirror raced;
    raced.receive_setup(make_setup_message(stage));
    raced.receive_keyframe(make_keyframe_message(stage));
    testing::internal::CaptureStderr();
    raced.ensure_applied("gladiator", /*difficulty=*/1, /*now_ms=*/0);
    (void)testing::internal::GetCapturedStderr();
    EXPECT_EQ(og::server::MirrorStatus::Unavailable, raced.status());
    EXPECT_EQ("staged preview level load failed", raced.error());
    EXPECT_EQ(nullptr, raced.world());

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    raced.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/1);
    ASSERT_EQ(og::server::MirrorStatus::Staged, raced.status());
    ASSERT_NE(nullptr, raced.world());
    EXPECT_EQ(og::sim::serialize_snapshot(
                  og::sim::peek_keyframe_snapshot(*raced.world())),
              stage.staged_keyframe_bytes());
    EXPECT_EQ(2u, raced.refresh_serial())
        << "one failed attempt + one campaign-moved retry";
}

// Wire hardening: an empty setup payload and a keyframe with no setup are
// both refused at reception — nothing is retained, no apply is attempted,
// and retained_pair keeps reporting "no pair" without touching its outputs.
TEST_F(MatchStageTest, mirror_rejects_empty_setup_and_setupless_keyframe)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());

    og::server::StagedPreviewMirror mirror;

    std::uint32_t generation = 77u;
    const std::vector<std::uint8_t>* setup_bytes = nullptr;
    const std::vector<std::uint8_t>* keyframe_bytes = nullptr;
    EXPECT_FALSE(mirror.retained_pair(generation, setup_bytes,
                                      keyframe_bytes));
    EXPECT_EQ(77u, generation) << "a refused pair writes none of the outputs";
    EXPECT_EQ(nullptr, setup_bytes);
    EXPECT_EQ(nullptr, keyframe_bytes);

    // A keyframe with NO setup received yet cannot pair — dropped.
    mirror.receive_keyframe(make_keyframe_message(stage));
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/0);
    EXPECT_EQ(og::server::MirrorStatus::Empty, mirror.status());
    EXPECT_EQ(0u, mirror.refresh_serial())
        << "a setupless keyframe is not an apply attempt";

    // An empty setup payload is refused outright, so the keyframe that
    // follows it still has nothing to pair with.
    og::sim::StagedMatchSetupMessage empty_setup = make_setup_message(stage);
    empty_setup.setup_bytes.clear();
    mirror.receive_setup(empty_setup);
    mirror.receive_keyframe(make_keyframe_message(stage));
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/0);
    EXPECT_EQ(og::server::MirrorStatus::Empty, mirror.status());
    EXPECT_EQ(0u, mirror.refresh_serial());
    EXPECT_FALSE(mirror.retained_pair(generation, setup_bytes,
                                      keyframe_bytes));
}

// Corrupt keyframe bytes: the deserialize throw lands as the honest
// "staged keyframe decode failed" Unavailable, the timed retry gate holds
// inside kMirrorRetryMs on an unchanged campaign, the slow clock reopens
// exactly one retry, and a fresh delivery of the real bytes re-arms the
// apply and heals.
TEST_F(MatchStageTest, mirror_keyframe_decode_failure_gates_the_timed_retry)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());

    og::server::StagedPreviewMirror mirror;
    mirror.receive_setup(make_setup_message(stage));
    og::sim::StagedMatchKeyframeMessage corrupt = make_keyframe_message(stage);
    corrupt.snapshot_bytes.assign(64, std::uint8_t{0x5A});
    mirror.receive_keyframe(corrupt);

    testing::internal::CaptureStderr();
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/1'000);
    const std::string diagnostics = testing::internal::GetCapturedStderr();
    EXPECT_EQ(og::server::MirrorStatus::Unavailable, mirror.status());
    EXPECT_EQ("staged keyframe decode failed", mirror.error());
    EXPECT_EQ(nullptr, mirror.world());
    EXPECT_EQ(1u, mirror.refresh_serial());
    EXPECT_NE(std::string::npos,
              diagnostics.find("staged_preview_mirror_unavailable"));

    // Unchanged campaign inside the retry window: gated, no new attempt.
    mirror.ensure_applied("modes", /*difficulty=*/1,
                          /*now_ms=*/1'000 + og::server::kMirrorRetryMs - 1);
    EXPECT_EQ(1u, mirror.refresh_serial())
        << "the owner never resends on our account — the gate must hold";

    // The slow clock reopens exactly one retry of the same corrupt pair.
    testing::internal::CaptureStderr();
    mirror.ensure_applied("modes", /*difficulty=*/1,
                          /*now_ms=*/1'000 + og::server::kMirrorRetryMs);
    (void)testing::internal::GetCapturedStderr();
    EXPECT_EQ(2u, mirror.refresh_serial());
    EXPECT_EQ(og::server::MirrorStatus::Unavailable, mirror.status());

    // A fresh keyframe delivery with the REAL bytes re-arms and heals.
    mirror.receive_keyframe(make_keyframe_message(stage));
    mirror.ensure_applied("modes", /*difficulty=*/1,
                          /*now_ms=*/1'000 + og::server::kMirrorRetryMs + 1);
    ASSERT_EQ(og::server::MirrorStatus::Staged, mirror.status());
    ASSERT_NE(nullptr, mirror.world());
    EXPECT_EQ(3u, mirror.refresh_serial());
    EXPECT_EQ(stage.stage_generation(), mirror.applied_generation());
}

// The broadcast + per-peer catch-up duplicate: a re-delivery of the SAME
// generation-paired keyframe after a successful apply must not re-arm the
// apply — the refresh serial (the preview's re-render key) stays put.
TEST_F(MatchStageTest, mirror_ignores_duplicate_keyframe_redelivery)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());

    og::server::StagedPreviewMirror mirror;
    mirror.receive_setup(make_setup_message(stage));
    mirror.receive_keyframe(make_keyframe_message(stage));
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/0);
    ASSERT_EQ(og::server::MirrorStatus::Staged, mirror.status());
    ASSERT_EQ(1u, mirror.refresh_serial());

    mirror.receive_keyframe(make_keyframe_message(stage));
    mirror.ensure_applied("modes", /*difficulty=*/1, /*now_ms=*/10);
    EXPECT_EQ(og::server::MirrorStatus::Staged, mirror.status());
    EXPECT_EQ(1u, mirror.refresh_serial())
        << "a duplicate of the applied pair re-applies nothing";
    EXPECT_EQ(stage.stage_generation(), mirror.applied_generation());
}

// The u16 wire cap, refused legibly: a host company save whose completed-
// levels ledger inflates the staged InitialSetup past the inner-message cap
// fails the stage with the size-refusal error (GO denied through
// ensure_current), and shrinking the ledger restages clean through the GO
// re-check — the refusal is a generation like any other.
TEST_F(MatchStageTest, oversize_staged_setup_fails_legibly_and_recovers)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    SaveData host_save;
    std::set<int>& ledger = host_save.completed_levels["modes"];
    for (int level = 100'000; level < 117'000; ++level)
        ledger.insert(level);

    og::server::MatchStage stage({.networked = true,
                                  .host_company_save = &host_save});
    testing::internal::CaptureStderr();
    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/0);
    const std::string diagnostics = testing::internal::GetCapturedStderr();

    EXPECT_EQ(og::server::StageStatus::Failed, stage.status());
    EXPECT_EQ("staged world exceeds the wire message size cap", stage.error());
    EXPECT_EQ(nullptr, stage.world());
    EXPECT_TRUE(stage.staged_setup_bytes().empty());
    EXPECT_TRUE(stage.staged_keyframe_bytes().empty());
    EXPECT_EQ(1u, stage.stage_generation())
        << "a size refusal is a generation too — the preview must notice";
    EXPECT_FALSE(stage.ensure_current(/*now_ms=*/1))
        << "GO must be refused while the stage is Failed";
    EXPECT_NE(std::string::npos, diagnostics.find("match_stage_failed"));

    // Shrinking the ledger moves the host-save digest; the GO re-check
    // restages synchronously and the same inputs now fit the wire.
    ledger.clear();
    EXPECT_TRUE(stage.ensure_current(/*now_ms=*/2));
    EXPECT_EQ(og::server::StageStatus::Staged, stage.status());
    ASSERT_NE(nullptr, stage.world());
    EXPECT_TRUE(stage.error().empty());
    EXPECT_EQ(2u, stage.stage_generation());
    EXPECT_FALSE(stage.staged_setup_bytes().empty());
    EXPECT_FALSE(stage.staged_keyframe_bytes().empty());
}

// Adoption refuses an unstaged stage: Empty and Failed stages hand nothing
// over, and the destination world is untouched (the caller falls back to
// the legacy display-seed path by contract).
TEST_F(MatchStageTest, adoption_refuses_an_unstaged_stage)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    LevelRuntimeData dst_level(820, /*headless=*/true,
                               &headless_level_data_hooks());
    SaveData dst_save;
    const std::int32_t untouched_id = dst_level.world().id;

    og::server::MatchStage empty_stage({.networked = true});
    ASSERT_EQ(og::server::StageStatus::Empty, empty_stage.status());
    EXPECT_FALSE(og::server::adopt_staged_world(dst_level, dst_save,
                                                empty_stage));

    og::server::MatchStage failed_stage({.networked = true});
    testing::internal::CaptureStderr();
    failed_stage.mark_failed("refused for the test");
    (void)testing::internal::GetCapturedStderr();
    ASSERT_EQ(og::server::StageStatus::Failed, failed_stage.status());
    EXPECT_FALSE(og::server::adopt_staged_world(dst_level, dst_save,
                                                failed_stage));
    EXPECT_EQ(untouched_id, dst_level.world().id)
        << "a refused adoption must not touch the destination world";
}

// GameWorld::adopt_scripts_from self-adoption guard: adopting a world's own
// VM is a no-op — the scripts stay live and the staged keyframe still
// re-serializes byte-identical afterwards.
TEST_F(MatchStageTest, self_script_adoption_is_a_no_op)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    stage.observe_inputs(make_modes_inputs(1001u), /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    GameWorld* const staged_world = stage.world();
    ASSERT_NE(nullptr, staged_world);
    const std::vector<std::uint8_t> before = staged_keyframe_bytes(stage);

    staged_world->adopt_scripts_from(*staged_world);
    EXPECT_EQ(before, staged_keyframe_bytes(stage))
        << "self-adoption must not move the VM out from under the world";
}

} // namespace
