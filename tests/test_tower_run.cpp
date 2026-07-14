/* Tower Climb end-to-end integration locks (tower-triple spec §6, WP-6).
 *
 * Drives the real seams a tower run crosses, in run order:
 *   - campaign.yaml `mode: tower` dispatch (mount -> current_progression()),
 *   - GO-time prepare_launch (fresh run: seed drawn, floor 1 provisioned;
 *     resume: heal + prefetch — D8),
 *   - the shared win fold via screen::endgame AND via the shadow's
 *     finalize_level_and_advance_cursor — the FIRST finalizer in local play —
 *     for the Gate win and a floor win (cursor/best advance, next floor
 *     provisioned, CTF rematch gate),
 *   - every non-win exit shape routing on_run_ended (per-invocation D10
 *     field-merge save0 write; the mirrored server+display pair CONVERGES on
 *     identical disk bytes rather than writing exactly once globally), across
 *     all five routing sites: screen::endgame, the shadow withdraw finalize,
 *     Esc-abort (local_transport_shadow_abort_level), and the curses loss
 *     branch (og_test_curses); the fifth site, the headless withdraw, is
 *     exercised under Classic in test_headless_server_runtime — a tower-mode
 *     dedicated server is unreachable by design (networked gating),
 *   - the resume heal regenerating a deleted floor byte-identically (§5.4),
 *   - results surfaces: mode popups per shape + retry suppression under the
 *     TESTING results path (§2.7),
 *   - LobbyServer::sanitize_settings rejecting the tower campaign for
 *     networked sessions (§5.9 backstop layer 3).
 *
 * Mount hygiene (§1.10): every test runs under the fixture, which backs up
 * save0, prunes generated floors, and remounts org.openglad.gladiator in
 * teardown (SaveData::load mounts the SAVED campaign — the --gtest_shuffle
 * trap).
 */
#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <openglad/core/constants.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/input.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/core/tower_constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/resources/game_mode.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/save_data.h>

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Test-only dialog queue (defined in picker_dialogs.cpp under TESTING).
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);

// Gameplay bootstrap (glad_gameplay.cpp): loads save0 + its level and installs
// the authoritative local transport shadow — the state Esc-abort routes on.
void glad_init(bool preserve_frame_timing = false);

namespace {

namespace fs = std::filesystem;

const std::string kTowerId{og::kTowerCampaignId};
constexpr short kGate = static_cast<short>(og::kTowerGateLevel);

void prune_all_floors()
{
    for (int id = og::kTowerFirstFloorLevel; id <= 760; ++id)
        (void)og::data::delete_tower_floor_files(id);
}

fs::path save0_path()
{
    return fs::path(get_user_path()) / "save" / "save0.gtl";
}

std::vector<char> read_floor_fss(int id)
{
    std::ifstream in(fs::path(get_user_path()) / "scen" /
                         std::format("scen{}.fss", id),
                     std::ios::binary);
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}

screen& scr()
{
    return *og::runtime::current_session->myscreen_;
}

class TowerRunE2E : public ::testing::Test
{
protected:
    void SetUp() override
    {
        trace_clear();
        restore_default_campaigns();
        prune_all_floors();

        // Preserve whatever save0 an earlier test (or the developer) left.
        std::error_code ec;
        had_save0_ = fs::exists(save0_path(), ec);
        if (had_save0_)
            fs::copy_file(save0_path(), save0_path().string() + ".towerbak",
                          fs::copy_options::overwrite_existing, ec);

        (void)unmount_campaign_package_with_error(get_mounted_campaign());
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error(kTowerId));
    }

    void TearDown() override
    {
        prune_all_floors();
        std::error_code ec;
        if (had_save0_)
        {
            fs::copy_file(save0_path().string() + ".towerbak", save0_path(),
                          fs::copy_options::overwrite_existing, ec);
            fs::remove(save0_path().string() + ".towerbak", ec);
        }
        else
        {
            fs::remove(save0_path(), ec);
        }

        // Reset the shared session save so no tower state leaks to the next
        // test, then heal the mount (§1.10).
        scr().save_data.reset();
        scr().save_data.current_campaign = "org.openglad.gladiator";
        scr().save_data.scen_num = 1;
        (void)unmount_campaign_package_with_error(get_mounted_campaign());
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("org.openglad.gladiator"));
        scr().sync_world_from_save_data();
        scr().world().end = 0;
        trace_clear();
    }

    // Arm the shared screen as a tower session sitting on `level` with the
    // world mirroring it (the state screen::endgame reads).
    static void arm_session(short level, std::uint32_t seed, short best)
    {
        SaveData& save = scr().save_data;
        save.reset();
        save.current_campaign = kTowerId;
        save.scen_num = level;
        save.current_levels[kTowerId] = level;
        save.tower_run_seed = seed;
        save.tower_best_floor = best;
        for (auto& score : save.m_score)
            score = 0;

        GameWorld& world = scr().world();
        world.end = 0;
        world.retry = false;
        world.id = level;
        world.current_scenario = level;
        world.completed_levels.clear();
        world.time_bonus_limit = 0; // deterministic zero time bonus
        for (auto& score : world.m_score)
            score = 0;
    }

    bool had_save0_ = false;
};

