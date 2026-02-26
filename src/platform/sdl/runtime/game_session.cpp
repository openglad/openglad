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
#include <openglad/interface/screen.h> // screen class
#include <openglad/interface/render/view.h>    // options class
#include <openglad/interface/render/sai2x.h>   // E_Screen
#include <openglad/platform/game_context.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/platform/input_hardware_state.h>
#include <openglad/platform/io_common.h>
#include "SDL.h"
#include <cassert>
#include <mutex>

// Defined in view.cpp — loads allkeys from defaults + keyprefs.dat.
void init_allkeys(int allkeys[][16]);

// This file owns current_session directly, so undef any transitional aliases to
// avoid accidental expansion inside this implementation unit.
#undef myscreen
#undef theprefs

namespace og::runtime {

thread_local GameSession* current_session = nullptr;
std::atomic<GameSession*> primary_session{nullptr};
std::atomic<std::uint64_t> GameSession::s_next_generation_{1};
std::mutex GameSession::s_live_sessions_mutex_;
std::unordered_map<const GameSession*, std::uint64_t> GameSession::s_live_sessions_;
std::atomic<std::uint64_t> primary_session_generation{0};

namespace {
std::mutex g_primary_session_mutex;
std::mutex g_render_surface_mutex;

std::uint64_t install_primary_session(GameSession* session)
{
    std::lock_guard<std::mutex> lock(g_primary_session_mutex);
    primary_session.store(session, std::memory_order_release);
    const std::uint64_t next_generation =
        primary_session_generation.load(std::memory_order_relaxed) + 1;
    primary_session_generation.store(next_generation, std::memory_order_release);
    return next_generation;
}

bool restore_primary_session_if_unchanged(GameSession* expected_session,
                                          std::uint64_t expected_generation,
                                          GameSession* restore_session)
{
    std::lock_guard<std::mutex> lock(g_primary_session_mutex);
    if (primary_session.load(std::memory_order_acquire) != expected_session)
        return false;
    if (primary_session_generation.load(std::memory_order_acquire) != expected_generation)
    {
        return false;
    }
    primary_session.store(restore_session, std::memory_order_release);
    primary_session_generation.store(expected_generation + 1, std::memory_order_release);
    return true;
}
} // namespace

void install_thread_session(GameSession* session)
{
    current_session = session;
    og::gameplay::current_game = session ? &session->gameplay_ : nullptr;
#ifndef NDEBUG
    if (session) {
        assert(og::gameplay::current_game == &session->gameplay_);
    } else {
        assert(og::gameplay::current_game == nullptr);
    }
#endif
}

GameSession::GameSession(const Config& session_cfg)
    : cfg_(session_cfg)
{
    generation_ = s_next_generation_.fetch_add(1, std::memory_order_relaxed);

    prev_session_ = current_session;
    prev_primary_session_ = primary_session.load(std::memory_order_acquire);
    prev_session_generation_ = prev_session_ ? prev_session_->generation_ : 0;

    // Allocate input hardware state.
    input_hw_ = std::make_unique<InputHardwareState>();

    // Allocate UI sub-objects (Phase 7).
    picker_ = std::make_unique<PickerState>();
    editor_ = std::make_unique<LevelEditorState>();

    if (cfg_.allocate_prefs) {
        prefs_owner_ = std::make_unique<options>();
        init_allkeys(allkeys_);
    }
    ctx_.mounted_campaign = og::resources::get_mounted_campaign();

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

    // Set session members before creating the screen; screen construction reads
    // session preferences during nested view initialization.
    theprefs_ = prefs_owner_.get();

    // Initialize legacy video pointer (VGA linear buffer address from DOS era).
    videoptr_ = reinterpret_cast<unsigned char*>(VIDEO_LINEAR);

    // Share SDL's keyboard state array. SDL_GetKeyboardState returns a pointer
    // to SDL's internal array — it's the same for all sessions.
    keystates_ = SDL_GetKeyboardState(nullptr);

    // Install current_session so nested constructors resolve this session's
    // preferences during screen boot.
    auto rollback_legacy_globals = [this]() noexcept {
        if (!cfg_.install_legacy_globals) {
            return;
        }
        restore_primary_session_if_unchanged(
            this, installed_primary_generation_, prev_primary_session_);
        if (current_session == this)
        {
            install_thread_session(prev_session_);
        }
    };
    if (cfg_.install_legacy_globals) {
        install_thread_session(this);
        installed_primary_generation_ = install_primary_session(this);
    }

    if (cfg_.allocate_screen) {
        try {
            auto screen_ptr = std::make_unique<::screen>(cfg_.numviews, &world_, cfg_.create_display);
            myscreen_ = screen_ptr.get();
            gameplay_.sim_events = ctx_.sim_events.get();

            // Ensure this session's curpal_ matches the screen's palette.
            // video_init_palettes() populates video::ourpalette per-instance,
            // but set_palette() writes to current_session->curpal_ — which is
            // a different session when install_legacy_globals is false (e.g.
            // demo sub-sessions).  Copy the authoritative palette here so that
            // rendering with this session active uses correct colors.
            std::copy(screen_ptr->ourpalette.begin(),
                      screen_ptr->ourpalette.end(), curpal_);
            screen_owner_ = std::move(screen_ptr);
        } catch (...) {
            rollback_legacy_globals();
            throw;
        }
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

    mark_session_live(this, generation_);
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
    if (myscreen_) {
        myscreen_->detach_world();
    }
    world_.end = 0;
    world_.clear();
    world_.enemy_freeze = 0;
    world_.control_hp = 0;
    sync_world_from_save(save_data, create_hit_effects);
    if (myscreen_) {
        myscreen_->attach_world(&world_);
    }
    world_.end = 0;
}

::screen* GameSession::screen_ptr() const { return screen_owner_.get(); }
options* GameSession::prefs_ptr() const { return prefs_owner_.get(); }
const cfg_store& GameSession::config() const { return ::cfg; }

GameSession::~GameSession()
{
    try {
        mark_session_dead(this);
    } catch (...) {
    }

    if (cfg_.install_legacy_globals) {
        try {
            restore_primary_session_if_unchanged(
                this, installed_primary_generation_, prev_primary_session_);
        } catch (...) {
        }
    }

    if (cfg_.install_legacy_globals) {
        if (current_session == this) {
            GameSession* restore_session = prev_session_;
            try {
                if (!is_session_live(restore_session, prev_session_generation_)) {
                    restore_session = nullptr;
                }
            } catch (...) {
                restore_session = nullptr;
            }
            install_thread_session(restore_session);
        }
    }

    screen_owner_.reset();
    prefs_owner_.reset();
    if (ctx_.rng == seeded_rng_.get()) {
        ctx_.rng = nullptr;
    }
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
    saved_session_generation_ = saved_session_ ? saved_session_->generation_ : 0;
    saved_primary_session_ = primary_session.load(std::memory_order_acquire);
    saved_primary_session_generation_ = saved_primary_session_ ? saved_primary_session_->generation_ : 0;

    // Install this session as current; legacy accessors follow current_session,
    // so this pointer swap is sufficient.
    // ctx() also reads from current_session->ctx_, so no separate context
    // installation is needed.
    install_thread_session(session_);
    installed_primary_generation_ = install_primary_session(session_);

    // Swap render surface if this session has its own.
    if (session_->session_surface_ && E_Screen) {
        render_swap_lock_ = std::unique_lock<std::mutex>(g_render_surface_mutex);
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
    GameSession* restore_primary = saved_primary_session_;
    if (!GameSession::is_session_live(restore_primary, saved_primary_session_generation_))
        restore_primary = nullptr;

    restore_primary_session_if_unchanged(
        session_, installed_primary_generation_, restore_primary);

    GameSession* restore_session = saved_session_;
    if (!GameSession::is_session_live(restore_session, saved_session_generation_))
        restore_session = nullptr;
    install_thread_session(restore_session);
}

GameSession::SessionScope::SessionScope(SessionScope&& other) noexcept
    : session_(other.session_)
    , saved_session_(other.saved_session_)
    , saved_primary_session_(other.saved_primary_session_)
    , saved_session_generation_(other.saved_session_generation_)
    , saved_primary_session_generation_(other.saved_primary_session_generation_)
    , installed_primary_generation_(other.installed_primary_generation_)
    , saved_render_surface_(other.saved_render_surface_)
    , did_swap_render_(other.did_swap_render_)
    , render_swap_lock_(std::move(other.render_swap_lock_))
{
    other.session_ = nullptr;
}

bool GameSession::is_session_live(const GameSession* session, std::uint64_t generation)
{
    if (!session)
        return true;
    std::lock_guard<std::mutex> lock(s_live_sessions_mutex_);
    const auto it = s_live_sessions_.find(session);
    return it != s_live_sessions_.end() && it->second == generation;
}

void GameSession::mark_session_live(const GameSession* session, std::uint64_t generation)
{
    std::lock_guard<std::mutex> lock(s_live_sessions_mutex_);
    s_live_sessions_[session] = generation;
}

void GameSession::mark_session_dead(const GameSession* session)
{
    std::lock_guard<std::mutex> lock(s_live_sessions_mutex_);
    s_live_sessions_.erase(session);
}

} // namespace og::runtime

// set_game_speed() — moved from core/util.cpp (Batch 7).
// Defined outside the namespace because base.h declares it at global scope.
void set_game_speed(float factor)
{
    og::runtime::current_session->g_game_speed_factor_ =
        (factor < 0.0f) ? 0.0f : factor;
}
