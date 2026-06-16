/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <openglad/core/frame_pacing.h>
#include <openglad/core/frame_rate_config.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/core/version.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/emscripten/web_runtime_diagnostics.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/platform/screen_lifecycle.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <openglad/legacy/colors.h>
#include <ctime>
#include <openglad/resources/gparser.h>
#include <string>
#include <cstring>
#include <format>
#include <algorithm>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>
#include <openglad/core/util.h>
#include <openglad/interface/input.h>
#include <openglad/resources/io.h>
#include <openglad/interface/render/text.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/platform/game_context.h>
// theprefs is now a macro defined in view.h (via game_session.h)

namespace
{
inline screen* active_screen()
{
    return og::runtime::current_session->myscreen_;
}

inline options* active_prefs()
{
    return og::runtime::current_session->theprefs_;
}

} // namespace

#ifdef __EMSCRIPTEN__
namespace {
std::unique_ptr<og::runtime::GameSession> g_web_session;
bool g_web_boot_started = false;
std::optional<og::ui::PickerLobbyGameStartConfig> g_web_game_start_config;
char g_web_arg0[] = "/play.html";
char* g_web_argv[] = {g_web_arg0, nullptr};
} // namespace
#endif

static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

#ifdef OUYA
#include <openglad/legacy/OuyaController.h>
#endif


#ifdef __EMSCRIPTEN__
// Game state machine for Emscripten - allows single main loop to handle all states
enum class GameState {
    Intro,
    Picker,
    Playing,
    Quit
};
static GameState g_game_state = GameState::Intro;
static bool g_state_initialized = false;

#ifndef TESTING
namespace {

EM_JS(void, publish_web_game_state_js, (int state), {
    window.__opengladGameState = state;
});

EM_JS(void, publish_web_numviews_js, (int numviews), {
    window.__opengladNumViews = numviews;
});

void publish_web_game_state()
{
    publish_web_game_state_js(static_cast<int>(g_game_state));
    const screen* const current_screen = active_screen();
    publish_web_numviews_js(current_screen != nullptr ? current_screen->numviews : 0);
}

} // namespace
#endif
#endif


#ifdef TESTING
// Defined in src/runtime/glad_gameplay.cpp (linked into test binaries via og_game_test).
extern bool g_test_remove_exits;
#endif

// Z's script: #include <process.h>

bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message);

void picker_main(Sint32 argc, char **argv);
void intro_main(Sint32 argc, char **argv);

short remaining_foes(screen *scr, walker* myguy);
short remaining_team(screen *scr, char myteam);
short score_panel(screen *scr);
short score_panel(screen *scr, short do_it);
short new_score_panel(screen *s, short /*do_it*/);
void draw_value_bar(short left, short top, walker * control, short mode, screen * scr);
void new_draw_value_bar(Sint32 left, Sint32 top,
                        walker  * control, short mode, screen * scr);
void draw_percentage_bar(Sint32 left, Sint32 top, unsigned char somecolor,
                         short somelength, screen * scr);
void init_input();

void draw_radar_gems(screen  *scr);
void draw_gem(short x, short y, short color, screen * scr);

void glad_main(screen *scr, Sint32 playermode);

// Zardus: FIX: from view.cpp. We need this here so that it doesn't
// try to create it before main and go nuts trying to load it
// theprefs is now a macro defined in view.h (via game_session.h)

// Frame state lives in GameSession::frame_state_
static inline GameLoopFrameState& g_frame_state() {
    return og::runtime::current_session->frame_state_;
}

short gameplay_numviews_for_start(
    const screen& current_screen,
    const og::ui::PickerLobbyGameStartConfig* lobby_config)
{
    const std::uint8_t player_count = lobby_config != nullptr
        ? lobby_config->save_data.numplayers
        : current_screen.save_data.numplayers;
    const int views = player_count == 0 ? 1 : player_count;
    return static_cast<short>(std::clamp(views, 1, MAX_PLAYERS));
}

void ready_screen_for_game_start(
    screen& current_screen,
    const og::ui::PickerLobbyGameStartConfig* lobby_config)
{
    current_screen.ready_for_battle(
        gameplay_numviews_for_start(current_screen, lobby_config));
}