// --- The headline run: fresh GO -> Gate win -> floor win -> loss -> heal. ---

TEST_F(TowerRunE2E, full_run_gate_win_floor_win_loss_and_resume_heal)
{
    // The campaign.yaml `mode: tower` key must reach the dispatch switch.
    ASSERT_EQ(og::mode::ProgressionKind::Tower,
              og::mode::current_progression().kind())
        << "mounting the tower package must dispatch TowerProgression";

    // GO #1 at the Gate = fresh run: a seed is drawn and floor 1 exists
    // (D8 prefetch — the Gate's exit prompt reads 701's title).
    arm_session(kGate, 0xDEADBEEFu, 0);
    SaveData& save = scr().save_data;
    ASSERT_TRUE(og::mode::current_progression().prepare_launch(
        save, /*networked_session=*/false));
    EXPECT_NE(0xDEADBEEFu, save.tower_run_seed) << "fresh run draws a seed";
    ASSERT_TRUE(og::data::tower_floor_files_exist(og::kTowerFirstFloorLevel));
    EXPECT_EQ(kGate, save.scen_num) << "prepare_launch never moves the cursor";
    const std::uint32_t run_seed = save.tower_run_seed;

    // Gate win via the finalize fold (screen::endgame drives the shared
    // fold + the results dispatch).
    scr().world().id = kGate;
    scr().world().current_scenario = kGate;
    trace_clear();
    (void)scr().endgame(0, static_cast<short>(og::kTowerFirstFloorLevel));
    EXPECT_EQ(static_cast<short>(og::kTowerFirstFloorLevel), save.scen_num);
    EXPECT_EQ(1, save.tower_best_floor) << "floors climbed = highest REACHED";
    EXPECT_EQ(og::kTowerFirstFloorLevel, save.current_levels[kTowerId]);
    EXPECT_TRUE(trace_contains("popup", "THE TOWER OPENS"))
        << "Gate win shows the mode popup, not the generic Victory!";
    EXPECT_TRUE(trace_contains("save", "save0"))
        << "D10: the floor-win checkpoint autosave still runs";
    EXPECT_FALSE(scr().world().retry)
        << "tower never offers RETRY (TESTING results path returns none)";
    EXPECT_EQ(run_seed, save.tower_run_seed) << "wins keep the run seed";

    // GO #2 (resume shape): floor 2 is prefetched at the NEXT GO.
    scr().world().end = 0;
    ASSERT_TRUE(og::mode::current_progression().prepare_launch(
        save, /*networked_session=*/false));
    EXPECT_EQ(run_seed, save.tower_run_seed) << "resume keeps the seed";
    ASSERT_TRUE(
        og::data::tower_floor_files_exist(og::kTowerFirstFloorLevel + 1));

    // Floor 1 win: cursor/best advance and 702 stays provisioned.
    scr().world().id = og::kTowerFirstFloorLevel;
    scr().world().current_scenario = og::kTowerFirstFloorLevel;
    trace_clear();
    (void)scr().endgame(0, static_cast<short>(og::kTowerFirstFloorLevel + 1));
    EXPECT_EQ(static_cast<short>(og::kTowerFirstFloorLevel + 1), save.scen_num);
    EXPECT_EQ(2, save.tower_best_floor);
    EXPECT_TRUE(trace_contains("popup", "FLOOR CLEARED"));
    EXPECT_TRUE(trace_contains("popup", "Floor 1 conquered."));
    EXPECT_TRUE(
        og::data::tower_floor_files_exist(og::kTowerFirstFloorLevel + 1));

    // Loss on floor 2 (team wipe): on_run_ended resets the cursor to the
    // Gate, retains best, and performs exactly ONE save0 write.
    scr().world().end = 0;
    scr().world().id = og::kTowerFirstFloorLevel + 1;
    scr().world().current_scenario = og::kTowerFirstFloorLevel + 1;
    trace_clear();
    (void)scr().endgame(1, -1);
    EXPECT_EQ(kGate, save.scen_num) << "run over: cursor back to the Gate";
    EXPECT_EQ(2, save.tower_best_floor) << "best survives the wipe";
    EXPECT_EQ(run_seed, save.tower_run_seed) << "seed survives (shareable)";
    EXPECT_EQ(2, trace_count("save"))
        << "exactly ONE SaveData::save call (2 trace lines per write) — the "
           "enumerated D10 exception";
    EXPECT_TRUE(trace_contains("popup", "THE TOWER CLAIMS YOU"));
    EXPECT_TRUE(trace_contains("popup", "You fell on Floor 2."));
    EXPECT_TRUE(trace_contains("popup", "Best climb: 2."));
    EXPECT_FALSE(scr().world().retry) << "retry suppressed on the loss path";

    // The reset reached disk as a field-merge (best/seed kept).
    {
        SaveData disk;
        ASSERT_TRUE(disk.load("save0"));
        EXPECT_EQ(kGate, disk.scen_num);
        EXPECT_EQ(2, disk.tower_best_floor);
        EXPECT_EQ(run_seed, disk.tower_run_seed);
    }

    // Resume heal (Continue Game across restarts): a deleted floor
    // regenerates byte-identically from (seed, N) via the same
    // ensure_level_available call the campaign picker makes.
    const std::vector<char> before =
        read_floor_fss(og::kTowerFirstFloorLevel);
    ASSERT_FALSE(before.empty());
    prune_all_floors();
    save.scen_num = static_cast<short>(og::kTowerFirstFloorLevel);
    og::mode::current_progression().ensure_level_available(save);
    ASSERT_TRUE(og::data::tower_floor_files_exist(og::kTowerFirstFloorLevel));
    EXPECT_EQ(before, read_floor_fss(og::kTowerFirstFloorLevel))
        << "heal must regenerate the exact bytes (§5.4 determinism)";
}

