// The Tier-B game-mode seam: kind_for_mode_string mapping, the exhaustive
// dispatch switch, Classic's default behaviors, the campaign.yaml `mode:`
// key round-trip with its LOAD-BEARING emit-only-when-non-empty stability
// guarantee, and mounted_campaign_mode memoization/invalidation.

#include <gtest/gtest.h>

#include <openglad/gameplay/game_world.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/campaign_yaml.h>
#include <openglad/resources/game_mode.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using og::mode::IProgression;
using og::mode::ProgressionKind;

constexpr const char* kGladiatorId = "gladiator";

// ---------------------------------------------------------------------------
// kind_for_mode_string — the pure mapping table.

TEST(GameModeKind, kind_for_mode_string_table)
{
    EXPECT_EQ(ProgressionKind::Classic, og::mode::kind_for_mode_string(""));
    EXPECT_EQ(ProgressionKind::Classic, og::mode::kind_for_mode_string("classic"));
    EXPECT_EQ(ProgressionKind::Tower, og::mode::kind_for_mode_string("tower"));

    // Unknown strings degrade to Classic (graceful degradation for packages
    // authored by newer builds). Matching is exact: no trim, no case fold.
    EXPECT_EQ(ProgressionKind::Classic, og::mode::kind_for_mode_string("TOWER"));
    EXPECT_EQ(ProgressionKind::Classic, og::mode::kind_for_mode_string("Tower"));
    EXPECT_EQ(ProgressionKind::Classic, og::mode::kind_for_mode_string(" tower"));
    EXPECT_EQ(ProgressionKind::Classic, og::mode::kind_for_mode_string("boss_rush"));
    EXPECT_EQ(ProgressionKind::Classic, og::mode::kind_for_mode_string("classic "));
}

// ---------------------------------------------------------------------------
// Dispatch — exhaustive over ProgressionKind, static stateless instances.

TEST(GameModeDispatch, every_kind_returns_a_usable_instance)
{
    // Classic maps to THE classic singleton (same object every call).
    IProgression& classic = og::mode::progression_for_kind(ProgressionKind::Classic);
    EXPECT_EQ(&classic, &og::mode::classic_progression());
    EXPECT_EQ(&classic, &og::mode::progression_for_kind(ProgressionKind::Classic));
    EXPECT_EQ(ProgressionKind::Classic, classic.kind());

    // Tower resolves to a live instance for every kind in the enum. Until
    // TowerProgression lands (WP-5) it falls back to the Classic instance;
    // afterwards it must self-report Tower. Either satisfies the dispatch
    // contract pinned here: no kind may dangle.
    IProgression& tower = og::mode::progression_for_kind(ProgressionKind::Tower);
    EXPECT_TRUE(tower.kind() == ProgressionKind::Classic ||
                tower.kind() == ProgressionKind::Tower);
}

// ---------------------------------------------------------------------------
// Classic defaults — the base-class behavior set IS Classic.

TEST(GameModeClassic, classic_defaults_are_identity)
{
    IProgression& classic = og::mode::classic_progression();
    SaveData save;
    save.scen_num = 7;

    EXPECT_EQ(ProgressionKind::Classic, classic.kind());
    EXPECT_TRUE(classic.prepare_launch(save, /*networked_session=*/false));
    EXPECT_TRUE(classic.prepare_launch(save, /*networked_session=*/true));
    EXPECT_TRUE(classic.marks_level_completed());
    EXPECT_TRUE(classic.persist_after_win());
    EXPECT_FALSE(classic.suppress_retry());

    // clamp_respawn_mode: identity for every value the difficulty submenu
    // can produce (0 = off, 1 = heroes, 2 = everyone,
    // 3 = Team 1 heroes only).
    for (short requested = 0; requested <= 3; ++requested)
        EXPECT_EQ(requested, classic.clamp_respawn_mode(requested));

    // ensure_level_available / on_run_ended: no-ops that leave the save
    // untouched (losses persist nothing — the Classic contract).
    GameWorld world(0);
    classic.ensure_level_available(save);
    og::mode::LevelOutcome loss;
    loss.ending = 1;
    loss.next_level = -1;
    classic.on_run_ended(save, world, loss);
    EXPECT_EQ(7, save.scen_num);

    // advance_cursor: pure passthrough.
    EXPECT_EQ(9, classic.advance_cursor(save, world, 9));
    EXPECT_EQ(7, save.scen_num) << "advance_cursor itself must not move the cursor";

    // Results surfaces: mode adds nothing.
    EXPECT_FALSE(classic.ending_popup(save, world, loss).has_value());
    EXPECT_TRUE(classic.results_summary_lines(save, world).empty());
}

