#include <openglad/runtime/game_session.h>

#include <openglad/legacy/base.h> // legacy globals: myscreen
#include <openglad/data/gparser.h> // cfg legacy global
#include <openglad/runtime/screen.h>
#include <openglad/render/view.h> // options + theprefs legacy global

extern options* theprefs;

namespace og::runtime {

GameSession::GameSession(const Config& session_cfg)
    : cfg_(session_cfg)
{
    prev_myscreen_ = myscreen;
    prev_theprefs_ = theprefs;

    if (cfg_.allocate_prefs) {
        ctx_.prefs = std::make_unique<options>();
    }
    ctx_.config = &::cfg;

    if (cfg_.allocate_seeded_rng) {
        seeded_rng_ = std::make_unique<SeededRandom>(cfg_.rng_seed);
        ctx_.rng = seeded_rng_.get();
    }
    if (!ctx_.rng) {
        ctx_.rng = &production_rng_;
    }

    if (cfg_.allocate_screen) {
        screen_owner_ = std::make_unique<::screen>(cfg_.numviews);
        ctx_.game_screen = screen_owner_.get();
    } else {
        ctx_.game_screen = nullptr;
    }

    if (cfg_.install_global_context) {
        // Note: GameContext only supports overriding the active pointer, not querying
        // the previous one. Nested sessions should be avoided until that is added.
        set_global_context(&ctx_);
    }

    if (cfg_.install_legacy_globals) {
        myscreen = ctx_.game_screen;
        theprefs = ctx_.prefs.get();
    }
}

GameSession::~GameSession()
{
    if (cfg_.install_legacy_globals) {
        if (myscreen == ctx_.game_screen)
            myscreen = prev_myscreen_;
        if (theprefs == ctx_.prefs.get())
            theprefs = prev_theprefs_;
    }

    if (cfg_.install_global_context) {
        // Reset to default global context. (Nested-session restoration is TODO.)
        set_global_context(nullptr);
    }

    ctx_.game_screen = nullptr;
    screen_owner_.reset();
    ctx_.prefs.reset();
    seeded_rng_.reset();
}

} // namespace og::runtime
