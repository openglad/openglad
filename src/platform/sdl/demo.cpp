/*
 * openglad_demo: Runs N independent AI-controlled game sessions in a grid,
 * sized dynamically to fit the display at native resolution.
 *
 * Each session loads a random scenario and spawns a random player team
 * (matching the enemy count and levels) to fight AI-vs-AI battles.
 *
 * Threading model:
 *   - N worker threads: each runs one session's simulation (act, AI, input)
 *   - 1 main thread: SDL event polling, per-session rendering, compositing
 *
 * Per-frame synchronization:
 *   1. Main thread signals all workers to tick one simulation frame
 *   2. Workers run game_frame() with enable_render=false (simulation only)
 *   3. Workers signal completion
 *   4. Main thread renders each session to its surface (sequential, via SessionScope)
 *   5. Main thread composites all surfaces and presents to SDL display
 *
 * SDL thread safety:
 *   - SDL_PollEvent: main thread only
 *   - SDL_RenderPresent/Copy/Clear: main thread only
 *   - SDL_UpdateTexture: main thread only
 *   - E_Screen->render swap: main thread only (via SessionScope)
 *   - Per-session SDL_Surface pixel writes: each worker writes only to its own surface
 *   - SDL_mixer (Mix_PlayChannel): thread-safe in SDL_mixer 2.x
 */

#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/core/version.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/legacy/base.h>
#include <openglad/resources/io.h>
#include <openglad/platform/sai2x.h>
#include <openglad/interface/render/view.h>
#include <openglad/platform/game_context.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/screen.h>
#include "SDL.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <format>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

// External declarations
void init_input();
short load_saved_game(const char* filename, screen* scr);

inline constexpr int CELL_W = 320;
inline constexpr int CELL_H = 200;

// Scenario pool — diverse levels from the main campaign plus bonus maps.
static const std::array<int, 20> SCENARIO_POOL = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16,
    9411, 9412, 9413, 9414,
};

// Playable families for random team generation.
static constexpr int PLAYABLE_FAMILIES[] = {
    FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
    FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL, FAMILY_FAERIE,
    FAMILY_SLIME, FAMILY_THIEF, FAMILY_GHOST, FAMILY_DRUID,
    FAMILY_ORC, FAMILY_BARBARIAN,
};
static constexpr int NUM_PLAYABLE = static_cast<int>(std::size(PLAYABLE_FAMILIES));

struct DemoSession {
    std::unique_ptr<og::runtime::GameSession> session;
    std::atomic<bool> finished{false};
};

// ---------------------------------------------------------------------------
// Worker synchronization
// ---------------------------------------------------------------------------
// Each frame:
//   main thread increments generation → workers wake, simulate, signal done
//   main thread waits for all workers done → renders → repeats
struct WorkerSync {
    std::mutex mtx;
    std::condition_variable start_cv;   // main → workers: "start ticking"
    std::condition_variable done_cv;    // workers → main: "I'm done"
    int generation = 0;                 // incremented each frame by main
    int workers_done = 0;              // count of workers finished this frame
    int num_workers = 0;               // total worker count
    bool shutdown = false;              // signal workers to exit
};

// ---------------------------------------------------------------------------
// Spawn a random player team that matches the enemy composition
// ---------------------------------------------------------------------------
// Scans the level for enemy living entities, then creates a player team
// with the same size and roughly matching levels but randomized classes.
static void spawn_random_player_team(screen* s, std::mt19937& rng)
{
    // Collect enemy levels
    std::vector<int> enemy_levels;
    for (auto& uptr : s->world().oblist) {
        walker* w = uptr.get();
        if (w && !w->dead() && w->order() == Order::Living && w->team_num() != 0) {
            enemy_levels.push_back(static_cast<int>(w->stats()->level()));
        }
    }

    if (enemy_levels.empty()) return;

    std::uniform_int_distribution<int> fam_dist(0, NUM_PLAYABLE - 1);

    for (int enemy_level : enemy_levels) {
        int fam = PLAYABLE_FAMILIES[fam_dist(rng)];
        guy g(fam);
        g.teamnum = 0;
        if (enemy_level > g.level)
            g.upgrade_to_level(static_cast<short>(enemy_level));

        walker* w = guy_create_and_add_walker(g, s);
        if (w) {
            w->set_team_num(0);
            w->teleport();
        }
    }
}

