#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include <openglad/core/irandom.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/save_data.h>

#ifdef ENABLE_COVERAGE
extern "C" void __gcov_dump(void);
#endif

namespace og::runtime {

static SessionState s_headless_session{};
thread_local SessionState* current_session = &s_headless_session;
std::atomic<SessionState*> primary_session{&s_headless_session};
std::atomic<GameplayContext*> primary_game{&s_headless_session.game_};

} // namespace og::runtime

void popup_dialog(const char* title, const char* message)
{
    std::fprintf(stderr, "[%s] %s\n", title, message);
}

std::uint32_t random(std::uint32_t x)
{
    static std::uint32_t state = 12345;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

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