// ---------------------------------------------------------------------------
// campaign.yaml `mode:` key — parse, emit-when-non-empty, byte stability.

std::string emitted_yaml_bytes(const og::data::CampaignYaml& data,
                               const std::string& name)
{
    const std::string rel_path = name;
    EXPECT_EQ(og::data::CampaignYamlWriteResult::Ok,
              og::data::write_campaign_yaml_with_result(rel_path.c_str(), data));
    // The PhysFS write dir is the user path; read the raw bytes back with
    // plain file IO so the emitter output is compared byte-for-byte.
    const std::filesystem::path full = std::filesystem::path(get_user_path()) / name;
    std::ifstream in(full, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << full;
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::error_code ec;
    std::filesystem::remove(full, ec);
    return buffer.str();
}

std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::stringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
        lines.push_back(line);
    return lines;
}

og::data::CampaignYaml sample_campaign()
{
    og::data::CampaignYaml data;
    data.title = "Mode Seam Test";
    data.version = "3";
    data.first_level = 5;
    data.suggested_power = 200;
    data.authors = "WP-3";
    data.contributors = "";
    data.description = "A campaign for the mode-key writer pins.";
    return data;
}

TEST(GameModeYaml, writer_omits_mode_when_empty)
{
    // LOAD-BEARING: an empty mode emits NO key at all, so every existing
    // (mode-less) campaign repack stays byte-stable.
    // Search for the KEY ("mode:"), not the bare word — the sample
    // description legitimately contains the substring "mode-key".
    const std::string bytes =
        emitted_yaml_bytes(sample_campaign(), "mode_seam_a.yaml");
    EXPECT_EQ(std::string::npos, bytes.find("mode:"))
        << "empty mode must not appear in emitter output:\n" << bytes;
}

TEST(GameModeYaml, writer_mode_line_is_the_only_delta)
{
    const std::string without =
        emitted_yaml_bytes(sample_campaign(), "mode_seam_b.yaml");

    og::data::CampaignYaml with_mode = sample_campaign();
    with_mode.mode = "tower";
    const std::string with =
        emitted_yaml_bytes(with_mode, "mode_seam_c.yaml");

    // Byte-stability pin: the mode key adds exactly one line and changes
    // nothing else about the emitted document.
    std::vector<std::string> with_lines = split_lines(with);
    const auto mode_line = std::find(with_lines.begin(), with_lines.end(),
                                     "mode: tower");
    ASSERT_NE(with_lines.end(), mode_line)
        << "emitted document must carry 'mode: tower':\n" << with;
    with_lines.erase(mode_line);
    EXPECT_EQ(split_lines(without), with_lines)
        << "the mode pair must be the ONLY emitter delta";
}

TEST(GameModeYaml, mode_round_trips_through_writer_and_parser)
{
    og::data::CampaignYaml data = sample_campaign();
    data.mode = "tower";
    ASSERT_EQ(og::data::CampaignYamlWriteResult::Ok,
              og::data::write_campaign_yaml_with_result("mode_seam_rt.yaml", data));

    og::data::CampaignYaml parsed;
    ASSERT_EQ(og::data::CampaignYamlReadResult::Ok,
              og::data::read_campaign_yaml("mode_seam_rt.yaml", parsed));
    EXPECT_TRUE(parsed.saw_mode);
    EXPECT_EQ("tower", parsed.mode);
    EXPECT_EQ("Mode Seam Test", parsed.title);

    og::data::CampaignYaml parsed_no_mode;
    ASSERT_EQ(og::data::CampaignYamlWriteResult::Ok,
              og::data::write_campaign_yaml_with_result("mode_seam_rt2.yaml",
                                                        sample_campaign()));
    ASSERT_EQ(og::data::CampaignYamlReadResult::Ok,
              og::data::read_campaign_yaml("mode_seam_rt2.yaml", parsed_no_mode));
    EXPECT_FALSE(parsed_no_mode.saw_mode);
    EXPECT_TRUE(parsed_no_mode.mode.empty());

    std::error_code ec;
    std::filesystem::remove(
        std::filesystem::path(get_user_path()) / "mode_seam_rt.yaml", ec);
    std::filesystem::remove(
        std::filesystem::path(get_user_path()) / "mode_seam_rt2.yaml", ec);
}

