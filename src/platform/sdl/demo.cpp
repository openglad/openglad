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
#include <openglad/core/util.h>
#include <openglad/core/version.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/legacy/base.h>
#include <openglad/resources/io.h>
#include <openglad/platform/sai2x.h>
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

// Demo sessions are PINNED to the classic 320x200 canvas: the compositor
// assumes every session surface is exactly one CELL_W x CELL_H cell, and demo
// sessions never call Screen::set_world_canvas_size, so their world canvas
// stays shared with the fixed-size UI canvas.
inline constexpr int CELL_W = 320;
inline constexpr int CELL_H = 200;

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
    // top-left region is written (the composite surface is display-sized but
    // only its grid area carries sessions).
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
        int grid_cols = display_w / CELL_W;
        int grid_rows = display_h / CELL_H;
        // The render loop is sequential on the main thread, so cell count
        // drives steady-state frame cost linearly. Cap to the documented
        // demo intent (4x3 = 12 cells, see CMakeLists.txt:1086) so a 4K+
        // desktop doesn't drop FPS into the single digits. Power users can
        // opt back in via OPENGLAD_DEMO_GRID=COLSxROWS.
        if (const char* grid_env = std::getenv("OPENGLAD_DEMO_GRID")) {
            int gc = 0, gr = 0;
            if (std::sscanf(grid_env, "%dx%d", &gc, &gr) == 2 && gc >= 1 && gr >= 1) {
                grid_cols = gc;
                grid_rows = gr;
            }
        } else {
            grid_cols = std::min(grid_cols, 4);
            grid_rows = std::min(grid_rows, 3);
        }
        const int num_sessions = grid_cols * grid_rows;

        Log("Display: {}x{}, grid: {}x{} = {} sessions\n",
            display_w, display_h, grid_cols, grid_rows, num_sessions);

        if (num_sessions <= 0) {
            LogError("Display too small for even one {}x{} cell\n", CELL_W, CELL_H);
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
            LogError("Failed to create composite surface/texture\n");
            return 1;
        }
        // SDL3 defaults alpha-format textures to BLEND; the composite pixels
        // are XRGB (alpha=0), so blending would present nothing.
        SDL_SetTextureBlendMode(composite_tex.get(), SDL_BLENDMODE_NONE);

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

        bool running = true;
        int frames_run = 0;
        int captured_frames = 0;

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
            SDL_FillSurfaceRect(composite_surface.get(), nullptr, 0x000000);
            for (int i = 0; i < num_sessions; i++) {
                SDL_Surface* src = demos[static_cast<size_t>(i)].session->session_surface_;
                int col = i % grid_cols;
                int row = i / grid_cols;
                composite_session(composite_surface.get(), src, col, row);
            }

            // Whole-grid dump: every cell at once, cropped to the grid area
            // (the composite surface itself is display-sized).
            if (capture_this_frame && capture.session_index < 0) {
                const std::string path = capture_path();
                if (!capture_writer->write(composite_surface.get(),
                                           host_session.curpal_, path,
                                           grid_cols * CELL_W,
                                           grid_rows * CELL_H)) {
                    throw std::runtime_error(std::format(
                        "openglad_demo failed to write capture frame {}: {}",
                        path, SDL_GetError()));
                }
                captured_frames++;
            }

            SDL_UpdateTexture(composite_tex.get(), nullptr,
                              composite_surface->pixels,
                              composite_surface->pitch);
            SDL_RenderClear(E_Screen->renderer);
            SDL_RenderTexture(E_Screen->renderer, composite_tex.get(),
                           nullptr, nullptr);
            SDL_RenderPresent(E_Screen->renderer);

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
            }

            // Frame pacing: sleep remainder of the target frame period.
            // A capture run has no viewer, so it takes the frames as fast as
            // the simulation produces them.
            if (!capture.enabled()) {
                auto elapsed = std::chrono::steady_clock::now() - frame_start;
                auto remaining = FRAME_PERIOD - elapsed;
                if (remaining > std::chrono::microseconds(1000)) {
                    std::this_thread::sleep_for(remaining);
                }
            }

            frames_run++;
            if (max_frames > 0 && frames_run >= max_frames) {
                running = false;
            }
            if (capture.limit > 0 && captured_frames >= capture.limit) {
                running = false;
            }
        }

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
        composite_tex.reset();
        composite_surface.reset();
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
