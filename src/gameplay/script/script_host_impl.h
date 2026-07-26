/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Private to src/gameplay/script/. This is the only place outside the
// vendored sources that may include Lua headers (check_vendor_leaks.sh).
// Lua is compiled as C++ (errors are exceptions, RAII-safe), so the headers
// are included WITHOUT extern "C".

#include <openglad/gameplay/script/script_host.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

namespace og::script {

struct ScriptHost::Impl {
    // Allocation accounting for the deterministic memory cap. Counts bytes
    // requested from Lua, not allocator overhead, so the cap trips at the
    // same allocation on every platform.
    struct AllocState {
        std::size_t used = 0;
        std::size_t cap = 0;
    };

    lua_State* L = nullptr;
    ScriptLimits limits;
    AllocState alloc;
    std::vector<ScriptError> errors;
    std::vector<std::string> log_lines;

    // Registry ref of the shared read-only sandbox root table.
    int sandbox_root_ref = 0;
    // Registry ref of the env-key → environment cache table.
    int env_cache_ref = 0;

    // Per outermost host entry instruction budget bookkeeping.
    std::int64_t instructions_remaining = 0;
    int entry_depth = 0;

    ~Impl();

    // Push a fresh write-isolated environment table (reads fall through to
    // the sandbox root; getmetatable() is fenced) onto the stack.
    void push_new_environment();
    // Push the shared environment for env_key (created on first use), or a
    // fresh isolated environment when env_key is empty.
    void push_environment(std::string_view env_key);
    // Push the sandbox root table onto the stack.
    void push_sandbox_root();

    // Run the function at the top of the stack (pops it) with the budget
    // armed and a traceback message handler. Records errors. nresults values
    // remain on the stack on success.
    bool protected_call(const char* where, int nargs, int nresults);

    void record_error(const char* where, const char* message);
};

}  // namespace og::script