// --- Non-win endgame shapes: on_run_ended EXACTLY once per exit. -----------

struct ExitShape
{
    const char* name;
    short ending;
    short next_level;
    const char* expected_popup;
};

TEST_F(TowerRunE2E, every_non_win_exit_shape_routes_on_run_ended_and_converges)
{
    // The display-route shapes converge on screen::endgame. Team wipe and
    // mission timeout produce the byte-identical (1, -1) shape by
    // construction, so one row carries both; the server-side quit/Esc-abort
    // sites (the shadow withdraw finalize and
    // local_transport_shadow_abort_level) are pinned by their own tests
    // below — here the display-side (1, dest) shape stands in for the
    // mirrored hook they pair with.
    const ExitShape shapes[] = {
        {"team_wipe_or_timeout", 1, -1, "THE TOWER CLAIMS YOU"},
        {"withdraw_quit", 1, kGate, "THE CLIMB ABANDONED"},
        {"protect_fail_save_all", 4, -1, "THE TOWER CLAIMS YOU"},
    };

    for (const ExitShape& shape : shapes)
    {
        SCOPED_TRACE(shape.name);
        // Mid-run on floor 3, best 3, with a save0 checkpoint on disk.
        arm_session(static_cast<short>(og::kTowerGateLevel + 3), 4242u, 3);
        ASSERT_TRUE(scr().save_data.save("save0"));
        trace_clear();

        (void)scr().endgame(shape.ending, shape.next_level);
        EXPECT_EQ(kGate, scr().save_data.scen_num);
        EXPECT_EQ(3, scr().save_data.tower_best_floor);
        EXPECT_EQ(2, trace_count("save"))
            << "one field-merge save0 write per routed run end (D10's write "
               "is per-invocation; 2 trace lines per write)";
        EXPECT_TRUE(trace_contains("popup", shape.expected_popup));
        EXPECT_FALSE(scr().world().retry);

        // The REAL local pair routes the hook on TWO mirrored SaveData
        // objects: first the authoritative server save, then the display
        // save that still holds the death floor. The contract is idempotent
        // CONVERGENCE (docs/game-modes.md), not global exactly-once: the
        // second routing performs its own field-merge write and the disk
        // state must land on the same bytes (Gate cursor, best/seed kept).
        // A display mirror still mid-level carries the death floor in its
        // WORLD too (endgame's entry sync re-derives scen_num from
        // world.current_scenario, which the first call's tail reset).
        scr().world().end = 0;
        scr().world().current_scenario =
            static_cast<short>(og::kTowerGateLevel + 3);
        scr().save_data.scen_num =
            static_cast<short>(og::kTowerGateLevel + 3); // stale display mirror
        (void)scr().endgame(shape.ending, shape.next_level);
        EXPECT_EQ(4, trace_count("save"))
            << "the mirrored display save routes its own converging write";
        EXPECT_EQ(kGate, scr().save_data.scen_num);
        EXPECT_EQ(3, scr().save_data.tower_best_floor);
        {
            SaveData disk;
            ASSERT_TRUE(disk.load("save0"));
            EXPECT_EQ(kGate, disk.scen_num) << "converged disk cursor";
            EXPECT_EQ(3, disk.tower_best_floor) << "best survives both writes";
            EXPECT_EQ(4242u, disk.tower_run_seed) << "seed survives both writes";
        }

        // Same-object re-fire (a stray double call on an already-reset
        // save): the Gate guard makes it a full no-op — no third write.
        scr().world().end = 0;
        (void)scr().endgame(shape.ending, shape.next_level);
        EXPECT_EQ(4, trace_count("save"))
            << "an already-reset save must not write save0 again";
        EXPECT_EQ(kGate, scr().save_data.scen_num);
        EXPECT_EQ(3, scr().save_data.tower_best_floor);
    }
}

