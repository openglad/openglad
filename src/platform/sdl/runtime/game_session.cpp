/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/platform/game_session.h>

#include <openglad/core/util.h> // LogError
#include <openglad/resources/gparser.h> // cfg_store, ::cfg
#include <algorithm>            // std::copy
#include <set>
#include <openglad/gameplay/guy.h> // complete type for unique_ptr<guy> destructor
#include <openglad/interface/input/button.h> // complete type for unique_ptr<vbutton> destructor
#include <openglad/platform/picker_ui_state.h>
#include <openglad/platform/level_editor_state.h>
#include <openglad/interface/render/pixien.h>  // complete type for PickerState's unique_ptr<pixieN>
#include <openglad/interface/screen.h> // screen class (pulls in base.h → myscreen macro)
#include <openglad/interface/render/view.h>    // options class (defines theprefs macro)
#include <openglad/interface/render/sai2x.h>   // E_Screen
#include <openglad/platform/game_context.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/interface/input/input.h> // provides MouseState, JoyData + includes input_hardware_state.h
#include "SDL.h"

// Defined in view.cpp — loads allkeys from defaults + keyprefs.dat.
void init_allkeys(int allkeys[][16]);

// The legacy global macros (myscreen, theprefs) expand through current_session.
// This file manages current_session itself, so we #undef the macros to avoid
// accidental expansion in the implementation below.
#undef myscreen
#undef theprefs

