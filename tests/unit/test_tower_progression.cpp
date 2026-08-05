/* Headless unit tests for TowerProgression (WP-5, spec §5.5).
 *
 * Covers the lifecycle: policy knobs (clamp_respawn_mode == 0, retry
 * suppression, D9 marks_level_completed == false, D10 persist_after_win),
 * dispatch through the game-mode switch, the networked-session veto,
 * fresh-run seed + prune + prefetch at GO (D8), resume heal, the
 * ensure_level_available picker hook, advance_cursor best/files/HOLD
 * semantics and its §2.4 idempotence, on_run_ended cursor-reset + the ONE
 * field-merge save0 write (best retained; Gate/foreign-campaign no-ops),
 * and the popup/summary formatters for every shape.
 *
 * Mount hygiene (spec §1.10): every test that writes save0 runs under the
 * fixture, whose teardown remounts gladiator — SaveData::load
 * mounts the SAVED campaign, the classic --gtest_shuffle trap.
 */
#include <gtest/gtest.h>

#include <openglad/core/tower_constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/resources/game_mode.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/mapgen/tower_floor_gen.h>
#include <openglad/resources/save_data.h>

#include "westlands_sim_fixture.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

using og::mode::IProgression;
using og::mode::LevelOutcome;
using og::mode::ModePopup;
using og::mode::ProgressionKind;

const std::string kTowerId{og::kTowerCampaignId};

void prune_all_floors()
{
    for (int id = og::kTowerFirstFloorLevel; id <= 760; ++id)
        (void)og::data::delete_tower_floor_files(id);
}

class TowerProgressionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        prune_all_floors();
        std::error_code ec;
        fs::remove(fs::path(get_user_path()) / "save" / "save0.gtl", ec);
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("gladiator"));
    }

    void TearDown() override
    {
        prune_all_floors();
        std::error_code ec;
        fs::remove(fs::path(get_user_path()) / "save" / "save0.gtl", ec);
        // §1.10: SaveData::load mounts the saved campaign; heal the mount
        // for whatever test runs next in the shuffle.
        (void)unmount_campaign_package_with_error(get_mounted_campaign());
        (void)mount_campaign_package_with_error("gladiator");
    }

    static IProgression& tower() { return og::mode::tower_progression(); }

    // SaveData is non-copyable/non-movable (owning team_list) — initialize
    // the caller's instance in place.
    static void init_tower_save(SaveData& save, short scen_num,
                                std::uint32_t seed, short best = 0)
    {
        save.current_campaign = kTowerId;
        save.scen_num = scen_num;
        save.tower_run_seed = seed;
        save.tower_best_floor = best;
    }
};

// --- Policy knobs + dispatch. --------------------------------------------------

TEST_F(TowerProgressionTest, policy_knobs)
{
    IProgression& t = tower();
    EXPECT_EQ(ProgressionKind::Tower, t.kind());
    EXPECT_FALSE(t.marks_level_completed()) << "D9: tower never marks levels";
    EXPECT_TRUE(t.persist_after_win()) << "D10: checkpointed climb";
    EXPECT_TRUE(t.suppress_retry());
    for (short requested = 0; requested <= 2; ++requested)
        EXPECT_EQ(0, t.clamp_respawn_mode(requested))
            << "D3: respawns clamped OFF in tower";
}

TEST_F(TowerProgressionTest, dispatch_switch_returns_tower_instance)
{
    IProgression& via_switch =
        og::mode::progression_for_kind(ProgressionKind::Tower);
    EXPECT_EQ(&tower(), &via_switch);
    EXPECT_EQ(ProgressionKind::Tower, via_switch.kind());
    // Classic stays classic.
    EXPECT_EQ(&og::mode::classic_progression(),
              &og::mode::progression_for_kind(ProgressionKind::Classic));
}

// --- prepare_launch. -------------------------------------------------------------

TEST_F(TowerProgressionTest, networked_session_is_vetoed)
{
    SaveData save;
    init_tower_save(save, og::kTowerGateLevel, 0);
    EXPECT_FALSE(tower().prepare_launch(save, /*networked_session=*/true))
        << "TOWER CLIMB is local-only (the prepare_launch backstop)";
}