TEST_F(TowerRunE2E, gate_loss_is_a_no_op_run_end_with_generic_popup)
{
    // Dying at (or Esc-aborting from) the authored Gate never ends a run:
    // no save0 write, no mode popup — the generic defeat shows instead.
    arm_session(kGate, 777u, 5);
    trace_clear();
    (void)scr().endgame(1, -1);
    EXPECT_EQ(kGate, scr().save_data.scen_num);
    EXPECT_EQ(5, scr().save_data.tower_best_floor);
    EXPECT_EQ(0, trace_count("save")) << "Gate loss persists nothing";
    EXPECT_TRUE(trace_contains("popup", "YOUR MEN ARE CRUSHED"));
    EXPECT_FALSE(trace_contains("popup", "THE TOWER CLAIMS YOU"));
}

// --- The shadow finalizers: the FIRST finalizer in every local/host flow. ---

TEST_F(TowerRunE2E, shadow_finalize_drives_gate_and_floor_wins)
{
    // WP-6 demands driving finalize_level_and_advance_cursor ITSELF for a
    // Gate win (cursor 701, best 1) and a floor win: in local play the
    // shadow's fold runs before screen::endgame's, so its fold context and
    // its persist_after_win()-gated save0 tail need e2e coverage independent
    // of the display site.
    arm_session(kGate, 0u, 0);
    SaveData& save = scr().save_data;
    ASSERT_TRUE(og::mode::current_progression().prepare_launch(
        save, /*networked_session=*/false));
    const std::uint32_t run_seed = save.tower_run_seed;
    ASSERT_TRUE(og::data::tower_floor_files_exist(og::kTowerFirstFloorLevel));

    // Gate win through the shadow finalize.
    trace_clear();
    ASSERT_TRUE(og::runtime::local_transport_shadow_testing_finalize_win(
        scr(), og::kTowerFirstFloorLevel, /*networked=*/false,
        /*own_player_index=*/0u));
    EXPECT_EQ(static_cast<short>(og::kTowerFirstFloorLevel), save.scen_num);
    EXPECT_EQ(1, save.tower_best_floor) << "floors climbed = highest REACHED";
    EXPECT_EQ(og::kTowerFirstFloorLevel, save.current_levels[kTowerId]);
    EXPECT_FALSE(save.is_level_completed(kGate))
        << "D9: the tower never marks levels completed, Gate included";
    EXPECT_EQ(2, trace_count("save"))
        << "persist_after_win()==true (D10): the shadow's save0 tail runs";
    {
        SaveData disk;
        ASSERT_TRUE(disk.load("save0"));
        EXPECT_EQ(static_cast<short>(og::kTowerFirstFloorLevel), disk.scen_num);
        EXPECT_EQ(1, disk.tower_best_floor);
        EXPECT_EQ(run_seed, disk.tower_run_seed)
            << "the win checkpoint carries the run seed";
    }

    // Floor 1 win through the shadow finalize: cursor/best advance and the
    // destination floor gets provisioned (advance_cursor regenerates missing
    // files even without the GO-time prefetch).
    scr().world().id = og::kTowerFirstFloorLevel;
    scr().world().current_scenario = og::kTowerFirstFloorLevel;
    trace_clear();
    ASSERT_TRUE(og::runtime::local_transport_shadow_testing_finalize_win(
        scr(), og::kTowerFirstFloorLevel + 1, /*networked=*/false,
        /*own_player_index=*/0u));
    EXPECT_EQ(static_cast<short>(og::kTowerFirstFloorLevel + 1), save.scen_num);
    EXPECT_EQ(2, save.tower_best_floor);
    EXPECT_TRUE(
        og::data::tower_floor_files_exist(og::kTowerFirstFloorLevel + 1));
    EXPECT_EQ(run_seed, save.tower_run_seed) << "wins keep the run seed";
    EXPECT_FALSE(save.is_level_completed(og::kTowerFirstFloorLevel));
}

