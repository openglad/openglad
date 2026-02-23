/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/runtime/game_session.h>

#include <openglad/legacy/base.h> // legacy globals: myscreen
#include <openglad/data/gparser.h> // cfg legacy global
#include <openglad/runtime/screen.h>
#include <openglad/render/view.h> // options + theprefs legacy global
#include <openglad/runtime/game_context.h>

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
        screen_owner_ = std::make_unique<::screen>(cfg_.numviews);
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
}

} // namespace og::runtime
