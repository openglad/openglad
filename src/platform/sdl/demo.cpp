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
 *   - SDL_RenderPresent/SDL_RenderTexture/SDL_RenderClear: main thread only
 *   - SDL_UpdateTexture: main thread only
 *   - E_Screen->render swap: main thread only (via SessionScope)
 *   - Per-session SDL_Surface pixel writes: each worker writes only to its own surface
 *   - Audio (SDL_PutAudioStreamData etc.): SDL3 audio streams lock internally
 */

#include <openglad/core/constants.h>
#include <openglad/core/frame_pacing.h>
#include <openglad/core/util.h>
#include <openglad/core/version.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/fps_overlay.h>
#include <openglad/interface/input.h>
#include <openglad/legacy/base.h>
#include <openglad/resources/io.h>
#include <openglad/platform/sai2x.h>
#include <openglad/interface/render/effects.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/game_context.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/screen.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <charconv>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <sstream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <string_view>
#include <unordered_map>
#include <vector>

// External declarations
void init_input();
short load_saved_game(const char* filename, screen* scr);

// Demo sessions are PINNED to the classic 320x200 canvas: every session
// surface is exactly CELL_W x CELL_H, and demo sessions never call
// Screen::set_world_canvas_size, so their world canvas stays shared with the
// fixed-size UI canvas. The compositor scales each surface into its display
// cell (aspect-preserving cover + center crop), so cells need not be 320x200
// on screen.
inline constexpr int CELL_W = 320;
inline constexpr int CELL_H = 200;

// Extra magnification applied on top of the cover scale when compositing a
// session into its display cell. Values above 1.0 crop deeper into the
// 320x200 frame. Overridable via OPENGLAD_DEMO_ZOOM (clamped to >= 1.0; below
// 1.0 the cell would no longer be filled).
inline constexpr float DEFAULT_CELL_ZOOM = 1.1f;

// Backing strip for the whole-grid FPS readout, in host-canvas pixels. Wide
// enough for "FPS: 99999" at the 5x6 normal font (6px advance per glyph) with
// a 2px inset, so the text is never clipped by the strip edge.
inline constexpr int FPS_STRIP_W = 64;
inline constexpr int FPS_STRIP_H = 10;

// Scenario pool — diverse levels from the main campaign plus bonus maps.
static const std::array<int, 20> SCENARIO_POOL = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16,
    9411, 9412, 9413, 9414,
};

// Playable families for random team generation.
static constexpr std::array<int, 14> PLAYABLE_FAMILIES = {
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

// Default campaign for the scenario pool above.
inline constexpr std::string_view DEFAULT_CAMPAIGN = "gladiator";

// ---------------------------------------------------------------------------
// Environment parsing helpers
// ---------------------------------------------------------------------------
// Every knob below is opt-in: with the variable unset the demo behaves exactly
// as it did before (scripts/test_demo_smoke.sh pins that production path).
static const char* env_or_null(const char* name)
{
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0')
        return nullptr;
    return raw;
}

static int env_int(const char* name, int fallback, int minimum)
{
    const char* raw = env_or_null(name);
    if (raw == nullptr)
        return fallback;
    const std::string_view text(raw);
    int value = 0;
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::runtime_error(std::format(
            "{} must be an integer, got '{}'", name, text));
    }
    return std::max(minimum, value);
}

static float env_float(const char* name, float fallback, float minimum)
{
    const char* raw = env_or_null(name);
    if (raw == nullptr)
        return fallback;
    const std::string_view text(raw);
    float value = 0.0f;
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::runtime_error(std::format(
            "{} must be a number, got '{}'", name, text));
    }
    return std::max(minimum, value);
}

// OPENGLAD_DEMO_SCENARIOS=605 or 1,7,9411 — an explicit scenario list that
// replaces the shuffled production pool (assigned to cells in order, wrapping).
static std::vector<int> env_scenario_list(const char* name)
{
    std::vector<int> ids;
    const char* raw = env_or_null(name);
    if (raw == nullptr)
        return ids;
    const std::string_view text(raw);
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string_view field = text.substr(
            start, comma == std::string_view::npos ? text.size() - start
                                                   : comma - start);
        int value = 0;
        const auto parsed = std::from_chars(
            field.data(), field.data() + field.size(), value);
        if (field.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != field.data() + field.size()) {
            throw std::runtime_error(std::format(
                "{} must be a comma-separated scenario id list, got '{}'",
                name, text));
        }
        ids.push_back(value);
        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }
    return ids;
}

// ---------------------------------------------------------------------------
// Opt-in frame capture (showcase media pipeline)
// ---------------------------------------------------------------------------
// Where the captured session's camera looks. "player" is the demo's own
// follow-a-hero camera; the other two exist because a showcase frame wants the
// arena, not whichever hero the AI walked off with.
enum class CaptureFocus {
    Player,  // unchanged demo behaviour
    Boss,    // follow the strongest live hostile
    Center,  // static wide shot of the map centre
};

struct CaptureSettings {
    std::string dir;          // empty ⇒ capture disabled
    int every = 1;            // dump every Nth rendered frame
    int start = 0;            // ignore this many rendered frames first
    int limit = 0;            // 0 ⇒ unlimited; otherwise stop after N dumps
    int session_index = 0;    // which grid cell to dump; -1 ⇒ the whole grid
    CaptureFocus focus = CaptureFocus::Player;

    [[nodiscard]] bool enabled() const noexcept { return !dir.empty(); }
};

// Re-aim the captured session's camera. Called once per rendered frame, before
// the draw, with the session scope active.
static void apply_capture_focus(screen& s, CaptureFocus focus)
{
    if (focus == CaptureFocus::Player)
        return;
    viewscreen* view = s.viewob[0].get();
    if (view == nullptr)
        return;

    if (focus == CaptureFocus::Center) {
        // viewscreen::redraw falls back to the LevelVisuals camera whenever it
        // has no control walker — the same free camera the level editor uses.
        view->control = nullptr;
        view->following_ = false;
        s.level_visuals().topx = std::max(
            0, (s.world().pixmaxx - view->xview) / 2);
        s.level_visuals().topy = std::max(
            0, (s.world().pixmaxy - view->yview) / 2);
        return;
    }

    // Boss: the highest-level live hostile living, re-derived every frame so
    // the camera hands off cleanly when one dies.
    walker* boss = nullptr;
    for (auto& uptr : s.world().oblist) {
        walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->order() != Order::Living ||
            w->team_num() == 0)
            continue;
        if (boss == nullptr || w->stats()->level() > boss->stats()->level())
            boss = w;
    }
    if (boss != nullptr) {
        view->control = boss;
        // §4.5 spectator follow: the watched walker is not user-tagged, so the
        // HUD and radar stay quiet for an AI target.
        view->following_ = true;
    }
}