TEST_F(TowerProgressionTest, fresh_run_draws_seed_prunes_and_prefetches)
{
    // Stale floors from a previous run: 1 and 2 on disk.
    ASSERT_TRUE(og::tower::generate_tower_floor_to_user_dir(777u, 1).written);
    ASSERT_TRUE(og::tower::generate_tower_floor_to_user_dir(777u, 2).written);
    ASSERT_TRUE(
        og::data::tower_floor_files_exist(og::kTowerFirstFloorLevel + 1));

    SaveData save;
    init_tower_save(save, og::kTowerGateLevel, 777u);
    ASSERT_TRUE(tower().prepare_launch(save, /*networked_session=*/false));

    // Floor 1 regenerated (prefetch — the Gate's exit prompt reads its
    // title), floor 2 pruned and NOT regenerated.
    EXPECT_TRUE(og::data::tower_floor_files_exist(og::kTowerFirstFloorLevel));
    EXPECT_FALSE(
        og::data::tower_floor_files_exist(og::kTowerFirstFloorLevel + 1));
    EXPECT_EQ(og::kTowerGateLevel, save.scen_num)
        << "prepare_launch never moves the cursor";
}

TEST_F(TowerProgressionTest, resume_heals_current_and_prefetches_next)
{
    SaveData save;
    init_tower_save(save, og::kTowerGateLevel + 3, 555u);
    ASSERT_FALSE(og::data::tower_floor_files_exist(save.scen_num));

    ASSERT_TRUE(tower().prepare_launch(save, /*networked_session=*/false));
    EXPECT_TRUE(og::data::tower_floor_files_exist(og::kTowerGateLevel + 3));
    EXPECT_TRUE(og::data::tower_floor_files_exist(og::kTowerGateLevel + 4));
    EXPECT_EQ(555u, save.tower_run_seed) << "resume keeps the run seed";
    EXPECT_EQ(og::kTowerGateLevel + 3, save.scen_num);
}

