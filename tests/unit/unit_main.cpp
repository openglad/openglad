#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdio>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <openglad/core/util.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/platform/game_session.h>
#include <openglad/resources/company.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/packs.h>
#include <openglad/resources/save_data.h>

#include "family_registry_dump.h"
#include "registry_difference.h"
#include "unit_core_pack_heal.h"

#ifdef ENABLE_COVERAGE
extern "C" void __gcov_dump(void);
#endif

namespace {

bool init_unit_filesystem(const std::filesystem::path& test_config_dir, const char* argv0)
{
    std::error_code ec;
    std::filesystem::create_directories(test_config_dir / "campaigns", ec);
    std::filesystem::create_directories(test_config_dir / "save", ec);
    std::filesystem::create_directories(test_config_dir / "cfg", ec);

    const char* physfs_argv0 =
        (argv0 != nullptr && argv0[0] != '\0') ? argv0 : "og_unit_tests";
    if (!og::resources::init(physfs_argv0))
    {
        std::fprintf(stderr, "error: PhysFS init failed: %s\n",
                     og::resources::filesystem_last_error().c_str());
        return false;
    }

    const std::string user_path = test_config_dir.string();
    if (!og::resources::set_write_dir(user_path))
    {
        std::fprintf(stderr, "error: PhysFS set write dir failed: %s\n",
                     og::resources::filesystem_last_error().c_str());
        og::resources::deinit();
        return false;
    }

    if (!og::resources::mount(user_path.c_str(), nullptr, 1))
    {
        std::fprintf(stderr, "error: PhysFS mount failed: %s\n",
                     og::resources::filesystem_last_error().c_str());
        og::resources::deinit();
        return false;
    }

    return true;
}

// Registry census. The five family registries, the pack script and
// family-chunk stores and the mounted campaign package are all
// process-global, so a test that leaves any of them changed hands its
// --gtest_shuffle neighbour a world it never set up. Fingerprint all four
// around every test, BEFORE the between-test heal below repairs anything,
// and fail the binary at the end naming each test that moved one.
//
// Collected in a side list and reported after RUN_ALL_TESTS rather than
// ADD_FAILURE'd from OnTestEnd — the shape tests/curses/curses_test_main.cpp
// uses for its mount gate, because a failure raised after a test has ended
// has no test to attach to.
struct RegistryFingerprint
{
    std::string families;
    std::size_t family_chunks = 0;
    std::size_t scripts = 0;
    std::string mounted_campaign;
};

RegistryFingerprint take_registry_fingerprint()
{
    RegistryFingerprint fingerprint;
    fingerprint.families = og::testing::dump_installed_families();
    fingerprint.family_chunks = og::script::pack_family_chunks().size();
    fingerprint.scripts = og::script::pack_scripts().size();
    fingerprint.mounted_campaign = get_mounted_campaign();
    return fingerprint;
}

std::vector<std::string>& registry_leaks()
{
    static std::vector<std::string> leaks;
    return leaks;
}

class HeadlessSessionListener final : public ::testing::EmptyTestEventListener
{
public:
    HeadlessSessionListener(og::runtime::GameSession& session,
                            GameWorld& fallback_world,
                            SaveData& fallback_save,
                            og::sim::SimEventLog& fallback_events)
        : session_(session),
          fallback_world_(fallback_world),
          fallback_save_(fallback_save),
          fallback_events_(fallback_events)
    {
    }

    void OnTestStart(const ::testing::TestInfo&) override
    {
        if (!gameplay_context_intact())
        {
            ADD_FAILURE() << "gameplay context corrupted before test";
            restore_context();
        }
        before_ = take_registry_fingerprint();
    }