static CaptureSettings capture_settings_from_env()
{
    CaptureSettings settings;
    const char* dir = env_or_null("OPENGLAD_DEMO_CAPTURE_DIR");
    if (dir == nullptr)
        return settings;
    settings.dir = dir;
    settings.every = env_int("OPENGLAD_DEMO_CAPTURE_EVERY", 1, 1);
    settings.start = env_int("OPENGLAD_DEMO_CAPTURE_START", 0, 0);
    settings.limit = env_int("OPENGLAD_DEMO_CAPTURE_LIMIT", 0, 0);
    // -1 captures the composited grid instead of a single cell.
    settings.session_index = env_int("OPENGLAD_DEMO_CAPTURE_SESSION", 0, -1);
    if (const char* focus = env_or_null("OPENGLAD_DEMO_CAPTURE_FOCUS")) {
        const std::string_view name(focus);
        if (name == "player")
            settings.focus = CaptureFocus::Player;
        else if (name == "boss")
            settings.focus = CaptureFocus::Boss;
        else if (name == "center")
            settings.focus = CaptureFocus::Center;
        else
            throw std::runtime_error(std::format(
                "OPENGLAD_DEMO_CAPTURE_FOCUS must be player, boss or center, "
                "got '{}'", name));
    }
    return settings;
}

// Writes captured frames as 8-bit indexed BMPs, which is what
// scripts/media/capture_showcase.sh feeds to ffmpeg. The game renders into a
// 32bpp canvas, but every pixel it plots comes from the 256-entry session
// palette, so the frame can be turned back into the indexed image it really
// is. Colours that are not palette-exact (blends, filtered scaling) fall back
// to a nearest-entry search, memoized per distinct pixel value.
class IndexedFrameWriter {
public:
    // Writes `src` (the session's 32bpp canvas) to `path` as an 8-bit BMP,
    // using `pal6` (the session's live 6-bit palette) as the colour table.
    // `width`/`height` of 0 mean the whole surface; otherwise only that
    // top-left region is written.
    bool write(SDL_Surface* src, const std::array<unsigned char, 768>& pal6,
               const std::string& path, int width = 0, int height = 0)
    {
        if (src == nullptr)
            return false;
        const int w = width > 0 ? std::min(width, src->w) : src->w;
        const int h = height > 0 ? std::min(height, src->h) : src->h;
        if (!ensure_surface(w, h))
            return false;
        refresh_palette(src->format, pal6);

        const bool must_lock = SDL_MUSTLOCK(src);
        if (must_lock && !SDL_LockSurface(src))
            return false;
        const auto* base = static_cast<const unsigned char*>(src->pixels);
        auto* out_base = static_cast<unsigned char*>(indexed_->pixels);
        for (int y = 0; y < h; ++y) {
            const auto* row = reinterpret_cast<const Uint32*>(
                base + static_cast<std::ptrdiff_t>(y) * src->pitch);
            unsigned char* out =
                out_base + static_cast<std::ptrdiff_t>(y) * indexed_->pitch;
            for (int x = 0; x < w; ++x)
                out[x] = index_for(row[x] & rgb_mask_);
        }
        if (must_lock)
            SDL_UnlockSurface(src);

        return SDL_SaveBMP(indexed_.get(), path.c_str());
    }

private:
    using SurfacePtr = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

    bool ensure_surface(int w, int h)
    {
        if (indexed_ && indexed_->w == w && indexed_->h == h)
            return true;
        indexed_ = SurfacePtr(
            SDL_CreateSurface(w, h, SDL_PIXELFORMAT_INDEX8), SDL_DestroySurface);
        if (!indexed_)
            return false;
        surface_palette_ = SDL_CreateSurfacePalette(indexed_.get());
        have_palette_ = false;
        return surface_palette_ != nullptr;
    }

    void refresh_palette(SDL_PixelFormat src_format,
                         const std::array<unsigned char, 768>& pal6)
    {
        if (have_palette_ && src_format == lut_format_ && pal6 == cached_pal_)
            return;
        cached_pal_ = pal6;
        lut_format_ = src_format;
        details_ = SDL_GetPixelFormatDetails(src_format);
        rgb_mask_ = details_ != nullptr
                        ? (details_->Rmask | details_->Gmask | details_->Bmask)
                        : 0xFFFFFFFFu;

        exact_.clear();
        nearest_.clear();
        for (int i = 0; i < 256; ++i) {
            const auto idx = static_cast<std::size_t>(i) * 3;
            // The engine stores 6-bit VGA components; *4 is how every draw
            // path widens them (see palette_color_lut in video_sdl.cpp).
            colors_[static_cast<std::size_t>(i)] = SDL_Color{
                static_cast<Uint8>(cached_pal_[idx] * 4),
                static_cast<Uint8>(cached_pal_[idx + 1] * 4),
                static_cast<Uint8>(cached_pal_[idx + 2] * 4), 255};
            const SDL_Color& c = colors_[static_cast<std::size_t>(i)];
            const Uint32 mapped =
                SDL_MapRGB(details_, nullptr, c.r, c.g, c.b) & rgb_mask_;
            exact_.emplace(mapped, static_cast<unsigned char>(i));
        }
        SDL_SetPaletteColors(surface_palette_, colors_.data(), 0, 256);
        have_palette_ = true;
    }

