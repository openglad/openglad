/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/platform/screen_lifecycle.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/game_context.h>

#include <openglad/legacy/base.h> // myscreen legacy global
#include <openglad/interface/screen.h>
namespace
{
std::unique_ptr<og::runtime::GameSession>& session_owner_storage()
{
    static std::unique_ptr<og::runtime::GameSession> owner;
    return owner;
}
}

std::unique_ptr<og::runtime::GameSession>& global_session_owner()
{
    return session_owner_storage();
}

og::runtime::GameSession* create_global_session(short numviews)
{
    og::runtime::GameSession::Config session_cfg;
    session_cfg.numviews = numviews;
    session_cfg.install_legacy_globals = true;

    auto& owner = session_owner_storage();
    owner = std::make_unique<og::runtime::GameSession>(session_cfg);
    return owner.get();
}

void destroy_global_session()
{
    session_owner_storage().reset();
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