TEST_F(TowerProgressionTest, heal_regenerates_byte_identical_floor)
{
    // The heal path must reproduce the exact bytes a fresh generation
    // yields — (seed, N) is the whole identity (§5.4).
    const int id = og::kTowerGateLevel + 3;
    ASSERT_TRUE(og::tower::generate_tower_floor_to_user_dir(555u, 3).written);
    std::vector<char> direct;
    {
        std::ifstream in(fs::path(get_user_path()) / "scen" /
                             std::format("scen{}.fss", id),
                         std::ios::binary);
        direct.assign((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    }
    prune_all_floors();

    SaveData save;
    init_tower_save(save, static_cast<short>(id), 555u);
    tower().ensure_level_available(save);
    ASSERT_TRUE(og::data::tower_floor_files_exist(id));
    std::vector<char> healed;
    {
        std::ifstream in(fs::path(get_user_path()) / "scen" /
                             std::format("scen{}.fss", id),
                         std::ios::binary);
        healed.assign((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    }
    EXPECT_EQ(direct, healed);
    // The picker-preview hook heals ONLY the current floor.
    EXPECT_FALSE(og::data::tower_floor_files_exist(id + 1));
}

TEST_F(TowerProgressionTest, ensure_level_available_ignores_the_gate)
{
    SaveData save;
    init_tower_save(save, og::kTowerGateLevel, 555u);
    tower().ensure_level_available(save); // the Gate ships in the .glad
    EXPECT_FALSE(og::data::tower_floor_files_exist(og::kTowerFirstFloorLevel));
}

// --- Provisioning-failure surfacing (RC-4): failed writes veto the GO. ------

TEST_F(TowerProgressionTest, fresh_run_vetoes_go_when_floor1_provisioning_fails)
{
    SaveData save;
    init_tower_save(save, og::kTowerGateLevel, 0u);

    // Force every floor write to fail: a regular FILE squats on the user
    // scen/ directory, so create_dir refuses (the advance-hold test's trick).
    const fs::path scen_dir = fs::path(get_user_path()) / "scen";
    std::error_code ec;
    fs::remove_all(scen_dir, ec);
    {
        std::ofstream blocker(scen_dir);
        blocker << "squatter";
    }

    EXPECT_FALSE(tower().prepare_launch(save, /*networked_session=*/false))
        << "a fresh run whose floor 1 cannot be written must veto the GO — "
           "otherwise the Gate win holds the cursor and the player replays "
           "the Gate believing it's Floor 1";
    EXPECT_EQ(og::kTowerGateLevel, save.scen_num);

    fs::remove(scen_dir, ec); // heal the squatter for the next test
}

TEST_F(TowerProgressionTest, resume_vetoes_go_when_current_floor_is_unhealable)
{
    SaveData save;
    init_tower_save(save, og::kTowerGateLevel + 3, 555u);
    ASSERT_FALSE(og::data::tower_floor_files_exist(save.scen_num));

    const fs::path scen_dir = fs::path(get_user_path()) / "scen";
    std::error_code ec;
    fs::remove_all(scen_dir, ec);
    {
        std::ofstream blocker(scen_dir);
        blocker << "squatter";
    }

    EXPECT_FALSE(tower().prepare_launch(save, /*networked_session=*/false))
        << "an unhealable CURRENT floor leaves nothing to load: veto";

    fs::remove(scen_dir, ec);
}

TEST_F(TowerProgressionTest, resume_survives_prefetch_failure_and_advance_holds)
{
    // Only the NEXT floor's fss is blocked (a directory squats on its path):
    // the current floor heals fine, so the GO proceeds — the prefetch
    // failure logs and advance_cursor retries (and holds) at the floor win.
    constexpr std::uint32_t kSeed = 555u;
    const int current_id = og::kTowerGateLevel + 3;
    const int next_id = current_id + 1;
    ASSERT_TRUE(og::tower::generate_tower_floor_to_user_dir(kSeed, 3).written);

    const fs::path next_fss = fs::path(get_user_path()) / "scen" /
                              std::format("scen{}.fss", next_id);
    std::error_code ec;
    fs::create_directories(next_fss, ec); // fopen("wb") on a dir fails

    SaveData save;
    init_tower_save(save, static_cast<short>(current_id), kSeed, /*best=*/3);
    EXPECT_TRUE(tower().prepare_launch(save, /*networked_session=*/false))
        << "a failed NEXT-floor prefetch must not veto a playable resume";
    EXPECT_FALSE(og::data::tower_floor_files_exist(next_id));

    GameWorld world(0);
    world.id = current_id;
    EXPECT_EQ(save.scen_num,
              tower().advance_cursor(save, world,
                                     static_cast<short>(next_id)))
        << "advance retries the provisioning and HOLDS when it still fails";
    EXPECT_EQ(4, save.tower_best_floor) << "the floor was still REACHED";

    fs::remove(next_fss, ec);
}

// --- Torn-set integrity (RC-3): files_exist sees every implied plane. -------

TEST_F(TowerProgressionTest, torn_multifloor_set_reads_absent_and_heals)
{
    // Find a genuinely multi-story floor for this seed (the fss floor_count
    // implies "{grid}_f1.png"); the spire/moat templates produce them early.
    constexpr std::uint32_t kSeed = 4242u;
    int multi_id = -1;
    for (int f = 1; f <= 30 && multi_id < 0; ++f)
    {
        ASSERT_TRUE(og::tower::generate_tower_floor_to_user_dir(kSeed, f).written);
        const int id = og::kTowerGateLevel + f;
        const fs::path plane = fs::path(get_user_path()) / "pix" /
                               std::format("scen{:04d}_f1.png", id);
        if (fs::exists(plane))
            multi_id = id;
    }
    ASSERT_GT(multi_id, 0) << "no multi-story floor in 1..30 for this seed";

    const fs::path fss_path = fs::path(get_user_path()) / "scen" /
                              std::format("scen{}.fss", multi_id);
    const fs::path plane_path =
        fs::path(get_user_path()) / "pix" /
        std::format("scen{:04d}_f1.png", multi_id);
    const auto read_bytes = [](const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::vector<char>((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    };
    const std::vector<char> fss_before = read_bytes(fss_path);
    const std::vector<char> plane_before = read_bytes(plane_path);
    ASSERT_FALSE(fss_before.empty());
    ASSERT_FALSE(plane_before.empty());
    ASSERT_TRUE(og::data::tower_floor_files_exist(multi_id));

    // Missing plane: absent + the loader hard-fails instead of shipping a
    // void story.
    std::error_code ec;
    fs::remove(plane_path, ec);
    EXPECT_FALSE(og::data::tower_floor_files_exist(multi_id))
        << "a missing _fN plane must read as a torn (absent) set";
    {
        westlands_fixture::LoadedWestlandsLevel torn(multi_id, kSeed);
        EXPECT_FALSE(torn.loaded)
            << "the loader must hard-fail on a declared-but-missing plane";
    }

    // Heal regenerates the exact bytes from (seed, N).
    SaveData save;
    init_tower_save(save, static_cast<short>(multi_id), kSeed);
    tower().ensure_level_available(save);
    ASSERT_TRUE(og::data::tower_floor_files_exist(multi_id));
    EXPECT_EQ(fss_before, read_bytes(fss_path));
    EXPECT_EQ(plane_before, read_bytes(plane_path));
    {
        westlands_fixture::LoadedWestlandsLevel healed(multi_id, kSeed);
        EXPECT_TRUE(healed.loaded) << "the healed set must load again";
    }

    // Zero-byte plane (the interrupted-IDBFS shape): absent again.
    {
        std::ofstream truncate(plane_path,
                               std::ios::binary | std::ios::trunc);
    }
    EXPECT_FALSE(og::data::tower_floor_files_exist(multi_id))
        << "a zero-byte plane must read as torn";
    tower().ensure_level_available(save);
    EXPECT_TRUE(og::data::tower_floor_files_exist(multi_id));
    EXPECT_EQ(plane_before, read_bytes(plane_path));

    // Truncated fss (floor_count unreadable): absent, then healed.
    {
        std::ofstream truncate(fss_path, std::ios::binary | std::ios::trunc);
        truncate.write(fss_before.data(),
                       static_cast<std::streamsize>(fss_before.size() / 2));
    }
    EXPECT_FALSE(og::data::tower_floor_files_exist(multi_id))
        << "a truncated fss must read as torn";
    tower().ensure_level_available(save);
    EXPECT_TRUE(og::data::tower_floor_files_exist(multi_id));
    EXPECT_EQ(fss_before, read_bytes(fss_path));
}

// --- advance_cursor. ---------------------------------------------------------------

TEST_F(TowerProgressionTest, advance_records_best_provisions_and_is_idempotent)
{
    GameWorld world(0);
    world.id = og::kTowerGateLevel + 1;
    SaveData save;
    init_tower_save(save, og::kTowerGateLevel + 1, 555u, /*best=*/1);

    const short next = static_cast<short>(og::kTowerGateLevel + 2);
    EXPECT_EQ(next, tower().advance_cursor(save, world, next));
    EXPECT_EQ(2, save.tower_best_floor)
        << "floors climbed = highest floor REACHED";
    EXPECT_TRUE(og::data::tower_floor_files_exist(next));

    // §2.4 idempotence: both local finalize sites fold the same SaveData.
    EXPECT_EQ(next, tower().advance_cursor(save, world, next));
    EXPECT_EQ(2, save.tower_best_floor);

    // A lifetime best from a deeper run is never lowered.
    save.tower_best_floor = 9;
    EXPECT_EQ(next, tower().advance_cursor(save, world, next));
    EXPECT_EQ(9, save.tower_best_floor);
}

TEST_F(TowerProgressionTest, advance_passes_through_non_floor_destinations)
{
    GameWorld world(0);
    SaveData save;
    init_tower_save(save, og::kTowerGateLevel, 555u);
    EXPECT_EQ(5, tower().advance_cursor(save, world, 5));
    EXPECT_EQ(0, save.tower_best_floor);
}

TEST_F(TowerProgressionTest, advance_holds_cursor_when_write_fails)
{
    GameWorld world(0);
    SaveData save;
    init_tower_save(save, og::kTowerGateLevel + 4, 555u);

    // Force every floor write to fail: a regular FILE squats on the user
    // scen/ directory, so create_dir refuses.
    const fs::path scen_dir = fs::path(get_user_path()) / "scen";
    std::error_code ec;
    fs::remove_all(scen_dir, ec);
    {
        std::ofstream blocker(scen_dir);
        blocker << "squatter";
    }

    const short next = static_cast<short>(og::kTowerGateLevel + 5);
    EXPECT_EQ(save.scen_num, tower().advance_cursor(save, world, next))
        << "HOLD: replay the floor instead of wrapping the campaign";
    EXPECT_EQ(5, save.tower_best_floor)
        << "the floor was REACHED even though provisioning failed (§5.5)";

    fs::remove(scen_dir, ec); // heal the squatter for the next test
}

// --- on_run_ended (D10). --------------------------------------------------------------

TEST_F(TowerProgressionTest, run_end_resets_cursor_with_one_field_merge_write)
{
    // Disk checkpoint: the last floor-win autosave, mid-run at floor 5.
    {
        SaveData disk;
        init_tower_save(disk, og::kTowerGateLevel + 5, 999u, /*best=*/5);
        ASSERT_TRUE(disk.save("save0"));
    }
    // Live session died on floor 7 (a deeper climb than the disk best).
    SaveData live;
    init_tower_save(live, og::kTowerGateLevel + 7, 999u, /*best=*/7);
    GameWorld world(0);
    world.id = og::kTowerGateLevel + 7;
    LevelOutcome outcome;
    outcome.ending = 1;
    outcome.next_level = -1;

    tower().on_run_ended(live, world, outcome);

    EXPECT_EQ(og::kTowerGateLevel, live.scen_num);
    EXPECT_EQ(7, live.tower_best_floor);

    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("save0"));
    EXPECT_EQ(og::kTowerGateLevel, reloaded.scen_num)
        << "relaunch must NOT resume the death floor";
    EXPECT_EQ(og::kTowerGateLevel,
              reloaded.current_levels[kTowerId]);
    EXPECT_EQ(7, reloaded.tower_best_floor) << "best retained (max of both)";
    EXPECT_EQ(999u, reloaded.tower_run_seed) << "seed kept (shareable)";
}

TEST_F(TowerProgressionTest, run_end_keeps_deeper_disk_best)
{
    {
        SaveData disk;
        init_tower_save(disk, og::kTowerGateLevel + 3, 999u, /*best=*/12);
        ASSERT_TRUE(disk.save("save0"));
    }
    SaveData live;
    init_tower_save(live, og::kTowerGateLevel + 3, 999u, /*best=*/3);
    GameWorld world(0);
    world.id = og::kTowerGateLevel + 3;
    LevelOutcome outcome;
    outcome.ending = 1;

    tower().on_run_ended(live, world, outcome);
    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("save0"));
    EXPECT_EQ(12, reloaded.tower_best_floor);
    EXPECT_EQ(12, live.tower_best_floor)
        << "the live save mirrors the merged best";
}

TEST_F(TowerProgressionTest, run_end_is_a_noop_at_the_gate_and_off_campaign)
{
    {
        SaveData disk;
        init_tower_save(disk, og::kTowerGateLevel + 5, 999u, /*best=*/5);
        ASSERT_TRUE(disk.save("save0"));
    }
    GameWorld world(0);
    world.id = og::kTowerGateLevel;
    LevelOutcome outcome;
    outcome.ending = 1;

    // Gate loss/withdraw: nothing ends.
    SaveData at_gate;
    init_tower_save(at_gate, og::kTowerGateLevel, 999u);
    tower().on_run_ended(at_gate, world, outcome);
    EXPECT_EQ(og::kTowerGateLevel, at_gate.scen_num);

    // Foreign campaign mounted mid-classic: nothing ends either.
    SaveData classic;
    classic.current_campaign = "gladiator";
    classic.scen_num = 705; // numerically a floor id, but not our campaign
    tower().on_run_ended(classic, world, outcome);
    EXPECT_EQ(705, classic.scen_num);

    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("save0"));
    EXPECT_EQ(og::kTowerGateLevel + 5, reloaded.scen_num)
        << "no-op shapes must not touch the disk checkpoint";
}

// --- Popups, summaries, formatters (§5.5/§5.7). ---------------------------------

TEST(TowerPopups, all_shapes)
{
    IProgression& t = og::mode::tower_progression();
    SaveData save;
    save.current_campaign = kTowerId;
    save.tower_best_floor = 9;
    save.tower_run_seed = 0xDEADBEEFu;
    GameWorld world(0);
    LevelOutcome outcome;

    // Floor win. The popup reads the POST-advance cursor (the win fold runs
    // advance_cursor before the results dispatch), so a normal win sees the
    // cursor one past the conquered floor.
    world.id = og::kTowerGateLevel + 23;
    save.scen_num = static_cast<short>(og::kTowerGateLevel + 24);
    outcome.ending = 0;
    auto popup = t.ending_popup(save, world, outcome);
    ASSERT_TRUE(popup.has_value());
    EXPECT_EQ("FLOOR CLEARED", popup->title);
    EXPECT_NE(std::string::npos, popup->body.find("Floor 23 conquered."));
    EXPECT_NE(std::string::npos, popup->body.find("Floor 24 awaits."));

    // Gate win.
    world.id = og::kTowerGateLevel;
    save.scen_num = static_cast<short>(og::kTowerFirstFloorLevel);
    popup = t.ending_popup(save, world, outcome);
    ASSERT_TRUE(popup.has_value());
    EXPECT_EQ("THE TOWER OPENS", popup->title);
    EXPECT_EQ("Floor 1 awaits.", popup->body);

    // Team wipe / timeout: the shareable-seed shape.
    world.id = og::kTowerGateLevel + 5;
    outcome.ending = 1;
    outcome.withdrawn = false;
    popup = t.ending_popup(save, world, outcome);
    ASSERT_TRUE(popup.has_value());
    EXPECT_EQ("THE TOWER CLAIMS YOU", popup->title);
    EXPECT_NE(std::string::npos, popup->body.find("You fell on Floor 5."));
    EXPECT_NE(std::string::npos, popup->body.find("Best climb: 9."));
    EXPECT_NE(std::string::npos, popup->body.find("Seed DEADBEEF"));

    // Withdraw / quit-mission mid-run.
    outcome.withdrawn = true;
    popup = t.ending_popup(save, world, outcome);
    ASSERT_TRUE(popup.has_value());
    EXPECT_EQ("THE CLIMB ABANDONED", popup->title);
    EXPECT_NE(std::string::npos,
              popup->body.find("You left the Tower on Floor 5."));
    EXPECT_EQ(std::string::npos, popup->body.find("Seed"))
        << "the abandon shape carries no seed line";

    // Gate loss: the generic popups suffice.
    world.id = og::kTowerGateLevel;
    outcome.withdrawn = false;
    EXPECT_FALSE(t.ending_popup(save, world, outcome).has_value());
}

TEST(TowerPopups, held_cursor_win_never_claims_the_next_floor)
{
    // RC-4: when advance_cursor HELD (destination provisioning failed), the
    // post-advance cursor still sits on the conquered floor — the popup must
    // say the floor stands again, never "Floor N awaits".
    IProgression& t = og::mode::tower_progression();
    SaveData save;
    save.current_campaign = kTowerId;
    GameWorld world(0);
    LevelOutcome outcome;
    outcome.ending = 0;

    // Held at a floor win.
    world.id = og::kTowerGateLevel + 5;
    save.scen_num = static_cast<short>(og::kTowerGateLevel + 5);
    auto popup = t.ending_popup(save, world, outcome);
    ASSERT_TRUE(popup.has_value());
    EXPECT_EQ("FLOOR CLEARED", popup->title);
    EXPECT_EQ(
        "Floor 5 conquered.\nThe stair could not be raised -\n"
        "Floor 5 stands again.",
        popup->body);

    // Held at the Gate win.
    world.id = og::kTowerGateLevel;
    save.scen_num = static_cast<short>(og::kTowerGateLevel);
    popup = t.ending_popup(save, world, outcome);
    ASSERT_TRUE(popup.has_value());
    EXPECT_EQ("THE TOWER OPENS", popup->title);
    EXPECT_EQ("The stair could not be raised.\nThe Gate stands again.",
              popup->body);
}

TEST(TowerPopups, summary_lines_win_only_surface)
{
    IProgression& t = og::mode::tower_progression();
    SaveData save;
    save.tower_best_floor = 9;
    GameWorld world(0);

    world.id = og::kTowerGateLevel + 2;
    const std::vector<std::string> lines =
        t.results_summary_lines(save, world);
    ASSERT_EQ(1u, lines.size());
    EXPECT_EQ("Floor 2 conquered - best 9", lines[0]);

    world.id = og::kTowerGateLevel;
    EXPECT_TRUE(t.results_summary_lines(save, world).empty())
        << "the Gate adds no overview line";
}

TEST(TowerFormatters, pure_helpers)
{
    EXPECT_EQ("Floor 42 conquered - best 42",
              og::mode::format_tower_summary(og::kTowerGateLevel + 42, 42));
    EXPECT_EQ("You fell on Floor 3.\nBest climb: 8.\nSeed 0000002A",
              og::mode::format_tower_loss(og::kTowerGateLevel + 3, 8, 42u,
                                          /*withdrawn=*/false));
    EXPECT_EQ("You left the Tower on Floor 3.\nBest climb: 8.",
              og::mode::format_tower_loss(og::kTowerGateLevel + 3, 8, 42u,
                                          /*withdrawn=*/true));
}

} // namespace