// ---------------------------------------------------------------------------
// mounted_campaign_mode — memoized identity of the mounted campaign, healed
// on mount changes. Mirrors the CampaignMetadataTest mount hygiene.

class MountedCampaignModeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        og::data::clear_campaign_metadata_cache();
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error(kGladiatorId));
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error(get_mounted_campaign());
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
        og::data::clear_campaign_metadata_cache();
    }

    static bool install_fake_package(const std::string& id,
                                     const std::string& yaml_content)
    {
        namespace fs = std::filesystem;
        const fs::path staging =
            fs::path(get_user_path()) / "mode_test_staging" / id;
        std::error_code ec;
        fs::create_directories(staging, ec);
        if (ec)
            return false;
        {
            std::ofstream out(staging / "campaign.yaml");
            out << yaml_content;
            if (!out)
                return false;
        }
        const fs::path archive =
            fs::path(get_user_path()) / "campaigns" / (id + ".glad");
        fs::remove(archive, ec);
        return zip_contents_with_error(staging.string(), archive.string()) ==
            ArchiveIoError::None;
    }

    static void remove_fake_package(const std::string& id)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove(fs::path(get_user_path()) / "campaigns" / (id + ".glad"), ec);
        fs::remove_all(fs::path(get_user_path()) / "mode_test_staging", ec);
        og::data::clear_campaign_metadata_cache();
    }

private:
    std::string previous_;
};

TEST_F(MountedCampaignModeTest, modeless_campaign_reports_empty_and_memoizes)
{
    EXPECT_EQ("", og::data::mounted_campaign_mode())
        << "shipped gladiator campaign.yaml has no mode key";
    // Second call rides the cache; same answer.
    EXPECT_EQ("", og::data::mounted_campaign_mode());
    EXPECT_EQ(&og::mode::classic_progression(), &og::mode::current_progression());
}

TEST_F(MountedCampaignModeTest, mode_key_flows_from_mounted_package)
{
    const std::string id = "test_tower_mode";
    ASSERT_TRUE(install_fake_package(id,
        "format_version: 1\ntitle: Fake Tower\nversion: 1\nmode: tower\n"));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(id));
    EXPECT_EQ("tower", og::data::mounted_campaign_mode());

    // Remount the classic campaign: the answer keys on the mounted id.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kGladiatorId));
    EXPECT_EQ("", og::data::mounted_campaign_mode());
    remove_fake_package(id);
}

TEST_F(MountedCampaignModeTest, remount_heals_stale_cached_mode)
{
    // v1 of the package has no mode; the lookup memoizes "".
    const std::string id = "test_mode_upgrade";
    ASSERT_TRUE(install_fake_package(id,
        "format_version: 1\ntitle: Upgrades\nversion: 1\n"));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(id));
    EXPECT_EQ("", og::data::mounted_campaign_mode());

    // The package is rebuilt WITH a mode. Remounting must serve the fresh
    // value, not the stale negative entry (mount-time invalidation rides
    // forget_campaign_display_title).
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kGladiatorId));
    ASSERT_TRUE(install_fake_package(id,
        "format_version: 1\ntitle: Upgrades\nversion: 2\nmode: tower\n"));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(id));
    EXPECT_EQ("tower", og::data::mounted_campaign_mode());

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kGladiatorId));
    remove_fake_package(id);
}

TEST_F(MountedCampaignModeTest, unknown_mode_string_falls_back_to_classic)
{
    const std::string id = "test_bogus_mode";
    ASSERT_TRUE(install_fake_package(id,
        "format_version: 1\ntitle: Bogus\nversion: 1\nmode: hyperspace\n"));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(id));
    EXPECT_EQ("hyperspace", og::data::mounted_campaign_mode())
        << "the raw string is reported; the KIND mapping degrades it";
    EXPECT_EQ(&og::mode::classic_progression(), &og::mode::current_progression());

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kGladiatorId));
    remove_fake_package(id);
}

// ---------------------------------------------------------------------------
// campaign.yaml `matchup:` — the versus axis. Same LOAD-BEARING
// emit-only-when-non-empty stability rule as `mode:`, one line after it.

TEST(GameModeYaml, writer_omits_matchup_when_empty)
{
    const std::string bytes =
        emitted_yaml_bytes(sample_campaign(), "matchup_seam_a.yaml");
    EXPECT_EQ(std::string::npos, bytes.find("matchup:"))
        << "empty matchup must not appear in emitter output:\n" << bytes;
}

