/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "script_raise.h"

#include <lua.h>
#include <lauxlib.h>

#include <cstdarg>
#include <cstdlib>

namespace og::script {

void script_raise(lua_State* L, const char* fmt, ...)
{
    // luaL_error's body (lauxlib.c), statement for statement.
    va_list argp;
    va_start(argp, fmt);
    luaL_where(L, 1);
    lua_pushvfstring(L, fmt, argp);
    va_end(argp);
    lua_concat(L, 2);
    lua_error(L);
    // lua_error never returns (it unwinds to the protected call), but the C
    // API does not declare it noreturn.
    std::abort();
}

}  // namespace og::script
