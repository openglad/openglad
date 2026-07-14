/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>

#include <openglad/core/util.h> // LogError
#include <openglad/resources/gparser.h> // cfg_store, ::cfg
#include <algorithm>            // std::copy
#include <openglad/gameplay/guy.h> // complete type for unique_ptr<guy> destructor
#include <openglad/interface/button.h> // complete type for unique_ptr<vbutton> destructor
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/interface/ui/level_editor_state.h>
#include <openglad/interface/screen.h> // screen class
#include <openglad/interface/sound.h> // soundob complete type for play_sound
#include <openglad/core/sound_ids.h> // NUMSOUNDS bound for play_sound dispatch
#include <openglad/platform/video_sdl.h>
#include <openglad/interface/render/view.h>    // options class
#include <openglad/platform/sai2x.h>   // E_Screen
#include <openglad/platform/game_context.h>
#include <openglad/interface/input.h> // provides MouseState, JoyData + includes input_hardware_state.h
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>
#include <openglad/platform/picker_lobby_network_runtime.h>
#include <SDL3/SDL.h>

// Defined in view.cpp — loads allkeys from defaults + keyprefs.dat.
void init_allkeys(int allkeys[][16]);

namespace {
PlatformBridge make_sdl_platform_bridge()
{
    PlatformBridge bridge;

    bridge.present_frame = [] {
        if (E_Screen)
            E_Screen->swap(0, 0, 320, 200);
    };

    bridge.play_sound = [](int sound_id) {
        if (!og::runtime::current_session || !og::runtime::current_session->myscreen_ ||
            !og::runtime::current_session->myscreen_->soundp ||
            sound_id < 0 || sound_id >= NUMSOUNDS)
        {
            return;
        }
        og::runtime::current_session->myscreen_->soundp->play_sound(
            static_cast<short>(sound_id));
    };

    // Music callbacks are intentionally left as no-ops until music playback is
    // moved behind a dedicated platform abstraction.
    bridge.play_music = [](const char*) {};
    bridge.stop_music = [] {};

    bridge.create_surface = [](int, int) -> video* {
        // ownership transferred to caller's RAII immediately
        return new sdl_video(true);
    };

    bridge.create_host_picker_lobby_client =
        [](const og::ui::PickerHostGameOptions& options) {
            return og::platform::create_platform_host_picker_lobby_client(
                options);
        };
    bridge.create_join_picker_lobby_client =
        [](const og::ui::PickerJoinGameOptions& options) {
            return og::platform::create_platform_join_picker_lobby_client(
                options);
        };
    bridge.list_relay_rooms =
        [](const std::string& base_url, const std::string& campaign_tag) {
            return og::platform::list_platform_relay_rooms(
                base_url,
                campaign_tag);
        };

    return bridge;
}
} // namespace