TEST(GameModeYaml, writer_matchup_line_is_the_only_delta)
{
    const std::string without =
        emitted_yaml_bytes(sample_campaign(), "matchup_seam_b.yaml");

    og::data::CampaignYaml with_matchup = sample_campaign();
    with_matchup.matchup = "versus";
    const std::string with =
        emitted_yaml_bytes(with_matchup, "matchup_seam_c.yaml");

    std::vector<std::string> with_lines = split_lines(with);
    const auto matchup_line = std::find(with_lines.begin(), with_lines.end(),
                                        "matchup: versus");
    ASSERT_NE(with_lines.end(), matchup_line)
        << "emitted document must carry 'matchup: versus':\n" << with;
    with_lines.erase(matchup_line);
    EXPECT_EQ(split_lines(without), with_lines)
        << "the matchup pair must be the ONLY emitter delta";
}

TEST(GameModeYaml, matchup_emits_right_after_mode_when_both_set)
{
    og::data::CampaignYaml data = sample_campaign();
    data.mode = "tower";
    data.matchup = "versus";
    const std::string bytes =
        emitted_yaml_bytes(data, "matchup_seam_d.yaml");
    const std::vector<std::string> lines = split_lines(bytes);
    const auto mode_line =
        std::find(lines.begin(), lines.end(), "mode: tower");
    ASSERT_NE(lines.end(), mode_line) << bytes;
    ASSERT_NE(lines.end(), mode_line + 1) << bytes;
    EXPECT_EQ("matchup: versus", *(mode_line + 1))
        << "matchup sits on the line after mode";
}

TEST(GameModeYaml, matchup_round_trips_through_writer_and_parser)
{
    og::data::CampaignYaml data = sample_campaign();
    data.matchup = "versus";
    ASSERT_EQ(og::data::CampaignYamlWriteResult::Ok,
              og::data::write_campaign_yaml_with_result("matchup_seam_rt.yaml",
                                                        data));

    og::data::CampaignYaml parsed;
    ASSERT_EQ(og::data::CampaignYamlReadResult::Ok,
              og::data::read_campaign_yaml("matchup_seam_rt.yaml", parsed));
    EXPECT_TRUE(parsed.saw_matchup);
    EXPECT_EQ("versus", parsed.matchup);
    EXPECT_FALSE(parsed.saw_mode) << "matchup is orthogonal to mode";

    og::data::CampaignYaml parsed_absent;
    ASSERT_EQ(og::data::CampaignYamlWriteResult::Ok,
              og::data::write_campaign_yaml_with_result("matchup_seam_rt2.yaml",
                                                        sample_campaign()));
    ASSERT_EQ(og::data::CampaignYamlReadResult::Ok,
              og::data::read_campaign_yaml("matchup_seam_rt2.yaml",
                                           parsed_absent));
    EXPECT_FALSE(parsed_absent.saw_matchup);
    EXPECT_TRUE(parsed_absent.matchup.empty());

    std::error_code ec;
    std::filesystem::remove(
        std::filesystem::path(get_user_path()) / "matchup_seam_rt.yaml", ec);
    std::filesystem::remove(
        std::filesystem::path(get_user_path()) / "matchup_seam_rt2.yaml", ec);
}

// ---------------------------------------------------------------------------
// campaign_matchup / campaign_mode by-id accessors + the mounted twins.
// Reuses the MountedCampaignModeTest mount hygiene (exact-restore fixture).

using CampaignMatchupTest = MountedCampaignModeTest;

TEST_F(CampaignMatchupTest, matchup_reads_through_private_mount_and_fast_path)
{
    const std::string id = "test_versus_matchup";
    ASSERT_TRUE(install_fake_package(id,
        "format_version: 1\ntitle: Versus Pack\nversion: 1\n"
        "matchup: versus\n"));

    // Slow path: the package is NOT mounted (gladiator is). The lookup rides
    // a throwaway private mount and must not disturb the active mount.
    EXPECT_EQ("versus", og::data::campaign_matchup(id));
    EXPECT_EQ(kGladiatorId, get_mounted_campaign());
    EXPECT_EQ("", og::data::campaign_mode(id))
        << "matchup-only package carries no mode";
    EXPECT_EQ("", og::data::mounted_campaign_matchup())
        << "gladiator is cooperative";

    // Fast path: mount it and read the mounted twins.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(id));
    EXPECT_EQ("versus", og::data::mounted_campaign_matchup());
    EXPECT_EQ("", og::data::mounted_campaign_mode());

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kGladiatorId));
    remove_fake_package(id);
}

