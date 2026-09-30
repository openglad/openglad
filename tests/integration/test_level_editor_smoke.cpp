#include "test_save_state_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

// myscreen is now a macro defined in base.h (via game_session.h)

// Implemented in src/level_editor.cpp
Sint32 level_editor();

std::string get_user_path();

// Declared in tests/coverage_internal/level_editor_internal.inc:2184 (the same
// declaration test_level_editor_issue12_wall_crash.cpp:76 already uses): the
// editor's LevelEditorData is a function-local static, so this is the only
// handle on the level it loaded. Read it only after level_editor() returns.
LevelRuntimeData* level_editor_testing_level();

// Declared in tests/coverage_internal/level_editor_internal.inc: the warning
// latch's per-step verdicts on a scratch LevelEditorData ("W" warned, "."
// silent, "x" load failed) for open first_id/1, reload, open second_id/1.
std::string level_editor_testing_generated_warnings(
    const std::string& first_id, const std::string& second_id);

// The world().end shortcut is the editor's whole startup path minus the event
// loop: it pins the world canvas classic, remounts the gladiator campaign and
// loads list_levels().front() into the static LevelEditorData BEFORE the loop
// sees the end flag at level_editor.cpp:3686 and breaks out to return OK (4).
TEST(LevelEditorSmoke, end_flag_exits_after_full_editor_startup)
{
    trace_clear();
    const char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 1;

    Sint32 r = level_editor();

    og::runtime::current_session->myscreen_->world().end = old_end;

    ASSERT_EQ(4, r) << "level_editor() returns OK (level_editor.cpp:66) after "
                       "the end flag breaks its loop";
    EXPECT_TRUE(trace_contains("canvas", "editor_pin_classic"))
        << "the editor pins the world canvas classic on entry";
    EXPECT_TRUE(trace_contains("canvas", "editor_unpin"))
        << "the editor's canvas pin is balanced by an unpin on the way out";
    LevelRuntimeData* level = level_editor_testing_level();
    ASSERT_NE(nullptr, level)
        << "entry must publish the editor's LevelEditorData level";
    EXPECT_TRUE(level->world().grid.valid())
        << "startup must have loaded list_levels().front() into the editor";
}

namespace {

// Builds <user>/campaigns/<campaign_id>.glad from a scratch tree
// whose only level is a scen1 written with the given provenance mark. The
// editor hardcodes the gladiator campaign, mounts the archive fresh on
// every entry (unmount + prepend-mount), and loads list_levels().front()
// at startup — so for "gladiator" this puts a marked/unmarked scen1 on the
// exact LevelEditorData::loadLevel path the warning hangs off.
bool install_scratch_campaign(const std::string& campaign_id, bool generated)
{
    namespace fs = std::filesystem;
    const fs::path user{get_user_path()};
    const fs::path staging = user / "editor_generated_test_staging";
    std::error_code ec;
    fs::remove_all(staging, ec);
    ec.clear();
    for (const fs::path& dir :
         {staging / "scen", staging / "pix", user / "scen", user / "pix",
          user / "campaigns"})
    {
        fs::create_directories(dir, ec);
        if (ec)
        {
            ADD_FAILURE() << "cannot create " << dir << ": " << ec.message();
            return false;
        }
    }
    {
        std::ofstream yaml(staging / "campaign.yaml", std::ios::binary);
        yaml << "format: 1\ntitle: Generated Warning Test\nfirst_level: 1\n";
        if (!yaml)
        {
            ADD_FAILURE() << "cannot write scratch campaign.yaml";
            return false;
        }
    }

    // Write the level via the real writer into the user dir, then move the
    // two files into the staging tree.
    GameWorld world(1);
    world.create_new_grid();
    world.title = generated ? "Marked" : "Classic";
    og::data::LevelFileMetadata metadata;
    metadata.grid_file = "scen0001"; // the mapgens' scen{:04d} convention
    metadata.generated = generated;
    og::data::LevelFileIoError err = og::data::LevelFileIoError::None;
    if (!og::data::save_level_to_user_dir(world, 1, metadata, &err))
    {
        ADD_FAILURE() << "save_level_to_user_dir failed, error "
                      << static_cast<int>(err);
        return false;
    }
    fs::rename(user / "scen/scen1.fss", staging / "scen/scen1.fss", ec);
    if (ec)
    {
        ADD_FAILURE() << "cannot move scen1.fss: " << ec.message();
        return false;
    }
    fs::rename(user / "pix/scen0001.png", staging / "pix/scen0001.png", ec);
    if (ec)
    {
        ADD_FAILURE() << "cannot move scen0001.png: " << ec.message();
        return false;
    }

    const fs::path archive = user / "campaigns" / (campaign_id + ".glad");
    fs::remove(archive, ec);
    const ArchiveIoError zip_err =
        zip_contents_with_error(staging.string(), archive.string());
    if (zip_err != ArchiveIoError::None)
        ADD_FAILURE() << "zip_contents_with_error failed, error "
                      << static_cast<int>(zip_err);
    fs::remove_all(staging, ec);
    return zip_err == ArchiveIoError::None;
}

bool install_scratch_gladiator(bool generated)
{
    return install_scratch_campaign("gladiator", generated);
}

} // namespace

