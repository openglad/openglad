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
short load_saved_game(const char* filename, screen* myscreen);

inline constexpr int GRID_COLS = 4;
inline constexpr int GRID_ROWS = 3;
inline constexpr int NUM_SESSIONS = GRID_COLS * GRID_ROWS;
inline constexpr int CELL_W = 320;
inline constexpr int CELL_H = 200;
inline constexpr int DISPLAY_W = GRID_COLS * CELL_W;
inline constexpr int DISPLAY_H = GRID_ROWS * CELL_H;

// Available scenario IDs (the ones shipped with the game data)
static const std::array<int, 4> SCENARIO_IDS = {9411, 9412, 9413, 9414};

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
        load_player_control_settings_from_cfg(cfg);

        // Override display size for the 4x3 grid
        cfg.apply_setting("graphics", "width", std::format("{}", DISPLAY_W));
        cfg.apply_setting("graphics", "height", std::format("{}", DISPLAY_H));
        cfg.apply_setting("graphics", "render", "normal");
        cfg.apply_setting("graphics", "fullscreen", "off");

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

        // Initialize each session with a scenario.
        // Suppress presentation during init to avoid flashing individual sessions.
        E_Screen->suppress_present = true;
        for (int i = 0; i < NUM_SESSIONS; i++) {
            int scen_id = SCENARIO_IDS[static_cast<size_t>(i) % SCENARIO_IDS.size()];
            init_session_game(demos[i], scen_id);
        }
        E_Screen->suppress_present = false;

        Log("openglad_demo: {} sessions initialized\n", NUM_SESSIONS);

        // --- Main loop ---
        // During session ticks, suppress_present prevents each session's
        // buffer_to_screen → E_Screen->swap from presenting to the display.
        // All rendering still goes to E_Screen->render (which is swapped to
        // the session's own surface via SessionScope).
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
                for (int i = 0; i < NUM_SESSIONS; i++) {
                    int scen_id = SCENARIO_IDS[
                        static_cast<size_t>(rand()) % SCENARIO_IDS.size()];
                    init_session_game(demos[i], scen_id);
                }
            }

            SDL_Delay(16); // ~60fps cap
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