TEST_F(CampaignMatchupTest, absent_and_unsafe_ids_read_cooperative)
{
    EXPECT_EQ("", og::data::campaign_matchup(""));
    EXPECT_EQ("", og::data::campaign_mode(""));
    EXPECT_EQ("", og::data::campaign_matchup("../escape"))
        << "unsafe ids must not steer a mount";
    EXPECT_EQ("", og::data::campaign_matchup("no_such_pkg"));
    EXPECT_EQ("", og::data::campaign_matchup(kGladiatorId))
        << "shipped gladiator campaign.yaml has no matchup key";
}

// ---------------------------------------------------------------------------
// CampaignData (the editor's campaign-info round-trip) must carry BOTH yaml
// identity axes. Before this, an editor save dropped `mode:`/`matchup:` on
// the floor (documented deferred gap in docs/game-modes.md) because
// campaign_data_to_yaml never copied them into the rewritten campaign.yaml.

TEST_F(CampaignMatchupTest, campaign_data_round_trips_mode_and_matchup)
{
    const std::string id = "test_cd_roundtrip";
    ASSERT_TRUE(install_fake_package(id,
        "format_version: 1\ntitle: Round Trip\nversion: 1\n"
        "mode: tower\nmatchup: versus\n"));

    CampaignData loaded(id);
    ASSERT_TRUE(loaded.load());
    EXPECT_EQ("tower", loaded.mode);
    EXPECT_EQ("versus", loaded.matchup);
    EXPECT_EQ("Round Trip", loaded.title);

    // The editor flow: load -> edit an unrelated field -> save (repack).
    loaded.title = "Round Trip v2";
    ASSERT_TRUE(loaded.save());

    // A fresh load of the repacked archive still carries both keys.
    og::data::clear_campaign_metadata_cache();
    CampaignData reloaded(id);
    ASSERT_TRUE(reloaded.load());
    EXPECT_EQ("Round Trip v2", reloaded.title);
    EXPECT_EQ("tower", reloaded.mode)
        << "the editor round-trip must not drop mode:";
    EXPECT_EQ("versus", reloaded.matchup)
        << "the editor round-trip must not drop matchup:";

    // A campaign WITHOUT the keys stays without them after a repack (the
    // writer's only-when-non-empty rule, exercised end-to-end).
    const std::string coop_id = "test_cd_coop";
    ASSERT_TRUE(install_fake_package(coop_id,
        "format_version: 1\ntitle: Coop Trip\nversion: 1\n"));
    CampaignData coop(coop_id);
    ASSERT_TRUE(coop.load());
    EXPECT_TRUE(coop.mode.empty());
    EXPECT_TRUE(coop.matchup.empty());
    ASSERT_TRUE(coop.save());
    og::data::clear_campaign_metadata_cache();
    EXPECT_EQ("", og::data::campaign_mode(coop_id));
    EXPECT_EQ("", og::data::campaign_matchup(coop_id));

    remove_fake_package(id);
    remove_fake_package(coop_id);
}

TEST_F(CampaignMatchupTest, forget_heals_stale_matchup_entry)
{
    // v1 has no matchup; the lookup memoizes "".
    const std::string id = "test_matchup_upgrade";
    ASSERT_TRUE(install_fake_package(id,
        "format_version: 1\ntitle: Upgrades\nversion: 1\n"));
    EXPECT_EQ("", og::data::campaign_matchup(id));

    // The package is rebuilt WITH a matchup. The memo serves the stale ""
    // until invalidation; forget_campaign_display_title heals exactly this id.
    ASSERT_TRUE(install_fake_package(id,
        "format_version: 1\ntitle: Upgrades\nversion: 2\n"
        "matchup: versus\n"));
    EXPECT_EQ("", og::data::campaign_matchup(id)) << "memoized pre-rebuild";
    og::data::forget_campaign_display_title(id);
    EXPECT_EQ("versus", og::data::campaign_matchup(id));

    // clear_campaign_metadata_cache also drops the entry.
    ASSERT_TRUE(install_fake_package(id,
        "format_version: 1\ntitle: Upgrades\nversion: 3\n"));
    EXPECT_EQ("versus", og::data::campaign_matchup(id)) << "memoized again";
    og::data::clear_campaign_metadata_cache();
    EXPECT_EQ("", og::data::campaign_matchup(id));

    remove_fake_package(id);
}

} // namespace
