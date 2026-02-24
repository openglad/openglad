/*
 * openglad_demo: Runs 12 independent AI-controlled game sessions in a 4x3 grid.
 *
 * Demonstrates that the engine supports multiple concurrent GameSession
 * instances within a single process. Each session:
 *   - Has its own screen, prefs, RNG, and render surface
 *   - Runs in 0-player spectator mode (AI-only)
 *   - Loads a scenario from the available set
 *
 * Rendering pipeline:
 *   1. For each session, activate() swaps E_Screen->render to session surface
 *   2. game_frame() renders to E_Screen->render (= session surface)
 *   3. E_Screen->suppress_present prevents immediate SDL_RenderPresent
 *   4. After all sessions tick, composite all surfaces into the display
 *   5. Present once via E_Screen->swap()
 */

#include <openglad/core/util.h>
#include <openglad/core/version.h>
#include <openglad/data/gparser.h>
#include <openglad/input/input.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/io.h>
#include <openglad/render/sai2x.h>
#include <openglad/render/view.h>
#include <openglad/runtime/game_context.h>
#include <openglad/runtime/game_loop.h>
#include <openglad/runtime/game_session.h>
#include <openglad/runtime/screen.h>
#include "SDL.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <format>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

// External declarations
// theprefs is now a macro defined in view.h (via game_session.h)
void init_input();
short load_saved_game(const char* filename, screen* scr);

inline constexpr int GRID_COLS = 4;
inline constexpr int GRID_ROWS = 3;
inline constexpr int NUM_SESSIONS = GRID_COLS * GRID_ROWS;
inline constexpr int CELL_W = 320;
inline constexpr int CELL_H = 200;
inline constexpr int DISPLAY_W = GRID_COLS * CELL_W;
inline constexpr int DISPLAY_H = GRID_ROWS * CELL_H;

// Scenario pool — diverse levels from the main campaign plus bonus maps.
// These are all present in the gladiator campaign archive or scen/ directory.
static const std::array<int, 20> SCENARIO_POOL = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16,
    9411, 9412, 9413, 9414,
};

struct DemoSession {
    std::unique_ptr<og::runtime::GameSession> session;
    bool finished = false;
};

