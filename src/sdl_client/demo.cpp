/*
 * openglad_demo: Runs 12 independent AI-controlled game sessions in a 4x3 grid.
 *
 * Demonstrates that the engine supports multiple concurrent GameSession
 * instances within a single process. Each session:
 *   - Has its own screen, prefs, RNG, and render surface
 *   - Runs in 0-player spectator mode (AI-only)
 *   - Loads a random scenario from the available set
 *
 * Single-threaded: each frame cycles through all sessions, activating
 * each one's globals via SessionScope, ticking one game_frame(), then
 * compositing all render surfaces into the display.
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

#include <array>
#include <cstdlib>
#include <ctime>
#include <format>
#include <memory>
#include <stdexcept>
#include <vector>

// External declarations
extern options* theprefs;
void init_input();
void glad_init();
short load_saved_game(const char* filename, screen* myscreen);

inline constexpr int GRID_COLS = 4;
inline constexpr int GRID_ROWS = 3;
inline constexpr int NUM_SESSIONS = GRID_COLS * GRID_ROWS;
inline constexpr int CELL_W = 320;
inline constexpr int CELL_H = 200;

// Available scenario IDs (the ones shipped with the game data)
static const std::array<int, 4> SCENARIO_IDS = {9411, 9412, 9413, 9414};

struct DemoSession {
    std::unique_ptr<og::runtime::GameSession> session;
    bool finished = false;
};

// Blit a 320x200 session surface into a region of the display surface.
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
        load_player_control_settings_from_cfg(cfg);

        // Override display size for the 4x3 grid
        cfg.apply_setting("graphics", "width",
                          std::format("{}", GRID_COLS * CELL_W));
        cfg.apply_setting("graphics", "height",
                          std::format("{}", GRID_ROWS * CELL_H));
        cfg.apply_setting("graphics", "render", "normal");
        // Disable fullscreen for the demo
        cfg.apply_setting("graphics", "fullscreen", "off");

        srand(static_cast<unsigned int>(time(nullptr)));

        // --- Phase 1: Create the display-owning "host" session ---
        // This session creates the SDL window and renderer.
        // Its screen won't be used for gameplay - just for display ownership.
        og::runtime::GameSession::Config host_cfg;
        host_cfg.numviews = 1;
        host_cfg.allocate_screen = true;
        host_cfg.create_display = true;
        host_cfg.allocate_prefs = true;
        host_cfg.install_legacy_globals = true;
        host_cfg.install_global_context = true;
        og::runtime::GameSession host_session(host_cfg);

        init_input();

        // --- Phase 2: Create 12 sub-sessions ---
        std::array<DemoSession, NUM_SESSIONS> demos;

        for (int i = 0; i < NUM_SESSIONS; i++) {
            og::runtime::GameSession::Config sub_cfg;
            sub_cfg.numviews = 1;
            sub_cfg.allocate_screen = true;
            sub_cfg.create_display = false;  // Share the host's display
            sub_cfg.allocate_prefs = true;
            sub_cfg.install_legacy_globals = false; // Don't clobber globals
            sub_cfg.install_global_context = false;
            sub_cfg.allocate_seeded_rng = true;
            sub_cfg.rng_seed = static_cast<std::uint32_t>(rand());

            demos[i].session = std::make_unique<og::runtime::GameSession>(sub_cfg);

            // Set up spectator mode with a random scenario
            {
                auto scope = demos[i].session->activate();
                screen* s = myscreen;
                if (s) {
                    s->save_data.numplayers = 0; // spectator
                    s->save_data.scen_num = static_cast<short>(SCENARIO_IDS[
                        static_cast<size_t>(i) % SCENARIO_IDS.size()]);
                    s->save_data.current_campaign = "org.openglad.gladiator";

                    // Initialize the game for this session
                    clear_keyboard();
                    load_saved_game("save0", s);
                    s->continuous_input();
                    s->redrawme = 1;
                    s->framecount = 0;
                    s->timerstart = query_timer_control();
                    demos[i].session->frame_state().done = false;
                    demos[i].session->frame_state().currentcycle = 0;
                    demos[i].session->frame_state().cycletime = 3;
                }
            }
        }

        Log("openglad_demo: {} sessions initialized\n", NUM_SESSIONS);

        // --- Phase 3: Main loop ---
        GameLoopDeps deps;
        deps.enable_event_poll = false; // We handle events ourselves
        deps.enable_render = true;

        bool running = true;
        while (running) {
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

            // Tick each session
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

            // Composite all session surfaces into the display
            if (E_Screen && E_Screen->render) {
                // Restore the host's render surface
                SDL_Surface* display = E_Screen->render;
                SDL_FillRect(display, nullptr, 0x000000);

                for (int i = 0; i < NUM_SESSIONS; i++) {
                    SDL_Surface* src = demos[i].session->render_surface();
                    int col = i % GRID_COLS;
                    int row = i / GRID_COLS;
                    composite_session(display, src, col, row);
                }

                // Present
                E_Screen->swap(0, 0,
                               GRID_COLS * CELL_W, GRID_ROWS * CELL_H);
            }

            // If all sessions are done, restart them
            if (active_count == 0) {
                Log("All sessions finished, restarting...\n");
                for (auto& d : demos) {
                    d.finished = false;
                    auto scope = d.session->activate();
                    screen* s = myscreen;
                    if (s) {
                        s->save_data.scen_num = static_cast<short>(SCENARIO_IDS[
                            static_cast<size_t>(rand()) % SCENARIO_IDS.size()]);
                        clear_keyboard();
                        load_saved_game("save0", s);
                        s->continuous_input();
                        s->redrawme = 1;
                        d.session->frame_state() = {};
                    }
                }
            }

            SDL_Delay(16); // ~60fps cap
        }

        // Cleanup: destroy sub-sessions first
        for (auto& d : demos) {
            d.session.reset();
        }

        // Host session destructor handles SDL cleanup
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
