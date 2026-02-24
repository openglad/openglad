/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/runtime/game_session.h>

#include <openglad/core/util.h> // LogError
#include <openglad/entities/guy.h> // complete type for unique_ptr<guy> destructor
#include <openglad/runtime/screen.h> // screen class (pulls in base.h → myscreen macro)
#include <openglad/render/view.h>    // options class (defines theprefs macro)
#include <openglad/render/sai2x.h>   // E_Screen
#include <openglad/runtime/game_context.h>
#include "SDL.h"

// The legacy global macros (myscreen, theprefs) expand through current_session.
// This file manages current_session itself, so we #undef the macros to avoid
// accidental expansion in the implementation below.
#undef myscreen
#undef theprefs

namespace og::runtime {

GameSession* current_session = nullptr;

GameSession::GameSession(const Config& session_cfg)
    : cfg_(session_cfg)
{
    // Preserve mounted-campaign state that lives on the context.  This is
    // populated before sessions are created (io_init) and must not be lost
    // when we install a session-specific context.
    GameContext& prev_ctx = ::ctx();

    prev_session_ = current_session;

    if (cfg_.allocate_prefs) {
        prefs_owner_ = std::make_unique<options>();
    }
    ctx_.mounted_campaign = prev_ctx.mounted_campaign;

    if (cfg_.allocate_seeded_rng) {
        seeded_rng_ = std::make_unique<SeededRandom>(cfg_.rng_seed);
        ctx_.rng = seeded_rng_.get();
    }
    if (!ctx_.rng) {
        ctx_.rng = &production_rng_;
    }

    if (cfg_.install_global_context) {
        set_global_context(&ctx_);
    }

    // Set session members before creating the screen, because the screen
    // constructor creates viewscreens whose constructors read theprefs (macro).
    theprefs_ = prefs_owner_.get();

    // Initialize legacy video pointer (VGA linear buffer address from DOS era).
    videoptr_ = reinterpret_cast<unsigned char*>(VIDEO_LINEAR);

    // Install current_session so the theprefs macro resolves to this session's
    // prefs during screen construction (viewscreen ctors read theprefs).
    if (cfg_.install_legacy_globals) {
        current_session = this;
    }

    if (cfg_.allocate_screen) {
        screen_owner_ = std::make_unique<::screen>(cfg_.numviews, cfg_.create_display);
        myscreen_ = screen_owner_.get();
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

GameSession::~GameSession()
{
    if (cfg_.install_legacy_globals) {
        if (current_session == this)
            current_session = prev_session_;
    }

    if (cfg_.install_global_context) {
        set_global_context(nullptr);
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
    saved_context_ = &::ctx();

    // Install this session as current.  The legacy macros (myscreen, theprefs)
    // dereference current_session, so this single pointer swap is sufficient.
    current_session = session_;
    set_global_context(&session_->ctx_);

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

    // Restore previous session.
    current_session = saved_session_;
    if (saved_context_) {
        set_global_context(saved_context_);
    } else {
        set_global_context(nullptr);
    }
}

GameSession::SessionScope::SessionScope(SessionScope&& other) noexcept
    : session_(other.session_)
    , saved_session_(other.saved_session_)
    , saved_context_(other.saved_context_)
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
