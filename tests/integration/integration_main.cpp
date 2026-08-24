#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <unistd.h>
#include <utility>
#include <vector>

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
#include <openglad/gameplay/script/script_coverage.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/game_context.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/platform/screen_lifecycle.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io.h>

extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
// Main-thread task queue, declared for injectors in tests/test_interact.h and
// defined at the bottom of this file.
void drain_main_thread_tasks();
#ifdef TESTING
extern bool g_test_remove_exits;
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
void picker_testing_yes_or_no_queue_clear();
void picker_testing_set_force_real_dialogs(bool enabled);
void campaign_picker_testing_set_auto_accept(bool enabled);
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
            picker.backdrops[static_cast<std::size_t>(i)].reset();
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
            // The camera too, not just the control walker: a test that leaves
            // a panned camera on a shared view would otherwise displace every
            // pixie draw in the tests declared after it.
            view->topx = 0;
            view->topy = 0;
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
    // #237: a test that aborted mid-screen (or left an override pending) must
    // not leak transition state into the tests declared after it — and the
    // window it left (black after an exit fade, or showing its last frame)
    // must not either: every canvas counts as presented as it stands.
    og::ui::menu_transition_testing_reset();
    if (og::runtime::current_session->myscreen_ != nullptr)
        og::runtime::current_session->myscreen_->testing_reset_window_state();
    campaign_picker_testing_set_auto_accept(true);
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

// --- Main-thread task queue (issue #257) -----------------------------------
// Injector threads post state mutations here; the menu runner drains them at
// the top of each frame. See tests/test_interact.h for the contract.
std::mutex s_main_thread_task_mutex;
std::condition_variable s_main_thread_task_cv;
std::vector<std::pair<std::uint64_t, std::function<void()>>>
    s_main_thread_tasks;
std::uint64_t s_main_thread_task_next = 0;
// High-water mark of tickets that actually RAN. Tasks run in post order, so
// one mark answers every waiter — as long as the discarded set below is
// consulted FIRST: cancelling ticket N and then running N+1 would otherwise
// report N as run.
std::uint64_t s_main_thread_task_settled = 0;
// Tickets resolved by cancellation instead of execution (a wait that timed
// out, or a drain at a test boundary). A waiter on one of these gets false.
std::set<std::uint64_t> s_main_thread_tasks_discarded;
// Bumped by every drain. A batch the pump already swapped out of the queue
// re-checks it before each task, so a task belonging to a finished test can
// never run inside the next one.
std::uint64_t s_main_thread_task_generation = 0;

// What a waiter sees for its ticket.
enum class MainThreadTaskFate
{
    Pending,
    Ran,
    Discarded,
};

MainThreadTaskFate main_thread_task_fate_locked(std::uint64_t ticket)
{
    if (s_main_thread_tasks_discarded.count(ticket) != 0)
        return MainThreadTaskFate::Discarded;
    if (s_main_thread_task_settled >= ticket)
        return MainThreadTaskFate::Ran;
    return MainThreadTaskFate::Pending;
}

// Removes a still-queued task and marks it discarded. Returns false when the
// ticket is no longer in the queue (already run, or mid-flight on the pump).
bool cancel_queued_main_thread_task_locked(std::uint64_t ticket)
{
    for (auto it = s_main_thread_tasks.begin(); it != s_main_thread_tasks.end();
         ++it)
    {
        if (it->first != ticket)
            continue;
        s_main_thread_tasks.erase(it);
        s_main_thread_tasks_discarded.insert(ticket);
        return true;
    }
    return false;
}

