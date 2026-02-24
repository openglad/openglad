/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/runtime/game_session.h>

#include <openglad/core/util.h> // LogError
#include <openglad/legacy/base.h> // legacy globals: myscreen
#include <openglad/data/gparser.h> // cfg legacy global
#include <openglad/runtime/screen.h>
#include <openglad/render/view.h> // options + theprefs legacy global
#include <openglad/render/sai2x.h> // E_Screen
#include <openglad/runtime/game_context.h>
#include "SDL.h"

extern options* theprefs;

namespace og::runtime {

GameSession::GameSession(const Config& session_cfg)
    : cfg_(session_cfg)
{
    // Preserve mounted-campaign state that lives on the context.  This is
    // populated before sessions are created (io_init) and must not be lost
    // when we install a session-specific context.
    GameContext& prev_ctx = ::ctx();

    prev_myscreen_ = myscreen;
    prev_theprefs_ = theprefs;

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

    // Install legacy globals BEFORE creating the screen, because the screen
    // constructor creates viewscreens whose constructors read theprefs.
    if (cfg_.install_legacy_globals && prefs_owner_) {
        theprefs = prefs_owner_.get();
    }

    if (cfg_.allocate_screen) {
        screen_owner_ = std::make_unique<::screen>(cfg_.numviews, cfg_.create_display);
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

    if (cfg_.install_legacy_globals) {
        myscreen = screen_owner_.get();
    }
}

::screen* GameSession::screen_ptr() const { return screen_owner_.get(); }
options* GameSession::prefs_ptr() const { return prefs_owner_.get(); }

GameSession::~GameSession()
{
    if (cfg_.install_legacy_globals) {
        if (myscreen == screen_owner_.get())
            myscreen = prev_myscreen_;
        if (prefs_owner_ && theprefs == prefs_owner_.get())
            theprefs = prev_theprefs_;
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
    // Save current globals
    saved_myscreen_ = myscreen;
    saved_theprefs_ = theprefs;
    saved_context_ = &::ctx();

    // Install this session's globals.
    // Set both myscreen and theprefs unconditionally to keep the
    // save/set/restore cycle symmetric. If the session doesn't own prefs,
    // theprefs is left as the session's prefs_ptr (nullptr), ensuring the
    // destructor's unconditional restore is always correct.
    myscreen = session_->screen_owner_.get();
    theprefs = session_->prefs_owner_ ? session_->prefs_owner_.get() : saved_theprefs_;
    set_global_context(&session_->ctx_);

    // Swap render surface if this session has its own.
    // Save E_Screen->render even if it is nullptr so the destructor can
    // restore it correctly.  We use a separate flag to track whether a
    // swap was performed rather than relying on saved_render_surface_
    // being non-null.
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

    // Restore previous globals
    myscreen = saved_myscreen_;
    theprefs = saved_theprefs_;
    if (saved_context_) {
        set_global_context(saved_context_);
    } else {
        set_global_context(nullptr);
    }
}

GameSession::SessionScope::SessionScope(SessionScope&& other) noexcept
    : session_(other.session_)
    , saved_myscreen_(other.saved_myscreen_)
    , saved_theprefs_(other.saved_theprefs_)
    , saved_context_(other.saved_context_)
    , saved_render_surface_(other.saved_render_surface_)
    , did_swap_render_(other.did_swap_render_)
{
    other.session_ = nullptr;
}

} // namespace og::runtime