    unsigned char index_for(Uint32 pixel)
    {
        if (const auto it = exact_.find(pixel); it != exact_.end())
            return it->second;
        if (const auto it = nearest_.find(pixel); it != nearest_.end())
            return it->second;

        Uint8 r = 0;
        Uint8 g = 0;
        Uint8 b = 0;
        SDL_GetRGB(pixel, details_, nullptr, &r, &g, &b);
        int best = 0;
        long best_distance = LONG_MAX;
        for (int i = 0; i < 256; ++i) {
            const SDL_Color& c = colors_[static_cast<std::size_t>(i)];
            const long dr = static_cast<long>(c.r) - r;
            const long dg = static_cast<long>(c.g) - g;
            const long db = static_cast<long>(c.b) - b;
            const long distance = dr * dr + dg * dg + db * db;
            if (distance < best_distance) {
                best_distance = distance;
                best = i;
            }
        }
        const auto index = static_cast<unsigned char>(best);
        nearest_.emplace(pixel, index);
        return index;
    }

    SurfacePtr indexed_{nullptr, SDL_DestroySurface};
    SDL_Palette* surface_palette_ = nullptr;
    const SDL_PixelFormatDetails* details_ = nullptr;
    std::array<unsigned char, 768> cached_pal_{};
    std::array<SDL_Color, 256> colors_{};
    std::unordered_map<Uint32, unsigned char> exact_;
    std::unordered_map<Uint32, unsigned char> nearest_;
    Uint32 rgb_mask_ = 0xFFFFFFFFu;
    SDL_PixelFormat lut_format_ = SDL_PIXELFORMAT_UNKNOWN;
    bool have_palette_ = false;
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
static void spawn_random_player_team(screen* s, std::mt19937& rng, int forced_size)
{
    // Collect enemy levels
    std::vector<int> enemy_levels;
    for (auto& uptr : s->world().oblist) {
        walker* w = uptr.get();
        if (w && !w->dead() && w->order() == Order::Living && w->team_num() != 0) {
            enemy_levels.push_back(static_cast<int>(w->stats()->level()));
        }
    }

    // OPENGLAD_DEMO_TEAM_SIZE pins the roster instead of matching the level's
    // living enemies one-for-one. Boss levels field a single high-level
    // defender behind generators, which is not a fight one AI can carry.
    if (forced_size > 0) {
        const int strongest =
            enemy_levels.empty()
                ? 1
                : *std::max_element(enemy_levels.begin(), enemy_levels.end());
        enemy_levels.resize(static_cast<std::size_t>(forced_size), strongest);
    }

    if (enemy_levels.empty()) return;

    std::uniform_int_distribution<int> fam_dist(0, NUM_PLAYABLE - 1);

    for (int enemy_level : enemy_levels) {
        int fam = PLAYABLE_FAMILIES[static_cast<std::size_t>(fam_dist(rng))];
        guy g(fam);
        g.teamnum = 0;
        if (enemy_level > g.level)
            g.upgrade_to_level(static_cast<short>(enemy_level));

        walker* w = guy_create_and_add_walker(g, s);
        if (w) {
            w->set_team_num(0);
            // Deploy onto the level's authored team-0 start markers exactly
            // like the real game (game.cpp), consuming one marker per member
            // and scattering only the overflow. Levels that stage the player
            // team somewhere specific — a boss arena's petitioners' floor,
            // say — then read the way the author meant them to.
            walker* marker = s->first_of(Order::Special, FAMILY_RESERVED_TEAM, 0);
            if (marker) {
                // set_floor MUST precede setxy: setxy re-buckets the
                // floor-keyed obmap at the walker's current floor.
                w->set_floor(marker->floor());
                w->setxy(marker->xpos(), marker->ypos());
                marker->set_dead(1);
            } else {
                w->teleport();
            }
            // Record the level-entry spawn point (mirrors the game.cpp deploy
            // loop; read it back so the teleport fallback is captured exactly).
            w->set_spawn_point(w->xpos(), w->ypos(),
                               static_cast<std::uint8_t>(w->floor()));
        }
    }

    // Retire the unused markers, as the deploy loop does.
    while (walker* marker = s->first_of(Order::Special, FAMILY_RESERVED_TEAM)) {
        marker->set_dead(1);
    }
}

// ---------------------------------------------------------------------------
// Session initialization (called from main thread)
// ---------------------------------------------------------------------------
static void init_session_game(DemoSession& demo, int scen_id, std::mt19937& rng,
                              const std::string& campaign, int forced_team_size)
{
    auto scope = demo.session->activate();
    screen* s = og::runtime::current_session->myscreen_;
    if (!s) return;

    s->save_data.numplayers = 0; // spectator mode
    s->save_data.scen_num = static_cast<short>(scen_id);
    s->save_data.current_campaign = campaign;

    // OPENGLAD_DEMO_CAMPAIGN_STATE=key=value[,key=value...] pre-seeds the
    // campaign decision store so captures can film og.campaign_var
    // consequences (mirrors openglad_text --campaign-state).
    if (const char* state_env = std::getenv("OPENGLAD_DEMO_CAMPAIGN_STATE")) {
        std::istringstream ss(state_env);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            size_t eq = tok.find('=');
            if (eq == std::string::npos || eq == 0 ||
                !s->save_data.campaign_state_set(campaign, tok.substr(0, eq),
                    static_cast<std::int32_t>(
                        std::atol(tok.c_str() + eq + 1)))) {
                throw std::runtime_error(std::format(
                    "OPENGLAD_DEMO_CAMPAIGN_STATE rejected entry '{}'", tok));
            }
        }
    }

    // The demo never selects a company, so this is the default "save0" slot
    // and the bootstrap stays byte-identical (§3.9).
    if (!s->save_data.save(og::data::active_company_slot())) {
        throw std::runtime_error(std::format(
            "openglad_demo failed to bootstrap save0 for scenario {}",
            scen_id));
    }
    if (load_saved_game(og::data::active_company_slot().c_str(), s) == 0) {
        throw std::runtime_error(std::format(
            "openglad_demo failed to load bootstrap save0 for scenario {}",
            scen_id));
    }

    // Spawn a random player team to fight the enemies
    spawn_random_player_team(s, rng, forced_team_size);

    // Point the camera at a player team member.  load_saved_game set
    // view->control before the team existed, so it's still nullptr.
    if (s->viewob[0]) {
        s->viewob[0]->control = s->viewob[0]->find_next_control();
    }

