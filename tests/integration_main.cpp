#include <gtest/gtest.h>

#include <SDL.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <mutex>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#ifdef ENABLE_COVERAGE
extern "C" void __gcov_dump(void);
#endif

#include <openglad/core/test_trace.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_context.h>
#include <openglad/platform/screen_lifecycle.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io.h>

namespace {

void handle_test_signal(int sig)
{
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    _exit(128 + sig);
}

class WorldCleanupListener final : public ::testing::EmptyTestEventListener
{
public:
    void OnTestEnd(const ::testing::TestInfo&) override
    {
        if (og::runtime::current_session != nullptr &&
            og::runtime::current_session->myscreen_ != nullptr)
        {
            og::runtime::current_session->myscreen_->world().delete_objects();
        }
    }
};

std::mutex s_allbuttons_mutex;

} // namespace

std::mutex& get_allbuttons_mutex()
{
    return s_allbuttons_mutex;
}

int main(int argc, char** argv)
{
#ifdef __linux__
    (void)prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() == 1)
    {
        _exit(1);
    }
#endif
    std::signal(SIGINT, handle_test_signal);
    std::signal(SIGTERM, handle_test_signal);

    ::testing::InitGoogleTest(&argc, argv);

    const auto test_config_dir = std::filesystem::temp_directory_path() /
        ("openglad_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_config_dir);
    setenv("OPENGLAD_CONFIG_DIR", test_config_dir.c_str(), 1);

    SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);

    init_logging();
    SDL_Init(SDL_INIT_VIDEO);
    io_init(argc, argv);
    cfg.apply_setting("graphics", "overscan_percentage", "0");

    create_global_screen(1);
    init_input();

    og::runtime::current_session->overscan_percentage_ = static_cast<float>(
        parse_int_strict(cfg.get_setting("graphics", "overscan_percentage")).value_or(0)) / 100.0f;
    update_overscan_setting();
    cfg.apply_setting("graphics", "overscan_percentage",
        std::format("{:.0f}", 100 * og::runtime::current_session->overscan_percentage_));

    static og::sim::SimEventLog test_events;
    static ProductionRandom test_rng;
    og::runtime::current_session->myscreen_->level_runtime_data().set_sim_context(
        &og::runtime::current_session->myscreen_->save_data,
        &og::runtime::current_session->myscreen_->world().enemy_freeze,
        &test_events,
        &test_rng,
        &cfg);

    ::testing::TestEventListeners& listeners =
        ::testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new WorldCleanupListener);

    const int result = RUN_ALL_TESTS();

    std::error_code ec;
    std::filesystem::remove_all(test_config_dir, ec);

#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    std::fflush(nullptr);
    _exit(result);
}
