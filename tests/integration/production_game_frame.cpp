// Ordinary SDL game-frame contracts. This translation unit and its linked
// og_game objects must be compiled without TESTING.
#ifdef TESTING
#error "Production frame contracts must link the ordinary runtime"
#endif

#include <openglad/core/colors.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/base.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/platform/sai2x.h>
#include <openglad/resources/gparser.h>

#include <SDL3/SDL.h>
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void require_frame(bool condition, const char* description)
{
    if (!condition)
        throw std::runtime_error(description);
}

using Pixel = std::array<Uint8, 3>;
Pixel pixel(screen& s, int x, int y)
{
    Pixel result{};
    s.get_pixel(x, y, &result[0], &result[1], &result[2]);
    return result;
}

bool palette_pixel(screen& s, int x, int y, unsigned char index)
{
    const Pixel actual = pixel(s, x, y);
    int red = 0, green = 0, blue = 0;
    query_palette_reg(index, &red, &green, &blue);
    return actual[0] / 4 == red && actual[1] / 4 == green &&
           actual[2] / 4 == blue;
}

std::vector<Pixel> pixels(screen& s, int x, int y, int width, int height)
{
    require_frame(x >= 0 && y >= 0 && width > 0 && height > 0 &&
                      x + width <= s.canvas_w() &&
                      y + height <= s.canvas_h(),
                  "frame sample rectangle is inside the real canvas");
    std::vector<Pixel> result;
    result.reserve(static_cast<std::size_t>(width) *
                   static_cast<std::size_t>(height));
    for (int row = y; row < y + height; ++row)
        for (int col = x; col < x + width; ++col)
            result.push_back(pixel(s, col, row));
    return result;
}

void step(screen& s, GameLoopFrameState& state, const GameLoopDeps& deps)
{
    const auto tick = s.world().tick_count_;
    require_frame(game_frame_with_result(s, state, deps) ==
                      GameFrameResult::Continue,
                  "render-only frame keeps the mission active");
    require_frame(s.world().tick_count_ == tick,
                  "render-only frame leaves simulation time unchanged");
}

void poison(screen& s, int x, int y)
{
    s.pointb(x, y, 211, 19, 147);
}