    s->continuous_input();
    s->redrawme = 1;
    s->framecount = 0;
    s->timerstart = static_cast<Uint32>(query_timer_control());
    og::runtime::reset_local_transport_shadow(*demo.session, *s);
    if (!og::runtime::local_transport_active(*demo.session)) {
        throw std::runtime_error(std::format(
            "openglad_demo failed to initialize local transport for scenario {}",
            scen_id));
    }
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
    // Install this session as thread_local current_session/current_game/current_game_session.
    og::runtime::current_session = demo.session.get();
    og::runtime::current_game_session = demo.session.get();
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
static void render_session_frame(screen& s, SDL_Surface* session_surface)
{
    if (s.redrawme) {
        s.draw_panels(s.numviews);
        s.refresh();
        s.redrawme = 0;
    }
    s.redraw();
    s.refresh();

    // SessionScope points E_Screen->render at this session's cell surface, but
    // that swap does not survive a frame: redraw() opens with
    // begin_gameplay_frame(), and set_active_canvas() re-derives render from
    // the display Screen's own world/UI canvases. So the finished frame is
    // sitting in the display's active canvas, not in the cell surface the
    // compositor blits. Copy it across here — the demo pins the world canvas
    // classic, so this is a straight 320x200 copy.
    if (session_surface != nullptr && E_Screen != nullptr &&
        E_Screen->render != nullptr && E_Screen->render != session_surface) {
        SDL_BlitSurface(E_Screen->render, nullptr, session_surface, nullptr);
    }
}

// Cell geometry for compositing a 320x200 session frame into its display
// cell. Cells split the destination evenly (per-index rounding, so the grid
// always covers the full display with no seams). The source is cover-scaled:
// magnify uniformly until the frame fills the cell in both dimensions, times
// the zoom boost, then center-crop the excess.
//
// Shared by the software (lockstep) and GPU (uncapped) compositors so the two
// paths cannot disagree about layout. The lockstep pixel output is pinned by
// scripts/test_demo_smoke.sh; keep the arithmetic bit-exact.
struct CellRects {
    SDL_Rect src;
    SDL_Rect dst;
};

static CellRects cell_rects(int dst_w, int dst_h, int grid_col, int grid_row,
                            int grid_cols, int grid_rows, float zoom_boost)
{
    CellRects r;
    r.dst.x = grid_col * dst_w / grid_cols;
    r.dst.y = grid_row * dst_h / grid_rows;
    r.dst.w = (grid_col + 1) * dst_w / grid_cols - r.dst.x;
    r.dst.h = (grid_row + 1) * dst_h / grid_rows - r.dst.y;

    const float cover = std::max(
        static_cast<float>(r.dst.w) / static_cast<float>(CELL_W),
        static_cast<float>(r.dst.h) / static_cast<float>(CELL_H));
    const float zoom = cover * std::max(1.0f, zoom_boost);
    r.src.w = std::clamp(
        static_cast<int>(std::lround(static_cast<float>(r.dst.w) / zoom)),
        1, CELL_W);
    r.src.h = std::clamp(
        static_cast<int>(std::lround(static_cast<float>(r.dst.h) / zoom)),
        1, CELL_H);
    r.src.x = (CELL_W - r.src.w) / 2;
    r.src.y = (CELL_H - r.src.h) / 2;
    return r;
}

static SDL_FRect to_frect(const SDL_Rect& r)
{
    return SDL_FRect{static_cast<float>(r.x), static_cast<float>(r.y),
                     static_cast<float>(r.w), static_cast<float>(r.h)};
}

// Software compositor cell blit (lockstep mode).
static void composite_session(SDL_Surface* dst, SDL_Surface* src,
                              int grid_col, int grid_row,
                              int grid_cols, int grid_rows, float zoom_boost)
{
    if (!src || !dst) return;
    CellRects r = cell_rects(dst->w, dst->h, grid_col, grid_row,
                             grid_cols, grid_rows, zoom_boost);
    SDL_BlitSurfaceScaled(src, &r.src, dst, &r.dst, SDL_SCALEMODE_LINEAR);
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

        unsigned int demo_seed = static_cast<unsigned int>(time(nullptr));
        if (const char* seed_env = std::getenv("OPENGLAD_DEMO_SEED")) {
            const std::string_view seed_text(seed_env);
            const auto parsed = std::from_chars(
                seed_text.data(), seed_text.data() + seed_text.size(),
                demo_seed);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != seed_text.data() + seed_text.size()) {
                throw std::runtime_error(std::format(
                    "OPENGLAD_DEMO_SEED must be an unsigned integer, got '{}'",
                    seed_text));
            }
        }
        Log("openglad_demo: seed {}\n", demo_seed);

        // Opt-in targeting/capture knobs. All strictly inert when unset.
        const char* campaign_env = env_or_null("OPENGLAD_DEMO_CAMPAIGN");
        const std::string demo_campaign =
            campaign_env != nullptr ? std::string(campaign_env)
                                    : std::string(DEFAULT_CAMPAIGN);
        const std::vector<int> scenario_override =
            env_scenario_list("OPENGLAD_DEMO_SCENARIOS");
        const int forced_team_size = env_int("OPENGLAD_DEMO_TEAM_SIZE", 0, 0);

        // OPENGLAD_DEMO_SHOW_FPS overrides the graphics/show_fps setting the
        // main game exposes as --show-fps; unset (or empty) defers to it, so a
        // fresh config dir means off. The log line is emitted here, not at the
        // first draw, so even a one-frame run records the decision — and it is
        // prefixed because gparser already logs a bare "FPS overlay on." for
        // the command-line flag.
        const char* fps_env = env_or_null("OPENGLAD_DEMO_SHOW_FPS");
        const bool show_fps =
            fps_env != nullptr
                ? (std::string_view(fps_env) != "0" &&
                   std::string_view(fps_env) != "off")
                : cfg.is_on("graphics", "show_fps");
        if (show_fps)
            Log("openglad_demo: FPS overlay on\n");