TEST_F(TowerRunE2E, shadow_finalize_honors_the_ctf_rematch_gate)
{
    // §2.4-B delta 1, pinned AT THE SHADOW: the shadow historically ran
    // add_level_completed unconditionally; the fold gained the CTF rematch
    // gate, and a shadow that forgets to fill ctx.rematch_shape regresses
    // exactly here (the fold-layer unit test would stay green). Classic
    // progression is required — under the tower mount marks_level_completed
    // is false and the assertion would be vacuous.
    (void)unmount_campaign_package_with_error(get_mounted_campaign());
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"));
    ASSERT_EQ(og::mode::ProgressionKind::Classic,
              og::mode::current_progression().kind());

    SaveData& save = scr().save_data;
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 500;
    save.current_levels["org.openglad.gladiator"] = 500;
    for (auto& score : save.m_score)
        score = 0;

    GameWorld& world = scr().world();
    const char type_before = world.type;
    const auto ctf_before = world.ctf;
    world.end = 0;
    world.id = 500;
    world.current_scenario = 500;
    world.time_bonus_limit = 0;
    world.completed_levels.clear();
    for (auto& score : world.m_score)
        score = 0;
    world.type = static_cast<char>(world.type | GameWorld::TYPE_CTF);
    world.ctf.active = true;
    world.ctf.winner_team = 1;

    // Decided match, next level == this level: the rematch shape.
    trace_clear();
    ASSERT_TRUE(og::runtime::local_transport_shadow_testing_finalize_win(
        scr(), /*next_level=*/500, /*networked=*/false, 0u));
    EXPECT_FALSE(save.is_level_completed(500))
        << "a decided-but-rematch CTF match must not mark the level";
    EXPECT_EQ(500, save.scen_num) << "rematch keeps the cursor on the level";

    // A decided ADVANCE (different next level) still marks it.
    trace_clear();
    ASSERT_TRUE(og::runtime::local_transport_shadow_testing_finalize_win(
        scr(), /*next_level=*/501, /*networked=*/false, 0u));
    EXPECT_TRUE(save.is_level_completed(500))
        << "a genuine advance still marks the finished level";
    EXPECT_EQ(501, save.scen_num);

    // Restore the shared world's mode bits for later tests in this binary.
    world.type = type_before;
    world.ctf = ctf_before;
}

TEST_F(TowerRunE2E, shadow_withdraw_finalize_routes_the_run_end)
{
    // The shadow's withdraw/quit finalize is its own run-end routing site
    // (docs/game-modes.md; tower-unreachable via exits in v1 since D9 blocks
    // can_withdraw, but the hook stays uniform): a mid-run withdraw resets
    // the on-disk cursor to the Gate and keeps best/seed, converging with
    // the display-side hook it pairs with.
    arm_session(static_cast<short>(og::kTowerGateLevel + 3), 777u, 3);
    ASSERT_TRUE(scr().save_data.save("save0"));
    trace_clear();

    ASSERT_TRUE(og::runtime::local_transport_shadow_testing_finalize_withdraw(
        scr(), /*destination_level=*/kGate, /*networked=*/false));

    // on_run_ended's field-merge write ran first; the withdraw tail then
    // reloaded save0 and wrote the destination cursor — both converge on
    // the Gate with best/seed retained, in memory and on disk.
    EXPECT_EQ(kGate, scr().save_data.scen_num);
    EXPECT_EQ(3, scr().save_data.tower_best_floor);
    EXPECT_EQ(777u, scr().save_data.tower_run_seed);
    SaveData disk;
    ASSERT_TRUE(disk.load("save0"));
    EXPECT_EQ(kGate, disk.scen_num);
    EXPECT_EQ(3, disk.tower_best_floor);
    EXPECT_EQ(777u, disk.tower_run_seed);
}