// Forward declarations
void glad_init(bool preserve_frame_timing = false);
void glad_init(bool preserve_frame_timing,
               const og::ui::PickerLobbyGameStartConfig* lobby_config);

#ifndef TESTING
namespace {
og::runtime::GameSession::Config default_session_config()
{
    og::runtime::GameSession::Config session_cfg;
    session_cfg.numviews = 1;
    session_cfg.allocate_screen = true;
    session_cfg.allocate_prefs = true;
    session_cfg.install_legacy_globals = true;
    return session_cfg;
}

void initialize_runtime_config(int argc, char* argv[])
{
    init_logging();  // Set up logging output (uses JS console on web)
    io_init(argc, argv);

    cfg.load_settings();
    cfg.commandline(argc, argv);
}

void bootstrap_runtime(int argc, char* argv[])
{
#ifdef OUYA
    OuyaControllerManager::init();
#endif

    //buffers: setting the seed
    srand(static_cast<unsigned int>(time(nullptr)));

    init_input();
    load_player_control_settings_from_cfg(cfg);
    save_player_control_settings_to_cfg(cfg);

    // Sync overscan from config (must be after session creation since
    // overscan_percentage is now a macro backed by current_session).
    og::runtime::current_session->overscan_percentage_ = static_cast<float>(
        parse_int_strict(cfg.get_setting("graphics", "overscan_percentage")).value_or(0)) / 100.0f;
    update_overscan_setting();
    cfg.apply_setting("graphics", "overscan_percentage",
        std::format("{:.0f}", 100 * og::runtime::current_session->overscan_percentage_));

    const int fps = og::core::target_fps_from_cfg(cfg);
    og::core::apply_target_fps_to_cfg(cfg, fps);
    og::runtime::current_session->target_fps_ = fps;
    og::runtime::current_session->show_fps_ = cfg.is_on("graphics", "show_fps");
    cfg.save_settings();
#if defined(__EMSCRIPTEN__) && !defined(TESTING)
    if (og::platform::web::should_skip_intro_for_tests())
        return;
#endif
    intro_main(argc, argv);
}
} // namespace
#endif

#ifdef __EMSCRIPTEN__
// Forward declarations for state handlers
void picker_init();
bool picker_frame();  // Returns true when should transition to game
bool picker_check_start_requested();
void picker_cleanup_for_game();
void picker_reinit_after_game();