        const CaptureSettings capture = capture_settings_from_env();
        std::unique_ptr<IndexedFrameWriter> capture_writer;
        if (capture.enabled()) {
            std::error_code ec;
            std::filesystem::create_directories(capture.dir, ec);
            if (ec) {
                throw std::runtime_error(std::format(
                    "OPENGLAD_DEMO_CAPTURE_DIR '{}' is not usable: {}",
                    capture.dir, ec.message()));
            }
            capture_writer = std::make_unique<IndexedFrameWriter>();
            Log("openglad_demo: capturing session {} to {} "
                "(every {} frames, start {}, limit {})\n",
                capture.session_index, capture.dir, capture.every,
                capture.start, capture.limit);
        }

        // Pacing mode, decided once at startup. Lockstep is the pixel-pinned
        // path — one sim tick per rendered frame, software compositing, sleep
        // to FRAME_PERIOD — and every run that dumps pixels (capture or
        // composite dump) must take it, or the byte-identity pins in
        // scripts/test_demo_smoke.sh and scripts/media/capture_showcase.sh
        // would depend on machine speed. Uncapped holds the sim at
        // FRAME_PERIOD on the wall clock and renders as fast as the machine
        // allows. OPENGLAD_DEMO_LOCKSTEP=1 forces lockstep for reproducible
        // debugging without a dump.
        const char* composite_dump = env_or_null("OPENGLAD_DEMO_COMPOSITE_DUMP");
        const char* lockstep_env = env_or_null("OPENGLAD_DEMO_LOCKSTEP");
        const bool lockstep =
            capture.enabled() || composite_dump != nullptr ||
            (lockstep_env != nullptr &&
             (std::string_view(lockstep_env) == "1" ||
              std::string_view(lockstep_env) == "on"));
        Log("openglad_demo: pacing {}\n", lockstep ? "lockstep" : "uncapped");
        // Machine-rate presenting would otherwise animate every render-only
        // cosmetic (weather, ripples, trails) at hardware speed; the wall
        // clock holds them at the classic 72 fps cadence. Lockstep keeps the
        // per-redraw advance its byte-pinned dumps rely on.
        if (!lockstep)
            effects_set_wall_clock_cadence(true);

        // OPENGLAD_DEMO_MAX_FRAMES bounds only the main loop. Grid sizing and
        // scenario selection remain the real demo paths; automation can pick a
        // cheap topology explicitly with OPENGLAD_DEMO_GRID=1x1.
        SDL_Init(SDL_INIT_VIDEO);
        int display_w;
        int display_h;
        const SDL_DisplayMode* dm =
            SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
        if (!dm) {
            LogError("SDL_GetDesktopDisplayMode failed: {}\n", SDL_GetError());
            return 1;
        } else {
            display_w = dm->w;
            display_h = dm->h;
        }
        // The compositor cover-scales each 320x200 session into its display
        // cell, so the grid is a fixed choice rather than a function of
        // display size. 6x4 = 24 cells is the documented demo intent (see
        // CMakeLists.txt, openglad_demo); the render loop is sequential on
        // the main thread, so cell count drives steady-state frame cost
        // linearly — tune with OPENGLAD_DEMO_GRID=COLSxROWS.
        int grid_cols = 6;
        int grid_rows = 4;
        if (const char* grid_env = std::getenv("OPENGLAD_DEMO_GRID")) {
            int gc = 0, gr = 0;
            if (std::sscanf(grid_env, "%dx%d", &gc, &gr) == 2 && gc >= 1 && gr >= 1) {
                grid_cols = gc;
                grid_rows = gr;
            }
        }
        const int num_sessions = grid_cols * grid_rows;
        const float cell_zoom =
            env_float("OPENGLAD_DEMO_ZOOM", DEFAULT_CELL_ZOOM, 1.0f);

        Log("Display: {}x{}, grid: {}x{} = {} sessions, cell zoom {:.2f}\n",
            display_w, display_h, grid_cols, grid_rows, num_sessions,
            cell_zoom);

        if (display_w < grid_cols || display_h < grid_rows) {
            LogError("Display too small for a {}x{} grid\n",
                     grid_cols, grid_rows);
            return 1;
        }

        cfg.apply_setting("graphics", "width", std::format("{}", display_w));
        cfg.apply_setting("graphics", "height", std::format("{}", display_h));
        // Start from the normal zoom preference; the display-owning Screen is
        // explicitly pinned below because each demo cell is fixed-size.
        cfg.apply_setting("graphics", "zoom", "1.0");
        cfg.apply_setting("graphics", "smoothing", "off");
        cfg.apply_setting("graphics", "fullscreen", "on");

        srand(demo_seed);

        // --- Create the display-owning "host" session ---
        og::runtime::GameSession::Config host_cfg;
        host_cfg.numviews = 1;
        host_cfg.allocate_screen = true;
        host_cfg.create_display = true;
        host_cfg.allocate_prefs = true;
        host_cfg.install_legacy_globals = true;
        og::runtime::GameSession host_session(host_cfg);
        // SessionScope swaps only the surface pointer, while canvas strides and
        // view geometry still come from the display-owning Screen. Hold its real
        // classic pin before constructing any 320x200 sub-session surfaces so a
        // desktop-sized zoom canvas cannot index past those surfaces. The pin
        // also survives resize/fullscreen events for the demo's lifetime.
        E_Screen->set_world_canvas_pinned_classic(true);
        host_session.myscreen_->relayout_views();

        // Uncapped mode measures how fast the machine can render; vsync would
        // pin that to the display refresh. OPENGLAD_DEMO_VSYNC=1 keeps the
        // driver default for tear-free viewing.
        if (!lockstep) {
            const char* vsync_env = env_or_null("OPENGLAD_DEMO_VSYNC");
            if (vsync_env == nullptr || std::string_view(vsync_env) == "0")
                E_Screen->set_vsync(false);
        }

        init_input();
        load_player_control_settings_from_cfg(cfg);

        // Create composite surface and texture at the full display resolution.
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> composite_surface(
            SDL_CreateSurface(display_w, display_h, SDL_PIXELFORMAT_XRGB8888),
            SDL_DestroySurface);
        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> composite_tex(
            SDL_CreateTexture(E_Screen->renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, display_w, display_h),
            SDL_DestroyTexture);
        if (!composite_surface || !composite_tex) {
            LogError("Failed to create composite {} ({}x{}, renderer={}): {}\n",
                     !composite_surface ? "surface" : "texture",
                     display_w, display_h,
                     static_cast<void*>(E_Screen->renderer), SDL_GetError());
            return 1;
        }
        // SDL3 defaults alpha-format textures to BLEND; the composite pixels
        // are XRGB (alpha=0), so blending would present nothing.
        SDL_SetTextureBlendMode(composite_tex.get(), SDL_BLENDMODE_NONE);