// The generated-scenario warning: the editor's startup load runs
// LevelEditorData::loadLevel on the campaign's first level; when that scen
// carries the SCEN_TYPE_GENERATED provenance mark, warn_if_generated shows
// the popup (a TRACE("popup", ...) under TESTING). The campaign archive is
// swapped for a scratch one whose scen1 carries the mark, then for one
// whose scen1 does not — same path, only the provenance byte differs.
TEST(LevelEditorSmoke, generated_scen_warns_on_open_and_classic_does_not)
{
    namespace fs = std::filesystem;
    const fs::path user{get_user_path()};
    const fs::path archive = user / "campaigns/gladiator.glad";
    // Keep the user dir's scen1/pix1 (install_scratch_gladiator writes and
    // moves them) and the real archive bytes restorable (shuffle-safe).
    og::test::ScopedPhysicalFileState fss_guard(user / "scen/scen1.fss");
    og::test::ScopedPhysicalFileState pix_guard(user / "pix/scen0001.png");
    ASSERT_TRUE(fss_guard.ready());
    ASSERT_TRUE(pix_guard.ready());
    const char old_end = og::runtime::current_session->myscreen_->world().end;

    {
        og::test::ScopedPhysicalFileState glad_guard(archive);
        ASSERT_TRUE(glad_guard.ready());

        ASSERT_TRUE(install_scratch_gladiator(/*generated=*/true));
        trace_clear();
        og::runtime::current_session->myscreen_->world().end = 1;
        (void)level_editor();
        og::runtime::current_session->myscreen_->world().end = old_end;
        ASSERT_TRUE(trace_contains("popup", "Generated scenario"))
            << "opening a marked scen must warn";

        ASSERT_TRUE(install_scratch_gladiator(/*generated=*/false));
        trace_clear();
        og::runtime::current_session->myscreen_->world().end = 1;
        (void)level_editor();
        og::runtime::current_session->myscreen_->world().end = old_end;
        ASSERT_FALSE(trace_contains("popup", "Generated scenario"))
            << "a classic scen must not warn";
    }

    // The real archive bytes are back; remount so later tests read the
    // genuine campaign, not the (now-deleted) scratch mount.
    (void)unmount_campaign_package_with_error("gladiator");
    (void)mount_campaign_package_with_error("gladiator");
}


// The warning is one per level per editor VISIT: leaving the editor and
// coming back to the same generated scen warns again (the latch lived in the
// process-static LevelEditorData and never reset, so the second visit -- or a
// later test's first -- was silent).
TEST(LevelEditorSmoke, generated_scen_warns_on_every_editor_entry)
{
    namespace fs = std::filesystem;
    const fs::path user{get_user_path()};
    const fs::path archive = user / "campaigns/gladiator.glad";
    og::test::ScopedPhysicalFileState fss_guard(user / "scen/scen1.fss");
    og::test::ScopedPhysicalFileState pix_guard(user / "pix/scen0001.png");
    ASSERT_TRUE(fss_guard.ready());
    ASSERT_TRUE(pix_guard.ready());
    const char old_end = og::runtime::current_session->myscreen_->world().end;

    {
        og::test::ScopedPhysicalFileState glad_guard(archive);
        ASSERT_TRUE(glad_guard.ready());
        ASSERT_TRUE(install_scratch_gladiator(/*generated=*/true));

        for (int visit = 1; visit <= 2; ++visit)
        {
            trace_clear();
            og::runtime::current_session->myscreen_->world().end = 1;
            (void)level_editor();
            og::runtime::current_session->myscreen_->world().end = old_end;
            EXPECT_TRUE(trace_contains("popup", "Generated scenario"))
                << "editor visit " << visit
                << " opened a marked scen and must warn";
        }
    }

    (void)unmount_campaign_package_with_error("gladiator");
    (void)mount_campaign_package_with_error("gladiator");
}

// Within one editor session the latch keys on (campaign, level): a reload of
// the level already warned about stays silent (the positive control), but a
// DIFFERENT campaign's generated level with the same number warns -- File >
// Campaign > Load swaps the editor's campaign under an unchanged level id.
TEST(LevelEditorSmoke, generated_scen_warning_keys_on_campaign_and_level)
{
    namespace fs = std::filesystem;
    const fs::path user{get_user_path()};
    const std::string first_id = "coverage.generated_warn_a";
    const std::string second_id = "coverage.generated_warn_b";
    og::test::ScopedPhysicalFileState fss_guard(user / "scen/scen1.fss");
    og::test::ScopedPhysicalFileState pix_guard(user / "pix/scen0001.png");
    ASSERT_TRUE(fss_guard.ready());
    ASSERT_TRUE(pix_guard.ready());
    struct ArchiveCleanup {
        fs::path a, b;
        ~ArchiveCleanup()
        {
            std::error_code ec;
            fs::remove(a, ec);
            fs::remove(b, ec);
        }
    } cleanup{user / "campaigns" / (first_id + ".glad"),
              user / "campaigns" / (second_id + ".glad")};
    ASSERT_TRUE(install_scratch_campaign(first_id, /*generated=*/true));
    ASSERT_TRUE(install_scratch_campaign(second_id, /*generated=*/true));

    EXPECT_EQ("W.W",
              level_editor_testing_generated_warnings(first_id, second_id))
        << "open A/1 warns, reload A/1 stays silent, open B/1 warns";
}