// Emscripten frame wrapper with timing control
// The browser calls this at ~60 FPS via requestAnimationFrame
// The wrapper keeps gameplay on the browser cadence helper while letting
// game_frame() own the normal gameplay render path.
static void emscripten_frame_wrapper() {
	screen* current_screen = active_screen();
	switch (g_game_state) {
		case GameState::Picker:
			og::runtime::current_session->gameplay_active_ = false;
			if (!g_state_initialized) {
				picker_reinit_after_game();
				g_state_initialized = true;
			}
			if (picker_frame()) {
				// Transition to playing state
				Log("Transitioning from PICKER to PLAYING\n");
				g_web_game_start_config = picker_lobby_consume_game_start_config();
                    if (g_web_game_start_config.has_value())
                        og::platform::web::maybe_apply_jitter_capture_start_config(
                            *g_web_game_start_config);
				picker_cleanup_for_game();
				g_game_state = GameState::Playing;
				g_state_initialized = false;
			}
			break;

		case GameState::Playing:
			if (!g_state_initialized) {
				// Initialize game state
				Log("GameState::Playing: Initializing game\n");
				release_mouse();
				if(current_screen == nullptr)
				{
					LogError("game_state_init_failed state=playing reason=missing_screen\n");
					og::runtime::current_session->gameplay_active_ = false;
					g_game_state = GameState::Quit;
					break;
				}
                    og::platform::web::reset_runtime_diagnostics_for_gameplay_start();
					{
                    if (g_web_game_start_config.has_value())
                        og::platform::web::maybe_apply_jitter_capture_start_config(
                            *g_web_game_start_config);
					ready_screen_for_game_start(
					    *current_screen,
					    g_web_game_start_config.has_value()
					        ? &*g_web_game_start_config
					        : nullptr);
					}
				og::runtime::current_session->gameplay_active_ = true;
				glad_init(
				    true,
				    g_web_game_start_config.has_value()
				        ? &*g_web_game_start_config
				        : nullptr);
				g_web_game_start_config.reset();
				g_frame_state().done = false;
				g_frame_state().initialized = false;
				g_frame_state().currentcycle = 0;
				g_frame_state().cycletime = 3;
				g_frame_state().last_frame_time = 0;
				g_frame_state().accumulated_time = 0;
				g_state_initialized = true;
			}
			if (current_screen == nullptr || g_frame_state().done)
				break;
			{
				const Uint32 current_time = SDL_GetTicks();
				std::uint32_t sim_interval = 0u;
				if (current_screen != nullptr &&
				    og::runtime::current_session->g_game_speed_factor_ > 0.0f)
				{
					const float scaled = og::core::rounded_render_tick_interval_ms(
						current_screen->world().timer_wait,
						og::runtime::current_session->g_game_speed_factor_);
					sim_interval = std::max<std::uint32_t>(
						1u, static_cast<std::uint32_t>(std::lround(scaled)));
				}
				og::core::BrowserFramePacingResult pacing;
				if (!g_frame_state().initialized) {
					pacing.target_interval_ms = sim_interval;
					pacing.should_run_frame = true;
				} else {
					const Uint32 delta =
						current_time - g_frame_state().last_frame_time;
					pacing = og::core::step_browser_frame_pacing(
						g_frame_state().accumulated_time,
						delta,
						sim_interval);
				}

				og::runtime::emit_runtime_trace(
					og::runtime::make_runtime_trace_record(
						"browser_pacing",
						pacing.should_run_frame ? "wrapper_frame_ready"
						                        : "wrapper_frame_skip"));

				run_browser_wrapper_frame(
					*current_screen,
					g_frame_state(),
					current_time,
					pacing);
				}
			if (g_frame_state().done) {
				Log("Game done, transitioning back to PICKER\n");
				og::runtime::current_session->gameplay_active_ = false;
				if (og::runtime::current_game_session != nullptr)
					og::runtime::clear_local_transport_shadow(
					    *og::runtime::current_game_session);
				clear_keyboard();
				current_screen->world().delete_objects();
				g_web_game_start_config.reset();
				g_game_state = GameState::Picker;
				g_state_initialized = false;
			}
			break;

		case GameState::Quit:
			og::runtime::current_session->gameplay_active_ = false;
			emscripten_cancel_main_loop();
			break;

		default:
			break;
	}
#ifndef TESTING
	publish_web_game_state();
#endif
}
#endif

#ifndef TESTING
#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void openglad_web_boot()
{
    if (g_web_boot_started) {
        return;
    }

    g_web_boot_started = true;

    try
    {
        og::platform::web::prepare_runtime_diagnostics_for_boot();

        initialize_runtime_config(1, g_web_argv);
        g_web_session = std::make_unique<og::runtime::GameSession>(default_session_config());
        bootstrap_runtime(1, g_web_argv);
        og::platform::web::seed_test_save_if_requested();

        // For Emscripten, initialize picker and start the unified main loop
        picker_init();
        Log("openglad_web_boot: After picker_init, about to check if game should start\n");

        // Check if picker_init resulted in a game start request
        if (picker_check_start_requested()) {
            Log("openglad_web_boot: Game start was requested during picker_init, starting in PLAYING state\n");
            g_web_game_start_config = picker_lobby_consume_game_start_config();
            if (g_web_game_start_config.has_value())
                og::platform::web::maybe_apply_jitter_capture_start_config(
                    *g_web_game_start_config);
            g_game_state = GameState::Playing;
            g_state_initialized = false;  // Will trigger glad_init on first frame
        } else {
            Log("openglad_web_boot: No game start requested, starting in PICKER state\n");
            g_web_game_start_config.reset();
            g_game_state = GameState::Picker;
            g_state_initialized = true;
        }

        // Initialize timing
        g_frame_state().last_frame_time = SDL_GetTicks();
        g_frame_state().accumulated_time = 0;
        publish_web_game_state();

        // Browser startup uses an explicit JS-triggered bootstrap, so let the
        // boot call return after scheduling the frame loop.
        emscripten_set_main_loop(emscripten_frame_wrapper, 0, 0);
    }
    catch (const std::runtime_error& e)
    {
        g_web_boot_started = false;
        g_web_session.reset();
        publish_web_game_state();
        LogError("Unrecoverable error: {}\n", e.what());
    }
}
#endif