TEST_F(TowerRunE2E, esc_abort_routes_the_run_end_on_the_server_save)
{
    // The FIFTH run-end routing site (docs/game-modes.md):
    // local_transport_shadow_abort_level ends the run on the AUTHORITATIVE
    // server save without passing screen::endgame or the withdraw finalize.
    // Build the real thing: a tower mid-run save0 whose generated floor
    // glad_init loads, with the authoritative local shadow installed.
    arm_session(static_cast<short>(og::kTowerGateLevel + 2), 20250u, 2);
    og::ui::initialize_starting_team(scr().save_data, {FAMILY_SOLDIER});
    scr().save_data.numplayers = 1;
    og::mode::current_progression().ensure_level_available(scr().save_data);
    ASSERT_TRUE(og::data::tower_floor_files_exist(og::kTowerGateLevel + 2));
    ASSERT_TRUE(scr().save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    og::runtime::GameSession& gameplay_session =
        *og::runtime::current_game_session;
    ASSERT_TRUE(og::runtime::local_transport_active(gameplay_session))
        << "glad_init must install the authoritative local shadow";
    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(
            gameplay_session);
    ASSERT_TRUE(server_screen != nullptr);
    ASSERT_EQ(static_cast<short>(og::kTowerGateLevel + 2),
              server_screen->save_data.scen_num);

    trace_clear();
    EXPECT_TRUE(
        og::runtime::local_transport_shadow_abort_level(gameplay_session))
        << "host/local abort ends the level authoritatively";

    // The abort routed the run end on the server save: the on-disk cursor is
    // back at the Gate with best/seed retained, and the authoritative world
    // ended (the display ends on the mirrored world.end).
    EXPECT_EQ(kGate, server_screen->save_data.scen_num);
    EXPECT_EQ(2, server_screen->save_data.tower_best_floor);
    EXPECT_EQ(1, static_cast<int>(server_screen->world().end));
    {
        SaveData disk;
        ASSERT_TRUE(disk.load("save0"));
        EXPECT_EQ(kGate, disk.scen_num);
        EXPECT_EQ(2, disk.tower_best_floor);
        EXPECT_EQ(20250u, disk.tower_run_seed);
    }

    og::runtime::clear_local_transport_shadow(gameplay_session);
    scr().world().delete_objects();
    scr().world().end = 0;
}

// --- Results surfaces (§2.7). ----------------------------------------------

TEST_F(TowerRunE2E, retry_suppressed_and_summary_lines_under_tower_mount)
{
    EXPECT_TRUE(og::mode::current_progression().suppress_retry())
        << "the results RETRY button is gated off under the tower mount";

    // The overview injection reads these exact lines.
    arm_session(static_cast<short>(og::kTowerGateLevel + 4), 99u, 7);
    scr().world().id = og::kTowerGateLevel + 4;
    const std::vector<std::string> lines =
        og::mode::current_progression().results_summary_lines(
            scr().save_data, scr().world());
    ASSERT_EQ(1u, lines.size());
    EXPECT_EQ("Floor 4 conquered - best 7", lines[0]);

    // Classic keeps retry (and adds no lines) once gladiator is remounted.
    (void)unmount_campaign_package_with_error(get_mounted_campaign());
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"));
    EXPECT_FALSE(og::mode::current_progression().suppress_retry());
    EXPECT_TRUE(og::mode::current_progression()
                    .results_summary_lines(scr().save_data, scr().world())
                    .empty());
}

// --- Full results UI: RETRY hidden + mode summary drawn (§2.7). -------------

struct ResultsThreadState
{
    bool started = false;
    bool finished = false;
    bool loop_seen = false;
    bool clicks_delivered = false;
    bool failsafe_used = false;
};

// Condition-driven wait on the results loop's TESTING seams (no wall-clock
// PACING — the timeout is only a failure bound so a broken loop cannot hang
// CI; the sanctioned wait_for_interactable shape).
template <typename Pred>
bool tower_results_wait(Pred pred, int timeout_ms = 30000)
{
    const Uint32 start = SDL_GetTicks();
    while (!pred())
    {
        if (SDL_GetTicks() - start > static_cast<Uint32>(timeout_ms))
            return false;
        SDL_Delay(2);
    }
    return true;
}

// Synchronized click: hold the synthetic press until the loop has polled at
// least two more frames (its edge detector needs an unpressed->pressed
// transition ACROSS frames), then release and let two more frames pass. The
// `loop live` escape keeps the OK click (which ends the loop) from waiting
// on frames that will never come.
bool tower_results_click(int game_x, int game_y)
{
    MouseState& mouse = query_mouse_no_poll();
    const int pressed_at = results_screen_testing_frame_count();
    mouse.x = static_cast<float>(game_x);
    mouse.y = static_cast<float>(game_y);
    mouse.left = true;
    const bool press_seen = tower_results_wait([pressed_at] {
        return results_screen_testing_frame_count() >= pressed_at + 2 ||
               !results_screen_testing_loop_live();
    });
    mouse.left = false;
    if (!press_seen)
        return false;
    const int released_at = results_screen_testing_frame_count();
    return tower_results_wait([released_at] {
        return results_screen_testing_frame_count() >= released_at + 2 ||
               !results_screen_testing_loop_live();
    });
}

int tower_results_ui_injector(void* data)
{
    og::runtime::current_session = og::runtime::primary_session.load();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    // Sync on the loop actually running and drawing frames.
    st->loop_seen = tower_results_wait([] {
        return results_screen_testing_loop_live() &&
               results_screen_testing_frame_count() >= 2;
    });
    if (st->loop_seen)
    {
        // Clicks on the (suppressed) RETRY face must do nothing: a YES
        // answer is queued by the test, so a live retry button would flip
        // retry to true and fail the assertion below.
        bool ok = true;
        for (int i = 0; i < 3; ++i)
            ok = tower_results_click(187, 171) && ok; // the classic RETRY rect
        ok = tower_results_click(132, 171) && ok;     // OK exits the loop
        st->clicks_delivered = ok;
    }

    // Failure bound ONLY — never the expected exit path (the test asserts
    // the exit-reason trace): if the loop somehow outlives the OK click,
    // end the world so the run fails on assertions instead of hanging.
    if (!tower_results_wait(
            [] { return !results_screen_testing_loop_live(); }))
    {
        st->failsafe_used = true;
        og::runtime::current_session->myscreen_->world().end = 1;
    }

    st->finished = true;
    return 0;
}

TEST_F(TowerRunE2E, full_results_ui_hides_retry_and_survives_mode_summary)
{
    // A floor-1 win overview under the tower mount: the mode summary line
    // ("Floor 1 conquered - best 2") is drawn, and the RETRY button is
    // hidden — with a YES queued, a live button would flip retry to true.
    arm_session(static_cast<short>(og::kTowerFirstFloorLevel), 313u, 2);
    og::mode::current_progression().ensure_level_available(scr().save_data);
    ASSERT_TRUE(og::data::tower_floor_files_exist(og::kTowerFirstFloorLevel));
    scr().world().id = og::kTowerFirstFloorLevel;

    std::map<int, guy*> before;
    std::map<int, walker*> after;
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true); // would confirm a retry
    trace_clear();
    results_screen_testing_set_force_full(true); // also resets the frame seam

    ResultsThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(tower_results_ui_injector,
                                          "tower_results_ui_injector", &st);
    ASSERT_TRUE(thread != nullptr);

    const bool retry = results_screen(
        0, static_cast<short>(og::kTowerFirstFloorLevel + 1), before, after);

    int rc = 0;
    SDL_WaitThread(thread, &rc);
    results_screen_testing_set_force_full(false);
    picker_testing_yes_or_no_queue_clear();
    scr().world().end = 0;

    ASSERT_TRUE(st.started && st.finished);
    ASSERT_TRUE(st.loop_seen) << "the results button loop never went live";
    EXPECT_TRUE(st.clicks_delivered)
        << "every synthetic click must be observed by the loop's edge "
           "detector before the loop exits";
    EXPECT_FALSE(st.failsafe_used)
        << "the loop must exit via the OK click, not the failure bound";
    EXPECT_TRUE(trace_contains("results", "exit ok_click"))
        << "the OK button must be the exit path";
    EXPECT_FALSE(trace_contains("results", "exit world_end"))
        << "a world_end exit means the clicks missed (vacuous run)";
    EXPECT_FALSE(retry)
        << "RETRY is suppressed under tower: clicks on its face do nothing";
    EXPECT_TRUE(trace_contains("results",
                               "mode_summary_drawn Floor 1 conquered - best 2"))
        << "the mode summary line must actually RENDER in overview mode";
}

