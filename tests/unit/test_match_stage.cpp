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
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/match_stage.h>

#include <algorithm>
#include <cstdint>
#include <string>
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

} // namespace