namespace og::runtime {

thread_local SessionState* current_session = nullptr;
thread_local GameSession* current_game_session = nullptr;
std::atomic<SessionState*> primary_session{nullptr};
std::atomic<GameplayContext*> primary_game{nullptr};

GameSession::GameSession(const Config& session_cfg)
    : cfg_(session_cfg)
{
    set_platform_bridge(make_sdl_platform_bridge());

    prev_session_ = current_session;
    prev_game_ = current_game;
    prev_game_session_ = current_game_session;

    // Allocate input hardware state.
    input_hw_ = std::make_unique<InputHardwareState>();

    // Allocate UI sub-objects (Phase 7).
    picker_ = std::make_unique<PickerState>();
    editor_ = std::make_unique<LevelEditorState>();

    if (cfg_.allocate_prefs) {
        prefs_owner_ = std::make_unique<options>();
        init_allkeys(allkeys_);
    }

    if (cfg_.allocate_seeded_rng) {
        seeded_rng_ = std::make_unique<SeededRandom>(cfg_.rng_seed);
        ctx_.rng = seeded_rng_.get();
    }
    if (!ctx_.rng) {
        ctx_.rng = &production_rng_;
    }

    // ctx() reads directly from current_session->ctx_.
    game_.sim_events = ctx_.sim_events.get();
    game_.config = &cfg;
    game_.world = &world_owner_;
    game_.session_rng_ref = &ctx_.rng;
    game_.gameplay_active_ref = &gameplay_active_;

    // Set prefs before creating the screen; viewscreen construction reads it.
    theprefs_ = prefs_owner_.get();

    // Legacy videoptr_ stays at its in-class nullptr default. It formerly held a
    // fabricated DOS-era VGA linear address (0xA0000); that pointer was never
    // written through in production (only putblack uses it, and only tests call
    // putblack, after pointing videoptr_ at a real buffer).

    // Share SDL's keyboard state array. SDL_GetKeyboardState returns a pointer
    // to SDL's internal array — it's the same for all sessions.
    keystates_ = SDL_GetKeyboardState(nullptr);

    // Ensure screen/viewscreen construction always runs with this session active.
    struct ConstructionSessionScope final {
        ConstructionSessionScope(GameSession& session, bool persist_globals)
            : persist_globals_(persist_globals)
            , saved_session_(current_session)
            , saved_game_(current_game)
            , saved_game_session_(current_game_session)
        {
            current_session = &session;
            current_game_session = &session;
            current_game = &session.game_;
            if (persist_globals_) {
                primary_session.store(&session, std::memory_order_release);
                primary_game.store(&session.game_, std::memory_order_release);
            }
        }

        ~ConstructionSessionScope()
        {
            if (!persist_globals_) {
                current_session = saved_session_;
                current_game_session = saved_game_session_;
                current_game = saved_game_;
            }
        }

        bool persist_globals_ = false;
        SessionState* saved_session_ = nullptr;
        GameplayContext* saved_game_ = nullptr;
        GameSession* saved_game_session_ = nullptr;
    } construction_scope(*this, cfg_.install_legacy_globals);

    if (cfg_.allocate_screen) {
        std::unique_ptr<video> surface;
        if (cfg_.create_display) {
            const PlatformBridge& bridge = platform_bridge();
            if (bridge.create_surface) {
                surface.reset(bridge.create_surface(320, 200));
            }
        }
        if (!surface) {
            surface = std::make_unique<sdl_video>(cfg_.create_display);
        }

        screen_owner_ = std::make_unique<::screen>(
            world_owner_,
            std::move(surface),
            cfg_.numviews,
            cfg_.create_display);
        myscreen_ = screen_owner_.get();
        myscreen_->set_render_interpolation_speed_factor(
            g_game_speed_factor_);
        game_.save = &myscreen_->save_data;
        myscreen_->level_runtime_data().set_sim_context(
            &myscreen_->save_data,
            &myscreen_->world().enemy_freeze,
            ctx_.sim_events.get(),
            ctx_.rng,
            &cfg);

        // Ensure this session's curpal_ matches the screen's palette.
        // video_init_palettes() populates video::ourpalette per-instance,
        // but set_palette() writes to current_session->curpal_ — which is
        // a different session when install_legacy_globals is false (e.g.
        // demo sub-sessions).  Copy the authoritative palette here so that
        // rendering with this session active uses correct colors.
        std::copy(screen_owner_->ourpalette.begin(),
                  screen_owner_->ourpalette.end(), curpal_.begin());
    }

    // Create per-session render surface for sub-sessions sharing a display.
    if (cfg_.allocate_screen && !cfg_.create_display) {
        session_surface_ = SDL_CreateRGBSurface(
            SDL_SWSURFACE, 320, 200, 32, 0, 0, 0, 0);
        if (!session_surface_) {
            LogError("GameSession: SDL_CreateRGBSurface failed: {}\n",
                     SDL_GetError());
        }
    }
}

::screen* GameSession::screen_ptr() const { return screen_owner_.get(); }
options* GameSession::prefs_ptr() const { return prefs_owner_.get(); }
const cfg_store& GameSession::config() const { return ::cfg; }
bool GameSession::has_local_transport_runtime() const noexcept
{
    return local_transport_runtime_ != nullptr;
}

GameSession::~GameSession()
{
    clear_local_transport_shadow(*this);

    if (cfg_.install_legacy_globals) {
        if (current_session == this)
            current_session = prev_session_;
        if (current_game_session == this)
            current_game_session = prev_game_session_;
        if (current_game == &game_)
            current_game = prev_game_;

        // Child threads inherit session/game via these atomics when their
        // thread_local pointers are null. Clear or restore them on teardown
        // so ensure_thread_session()/ensure_thread_game() never pick a dead
        // session context.
        SessionState* expected_session = this;
        (void)primary_session.compare_exchange_strong(
            expected_session, prev_session_, std::memory_order_acq_rel,
            std::memory_order_acquire);
        GameplayContext* expected_game = &game_;
        (void)primary_game.compare_exchange_strong(
            expected_game, prev_game_, std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    screen_owner_.reset();
    prefs_owner_.reset();
    seeded_rng_.reset();

    if (session_surface_) {
        SDL_DestroySurface(session_surface_);
        session_surface_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// SessionScope: RAII activation of a session's globals
// ---------------------------------------------------------------------------

GameSession::SessionScope GameSession::activate(bool swap_render)
{
    return SessionScope(*this, swap_render);
}

GameSession::SessionScope::SessionScope(GameSession& session, bool swap_render)
    : session_(&session)
{
    // Save current session
    saved_session_ = current_session;
    saved_game_session_ = current_game_session;
    saved_game_ = current_game;

    // Install this session as current.
    // ctx() also reads from current_session->ctx_, so no separate context
    // installation is needed.
    current_session = session_;
    current_game_session = session_;
    current_game = &session_->game_;

    // Swap render surface if this session has its own.
    if (swap_render && session_->session_surface_ && E_Screen) {
        saved_render_surface_ = E_Screen->render;
        did_swap_render_ = true;
        E_Screen->render = session_->session_surface_;
    }
}

GameSession::SessionScope::~SessionScope()
{
    if (!session_) return; // moved-from

    // Restore render surface — mirror the activation condition.
    if (did_swap_render_ && E_Screen) {
        E_Screen->render = saved_render_surface_;
    }

    // Restore previous session.  ctx() follows current_session automatically.
    current_session = saved_session_;
    current_game_session = saved_game_session_;
    current_game = saved_game_;
}

GameSession::SessionScope::SessionScope(SessionScope&& other) noexcept
    : session_(other.session_)
    , saved_session_(other.saved_session_)
    , saved_game_(other.saved_game_)
    , saved_game_session_(other.saved_game_session_)
    , saved_render_surface_(other.saved_render_surface_)
    , did_swap_render_(other.did_swap_render_)
{
    other.session_ = nullptr;
}

} // namespace og::runtime

// set_game_speed() — moved from core/util.cpp (Batch 7).
// Defined outside the namespace because base.h declares it at global scope.
void set_game_speed(float factor)
{
    const float clamped_factor = (factor < 0.0f) ? 0.0f : factor;
    og::runtime::current_session->g_game_speed_factor_ = clamped_factor;
    if (og::runtime::current_session->myscreen_ != nullptr)
    {
        og::runtime::current_session->myscreen_->
            set_render_interpolation_speed_factor(clamped_factor);
    }
}