// --- The local picker settings round-trip (§5.9 locality amendment). --------
//
// The regression net that was missing when the ghost-session bug shipped: the
// tower e2e above set save fields directly, but the REAL GO path routes the
// save through the LocalPickerLobbyClient echo (do_pick_campaign ->
// picker_lobby_sync_settings_from_save -> LobbyServer sanitize ->
// apply_state_to_save). An unconditional sanitize rejection silently
// reverted the pick to gladiator/scen1 with the tower still mounted.

TEST_F(TowerRunE2E, local_picker_settings_round_trip_preserves_tower_pick)
{
    arm_session(kGate, 0u, 0);
    SaveData& save = scr().save_data;
    og::ui::initialize_starting_team(save, {FAMILY_SOLDIER});
    save.numplayers = 1;

    // The exact sequence a campaign pick performs (do_pick_campaign):
    // initialize the local lobby echo, then sync settings from the save.
    picker_lobby_initialize_from_save();
    picker_lobby_sync_settings_from_save();

    EXPECT_EQ(kTowerId, save.current_campaign)
        << "the settings echo must not revert a just-picked tower";
    EXPECT_EQ(kGate, save.scen_num)
        << "the settings echo must not revert the Gate cursor";
    EXPECT_EQ(kTowerId, get_mounted_campaign())
        << "mount and save must stay coherent through the round-trip";

    // The preserved pick actually plays: GO provisions floor 1 and the
    // generated level loads through the real og_open_read fallback chain.
    ASSERT_TRUE(og::mode::current_progression().prepare_launch(
        save, /*networked_session=*/false));
    ASSERT_TRUE(og::data::tower_floor_files_exist(og::kTowerFirstFloorLevel));
    scr().world().id = static_cast<short>(og::kTowerFirstFloorLevel);
    EXPECT_TRUE(scr().load_level())
        << "Gate -> floor 1: the generated floor must load";

    picker_lobby_shutdown();
    scr().world().delete_objects();
    scr().world().end = 0;
}

TEST_F(TowerRunE2E, lobby_settings_echo_keeps_mount_and_save_coherent)
{
    // WI-2 defense-in-depth: whenever apply_state_to_save changes (or keeps)
    // the campaign, the MOUNT must follow — the fixture mounted the tower,
    // but the save points at gladiator, so the echo must remount gladiator
    // rather than leave a split-brain mount/save pair.
    ASSERT_EQ(kTowerId, get_mounted_campaign());
    SaveData& save = scr().save_data;
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    og::ui::initialize_starting_team(save, {FAMILY_SOLDIER});
    save.numplayers = 1;

    picker_lobby_initialize_from_save();
    picker_lobby_sync_settings_from_save();

    EXPECT_EQ("org.openglad.gladiator", save.current_campaign);
    EXPECT_EQ(1, save.scen_num);
    EXPECT_EQ("org.openglad.gladiator", get_mounted_campaign())
        << "a campaign echo must never leave mount != save";

    picker_lobby_shutdown();
}