int main(int argc, char *argv[])
{
#ifdef __EMSCRIPTEN__
    (void)argc;
    (void)argv;
    openglad_web_boot();
    return 0;
#else
	try
	{
		initialize_runtime_config(argc, argv);

		// GameSession is the RAII root for all runtime state.
		// It owns the screen, prefs, and installs legacy global shims.
		// Must be created before any macro-backed globals are accessed
		// (overscan_percentage, player_keys, keystates, etc.).
		og::runtime::GameSession session(default_session_config());
		bootstrap_runtime(argc, argv);

		// Native build: use traditional blocking calls
		picker_main(argc, argv);
		text_shutdown();
		io_exit();

	// Session destructor restores previous globals and frees owned resources.
	}
	catch (const std::runtime_error& e)
	{
		LogError("Unrecoverable error: {}\n", e.what());
		return 1;
	}

	return 0;
#endif
}
#endif // TESTING

void draw_radar_gems(screen* s)
{
	short upper_left_x = 246;
	short upper_left_y = 140;
	short upper_right_x = upper_left_x + 65;
	short upper_right_y = upper_left_y;
	short lower_left_x = upper_left_x;
	short lower_left_y = upper_left_y + 49;
	short lower_right_x = upper_right_x;
	short lower_right_y = lower_left_y;

	short team_light;

	static short old_team_num = -1;
	if (old_team_num == s->viewob[0]->control->team_num())
		return;
	old_team_num = s->viewob[0]->control->team_num();

	team_light = s->viewob[0]->control->query_team_color();

	draw_gem(upper_left_x, upper_left_y, team_light, s);
	draw_gem(upper_right_x, upper_right_y, team_light, s);
	draw_gem(lower_left_x, lower_left_y, team_light, s);
	draw_gem(lower_right_x, lower_right_y, team_light, s);
}

void draw_gem(short x, short y, short color, screen* s)
{
	const unsigned char light = static_cast<unsigned char>(color);
	const unsigned char med = static_cast<unsigned char>(light + 2);
	const unsigned char darker = static_cast<unsigned char>(med + 2);
	const unsigned char darkest = static_cast<unsigned char>(darker + 2);

	s->point(x, y, light);
	s->point(x-1, y+1, light);
	s->point(x, y+1, med);
	s->point(x+1, y+1, darker);
	s->point(x-2, y+2, light);
	s->hor_line(x-1, y+2, 3, med);
	s->point(x+2, y+2, darkest);
	s->point(x-1, y+3, darker);
	s->point(x, y+3, med);
	s->point(x+1, y+3, darkest);
	s->point(x, y+4, darkest);
}

bool float_eq(float a, float b);

