#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdio>
#include <filesystem>

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

#ifdef ENABLE_COVERAGE
extern "C" void __gcov_dump(void);
#endif

std::string get_asset_path();

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

// Family behavior lives in the core class pack. Its descriptors carry no
// C++ behavior callbacks, so a headless unit
// binary that skips io_init's asset mounts would run a sim whose specials,
// potions and effects all silently do nothing. Mount the shipped packs/ tree
// the same way io_init does. Idempotent, and re-asserted after every test:
// a test that tears PhysFS down (or remounts a campaign, which rescans
// packs/) must not leave later tests with no family behavior at all.
void mount_core_pack()
{
    const bool mounted = og::resources::mount(
        (get_asset_path() + "packs/").c_str(), "packs/", 1);
    if (!mounted)
    {
        std::fprintf(stderr,
                     "error: core class pack not mounted (%s) — family "
                     "behavior will be absent\n",
                     og::resources::filesystem_last_error().c_str());
        return;
    }
    if (og::script::pack_scripts().empty())
        og::resources::refresh_pack_scripts();
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
    }

    void OnTestEnd(const ::testing::TestInfo&) override
    {
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
        mount_core_pack();
    }

private:
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
};

} // namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    // Some unit tests construct a real screen, whose sound object opens an
    // SDL audio device.  Keep that path deterministic on headless runners:
    // the legacy sound setup exits successfully when no device is available,
    // which would otherwise make CTest mistake a truncated suite for a pass.
    setenv("SDL_AUDIODRIVER", "dummy", 1);

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
    mount_core_pack();

    ::testing::TestEventListeners& listeners =
        ::testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new HeadlessSessionListener(
        session, fallback_world, fallback_save, fallback_events));

    const int result = RUN_ALL_TESTS();

    (void)og::resources::deinit();
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    std::error_code ec;
    std::filesystem::remove_all(test_config_dir, ec);
    return result;
}