// --- Display transition: heal once, then fail LOUDLY (WI-3a). ---------------

TEST_F(TowerRunE2E, display_transition_heals_missing_floor_and_fails_loudly)
{
    // The heal: a tower floor deleted between the server's transition and
    // the display's load regenerates from (seed, N) and the session
    // continues.
    arm_session(static_cast<short>(og::kTowerGateLevel + 2), 313u, 2);
    scr().save_data.numplayers = 1;
    og::ui::initialize_starting_team(scr().save_data, {FAMILY_SOLDIER});
    prune_all_floors();
    trace_clear();
    EXPECT_TRUE(og::runtime::local_transport_shadow_testing_display_transition(
        scr(), og::kTowerGateLevel + 2))
        << "the transition must heal a regenerable floor and continue";
    EXPECT_TRUE(trace_contains("net", "display_transition_healed"));
    EXPECT_TRUE(og::data::tower_floor_files_exist(og::kTowerGateLevel + 2));

    // Unhealable (a file squats on the user scen/ directory, so the
    // regeneration write fails too): the transition reports failure — the
    // caller ends the session loudly instead of keeping the stale world.
    prune_all_floors();
    const fs::path scen_dir = fs::path(get_user_path()) / "scen";
    std::error_code ec;
    fs::remove_all(scen_dir, ec);
    {
        std::ofstream blocker(scen_dir);
        blocker << "squatter";
    }
    trace_clear();
    EXPECT_FALSE(og::runtime::local_transport_shadow_testing_display_transition(
        scr(), og::kTowerGateLevel + 2))
        << "an unhealable transition must FAIL, never silently keep the "
           "previous level's world";
    EXPECT_TRUE(trace_contains("net", "display_transition_failed"));
    fs::remove(scen_dir, ec);

    scr().world().delete_objects();
    scr().world().end = 0;
}

// --- LobbyServer sanitize backstop (§5.9 layer 3). --------------------------

class TowerMockLobbyTransport final : public og::sim::ITransport
{
public:
    void send(og::sim::PeerId peer_id, const std::uint8_t* data,
              std::size_t len) override
    {
        sent_.push_back({peer_id, std::vector<std::uint8_t>(data, data + len)});
    }
    [[nodiscard]] bool supports_typed_messages() const noexcept override
    {
        return false;
    }
    [[nodiscard]] std::vector<og::sim::ReceivedMessage> poll() override
    {
        std::vector<og::sim::ReceivedMessage> drained = std::move(received_);
        received_.clear();
        return drained;
    }
    [[nodiscard]] std::vector<og::sim::TypedReceivedMessage>
    poll_typed() override
    {
        return {};
    }
    void accept_connections() override {}
    void disconnect(og::sim::PeerId) override {}
    [[nodiscard]] std::vector<og::sim::PeerId> connected_peers() const override
    {
        return {};
    }
    void queue_lobby_message(og::sim::PeerId peer_id,
                             const og::sim::LobbyMessage& message)
    {
        received_.push_back(
            {peer_id, og::sim::serialize_lobby_message(message)});
    }

private:
    std::vector<og::sim::ReceivedMessage> sent_;
    std::vector<og::sim::ReceivedMessage> received_;
};

og::sim::LobbyMessage make_settings_change(const std::string& campaign_id,
                                           std::int16_t scenario_id)
{
    og::sim::LobbySettings settings;
    settings.campaign_id = campaign_id;
    settings.scenario_id = scenario_id;
    settings.difficulty = 1;
    settings.allied_mode = 1;
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = settings,
    };
    return message;
}

TEST(TowerLobbyGating, sanitize_rejects_tower_campaign_for_networked_sessions)
{
    TowerMockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    {
        og::sim::LobbyPlayer player;
        player.name = "Host";
        player.team = 0;
        og::sim::LobbyMessage join;
        join.payload = og::sim::LobbyJoinMessage{.player = player};
        transport.queue_lobby_message(11u, join);
        server.poll_incoming_messages();
    }

    const og::sim::LobbySettings before = server.state().settings;
    ASSERT_NE(std::string(og::kTowerCampaignId), before.campaign_id);

    // A crafted client pushes the tower campaign: rejected to the fallback
    // PAIR (campaign and scenario together).
    transport.queue_lobby_message(
        11u, make_settings_change(std::string(og::kTowerCampaignId), 700));
    server.poll_incoming_messages();
    og::sim::LobbyState state = server.state();
    EXPECT_EQ(before.campaign_id, state.settings.campaign_id);
    EXPECT_EQ(before.scenario_id, state.settings.scenario_id);

    // A legitimate campaign still passes through untouched.
    transport.queue_lobby_message(
        11u, make_settings_change("org.openglad.ctf", 500));
    server.poll_incoming_messages();
    state = server.state();
    EXPECT_EQ("org.openglad.ctf", state.settings.campaign_id);
    EXPECT_EQ(500, state.settings.scenario_id);
}

} // namespace