        // Whole-grid captures bypass the scaled on-screen composite: the
        // indexed writer depends on palette-exact pixels, so capture frames
        // are assembled from unscaled 1:1 cell blits at native resolution.
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>
            capture_grid_surface(nullptr, SDL_DestroySurface);
        if (capture.enabled() && capture.session_index < 0) {
            capture_grid_surface.reset(SDL_CreateSurface(
                grid_cols * CELL_W, grid_rows * CELL_H,
                SDL_PIXELFORMAT_XRGB8888));
            if (!capture_grid_surface) {
                LogError("Failed to create capture grid surface: {}\n",
                         SDL_GetError());
                return 1;
            }
        }

        // Private scratch for the FPS readout. The host canvas cannot be used:
        // it is the surface every cell is copied out of in render_session_frame,
        // so anything left there reaches the next frame's capture output.
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> fps_strip(
            nullptr, SDL_DestroySurface);
        FpsCounter fps_counter;
        if (show_fps) {
            fps_strip.reset(SDL_CreateSurface(FPS_STRIP_W, FPS_STRIP_H,
                                              SDL_PIXELFORMAT_XRGB8888));
            if (!fps_strip) {
                LogError("Failed to create FPS overlay surface: {}\n",
                         SDL_GetError());
                return 1;
            }
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

        // Uncapped-mode GPU compositor: one streaming texture per cell, so
        // the renderer scales cells itself and the per-frame CPU cost is N
        // small uploads instead of a full-display software blit plus one
        // display-sized upload. Lockstep keeps the software compositor: its
        // pixel output is pinned by scripts/test_demo_smoke.sh. The textures
        // share the session surface format so SDL_UpdateTexture is a straight
        // row copy.
        using SdlTexturePtr =
            std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
        std::vector<SdlTexturePtr> cell_tex;
        SdlTexturePtr fps_tex(nullptr, SDL_DestroyTexture);
        if (!lockstep) {
            for (int i = 0; i < num_sessions; i++) {
                SDL_Surface* src =
                    demos[static_cast<size_t>(i)].session->session_surface_;
                // GameSession treats a failed session-surface allocation as
                // non-fatal; degrade to a blank cell like the software
                // compositor does instead of dereferencing null.
                if (src == nullptr) {
                    cell_tex.emplace_back(nullptr, SDL_DestroyTexture);
                    continue;
                }
                SdlTexturePtr tex(
                    SDL_CreateTexture(E_Screen->renderer, src->format,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      CELL_W, CELL_H),
                    SDL_DestroyTexture);
                if (!tex) {
                    LogError("Failed to create cell texture {}: {}\n", i,
                             SDL_GetError());
                    return 1;
                }
                // XRGB pixels carry alpha 0; blending would present nothing.
                SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_NONE);
                // Matches the software compositor's SDL_SCALEMODE_LINEAR.
                SDL_SetTextureScaleMode(tex.get(), SDL_SCALEMODE_LINEAR);
                cell_tex.push_back(std::move(tex));
            }
            if (show_fps) {
                fps_tex.reset(SDL_CreateTexture(
                    E_Screen->renderer, fps_strip->format,
                    SDL_TEXTUREACCESS_STREAMING, FPS_STRIP_W, FPS_STRIP_H));
                if (!fps_tex) {
                    LogError("Failed to create FPS overlay texture: {}\n",
                             SDL_GetError());
                    return 1;
                }
                SDL_SetTextureBlendMode(fps_tex.get(), SDL_BLENDMODE_NONE);
                // Integer-magnified 5x6 font: NEAREST keeps it crisp, as in
                // the software path's scaled blit.
                SDL_SetTextureScaleMode(fps_tex.get(), SDL_SCALEMODE_NEAREST);
            }
        }

