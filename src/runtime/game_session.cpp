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
    // Preserve any existing global-context state that lives outside the legacy
    // globals (e.g. IO mounts tracked on the context). The project currently
    // uses ctx().mounted_campaign as a source of truth for the "active" campaign.
    // This is populated before sessions are created (io_init) and must not be
    // lost when we install a session-specific context.
    GameContext& prev_ctx = ::ctx();

    prev_myscreen_ = myscreen;
    prev_theprefs_ = theprefs;

    if (cfg_.allocate_prefs) {
        ctx_.prefs = std::make_unique<options>();
    }
    ctx_.config = &::cfg;
    ctx_.mounted_campaign = prev_ctx.mounted_campaign;

    if (cfg_.allocate_seeded_rng) {
        seeded_rng_ = std::make_unique<SeededRandom>(cfg_.rng_seed);
        ctx_.rng = seeded_rng_.get();
    }
    if (!ctx_.rng) {
        ctx_.rng = &production_rng_;
    }

    // Install context and legacy globals BEFORE creating the screen, because
    // the screen constructor creates viewscreens whose constructors call
    // active_prefs() which needs theprefs / ctx_ to be reachable.
    if (cfg_.install_global_context) {
        // Note: GameContext only supports overriding the active pointer, not querying
        // the previous one. Nested sessions should be avoided until that is added.
        set_global_context(&ctx_);
    }

    if (cfg_.install_legacy_globals) {
        theprefs = ctx_.prefs.get();
    }

    if (cfg_.allocate_screen) {
        screen_owner_ = std::make_unique<::screen>(cfg_.numviews);
        ctx_.game_screen = screen_owner_.get();
    } else {
        ctx_.game_screen = nullptr;
    }

    if (cfg_.install_legacy_globals) {
        myscreen = ctx_.game_screen;
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
