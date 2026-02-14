#include "runtime/screen_lifecycle.h"

#include <openglad/runtime/game_session.h>
#include <openglad/runtime/game_context.h>

#include "base.h" // myscreen legacy global
#include "runtime/screen.h"

namespace
{
std::unique_ptr<og::runtime::GameSession> g_session_owner;
}

std::unique_ptr<og::runtime::GameSession>& global_session_owner()
{
    return g_session_owner;
}

og::runtime::GameSession* create_global_session(short numviews)
{
    og::runtime::GameSession::Config session_cfg;
    session_cfg.numviews = numviews;
    session_cfg.install_legacy_globals = true;
    session_cfg.install_global_context = true;

    g_session_owner = std::make_unique<og::runtime::GameSession>(session_cfg);
    return g_session_owner.get();
}

void destroy_global_session()
{
    g_session_owner.reset();
}

screen* create_global_screen(short numviews)
{
    og::runtime::GameSession* s = create_global_session(numviews);
    return s ? s->screen_ptr() : nullptr;
}

void destroy_global_screen()
{
    destroy_global_session();
}
