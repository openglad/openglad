#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include <openglad/core/irandom.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/packs.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/resources/save_data.h>

#ifdef ENABLE_COVERAGE
extern "C" void __gcov_dump(void);
#endif

std::string get_asset_path();

namespace og::runtime {

static SessionState s_headless_session{};
thread_local SessionState* current_session = &s_headless_session;
std::atomic<SessionState*> primary_session{&s_headless_session};
std::atomic<GameplayContext*> primary_game{&s_headless_session.game_};

} // namespace og::runtime

namespace {

bool init_unit_filesystem(const std::filesystem::path& test_config_dir,
                          const char* argv0)
{
    std::error_code ec;
    std::filesystem::create_directories(test_config_dir / "campaigns", ec);
    std::filesystem::create_directories(test_config_dir / "save", ec);
    std::filesystem::create_directories(test_config_dir / "cfg", ec);

    const char* physfs_argv0 =
        (argv0 != nullptr && argv0[0] != '\0') ? argv0 : "og_headless_unit_tests";
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

// Family data and behavior both live in the core class pack: the five
// registries start empty and are filled by install_classpacks() from the
// mounted packs/ tree. A headless unit binary
// that skips io_init's asset mounts would otherwise run against registries
// where every get_*_family_descriptor answers nullptr — no soldier, no
// knife, no specials. Mount the shipped packs/ tree the same way io_init
// does. Idempotent, and re-asserted after every test: a test that tears
// PhysFS down (or remounts a campaign, which rescans packs/) must not leave
// later tests with no families at all.
void mount_core_pack()
{
    const bool mounted = og::resources::mount(
        (get_asset_path() + "packs/").c_str(), "packs/", 1);
    if (!mounted)
    {
        std::fprintf(stderr,
                     "error: core class pack not mounted (%s) — the family "
                     "registries will be empty\n",
                     og::resources::filesystem_last_error().c_str());
        return;
    }
    // Family chunks carry the shipped families' declarations AND their
    // hooks, so a test that isolated itself by clearing them needs the same
    // heal a cleared script registry gets.
    if (og::script::pack_scripts().empty() ||
        og::script::pack_family_chunks().empty())
        og::resources::refresh_pack_scripts();
}

class HeadlessSessionListener final : public ::testing::EmptyTestEventListener
{
public:
    HeadlessSessionListener(og::runtime::SessionState& session,
                            GameWorld& fallback_world,
                            SaveData& fallback_save)
        : session_(session)
        , fallback_world_(fallback_world)
        , fallback_save_(fallback_save)
    {
    }

    void OnTestStart(const ::testing::TestInfo&) override
    {
        if (!gameplay_context_intact())
        {
            ADD_FAILURE() << "headless session context corrupted before test";
            restore_context();
        }
    }

    void OnTestEnd(const ::testing::TestInfo&) override
    {
        if (!gameplay_context_intact())
        {
            ADD_FAILURE() << "headless session context corrupted by test";
            restore_context();
        }

        fallback_world_.delete_objects();
        restore_context();
        // Structural filesystem reset: a test that tears PhysFS down or
        // drops the user-dir mount must not leave later tests without the
        // core pack (and therefore without any family).
        if (!og::resources::is_initialized())
            (void)og::resources::init("og_headless_unit_tests");
        mount_core_pack();
    }

private:
    bool gameplay_context_intact() const
    {
        return og::runtime::current_session == &session_ &&
               current_game == &session_.game_ &&
               session_.game_.world == &fallback_world_ &&
               session_.game_.save == &fallback_save_ &&
               session_.game_.sim_events == session_.ctx_.sim_events.get();
    }

    void restore_context()
    {
        og::runtime::current_session = &session_;
        og::runtime::primary_session.store(&session_, std::memory_order_release);
        current_game = &session_.game_;
        og::runtime::primary_game.store(&session_.game_, std::memory_order_release);
        session_.game_.world = &fallback_world_;
        session_.game_.save = &fallback_save_;
        session_.game_.sim_events = session_.ctx_.sim_events.get();
        session_.game_.config = &cfg;
        session_.game_.session_rng_ref = &session_.ctx_.rng;
        session_.game_.gameplay_active_ref = &session_.gameplay_active_;
        session_.gameplay_active_ = false;
    }

    og::runtime::SessionState& session_;
    GameWorld& fallback_world_;
    SaveData& fallback_save_;
};

} // namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    const auto test_config_dir = std::filesystem::temp_directory_path() /
        ("openglad_headless_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_config_dir);
    setenv("OPENGLAD_CONFIG_DIR", test_config_dir.c_str(), 1);

    init_logging();
    if (!init_unit_filesystem(test_config_dir, argc > 0 ? argv[0] : nullptr))
    {
        std::error_code ec;
        std::filesystem::remove_all(test_config_dir, ec);
        return 1;
    }

    static FixedRandom test_rng{0};
    static GameWorld fallback_world(0);
    static SaveData fallback_save;
    og::runtime::SessionState& session = og::runtime::s_headless_session;
    session.ctx_.rng = &test_rng;
    session.game_.world = &fallback_world;
    session.game_.save = &fallback_save;
    session.game_.sim_events = session.ctx_.sim_events.get();
    session.game_.config = &cfg;
    session.game_.session_rng_ref = &session.ctx_.rng;
    session.game_.gameplay_active_ref = &session.gameplay_active_;
    current_game = &session.game_;

    init_all_registries();
    mount_core_pack();
    ::testing::TestEventListeners& listeners =
        ::testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new HeadlessSessionListener(session, fallback_world, fallback_save));

    const int result = RUN_ALL_TESTS();

    (void)og::resources::deinit();
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    std::error_code ec;
    std::filesystem::remove_all(test_config_dir, ec);
    return result;
}