    void OnTestEnd(const ::testing::TestInfo& info) override
    {
        record_registry_leak(info);

        if (!gameplay_context_intact())
        {
            ADD_FAILURE() << "gameplay context corrupted by test";
            restore_context();
        }

        fallback_world_.delete_objects();
        restore_context();
        // [SAVE-R8] Structural active-company reset (see company.h): keeps
        // slot changes from leaking across tests under --gtest_shuffle.
        (void)og::data::set_active_company_slot("save0");
        // Structural filesystem reset, same rationale: tests that tear down
        // PhysFS, redirect the write dir, or drop the user-dir mount (e.g.
        // the deliberate PhysfsWrappers simulated-fatal-assert landmine)
        // must not leak that state into later tests under --gtest_shuffle.
        if (!og::resources::is_initialized())
            (void)og::resources::init("og_unit_tests");
        const std::string user_path = get_user_path();
        (void)og::resources::set_write_dir(user_path);
        (void)og::resources::mount(user_path.c_str(), nullptr, 1);
        og::test::mount_core_pack();
    }

private:
    void record_registry_leak(const ::testing::TestInfo& info)
    {
        const RegistryFingerprint after = take_registry_fingerprint();
        std::string changed;
        if (after.families != before_.families)
            changed += "\n    installed families changed:\n" +
                       og::testing::first_registry_difference(before_.families,
                                                              after.families);
        if (after.family_chunks != before_.family_chunks)
            changed += "\n    pack family chunks: " +
                       std::to_string(before_.family_chunks) + " -> " +
                       std::to_string(after.family_chunks);
        if (after.scripts != before_.scripts)
            changed += "\n    pack scripts: " +
                       std::to_string(before_.scripts) + " -> " +
                       std::to_string(after.scripts);
        if (after.mounted_campaign != before_.mounted_campaign)
            changed += "\n    mounted campaign: \"" +
                       before_.mounted_campaign + "\" -> \"" +
                       after.mounted_campaign + "\"";
        if (changed.empty())
            return;
        registry_leaks().push_back(std::string(info.test_suite_name()) + "." +
                                   info.name() + changed);
    }

    bool gameplay_context_intact() const
    {
        return og::runtime::current_session == &session_ &&
               current_game == &session_.game_ &&
               session_.game_.world == &fallback_world_ &&
               session_.game_.save == &fallback_save_ &&
               session_.game_.sim_events == &fallback_events_;
    }

    void restore_context()
    {
        og::runtime::current_session = &session_;
        current_game = &session_.game_;
        session_.game_.world = &fallback_world_;
        session_.game_.save = &fallback_save_;
        session_.game_.sim_events = &fallback_events_;
    }

    og::runtime::GameSession& session_;
    GameWorld& fallback_world_;
    SaveData& fallback_save_;
    og::sim::SimEventLog& fallback_events_;
    RegistryFingerprint before_;
};

} // namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    const auto test_config_dir = std::filesystem::temp_directory_path() /
        ("openglad_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_config_dir);
    setenv("OPENGLAD_CONFIG_DIR", test_config_dir.c_str(), 1);

    init_logging();
    if (!init_unit_filesystem(test_config_dir, argc > 0 ? argv[0] : nullptr))
    {
        std::error_code ec;
        std::filesystem::remove_all(test_config_dir, ec);
        return 1;
    }

    // Entity code (living/walker) dereferences current_session->current_difficulty_.
    // Provide a zero-initialized session so set_difficulty() doesn't segfault.
    og::runtime::GameSession::Config cfg{};
    cfg.allocate_screen = false;
    cfg.allocate_prefs = false;
    cfg.install_legacy_globals = true;
    og::runtime::GameSession session(cfg);
    GameWorld fallback_world(0);
    SaveData fallback_save;
    og::sim::SimEventLog fallback_events;

    session.game_.world = &fallback_world;
    session.game_.save = &fallback_save;
    session.game_.sim_events = &fallback_events;
    current_game = &session.game_;

    init_all_registries();
    og::test::mount_core_pack();

    ::testing::TestEventListeners& listeners =
        ::testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new HeadlessSessionListener(
        session, fallback_world, fallback_save, fallback_events));

    const int result = RUN_ALL_TESTS();

    int exit_code = result;
    if (!registry_leaks().empty())
    {
        std::fprintf(stderr,
                     "\nREGISTRY LEAK: %zu test(s) left the process family "
                     "registries, pack stores or campaign mount changed:\n",
                     registry_leaks().size());
        for (const std::string& leak : registry_leaks())
            std::fprintf(stderr, "  %s\n", leak.c_str());
        std::fprintf(stderr,
                     "A test that edits any of them restores it itself — the "
                     "between-test heal repairs the pack stores only, and only "
                     "after the damage has been handed on.\n");
        exit_code = 1;
    }

    (void)og::resources::deinit();
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    std::error_code ec;
    std::filesystem::remove_all(test_config_dir, ec);
    return exit_code;
}