// Installed as og::ui::g_picker_main_thread_pump. Tasks run OUTSIDE the queue
// lock (they re-enter menu/lobby code freely) and settle in post order.
void run_main_thread_tasks()
{
    std::vector<std::pair<std::uint64_t, std::function<void()>>> batch;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(s_main_thread_task_mutex);
        batch.swap(s_main_thread_tasks);
        generation = s_main_thread_task_generation;
    }
    for (auto& [ticket, task] : batch)
    {
        bool run_it = false;
        {
            std::lock_guard<std::mutex> lock(s_main_thread_task_mutex);
            // A drain (or this ticket's own timeout) landed while the batch
            // was running: the rest of it belongs to a scope that is over.
            run_it = s_main_thread_task_generation == generation &&
                     s_main_thread_tasks_discarded.count(ticket) == 0;
        }
        if (run_it && task)
            task();
        {
            std::lock_guard<std::mutex> lock(s_main_thread_task_mutex);
            if (run_it)
                s_main_thread_task_settled = ticket;
            else
                s_main_thread_tasks_discarded.insert(ticket);
        }
        s_main_thread_task_cv.notify_all();
    }
}

class WorldCleanupListener final : public ::testing::EmptyTestEventListener
{
public:
    void OnTestStart(const ::testing::TestInfo&) override
    {
        drain_main_thread_tasks();
        og::ui::g_picker_main_thread_pump = &run_main_thread_tasks;
        reset_integration_ui_state();
        // After the window reset above: the reset itself never fades.
        og::video_testing::reset_fade_violations();
    }

    void OnTestEnd(const ::testing::TestInfo&) override
    {
        og::ui::g_picker_main_thread_pump = nullptr;
        drain_main_thread_tasks();
        // Fade-ownership invariants (video_sdl.h): a fade-in over a window
        // that is not black, or a fade-out of a buffer the window never
        // showed. Every flow test in this binary is an oracle for the class;
        // read BEFORE the reset below so a violation always fails the test
        // that caused it.
        for (const std::string& violation :
             og::video_testing::fade_violation_messages())
        {
            ADD_FAILURE() << "FADE VIOLATION: " << violation
                          << " — see docs/menu-engine.md, \"Drawing and "
                             "transitions\"";
        }
        reset_integration_ui_state();
        og::video_testing::reset_fade_violations();
    }
};

std::mutex s_allbuttons_mutex;

// [SAVE-R5](c) stray-slot sweeps: opt-in pre-seeding of stray company slots
// into the fresh per-PID config dir BEFORE any test boots the picker.
// OPENGLAD_TEST_SEED_STRAY_SLOTS holds comma-separated slot names; each
// becomes a loadable v14 company with an old last-played stamp (any save0 a
// test writes with a real-now stamp outranks it — the design's contract that
// legitimate flows MUST stamp). The first seeded slot carries a one-soldier
// roster so roster-adjacent leaks are observable too.
void seed_stray_company_slots_from_env()
{
    const char* raw = std::getenv("OPENGLAD_TEST_SEED_STRAY_SLOTS");
    if (raw == nullptr || raw[0] == '\0')
        return;
    std::int64_t stamp = 1000000000; // 2001 — older than any real-now stamp
    bool with_soldier = true;
    const std::string list(raw);
    for (std::size_t start = 0; start < list.size();)
    {
        std::size_t end = list.find(',', start);
        if (end == std::string::npos)
            end = list.size();
        const std::string slot = list.substr(start, end - start);
        start = end + 1;
        if (slot.empty())
            continue;
        SaveData sd;
        sd.reset();
        sd.save_name = "STRAY " + slot;
        sd.current_campaign = "gladiator";
        sd.last_played_unix_s = stamp++;
        if (with_soldier)
        {
            auto member = std::make_unique<guy>(FAMILY_SOLDIER);
            member->name = "STRAYGUY";
            member->teamnum = 0;
            member->deployed = true;
            sd.team_list[0] = std::move(member);
            sd.team_size = 1;
            with_soldier = false;
        }
        if (sd.save_with_error(slot) != SaveDataIoError::None)
            std::fprintf(stderr, "stray-slot seed FAILED for '%s'\n",
                         slot.c_str());
        else
            std::fprintf(stderr, "stray-slot seeded: '%s'\n", slot.c_str());
    }
}

} // namespace

std::mutex& get_allbuttons_mutex()
{
    return s_allbuttons_mutex;
}