void draw_value_bar(short left, short top,
                    walker  * control, short mode, screen * s)
{
	float points;
	Sint32 bar_length = 0;
	Sint32 bar_remainder = 60 - bar_length;
	Sint32 i = 0;
	Sint32 j = 0;
	unsigned char whatcolor = 0;

	if (mode == 0) // hitpoint bar
	{
		points = control->stats()->hitpoints();

		if (float_eq(points, control->stats()->max_hitpoints()))
			whatcolor = MAX_HP_COLOR;
		else if ( (points * 3) < control->stats()->max_hitpoints())
			whatcolor = LOW_HP_COLOR;
		else if ( (points * 3 / 2) < control->stats()->max_hitpoints())
			whatcolor = MID_HP_COLOR;
		else if (points < control->stats()->max_hitpoints())
			whatcolor = HIGH_HP_COLOR;
			else 
				whatcolor = ORANGE_START;

			if (points > control->stats()->max_hitpoints())
				bar_length = 60;
			else
				bar_length = static_cast<Sint32>(ceilf(points * 60.0f / control->stats()->max_hitpoints()));
			bar_remainder = 60 - bar_length;

			s->draw_box(left, top, left+61, top+6, BOX_COLOR, 0);
			//myscreen->fastbox(left, top, 61, 6, BOX_COLOR, 1);
			if ( points > control->stats()->max_hitpoints())
				for (i=0;i<bar_length/2;i++)
					for (j=0; j<3; j++)
					{
						const unsigned char col = static_cast<unsigned char>(whatcolor + static_cast<unsigned char>((i + j) % 16));
						s->ver_line(left+1+(bar_length/2)-i-1, top+1+(2-j), 1, col);
						s->ver_line(left+1+(bar_length/2)-i-1, top+1+(2+j), 1, col);
						s->ver_line(left+1+i+(bar_length/2), top+1+(2-j), 1, col);
						s->ver_line(left+1+i+(bar_length/2), top+1+(2+j), 1, col);
					}
			else
				s->fastbox(left+1, top+1, bar_length, 5, whatcolor);
			s->fastbox(left+1+bar_length, top+1, bar_remainder, 5, BAR_BACK_COLOR);

		//This part rounds the corners (via 4 masks)

			for (i=0;i<4;i++)
			{
				//upper left
				s->ver_line(left+i, top, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+i, top, 2-i, 27);
				//upper right
				s->ver_line(left+61-i, top, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+61-i, top, 2-i, 27);
				//lower left
				s->ver_line(left+i, top+4+i, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+i, top+5+i, 2-i, 27);
				//lower right
				s->ver_line(left+61-i, top+4+i, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+61-i, top+5+i, 2-i, 27);
			}
	}  // end of doing hp stuff..
	else if (mode == 1) // sp stuff ..
	{
		points = control->stats()->magicpoints();

		if (float_eq(points, control->stats()->max_magicpoints()))
			whatcolor = MAX_MP_COLOR;
		else if ( (points * 3) < control->stats()->max_magicpoints())
			whatcolor = LOW_MP_COLOR;
		else if ( (points * 3 / 2) < control->stats()->max_magicpoints())
			whatcolor = MID_MP_COLOR;
		else if (points < control->stats()->max_magicpoints())
			whatcolor = HIGH_MP_COLOR;
			else 
				whatcolor = WATER_START;

			if (points > control->stats()->max_magicpoints())
				bar_length = 60;
			else
				bar_length = static_cast<Sint32>(ceilf(points * 60.0f / control->stats()->max_magicpoints()));
			bar_remainder = 60 - bar_length;

			s->draw_box(left, top, left+61, top+6, BOX_COLOR, 0);
			if ( points > control->stats()->max_magicpoints())
				for (i=0;i<bar_length/2;i++)
					for (j=0; j<3; j++)
					{
						const unsigned char col = static_cast<unsigned char>(whatcolor + static_cast<unsigned char>((i + j) % 16));
						s->ver_line(left+1+(bar_length/2)-i-1, top+1+(2-j), 1, col);
						s->ver_line(left+1+(bar_length/2)-i-1, top+1+(2+j), 1, col);
						s->ver_line(left+1+i+(bar_length/2), top+1+(2-j), 1, col);
						s->ver_line(left+1+i+(bar_length/2), top+1+(2+j), 1, col);
					}
			else
				s->fastbox(left+1, top+1, bar_length, 5, whatcolor);
			s->fastbox(left+1+bar_length, top+1, bar_remainder, 5, BAR_BACK_COLOR);

		//This part rounds the corners (via 4 masks)

			for (i=0;i<4;i++)
			{
				//upper left
				s->ver_line(left+i, top, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+i, top, 2-i, 27);
				//upper right
				s->ver_line(left+61-i, top, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+61-i, top, 2-i, 27);
				//lower left
				s->ver_line(left+i, top+4+i, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+i, top+5+i, 2-i, 27);
				//lower right
				s->ver_line(left+61-i, top+4+i, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+61-i, top+5+i, 2-i, 27);
			}
	} // end of sp stuff
} // end of drawing routine ..

// Note: new_draw_value_bar/new_score_panel/draw_percentage_bar moved to
// src/runtime/score_panel.cpp so score_panel() can be linked without main().