static void init_session_game(DemoSession& demo, int scen_id)
{
    auto scope = demo.session->activate();
    screen* s = myscreen;
    if (!s) return;

    s->save_data.numplayers = 0; // spectator mode
    s->save_data.scen_num = static_cast<short>(scen_id);
    s->save_data.current_campaign = "org.openglad.gladiator";

    load_saved_game("save0", s);
    s->continuous_input();
    s->redrawme = 1;
    s->framecount = 0;
    s->timerstart = query_timer_control();
    demo.session->frame_state() = {};
    demo.finished = false;
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

        // Override display size for the 4x3 grid
        cfg.apply_setting("graphics", "width", std::format("{}", DISPLAY_W));
        cfg.apply_setting("graphics", "height", std::format("{}", DISPLAY_H));
        cfg.apply_setting("graphics", "render", "normal");
        cfg.apply_setting("graphics", "fullscreen", "off");

        // Seed rand() for non-deterministic session variety. The demo is
        // intentionally non-deterministic: each run produces different RNG seeds
        // and scenario selections. Determinism is not a goal here.
        srand(static_cast<unsigned int>(time(nullptr)));

        // --- Create the display-owning "host" session ---
        // This creates the SDL window and renderer at DISPLAY_W x DISPLAY_H.
        // Its screen object is only used for display ownership.
        og::runtime::GameSession::Config host_cfg;
        host_cfg.numviews = 1;
        host_cfg.allocate_screen = true;
        host_cfg.create_display = true;
        host_cfg.allocate_prefs = true;
        host_cfg.install_legacy_globals = true;
        host_cfg.install_global_context = true;
        og::runtime::GameSession host_session(host_cfg);

        init_input();
        load_player_control_settings_from_cfg(cfg);

        // Create a composite surface and texture at the full display resolution.
        // E_Screen->render is only 320x200 (for single-session rendering).
        // We need DISPLAY_W x DISPLAY_H for the 4x3 grid composite.
        SDL_Surface* composite_surface = SDL_CreateRGBSurface(
            SDL_SWSURFACE, DISPLAY_W, DISPLAY_H, 32, 0, 0, 0, 0);
        SDL_Texture* composite_tex = SDL_CreateTexture(
            E_Screen->renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, DISPLAY_W, DISPLAY_H);
        if (!composite_surface || !composite_tex) {
            LogError("Failed to create composite surface/texture\n");
            return 1;
        }

        // --- Create 12 sub-sessions ---
        std::array<DemoSession, NUM_SESSIONS> demos;

        for (int i = 0; i < NUM_SESSIONS; i++) {
            og::runtime::GameSession::Config sub_cfg;
            sub_cfg.numviews = 1;
            sub_cfg.allocate_screen = true;
            sub_cfg.create_display = false;  // Share the host's display
            sub_cfg.allocate_prefs = true;
            sub_cfg.install_legacy_globals = false;
            sub_cfg.install_global_context = false;
            sub_cfg.allocate_seeded_rng = true;
            sub_cfg.rng_seed = static_cast<std::uint32_t>(rand());

            demos[i].session = std::make_unique<og::runtime::GameSession>(sub_cfg);
        }

        // Build a shuffled list of scenario IDs so each session gets a
        // different level.  We sample without replacement from the pool.
        std::mt19937 demo_rng(static_cast<unsigned>(time(nullptr)));
        auto pick_scenarios = [&]() {
            std::vector<int> picks;
            std::vector<int> pool(SCENARIO_POOL.begin(), SCENARIO_POOL.end());
            std::shuffle(pool.begin(), pool.end(), demo_rng);
            for (int i = 0; i < NUM_SESSIONS; i++)
                picks.push_back(pool[static_cast<size_t>(i) % pool.size()]);
            return picks;
        };

        // Initialize each session with a unique scenario.
        // Suppress presentation during init to avoid flashing individual sessions.
        std::vector<int> chosen = pick_scenarios();
        E_Screen->suppress_present = true;
        for (int i = 0; i < NUM_SESSIONS; i++) {
            init_session_game(demos[i], chosen[static_cast<size_t>(i)]);
        }
        E_Screen->suppress_present = false;

        for (int i = 0; i < NUM_SESSIONS; i++) {
            Log("  session {}: scenario {}\n", i, chosen[static_cast<size_t>(i)]);
        }
        Log("openglad_demo: {} sessions initialized\n", NUM_SESSIONS);

        // --- Main loop ---
        // During session ticks, suppress_present prevents each session's
        // buffer_to_screen → E_Screen->swap from presenting to the display.
        // All rendering still goes to E_Screen->render (which is swapped to
        // the session's own surface via SessionScope).
        GameLoopDeps deps;
        deps.enable_event_poll = false;   // We handle events ourselves
        deps.enable_render = true;
        deps.enable_frame_timing = false; // We pace the simulation externally
                                          // with a single sleep per frame (below),
                                          // avoiding per-session multiplicative delays.

        // Match the normal game's simulation rate.  timer_wait defaults to 6
        // ticks, and 1 tick = 13.6ms, so target frame period = 81.6ms (~12 fps).
        // This is the game logic tick rate, not a display framerate — it controls
        // how fast walkers move, attacks land, etc.
        constexpr int TIMER_WAIT_TICKS = 6;
        constexpr std::chrono::microseconds FRAME_PERIOD{TIMER_WAIT_TICKS * 13600};

        bool running = true;
        while (running) {
            auto frame_start = std::chrono::steady_clock::now();

            // Poll events on the main display
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                }
                if (event.type == SDL_KEYDOWN &&
                    event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                }
                handle_events(event);
            }

            if (!running) break;

            // Suppress presentation during individual session ticks
            E_Screen->suppress_present = true;

            // Tick each session - rendering goes to each session's own surface
            int active_count = 0;
            for (int i = 0; i < NUM_SESSIONS; i++) {
                if (demos[i].finished) continue;
                active_count++;

                auto scope = demos[i].session->activate();
                screen* s = myscreen;
                if (!s) continue;

                GameLoopFrameState& fs = demos[i].session->frame_state();
                bool done = game_frame(*s, fs, deps);
                if (done) {
                    demos[i].finished = true;
                    Log("Session {} finished\n", i);
                }
            }

            E_Screen->suppress_present = false;

            // Composite all session surfaces into the composite surface
            SDL_FillRect(composite_surface, nullptr, 0x000000);
            for (int i = 0; i < NUM_SESSIONS; i++) {
                SDL_Surface* src = demos[i].session->render_surface();
                int col = i % GRID_COLS;
                int row = i / GRID_COLS;
                composite_session(composite_surface, src, col, row);
            }

            // Upload composite to our full-resolution texture and present.
            SDL_UpdateTexture(composite_tex, nullptr,
                              composite_surface->pixels,
                              composite_surface->pitch);
            SDL_RenderClear(E_Screen->renderer);
            SDL_RenderCopy(E_Screen->renderer, composite_tex,
                           nullptr, nullptr);
            SDL_RenderPresent(E_Screen->renderer);

            // If all sessions are done, restart them with new scenarios
            if (active_count == 0) {
                Log("All sessions finished, restarting...\n");
                chosen = pick_scenarios();
                for (int i = 0; i < NUM_SESSIONS; i++) {
                    init_session_game(demos[i], chosen[static_cast<size_t>(i)]);
                }
            }

            // Sleep once for all 12 sessions to match the normal game's tick rate.
            // The normal game does time_delay(timer_wait - query_timer()) per frame,
            // sleeping for the remainder of timer_wait*13.6ms after game work.
            // We replicate that here: measure total frame work, sleep the remainder.
            auto elapsed = std::chrono::steady_clock::now() - frame_start;
            auto remaining = FRAME_PERIOD - elapsed;
            if (remaining > std::chrono::microseconds(1000)) {
                std::this_thread::sleep_for(remaining);
            }
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
