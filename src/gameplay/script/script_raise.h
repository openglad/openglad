/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Raising a Lua error from a C function, private to src/gameplay/script/.

struct lua_State;

namespace og::script {

// Raise a Lua error whose message is `fmt` formatted by lua_pushvfstring and
// prefixed with the calling Lua position — exactly what luaL_error produces,
// byte for byte, because the body is luaL_error's own. This function is a C++
// frame, not a Lua one, so luaL_where's level 1 still names the Lua function
// that called the binding.
//
// The difference is the declaration: luaL_error is declared to return int, so
// every `return luaL_error(...)` compiled to a return path the error never
// takes (an uncovered line the coverage gate counted against us), and every
// bare call needed a dead `return x; // unreachable` after it. [[noreturn]]
// lets the compiler know the call is the end of the path.
//
// scripts/check_lua_error_sites.sh fails the build on a luaL_error call
// anywhere in src/ or include/ outside script_raise.cpp.
[[noreturn]] void script_raise(lua_State* L, const char* fmt, ...);

}  // namespace og::script