namespace og::runtime {

thread_local GameSession* current_session = nullptr;
std::atomic<GameSession*> primary_session{nullptr};

GameSession::GameSession(const Config& session_cfg)
    : cfg_(session_cfg)
{
    // Preserve mounted-campaign state that lives on the context.  This is
    // populated before sessions are created (io_init) and must not be lost
    // when we install a session-specific context.
    GameContext& prev_ctx = ::ctx();

    prev_session_ = current_session;

    // Allocate input hardware state.
    input_hw_ = std::make_unique<InputHardwareState>();

    // Allocate UI sub-objects (Phase 7).
    picker_ = std::make_unique<PickerState>();
    editor_ = std::make_unique<LevelEditorState>();

    // Wire up the timer anchor so reset_timer()/query_timer() use this session's time.
    g_reset_time_ptr = &reset_time_;

    if (cfg_.allocate_prefs) {
        prefs_owner_ = std::make_unique<options>();
        init_allkeys(allkeys_);
    }
    ctx_.mounted_campaign = prev_ctx.mounted_campaign;

    if (cfg_.allocate_seeded_rng) {
        seeded_rng_ = std::make_unique<SeededRandom>(cfg_.rng_seed);
        ctx_.rng = seeded_rng_.get();
    }
    if (!ctx_.rng) {
        ctx_.rng = &production_rng_;
    }
    gameplay_.sim_events = ctx_.sim_events.get();
    gameplay_.world = &world_;

    // ctx() now reads from current_session->ctx_ directly; no need to
    // call set_global_context.

    // Set session members before creating the screen, because the screen
    // constructor creates viewscreens whose constructors read theprefs (macro).
    theprefs_ = prefs_owner_.get();

    // Initialize legacy video pointer (VGA linear buffer address from DOS era).
    videoptr_ = reinterpret_cast<unsigned char*>(VIDEO_LINEAR);

    // Share SDL's keyboard state array. SDL_GetKeyboardState returns a pointer
    // to SDL's internal array — it's the same for all sessions.
    keystates_ = SDL_GetKeyboardState(nullptr);

    // Install current_session so the theprefs macro resolves to this session's
    // prefs during screen construction (viewscreen ctors read theprefs).
    if (cfg_.install_legacy_globals) {
        current_session = this;
        primary_session.store(this, std::memory_order_release);
        og::gameplay::current_game = &gameplay_;
    }

    if (cfg_.allocate_screen) {
        screen_owner_ = std::make_unique<::screen>(cfg_.numviews, &world_, cfg_.create_display);
        myscreen_ = screen_owner_.get();
        gameplay_.sim_events = ctx_.sim_events.get();

        // Ensure this session's curpal_ matches the screen's palette.
        // video_init_palettes() populates video::ourpalette per-instance,
        // but set_palette() writes to current_session->curpal_ — which is
        // a different session when install_legacy_globals is false (e.g.
        // demo sub-sessions).  Copy the authoritative palette here so that
        // rendering with this session active uses correct colors.
        std::copy(screen_owner_->ourpalette.begin(),
                  screen_owner_->ourpalette.end(), curpal_);
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

void GameSession::sync_world_from_save(const SaveData& save_data, bool create_hit_effects)
{
    world_.my_team = save_data.my_team;
    world_.allied_mode = static_cast<unsigned char>(save_data.allied_mode);
    world_.current_scenario = save_data.scen_num;
    world_.withdraw_requested = false;
    world_.create_hit_effects = create_hit_effects;
    auto it = save_data.completed_levels.find(save_data.current_campaign);
    world_.completed_levels = (it != save_data.completed_levels.end()) ? it->second : std::set<int>{};
    std::copy(std::begin(save_data.m_score), std::end(save_data.m_score), std::begin(world_.m_score));
}

void GameSession::sync_save_from_world(SaveData& save_data)
{
    save_data.my_team = world_.my_team;
    save_data.allied_mode = static_cast<short>(world_.allied_mode);
    save_data.scen_num = world_.current_scenario;
    std::copy(std::begin(world_.m_score), std::end(world_.m_score), std::begin(save_data.m_score));
    save_data.completed_levels[save_data.current_campaign] = world_.completed_levels;
}

void GameSession::initialize_world_for_screen_boot(const SaveData& save_data, bool create_hit_effects)
{
    world_.control_hp = 0;
    world_.end = 0;
    world_.timer_wait = 6;
    world_.enemy_freeze = 0;
    world_.level_done = 0;
    world_.retry = false;
    sync_world_from_save(save_data, create_hit_effects);
}

void GameSession::prepare_world_for_battle(const SaveData& save_data, bool create_hit_effects)
{
    world_.end = 0;
    world_.retry = false;
    world_.enemy_freeze = 0;
    world_.control_hp = 0;
    sync_world_from_save(save_data, create_hit_effects);
}

void GameSession::reset_world_for_new_game(const SaveData& save_data, bool create_hit_effects)
{
    world_.end = 0;
    world_.clear();
    world_.enemy_freeze = 0;
    world_.control_hp = 0;
    sync_world_from_save(save_data, create_hit_effects);
    world_.end = 0;
}

::screen* GameSession::screen_ptr() const { return screen_owner_.get(); }
options* GameSession::prefs_ptr() const { return prefs_owner_.get(); }
const cfg_store& GameSession::config() const { return ::cfg; }

GameSession::~GameSession()
{
    if (cfg_.install_legacy_globals) {
        if (current_session == this) {
            current_session = prev_session_;
            og::gameplay::current_game = prev_session_ ? &prev_session_->gameplay_ : nullptr;
        }
    }

    screen_owner_.reset();
    prefs_owner_.reset();
    seeded_rng_.reset();

    if (session_surface_) {
        SDL_FreeSurface(session_surface_);
        session_surface_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// SessionScope: RAII activation of a session's globals
// ---------------------------------------------------------------------------

GameSession::SessionScope GameSession::activate()
{
    return SessionScope(*this);
}

GameSession::SessionScope::SessionScope(GameSession& session)
    : session_(&session)
{
    // Save current session
    saved_session_ = current_session;

    // Install this session as current.  The legacy macros (myscreen, theprefs)
    // dereference current_session, so this single pointer swap is sufficient.
    // ctx() also reads from current_session->ctx_, so no separate context
    // installation is needed.
    current_session = session_;
    og::gameplay::current_game = &session_->gameplay_;

    // Swap render surface if this session has its own.
    if (session_->session_surface_ && E_Screen) {
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
    og::gameplay::current_game = saved_session_ ? &saved_session_->gameplay_ : nullptr;
}

GameSession::SessionScope::SessionScope(SessionScope&& other) noexcept
    : session_(other.session_)
    , saved_session_(other.saved_session_)
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
    og::runtime::current_session->g_game_speed_factor_ =
        (factor < 0.0f) ? 0.0f : factor;
}