std::uint64_t post_main_thread_task(std::function<void()> task)
{
    std::lock_guard<std::mutex> lock(s_main_thread_task_mutex);
    const std::uint64_t ticket = ++s_main_thread_task_next;
    s_main_thread_tasks.emplace_back(ticket, std::move(task));
    return ticket;
}

bool wait_for_main_thread_task(std::uint64_t ticket, int timeout_ms)
{
    const auto ticket_id = static_cast<unsigned long long>(ticket);
    std::unique_lock<std::mutex> lock(s_main_thread_task_mutex);
    MainThreadTaskFate fate = MainThreadTaskFate::Pending;
    const bool resolved = s_main_thread_task_cv.wait_for(
        lock, std::chrono::milliseconds(timeout_ms), [ticket, &fate] {
            fate = main_thread_task_fate_locked(ticket);
            return fate != MainThreadTaskFate::Pending;
        });
    if (resolved)
    {
        if (fate == MainThreadTaskFate::Ran)
            return true;
        std::fprintf(stderr,
                     "  [interact] main-thread task %llu was DISCARDED before "
                     "it ran\n",
                     ticket_id);
        return false;
    }
    // Timed out. Cancel the ticket so it can never run later — against a
    // caller's stack that has since gone away, or inside the next test.
    if (cancel_queued_main_thread_task_locked(ticket))
    {
        std::fprintf(stderr,
                     "  [interact] TIMEOUT waiting for main-thread task %llu "
                     "(%d ms); cancelled while still queued\n",
                     ticket_id, timeout_ms);
        return false;
    }
    // Not in the queue: it either settled in the instant the wait expired, or
    // the pump is running it right now. A running task cannot be cancelled,
    // so wait for it to finish rather than returning to a scope it may still
    // be reading.
    if (main_thread_task_fate_locked(ticket) == MainThreadTaskFate::Ran)
        return true;
    std::fprintf(stderr,
                 "  [interact] TIMEOUT waiting for main-thread task %llu "
                 "(%d ms); it is already running — waiting it out\n",
                 ticket_id, timeout_ms);
    const bool finished = s_main_thread_task_cv.wait_for(
        lock, std::chrono::milliseconds(timeout_ms), [ticket, &fate] {
            fate = main_thread_task_fate_locked(ticket);
            return fate != MainThreadTaskFate::Pending;
        });
    if (finished && fate == MainThreadTaskFate::Ran)
        return true;
    if (!finished)
    {
        // The pump never came back. Nothing can make this safe; say so loudly
        // and report the task as not run.
        s_main_thread_tasks_discarded.insert(ticket);
        std::fprintf(stderr,
                     "  [interact] main-thread task %llu NEVER FINISHED (%d "
                     "ms); its captures may still be read\n",
                     ticket_id, timeout_ms);
    }
    return false;
}

void drain_main_thread_tasks()
{
    {
        std::lock_guard<std::mutex> lock(s_main_thread_task_mutex);
        s_main_thread_tasks.clear();
        // Every posted ticket that has not run yet is discarded: the queued
        // ones cleared just now, plus anything a batch still holds (the
        // generation bump stops the pump from running those). A task the
        // pump happens to be executing at this instant is counted with them
        // — it belongs to a scope that is already over, so its waiter is
        // told "not run" rather than being handed a result nobody can use.
        for (std::uint64_t ticket = s_main_thread_task_settled + 1;
             ticket <= s_main_thread_task_next; ++ticket)
            s_main_thread_tasks_discarded.insert(ticket);
        ++s_main_thread_task_generation;
    }
    s_main_thread_task_cv.notify_all();
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
    seed_stray_company_slots_from_env();
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
    // Same reason as __gcov_dump above: _exit() skips static destructors, so
    // the pack-Lua recorder's exit-time dump would never happen. No-op unless
    // OPENGLAD_LUA_COVERAGE armed it.
    og::script::coverage::flush_to_output_dir();
    std::fflush(nullptr);
    _exit(result);
}