void verify_frame_contracts()
{
    require_frame(SDL_Init(SDL_INIT_VIDEO), "production frame SDL boot");
    cfg.apply_setting("graphics", "fullscreen", "windowed");
    cfg.apply_setting("graphics", "width", "640");
    cfg.apply_setting("graphics", "height", "400");
    cfg.apply_setting("graphics", "smoothing", "off");
    cfg.apply_setting("graphics", "zoom", "1.0");
    for (const char* effect : {"shadows", "reflections", "weather", "ripples",
                               "trails", "dust", "fire_glow", "screen_shake",
                               "floor_glide"})
        cfg.apply_setting("effects", effect, "off");

    og::runtime::GameSession::Config options;
    options.allocate_seeded_rng = true;
    options.rng_seed = 0x5eed0c07u;
    og::runtime::GameSession session(options);
    session.target_fps_ = 60;
    session.gameplay_active_ = true;
    screen& s = *session.screen_ptr();
    require_frame(E_Screen != nullptr && E_Screen->renderer != nullptr,
                  "production frame owns the actual SDL renderer");
    s.set_world_canvas_pinned_classic(true);
    s.relayout_views();
    E_Screen->set_vsync(false);
    init_input();

    s.save_data.reset();
    s.save_data.current_campaign = "gladiator";
    s.save_data.current_levels["gladiator"] = 1;
    s.save_data.scen_num = 1;
    s.save_data.numplayers = 1;
    auto hero = std::make_unique<guy>(FAMILY_SOLDIER);
    hero->upgrade_to_level(3);
    hero->teamnum = 0;
    s.save_data.team_list[0] = std::move(hero);
    s.save_data.team_size = 1;
    require_frame(s.save_data.save("save0"), "private production save is written");
    require_frame(load_saved_game_with_error("save0", &s) ==
                      LoadSavedGameError::None,
                  "production frame loads the requested scenario, without fallback");
    og::runtime::reset_local_transport_shadow(session, s);
    require_frame(og::runtime::local_transport_active(session),
                  "production frame has the real local transport");
    require_frame(s.viewob[0] != nullptr && s.viewob[0]->control != nullptr,
                  "production frame has a real seat control");
    viewscreen& view = *s.viewob[0];
    walker& control = *view.control;
    require_frame(!control.dead() && control.user() == 0,
                  "production frame has a living human HUD owner");
    view.prefs[PREF_VIEW] = PREF_VIEW_FULL;
    view.prefs[PREF_LIFE] = PREF_LIFE_BARS;
    s.relayout_views();
    s.world().set_weather(WeatherKind::None);
    s.world().end = 0;
    s.redrawme = 0;
    GameLoopFrameState state{};
    GameLoopDeps deps;
    deps.enable_tick = false;
    deps.enable_frame_timing = false;
    deps.enable_event_poll = false;

    // A real black display is the control for the final present. The CPU
    // sentinel changes only when ordinary scene drawing is enabled.
    step(s, state, deps);
    require_frame(s.fadeblack(false) == 1, "production scene fades to black");
    require_frame(s.window_is_black(), "fade establishes a black display");
    require_frame(SDL_FillSurfaceRect(E_Screen->render, nullptr,
                      SDL_MapSurfaceRGB(E_Screen->render, 211, 19, 147)),
                  "CPU frame sentinel is installed without presenting");
    const auto sentinel = pixels(s, 0, 0, s.canvas_w(), s.canvas_h());
    deps.enable_render = false;
    step(s, state, deps);
    require_frame(pixels(s, 0, 0, s.canvas_w(), s.canvas_h()) == sentinel,
                  "disabled rendering preserves every CPU pixel");
    require_frame(s.window_is_black(), "disabled rendering never presents");
    deps.enable_render = true;
    step(s, state, deps);
    require_frame(pixels(s, 0, 0, s.canvas_w(), s.canvas_h()) != sentinel,
                  "ordinary frame replaces the scene sentinel");
    require_frame(!s.window_is_black() &&
                      E_Screen->last_presented_canvas() == CanvasTarget::World,
                  "ordinary frame presents the completed World canvas");

    // Interior samples avoid the rounded bar border and text. The classic
    // one-seat bar begins at pane origin + (2,10).
    const int bar_x = view.xloc + 2;
    const int bar_y = view.yloc + 10;
    const float maximum_hp = control.stats()->max_hitpoints();
    require_frame(maximum_hp > 0, "seat has positive maximum health");
    for (bool full : {true, false, true})
    {
        control.stats()->set_hitpoints(full ? maximum_hp : maximum_hp / 4.0f);
        step(s, state, deps);
        require_frame(palette_pixel(s, bar_x + 5, bar_y + 3,
                          full ? MAX_HP_COLOR : LOW_HP_COLOR),
                      "ordinary frame paints the current health color");
        require_frame(palette_pixel(s, bar_x + 40, bar_y + 3,
                          full ? MAX_HP_COLOR : BAR_BACK_COLOR),
                      "ordinary frame paints the current health length");
    }

    // A fixed world with visual effects disabled must give a reproducible
    // scene. This is a bounded stability assertion, never a retry-until-pass.
    step(s, state, deps);
    const auto baseline = pixels(s, 70, 32, 160, 110);
    step(s, state, deps);
    require_frame(pixels(s, 70, 32, 160, 110) == baseline,
                  "fixed production scene is stable before debug comparison");
    require_frame(!session.debug_draw_obmap_ && s.world().myobmap != nullptr &&
                      !s.world().myobmap->pos_to_walker.empty(),
                  "debug map control starts off with live collision cells");
    for (bool enabled : {true, false})
    {
        bool sent = false;
        deps.enable_event_poll = true;
        deps.poll_event = [&sent](SDL_Event* event) {
            if (sent)
                return 0;
            sent = true;
            *event = {};
            event->type = SDL_EVENT_KEY_DOWN;
            event->key.key = SDLK_F12;
            return 1;
        };
        step(s, state, deps);
        deps.enable_event_poll = false;
        deps.poll_event = {};
        require_frame(session.debug_draw_obmap_ == enabled,
                      "F12 changes the production debug-map state");
        require_frame((pixels(s, 70, 32, 160, 110) == baseline) == !enabled,
                      "F12 debug overlay changes pixels and restores the scene");
    }

    s.world().type = static_cast<char>(s.world().type | GameWorld::TYPE_SCRIPTED);
    s.world().mode.active = true;
    s.world().mode.cameras[0].entity_id =
        static_cast<std::int32_t>(control.entity_id());
    s.world().mode.cameras[0].style = og::sim::kCameraStyleInset;
    step(s, state, deps);
    require_frame(s.camera_view_ != nullptr && !s.camera_docked_ &&
                      s.numviews == 1 && !s.camera_pane_rects_.empty(),
                  "ordinary frame materializes one inset without adding a seat");
    const CameraPaneRect pane = s.camera_pane_rects_.front();
    require_frame(palette_pixel(s, pane.x - 1, pane.y - 1, GREY),
                  "ordinary frame paints the camera bevel above the HUD");
    poison(s, pane.x - 1, pane.y - 1);
    step(s, state, deps);
    require_frame(palette_pixel(s, pane.x - 1, pane.y - 1, GREY),
                  "ordinary frame refreshes the camera bevel");

    // With both fresh deadlines in the future, this invocation only handles
    // the pending repaint and sleeps. No ordinary render can mask its loss.
    GameLoopFrameState waiting{};
    GameLoopDeps pending;
    pending.enable_event_poll = false;
    pending.fixed_tick_ms = 80;
    pending.now_ms = [] { return 1000u; };
    unsigned sleeps = 0;
    unsigned ordinary_renders = 0;
    pending.sleep_ms = [&sleeps](std::uint32_t delay) {
        require_frame(delay > 0, "waiting frame requests positive sleep");
        ++sleeps;
    };
    pending.on_render = [&ordinary_renders](screen&) { ++ordinary_renders; };
    poison(s, pane.x - 1, pane.y - 1);
    poison(s, bar_x + 5, bar_y + 3);
    s.redrawme = 1;
    step(s, waiting, pending);
    require_frame(sleeps == 1 && ordinary_renders == 0 && s.redrawme == 0,
                  "pending repaint finishes before either scheduled frame");
    require_frame(palette_pixel(s, pane.x - 1, pane.y - 1, GREY) &&
                      palette_pixel(s, bar_x + 5, bar_y + 3, MAX_HP_COLOR),
                  "pending repaint restores camera and health pixels");
    pending.enable_render = false;
    poison(s, pane.x - 1, pane.y - 1);
    const auto disabled_camera_pixel = pixel(s, pane.x - 1, pane.y - 1);
    s.redrawme = 1;
    step(s, waiting, pending);
    require_frame(s.redrawme == 0 &&
                      pixel(s, pane.x - 1, pane.y - 1) == disabled_camera_pixel,
                  "disabled pending repaint clears its request without drawing");

    s.world().mode.cameras[0] = {};
    step(s, state, deps);
    require_frame(s.camera_view_ == nullptr && s.camera_pane_rects_.empty(),
                  "ordinary frame removes the withdrawn camera");
    og::runtime::clear_local_transport_shadow(session);
}
} // namespace

bool verify_production_game_frame()
{
    try
    {
        verify_frame_contracts();
        return true;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "production frame assertion failed: %s\n", error.what());
        return false;
    }
}