// ---------------------------------------------------------------------------
// Session initialization (called from main thread)
// ---------------------------------------------------------------------------
static void init_session_game(DemoSession& demo, int scen_id, std::mt19937& rng)
{
    auto scope = demo.session->activate();
    screen* s = og::runtime::current_session->myscreen_;
    if (!s) return;

    s->save_data.numplayers = 0; // spectator mode
    s->save_data.scen_num = static_cast<short>(scen_id);
    s->save_data.current_campaign = "org.openglad.gladiator";

    if (!s->save_data.save("save0")) {
        throw std::runtime_error(std::format(
            "openglad_demo failed to bootstrap save0 for scenario {}",
            scen_id));
    }
    if (load_saved_game("save0", s) == 0) {
        throw std::runtime_error(std::format(
            "openglad_demo failed to load bootstrap save0 for scenario {}",
            scen_id));
    }

    // Spawn a random player team to fight the enemies
    spawn_random_player_team(s, rng);

    // Point the camera at a player team member.  load_saved_game set
    // view->control before the team existed, so it's still nullptr.
    if (s->viewob[0]) {
        s->viewob[0]->control = s->viewob[0]->find_next_control();
    }

    s->continuous_input();
    s->redrawme = 1;
    s->framecount = 0;
    s->timerstart = query_timer_control();
    demo.session->frame_state_ = {};
    demo.finished.store(false, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Worker thread function
// ---------------------------------------------------------------------------
// Each worker thread owns one session. It installs the session as thread_local
// current_session and waits for the main thread to signal each frame.
static void worker_thread_func(WorkerSync& sync, DemoSession& demo, int session_idx)
{
    // Install this session as thread_local current_session and current_game.
    og::runtime::current_session = demo.session.get();
    current_game = &demo.session->game_;

    // Simulation-only deps: no rendering, no event polling, no frame timing.
    // The main thread handles all of these.
    GameLoopDeps sim_deps;
    sim_deps.enable_render = false;
    sim_deps.enable_event_poll = false;
    sim_deps.enable_frame_timing = false;

    int last_gen = 0;

    while (true) {
        // Wait for the main thread to signal the next frame.
        {
            std::unique_lock lock(sync.mtx);
            sync.start_cv.wait(lock, [&] {
                return sync.generation > last_gen || sync.shutdown;
            });
            if (sync.shutdown) break;
            last_gen = sync.generation;
        }

        // Run one simulation tick if this session is still active.
        if (!demo.finished.load(std::memory_order_relaxed)) {
            screen* s = demo.session->myscreen_;
            if (s) {
                GameLoopFrameState& fs = demo.session->frame_state_;
                bool done = game_frame(*s, fs, sim_deps);
                if (done) {
                    demo.finished.store(true, std::memory_order_relaxed);
                    Log("Session {} finished (worker thread)\n", session_idx);
                }
            }
        }

        // Signal completion to the main thread.
        {
            std::lock_guard lock(sync.mtx);
            sync.workers_done++;
        }
        sync.done_cv.notify_one();
    }
}

// ---------------------------------------------------------------------------
// Render one session's frame to its surface (main thread only)
// ---------------------------------------------------------------------------
// This does the rendering half of game_frame: redraw + refresh.
// Must be called with the session's SessionScope active.
static void render_session_frame(screen& s)
{
    if (s.redrawme) {
        s.draw_panels(s.numviews);
        s.refresh();
        s.redrawme = 0;
    }
    s.redraw();
    s.refresh();
}

// Blit a 320x200 session surface into a sub-region of the composite surface.
static void composite_session(SDL_Surface* dst, SDL_Surface* src,
                              int grid_col, int grid_row)
{
    if (!src || !dst) return;
    SDL_Rect dst_rect;
    dst_rect.x = grid_col * CELL_W;
    dst_rect.y = grid_row * CELL_H;
    dst_rect.w = CELL_W;
    dst_rect.h = CELL_H;
    SDL_BlitSurface(src, nullptr, dst, &dst_rect);
}

int main(int argc, char* argv[])
{
    try
    {
        init_logging();
        io_init(argc, argv);

        cfg.load_settings();

        int max_frames = 0;
        if (const char* max_frames_env = std::getenv("OPENGLAD_DEMO_MAX_FRAMES")) {
            max_frames = std::max(0, std::atoi(max_frames_env));
        }

        // Detect native display resolution for fullscreen grid layout
        SDL_Init(SDL_INIT_VIDEO);
        SDL_DisplayMode dm;
        int display_w = 0;
        int display_h = 0;
        if (SDL_GetDesktopDisplayMode(0, &dm) != 0) {
            if (max_frames > 0) {
                display_w = CELL_W;
                display_h = CELL_H;
                LogWarn("SDL_GetDesktopDisplayMode failed in smoke mode: {}. Using {}x{} fallback.\n",
                        SDL_GetError(), display_w, display_h);
            } else {
                LogError("SDL_GetDesktopDisplayMode failed: {}\n", SDL_GetError());
                return 1;
            }
        } else {
            display_w = dm.w;
            display_h = dm.h;
        }
        const int grid_cols = display_w / CELL_W;
        const int grid_rows = display_h / CELL_H;
        const int num_sessions = grid_cols * grid_rows;

        Log("Display: {}x{}, grid: {}x{} = {} sessions\n",
            display_w, display_h, grid_cols, grid_rows, num_sessions);

        if (num_sessions <= 0) {
            LogError("Display too small for even one {}x{} cell\n", CELL_W, CELL_H);
            return 1;
        }

        cfg.apply_setting("graphics", "width", std::format("{}", display_w));
        cfg.apply_setting("graphics", "height", std::format("{}", display_h));
        cfg.apply_setting("graphics", "render", "normal");
        cfg.apply_setting("graphics", "fullscreen", "on");

        srand(static_cast<unsigned int>(time(nullptr)));

        // --- Create the display-owning "host" session ---
        og::runtime::GameSession::Config host_cfg;
        host_cfg.numviews = 1;
        host_cfg.allocate_screen = true;
        host_cfg.create_display = true;
        host_cfg.allocate_prefs = true;
        host_cfg.install_legacy_globals = true;
        og::runtime::GameSession host_session(host_cfg);

        init_input();
        load_player_control_settings_from_cfg(cfg);

        // Create composite surface and texture at the full display resolution.
        SDL_Surface* composite_surface = SDL_CreateRGBSurface(
            SDL_SWSURFACE, display_w, display_h, 32, 0, 0, 0, 0);
        SDL_Texture* composite_tex = SDL_CreateTexture(
            E_Screen->renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, display_w, display_h);
        if (!composite_surface || !composite_tex) {
            LogError("Failed to create composite surface/texture\n");
            return 1;
        }

        // --- Create sub-sessions (main thread) ---
        std::vector<DemoSession> demos(static_cast<size_t>(num_sessions));

        for (int i = 0; i < num_sessions; i++) {
            og::runtime::GameSession::Config sub_cfg;
            sub_cfg.numviews = 1;
            sub_cfg.allocate_screen = true;
            sub_cfg.create_display = false;
            sub_cfg.allocate_prefs = true;
            sub_cfg.install_legacy_globals = false;
            sub_cfg.allocate_seeded_rng = true;
            sub_cfg.rng_seed = static_cast<std::uint32_t>(rand());

            demos[static_cast<size_t>(i)].session =
                std::make_unique<og::runtime::GameSession>(sub_cfg);
        }

        // Build a shuffled list of scenario IDs.
        std::mt19937 demo_rng(static_cast<unsigned>(time(nullptr)));
        auto pick_scenarios = [&]() {
            std::vector<int> picks;
            std::vector<int> pool(SCENARIO_POOL.begin(), SCENARIO_POOL.end());
            std::shuffle(pool.begin(), pool.end(), demo_rng);
            for (int i = 0; i < num_sessions; i++)
                picks.push_back(pool[static_cast<size_t>(i) % pool.size()]);
            return picks;
        };

        // Initialize each session with a unique scenario (main thread).
        std::vector<int> chosen = pick_scenarios();
        E_Screen->suppress_present = true;
        for (int i = 0; i < num_sessions; i++) {
            init_session_game(demos[static_cast<size_t>(i)],
                              chosen[static_cast<size_t>(i)], demo_rng);
        }
        E_Screen->suppress_present = false;

        for (int i = 0; i < num_sessions; i++) {
            Log("  session {}: scenario {}\n", i, chosen[static_cast<size_t>(i)]);
        }
        Log("openglad_demo: {} sessions initialized, spawning {} worker threads\n",
            num_sessions, num_sessions);

        // --- Spawn worker threads ---
        WorkerSync sync;
        sync.num_workers = num_sessions;
        std::vector<std::thread> workers(static_cast<size_t>(num_sessions));
        for (int i = 0; i < num_sessions; i++) {
            workers[static_cast<size_t>(i)] = std::thread(
                worker_thread_func,
                std::ref(sync), std::ref(demos[static_cast<size_t>(i)]), i);
        }

        // --- Main loop ---
        constexpr int TIMER_WAIT_TICKS = 6;
        constexpr std::chrono::microseconds FRAME_PERIOD{TIMER_WAIT_TICKS * 13600};

        bool running = true;
        int frames_run = 0;
        while (running) {
            auto frame_start = std::chrono::steady_clock::now();

            // --- Phase 1: Poll SDL events (main thread only) ---
            // The demo manages its own shutdown — don't forward quit events
            // to handle_events() which would call exit(0) via quit().
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                    continue;
                }
                if (event.type == SDL_KEYDOWN &&
                    event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                    continue;
                }
                handle_events(event);
            }

            if (!running) break;

            // --- Phase 2: Signal workers to simulate one frame ---
            {
                std::lock_guard lock(sync.mtx);
                sync.workers_done = 0;
                sync.generation++;
            }
            sync.start_cv.notify_all();

            // --- Phase 3: Wait for all workers to finish simulation ---
            {
                std::unique_lock lock(sync.mtx);
                sync.done_cv.wait(lock, [&] {
                    return sync.workers_done >= num_sessions;
                });
            }

            // --- Phase 4: Render each session to its surface (main thread) ---
            // This is sequential: only the main thread touches E_Screen.
            E_Screen->suppress_present = true;
            int active_count = 0;
            for (int i = 0; i < num_sessions; i++) {
                if (demos[static_cast<size_t>(i)].finished.load(std::memory_order_relaxed))
                    continue;
                active_count++;

                auto scope = demos[static_cast<size_t>(i)].session->activate();
                screen* s = og::runtime::current_session->myscreen_;
                if (!s) continue;

                render_session_frame(*s);
            }
            E_Screen->suppress_present = false;

            // --- Phase 5: Composite and present (main thread only) ---
            SDL_FillRect(composite_surface, nullptr, 0x000000);
            for (int i = 0; i < num_sessions; i++) {
                SDL_Surface* src = demos[static_cast<size_t>(i)].session->session_surface_;
                int col = i % grid_cols;
                int row = i / grid_cols;
                composite_session(composite_surface, src, col, row);
            }

            SDL_UpdateTexture(composite_tex, nullptr,
                              composite_surface->pixels,
                              composite_surface->pitch);
            SDL_RenderClear(E_Screen->renderer);
            SDL_RenderCopy(E_Screen->renderer, composite_tex,
                           nullptr, nullptr);
            SDL_RenderPresent(E_Screen->renderer);

            // If all sessions are done, restart them with new scenarios.
            if (active_count == 0) {
                Log("All sessions finished, restarting...\n");
                chosen = pick_scenarios();
                E_Screen->suppress_present = true;
                for (int i = 0; i < num_sessions; i++) {
                    init_session_game(demos[static_cast<size_t>(i)],
                                      chosen[static_cast<size_t>(i)], demo_rng);
                }
                E_Screen->suppress_present = false;
            }

            // Frame pacing: sleep remainder of the target frame period.
            auto elapsed = std::chrono::steady_clock::now() - frame_start;
            auto remaining = FRAME_PERIOD - elapsed;
            if (remaining > std::chrono::microseconds(1000)) {
                std::this_thread::sleep_for(remaining);
            }

            if (max_frames > 0) {
                frames_run++;
                if (frames_run >= max_frames) {
                    running = false;
                }
            }
        }

        // --- Shutdown worker threads ---
        {
            std::lock_guard lock(sync.mtx);
            sync.shutdown = true;
        }
        sync.start_cv.notify_all();

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

        // Cleanup
        SDL_DestroyTexture(composite_tex);
        SDL_FreeSurface(composite_surface);
        for (auto& d : demos) {
            d.session.reset();
        }

        text_shutdown();
        io_exit();
    }
    catch (const std::runtime_error& e)
    {
        LogError("Unrecoverable error: {}\n", e.what());
        return 1;
    }

    return 0;
}