        // Build a shuffled list of scenario IDs.
        std::mt19937 demo_rng(demo_seed);
        auto pick_scenarios = [&]() {
            std::vector<int> picks;
            if (!scenario_override.empty()) {
                // Explicit list: assigned in order so a capture run always
                // lands the same level in the same cell.
                for (int i = 0; i < num_sessions; i++) {
                    picks.push_back(scenario_override[
                        static_cast<size_t>(i) % scenario_override.size()]);
                }
                return picks;
            }
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
                              chosen[static_cast<size_t>(i)], demo_rng,
                              demo_campaign, forced_team_size);
        }
        E_Screen->suppress_present = false;

        Log("openglad_demo: campaign {}\n", demo_campaign);
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
        // Uncapped-mode sim scheduler: 0..8 wall-clock-due ticks per loop
        // pass, so game speed holds at exactly one tick per FRAME_PERIOD
        // until a single render pass exceeds 8 periods (~653 ms); past that
        // the backlog is dropped with a log line instead of slowing the game.
        og::core::FixedStepAccumulator sim_clock{FRAME_PERIOD, /*max_catchup=*/8};

        auto last_time = std::chrono::steady_clock::now();
        const auto run_start = last_time;
        bool running = true;
        // 64-bit: an unattended uncapped run presents at machine rate, and a
        // 32-bit rendered-frame counter would overflow within days.
        long long frames_run = 0;       // sim ticks, in both pacing modes
        long long rendered_frames = 0;  // loop passes that presented
        int captured_frames = 0;

        // Phases 2+3: one barriered sim generation across all sessions.
        auto run_generation = [&] {
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
        };

        while (running) {
            auto frame_start = std::chrono::steady_clock::now();

            // --- Phase 1: Poll SDL events (main thread only) ---
            // The demo manages its own shutdown — don't forward quit events
            // to handle_events() which would call exit(0) via quit().
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                    continue;
                }
                if (event.type == SDL_EVENT_KEY_DOWN &&
                    event.key.key == SDLK_ESCAPE) {
                    running = false;
                    continue;
                }
                handle_events(event);
            }

            if (!running) break;

            // Lockstep: exactly one sim tick per rendered frame. Uncapped:
            // whatever the wall clock owes, clamped so the run never exceeds
            // its OPENGLAD_DEMO_MAX_FRAMES tick budget.
            int ticks_this_iter = 1;
            if (!lockstep) {
                const auto now = std::chrono::steady_clock::now();
                const auto due = sim_clock.advance(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        now - last_time));
                last_time = now;
                if (due.dropped_ticks > 0) {
                    Log("openglad_demo: dropped {} sim ticks after a stall\n",
                        due.dropped_ticks);
                }
                ticks_this_iter = due.ticks;
                if (max_frames > 0) {
                    ticks_this_iter = static_cast<int>(std::min<long long>(
                        ticks_this_iter, max_frames - frames_run));
                }
            }
            for (int t = 0; t < ticks_this_iter; ++t)
                run_generation();

            // Frame gating for the opt-in capture, decided once so the
            // single-cell and whole-grid paths cannot disagree.
            const bool capture_this_frame =
                capture_writer != nullptr &&
                frames_run >= capture.start &&
                ((frames_run - capture.start) % capture.every) == 0 &&
                (capture.limit == 0 || captured_frames < capture.limit);
            const auto capture_path = [&] {
                return std::format("{}/frame{:05d}.bmp", capture.dir,
                                   captured_frames);
            };

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

                if (capture_writer &&
                    (capture.session_index < 0 || i == capture.session_index))
                    apply_capture_focus(*s, capture.focus);

                render_session_frame(
                    *s, demos[static_cast<size_t>(i)].session->session_surface_);

                // --- Phase 4b: opt-in frame dump (showcase media) ---
                if (capture_this_frame && i == capture.session_index) {
                    const std::string path = capture_path();
                    og::runtime::GameSession& sess =
                        *demos[static_cast<size_t>(i)].session;
                    if (!capture_writer->write(sess.session_surface_,
                                               sess.curpal_, path)) {
                        throw std::runtime_error(std::format(
                            "openglad_demo failed to write capture frame {}: {}",
                            path, SDL_GetError()));
                    }
                    captured_frames++;
                }
            }
            E_Screen->suppress_present = false;

            // --- Phase 5: Composite and present (main thread only) ---
            if (lockstep) {
                // Software compositor: this branch's pixel output (and the
                // composite dump written from it) is byte-pinned by
                // scripts/test_demo_smoke.sh.
                SDL_FillSurfaceRect(composite_surface.get(), nullptr, 0x000000);
                for (int i = 0; i < num_sessions; i++) {
                    SDL_Surface* src = demos[static_cast<size_t>(i)].session->session_surface_;
                    int col = i % grid_cols;
                    int row = i / grid_cols;
                    composite_session(composite_surface.get(), src, col, row,
                                      grid_cols, grid_rows, cell_zoom);
                }

                // Whole-grid dump: every cell at once, at native cell resolution
                // (the on-screen composite is scaled, so it is unusable here).
                if (capture_this_frame && capture.session_index < 0) {
                    for (int i = 0; i < num_sessions; i++) {
                        SDL_Surface* src =
                            demos[static_cast<size_t>(i)].session->session_surface_;
                        SDL_Rect cell{(i % grid_cols) * CELL_W,
                                      (i / grid_cols) * CELL_H, CELL_W, CELL_H};
                        SDL_BlitSurface(src, nullptr, capture_grid_surface.get(),
                                        &cell);
                    }
                    const std::string path = capture_path();
                    if (!capture_writer->write(capture_grid_surface.get(),
                                               host_session.curpal_, path)) {
                        throw std::runtime_error(std::format(
                            "openglad_demo failed to write capture frame {}: {}",
                            path, SDL_GetError()));
                    }
                    captured_frames++;
                }

                // One readout for the whole grid, sampled once per presented
                // frame, so it measures the compositor loop rather than any one
                // session. It lands here — after both capture paths have consumed
                // the session surfaces, before the present — so it reaches the
                // screen and OPENGLAD_DEMO_COMPOSITE_DUMP but never a capture
                // frame, which must stay palette-exact.
                if (show_fps) {
                    const int fps = fps_counter.update(SDL_GetTicks());
                    const std::string fps_text = std::format("FPS: {}", fps);
                    // Text draws through og::runtime::current_session and
                    // E_Screen->render, not through the screen it is called on;
                    // both must name the host for the palette lookup to resolve.
                    auto host_scope = host_session.activate();
                    text& font = host_session.myscreen_->text_normal;
                    const int text_w = std::clamp(font.query_width(fps_text) + 4,
                                                  1, FPS_STRIP_W);
                    SDL_Surface* saved_render = E_Screen->render;
                    E_Screen->render = fps_strip.get();
                    SDL_FillSurfaceRect(fps_strip.get(), nullptr, 0x000000);
                    font.write_xy(2, 2, fps_text, YELLOW, static_cast<short>(1));
                    E_Screen->render = saved_render;

                    // Blit only the used span so the backing box hugs the text
                    // instead of trailing the strip's worst-case width.
                    const SDL_Rect strip_src{0, 0, text_w, FPS_STRIP_H};

                    // Integer magnification keeps the 5x6 font crisp on HiDPI
                    // displays; the clamps hold the strip on-surface when the
                    // display is smaller than the strip plus its margin.
                    const int fps_scale = std::max(1, display_w / 1024);
                    const int strip_w = text_w * fps_scale;
                    const int strip_h = FPS_STRIP_H * fps_scale;
                    const int margin = 8 * fps_scale;
                    SDL_Rect strip_dst{
                        std::max(0, display_w - strip_w - margin),
                        std::clamp(margin, 0, std::max(0, display_h - strip_h)),
                        strip_w, strip_h};
                    SDL_BlitSurfaceScaled(fps_strip.get(), &strip_src,
                                          composite_surface.get(), &strip_dst,
                                          SDL_SCALEMODE_NEAREST);
                }

                SDL_UpdateTexture(composite_tex.get(), nullptr,
                                  composite_surface->pixels,
                                  composite_surface->pitch);
                SDL_RenderClear(E_Screen->renderer);
                SDL_RenderTexture(E_Screen->renderer, composite_tex.get(),
                               nullptr, nullptr);
                SDL_RenderPresent(E_Screen->renderer);
            } else {
                // GPU compositor: the renderer scales each cell straight from
                // its streaming texture. No capture path runs here — capture
                // and composite dumps force lockstep at startup.
                SDL_SetRenderDrawColor(E_Screen->renderer, 0, 0, 0, 255);
                SDL_RenderClear(E_Screen->renderer);
                for (int i = 0; i < num_sessions; i++) {
                    SDL_Surface* src =
                        demos[static_cast<size_t>(i)].session->session_surface_;
                    if (src == nullptr ||
                        !cell_tex[static_cast<size_t>(i)]) {
                        continue;  // surface allocation failed: blank cell
                    }
                    SDL_UpdateTexture(cell_tex[static_cast<size_t>(i)].get(),
                                      nullptr, src->pixels, src->pitch);
                    const CellRects r = cell_rects(
                        display_w, display_h, i % grid_cols, i / grid_cols,
                        grid_cols, grid_rows, cell_zoom);
                    const SDL_FRect src_f = to_frect(r.src);
                    const SDL_FRect dst_f = to_frect(r.dst);
                    SDL_RenderTexture(E_Screen->renderer,
                                      cell_tex[static_cast<size_t>(i)].get(),
                                      &src_f, &dst_f);
                }

                if (show_fps) {
                    const int fps = fps_counter.update(SDL_GetTicks());
                    const std::string fps_text = std::format("FPS: {}", fps);
                    // Text draws through og::runtime::current_session and
                    // E_Screen->render, not through the screen it is called on;
                    // both must name the host for the palette lookup to resolve.
                    auto host_scope = host_session.activate();
                    text& font = host_session.myscreen_->text_normal;
                    const int text_w = std::clamp(font.query_width(fps_text) + 4,
                                                  1, FPS_STRIP_W);
                    SDL_Surface* saved_render = E_Screen->render;
                    E_Screen->render = fps_strip.get();
                    SDL_FillSurfaceRect(fps_strip.get(), nullptr, 0x000000);
                    font.write_xy(2, 2, fps_text, YELLOW, static_cast<short>(1));
                    E_Screen->render = saved_render;

                    SDL_UpdateTexture(fps_tex.get(), nullptr,
                                      fps_strip->pixels, fps_strip->pitch);

                    // Same strip geometry as the software path, rendered from
                    // only the used span so the backing box hugs the text.
                    const SDL_Rect strip_src{0, 0, text_w, FPS_STRIP_H};
                    const int fps_scale = std::max(1, display_w / 1024);
                    const int strip_w = text_w * fps_scale;
                    const int strip_h = FPS_STRIP_H * fps_scale;
                    const int margin = 8 * fps_scale;
                    const SDL_Rect strip_dst{
                        std::max(0, display_w - strip_w - margin),
                        std::clamp(margin, 0, std::max(0, display_h - strip_h)),
                        strip_w, strip_h};
                    const SDL_FRect strip_src_f = to_frect(strip_src);
                    const SDL_FRect strip_dst_f = to_frect(strip_dst);
                    SDL_RenderTexture(E_Screen->renderer, fps_tex.get(),
                                      &strip_src_f, &strip_dst_f);
                }

                SDL_RenderPresent(E_Screen->renderer);
            }
            rendered_frames++;

            // If all sessions are done, restart them with new scenarios.
            if (active_count == 0) {
                Log("All sessions finished, restarting...\n");
                chosen = pick_scenarios();
                E_Screen->suppress_present = true;
                for (int i = 0; i < num_sessions; i++) {
                    init_session_game(demos[static_cast<size_t>(i)],
                                      chosen[static_cast<size_t>(i)], demo_rng,
                                      demo_campaign, forced_team_size);
                }
                E_Screen->suppress_present = false;
                if (!lockstep) {
                    // Re-init takes seconds of wall time; a stale elapsed
                    // base would burst catch-up ticks into fresh scenarios.
                    sim_clock.reset();
                    last_time = std::chrono::steady_clock::now();
                }
            }

            // Frame pacing: sleep remainder of the target frame period.
            // A capture run has no viewer, so it takes the frames as fast as
            // the simulation produces them. Uncapped never sleeps: the
            // present rate is the point.
            if (lockstep && !capture.enabled()) {
                auto elapsed = std::chrono::steady_clock::now() - frame_start;
                auto remaining = FRAME_PERIOD - elapsed;
                if (remaining > std::chrono::microseconds(1000)) {
                    std::this_thread::sleep_for(remaining);
                }
            }

            frames_run += ticks_this_iter;
            if (max_frames > 0 && frames_run >= max_frames) {
                running = false;
            }
            if (capture.limit > 0 && captured_frames >= capture.limit) {
                running = false;
            }

            // Opt-in debug dump of the final presented composite (the scaled
            // grid exactly as shown on screen), for eyeballing the layout
            // without a screen recorder. Setting it implies lockstep, so the
            // composite surface always holds the presented frame here.
            if (!running && composite_dump != nullptr) {
                if (!SDL_SaveBMP(composite_surface.get(), composite_dump)) {
                    LogError("composite dump to '{}' failed: {}\n",
                             composite_dump, SDL_GetError());
                }
            }
        }

        // Pre-formatted with std::format: Log's own {} substitution ignores
        // precision specifiers.
        const double run_secs = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - run_start).count();
        Log(std::format(
            "openglad_demo: {} sim ticks, {} rendered frames in {:.3f} s "
            "({:.1f} fps, {:.2f} ticks/s)\n",
            frames_run, rendered_frames, run_secs,
            static_cast<double>(rendered_frames) / std::max(run_secs, 1e-6),
            static_cast<double>(frames_run) / std::max(run_secs, 1e-6)));

        if (capture_writer) {
            Log("openglad_demo: captured {} frames to {}\n",
                captured_frames, capture.dir);
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

        // Cleanup (the unique_ptr deleters also free on any early-return/throw path)
        cell_tex.clear();
        fps_tex.reset();
        composite_tex.reset();
        composite_surface.reset();
        capture_grid_surface.reset();
        fps_strip.reset();
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
