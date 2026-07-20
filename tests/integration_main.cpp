#include <gtest/gtest.h>

#include <SDL3/SDL.h>

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
#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/game_context.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/platform/screen_lifecycle.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io.h>

extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
#ifdef TESTING
extern bool g_test_remove_exits;
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
void picker_testing_yes_or_no_queue_clear();
void picker_testing_set_force_real_dialogs(bool enabled);
#endif

namespace {

void reset_integration_ui_state()
{
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    clear_keyboard();
    clear_key_press_event();
    set_game_speed(1.0f);
    // [SAVE-R8] Structural active-company reset: a test that repoints the
    // process-wide slot (directly or via ScopedActiveCompany misuse) must not
    // leak it into later tests under --gtest_shuffle.
    (void)og::data::set_active_company_slot("save0");

    if (og::runtime::current_session == nullptr)
        return;

    if (og::runtime::current_game_session != nullptr)
        og::runtime::clear_local_transport_shadow(
            *og::runtime::current_game_session);
    if (current_game != nullptr && current_game->sim_events != nullptr)
        current_game->sim_events->clear();

    if (og::runtime::current_session->input_hw_ != nullptr) {
        og::runtime::current_session->input_hw_->mouse = {};
        og::runtime::current_session->input_hw_->mouse_buttons = 0;
        og::runtime::current_session->input_hw_->picker_was_left_down = false;
        og::runtime::current_session->input_hw_->picker_was_right_down = false;
    }

    if (og::runtime::current_session->picker_ != nullptr) {
        PickerState& picker = *og::runtime::current_session->picker_;
        for (int i = 0; i < 5; i++) {
            picker.backdrops[i].reset();
            picker.backpics[i].free();
        }
        picker.main_columns_pix.reset();
        picker.main_columns_data.free();
        picker.main_title_logo_pix.reset();
        picker.main_title_logo_data.free();
        picker.old_guy = nullptr;
        picker.menu_nav_enabled = false;
        picker.menu_nav_enabled_time = 0;
        picker.intercept_scope = 0;
        picker.selected_menu_item = nullptr;
        picker.hire_session = nullptr;
        picker.train_session = nullptr;
    }

    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    og::runtime::current_session->raw_key_ = 0;
    og::runtime::current_session->raw_text_input_.clear();
    og::runtime::current_session->text_input_event_ = 0;
    og::runtime::current_session->scroll_amount_ = 0;
    og::runtime::current_session->input_continue_ = false;
    og::runtime::current_session->pending_timer_wait_request_ =
        kNoTimerWaitRequest;
    og::runtime::current_session->current_guy_.reset();
    og::runtime::current_session->current_type_ = 0;
    og::runtime::current_session->current_team_num_ = 0;
    og::runtime::current_session->editguy_ = 0;
    og::runtime::current_session->current_difficulty_ = 1;
    og::runtime::current_session->message_.clear();
    og::runtime::current_session->debug_draw_paths_ = false;
    og::runtime::current_session->debug_draw_obmap_ = false;
    og::runtime::current_session->frame_state_ = {};
    og::runtime::current_session->ctx_.input = {};
    og::runtime::current_session->replay_recorder_.reset();
    og::runtime::current_session->replay_output_path_.clear();
    og::runtime::current_session->gameplay_active_ = false;
    og::runtime::current_session->help_end_of_file_ = 0;

    if (og::runtime::current_session->myscreen_ != nullptr) {
        // Preserve the world grid across the inter-test reset; PR #28 added
        // tests that rely on a previously-created grid persisting between
        // sibling tests in the same binary.
        PixieData saved_grid = std::move(
            og::runtime::current_session->myscreen_->world().grid);
        const std::int32_t saved_pixmaxx =
            og::runtime::current_session->myscreen_->world().pixmaxx;
        const std::int32_t saved_pixmaxy =
            og::runtime::current_session->myscreen_->world().pixmaxy;
        og::runtime::current_session->myscreen_->level_runtime_data().clear();
        og::runtime::current_session->myscreen_->world().grid =
            std::move(saved_grid);
        og::runtime::current_session->myscreen_->world().pixmaxx = saved_pixmaxx;
        og::runtime::current_session->myscreen_->world().pixmaxy = saved_pixmaxy;
        og::runtime::current_session->myscreen_->world().reset_level_progress();
        og::runtime::current_session->myscreen_->world().delete_objects();
        og::runtime::current_session->myscreen_->world().control_hp = 0.0f;
        og::runtime::current_session->myscreen_->world().game_ended = false;
        og::runtime::current_session->myscreen_->world().end = 0;
        og::runtime::current_session->myscreen_->world().retry = 0;
        og::runtime::current_session->myscreen_->world().next_level = -1;
        og::runtime::current_session->myscreen_->world().ending = 0;
        og::runtime::current_session->myscreen_->world().level_done = 0;
        og::runtime::current_session->myscreen_->world().withdraw_requested = false;
        og::runtime::current_session->myscreen_->world().withdraw_level = -1;
        for (auto& view : og::runtime::current_session->myscreen_->viewob) {
            if (view == nullptr)
                continue;
            view->control = nullptr;
        }
    }

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 0;
#ifdef TESTING
    g_test_remove_exits = false;
    g_test_in_game.store(false, std::memory_order_release);
    g_test_game_epoch.store(0, std::memory_order_release);
    picker_testing_yes_or_no_queue_clear();
    picker_testing_set_force_real_dialogs(false);
#endif
    results_screen_testing_set_force_full(false);
}

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
    void OnTestStart(const ::testing::TestInfo&) override
    {
        reset_integration_ui_state();
    }

    void OnTestEnd(const ::testing::TestInfo&) override
    {
        reset_integration_ui_state();
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
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif

    ::testing::InitGoogleTest(&argc, argv);

    const auto test_config_dir = std::filesystem::temp_directory_path() /
        ("openglad_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_config_dir);
    setenv("OPENGLAD_CONFIG_DIR", test_config_dir.c_str(), 1);

    // Default to the offscreen driver but let the environment override it:
    // offscreen probes EGL, which can block forever on a wedged GPU driver
    // (SDL_VIDEODRIVER=dummy is the pure-software escape hatch). offscreen
    // requires OpenGL, which is unavailable on macOS, so default to dummy there.
#if defined(__APPLE__)
    SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 0);
#else
    SDL_setenv_unsafe("SDL_VIDEODRIVER", "offscreen", 0);
#endif
    SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 0);

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
