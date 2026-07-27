/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "script_host_impl.h"

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/script/script_coverage.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace og::script {

namespace {

// ---------------------------------------------------------------------------
// Memory cap allocator
// ---------------------------------------------------------------------------

void* capped_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    auto* st = static_cast<ScriptHost::Impl::AllocState*>(ud);
    // Per the Lua contract: when ptr is null, osize encodes an object type
    // tag, not a size — the old size is zero.
    const size_t old_size = (ptr != nullptr) ? osize : 0;

    if (nsize == 0) {
        st->used -= old_size;
        std::free(ptr);
        return nullptr;
    }
    if (nsize > old_size && st->used + (nsize - old_size) > st->cap)
        return nullptr;  // Lua raises a deterministic "not enough memory"
    void* np = std::realloc(ptr, nsize);
    if (np != nullptr)
        st->used = st->used - old_size + nsize;
    return np;
}

// ---------------------------------------------------------------------------
// Instruction budget
// ---------------------------------------------------------------------------

inline constexpr int kBudgetCheckInterval = 4096;

// The Impl pointer rides in the lua extra space so the count hook can find
// its budget without touching the registry.
ScriptHost::Impl** extra_slot(lua_State* L)
{
    return static_cast<ScriptHost::Impl**>(lua_getextraspace(L));
}

void budget_hook(lua_State* L, lua_Debug* /*ar*/)
{
    ScriptHost::Impl* impl = *extra_slot(L);
    impl->instructions_remaining -= kBudgetCheckInterval;
    if (impl->instructions_remaining <= 0)
        luaL_error(L, "instruction budget exceeded");
}

// Coverage variant: the SAME budget hook with LUA_MASKLINE folded in, so
// line recording reuses the mechanism instead of adding a second one. Only
// installed when og::script::coverage::enabled(); a coverage-less run gets
// budget_hook/LUA_MASKCOUNT verbatim (see protected_call below), which is
// what keeps the parity gate byte-exact.
void budget_and_coverage_hook(lua_State* L, lua_Debug* ar)
{
    if (ar->event == LUA_HOOKLINE) {
        // One call records BOTH metrics for the currently executing
        // function off this one event, which is what puts them on one grid:
        // the FUNCTION is recorded as having run because a line of its own
        // body just ran, so a hook the engine DISPATCHED but never entered
        // cannot read as covered the way it would if the hit were stamped
        // at the call site. (A dispatched empty body does count — see
        // script_coverage.h.) The hit is credited to the generation whose
        // compiled prototype is executing — resolved through the registry
        // bind_compiled_chunk maintains, never a chunk-level "most recently
        // declared" digest, which mis-credited stale closures after a
        // re-declaration.
        coverage::record_hook_line(L, ar);
        return;
    }
    budget_hook(L, ar);
}

// ---------------------------------------------------------------------------
// og.* deterministic arithmetic (cookbook R1-R3; see design doc)
// ---------------------------------------------------------------------------

int og_div(lua_State* L)
{
    const lua_Integer a = luaL_checkinteger(L, 1);
    const lua_Integer b = luaL_checkinteger(L, 2);
    if (b == 0)
        return luaL_error(L, "og.div: division by zero");
    if (a == std::numeric_limits<lua_Integer>::min() && b == -1)
        return luaL_error(L, "og.div: overflow");
    lua_pushinteger(L, a / b);  // C truncation toward zero
    return 1;
}

int og_mod(lua_State* L)
{
    const lua_Integer a = luaL_checkinteger(L, 1);
    const lua_Integer b = luaL_checkinteger(L, 2);
    if (b == 0)
        return luaL_error(L, "og.mod: division by zero");
    if (a == std::numeric_limits<lua_Integer>::min() && b == -1)
        return luaL_error(L, "og.mod: overflow");
    lua_pushinteger(L, a % b);  // C remainder (sign of dividend)
    return 1;
}

// Each og.f* performs exactly one operation in float precision so that
// transliterated float expressions keep the C++ per-op rounding.
int og_fadd(lua_State* L)
{
    const float a = static_cast<float>(luaL_checknumber(L, 1));
    const float b = static_cast<float>(luaL_checknumber(L, 2));
    lua_pushnumber(L, static_cast<lua_Number>(a + b));
    return 1;
}

int og_fsub(lua_State* L)
{
    const float a = static_cast<float>(luaL_checknumber(L, 1));
    const float b = static_cast<float>(luaL_checknumber(L, 2));
    lua_pushnumber(L, static_cast<lua_Number>(a - b));
    return 1;
}

int og_fmul(lua_State* L)
{
    const float a = static_cast<float>(luaL_checknumber(L, 1));
    const float b = static_cast<float>(luaL_checknumber(L, 2));
    lua_pushnumber(L, static_cast<lua_Number>(a * b));
    return 1;
}

int og_fdiv(lua_State* L)
{
    const float a = static_cast<float>(luaL_checknumber(L, 1));
    const float b = static_cast<float>(luaL_checknumber(L, 2));
    lua_pushnumber(L, static_cast<lua_Number>(a / b));
    return 1;
}

// Narrowing helpers reproducing C++ integer truncation (modular, C++20).
template <typename T>
int og_narrow(lua_State* L)
{
    const lua_Integer v = luaL_checkinteger(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(
                           static_cast<T>(static_cast<std::uint64_t>(v))));
    return 1;
}

int og_trunc(lua_State* L)
{
    const lua_Number x = luaL_checknumber(L, 1);
    // Reject values whose truncation cannot be represented (and NaN).
    if (!(std::fabs(x) < 9223372036854775808.0))
        return luaL_error(L, "og.trunc: value out of integer range");
    lua_pushinteger(L, static_cast<lua_Integer>(std::trunc(x)));
    return 1;
}

// Address-free tostring: identical output on every peer and every run.
// Values without a __tostring metamethod stringify as their type name alone
// (never "table: 0x..." — pointer text is nondeterministic under ASLR).
void push_det_tostring(lua_State* L, int idx)
{
    idx = lua_absindex(L, idx);
    if (luaL_callmeta(L, idx, "__tostring")) {
        if (!lua_isstring(L, -1))
            luaL_error(L, "'__tostring' must return a string");
        return;
    }
    switch (lua_type(L, idx)) {
        case LUA_TNIL:
        case LUA_TBOOLEAN:
        case LUA_TNUMBER:
        case LUA_TSTRING:
            luaL_tolstring(L, idx, nullptr);
            break;
        default:
            lua_pushstring(L, luaL_typename(L, idx));
            break;
    }
}

int og_tostring(lua_State* L)
{
    luaL_checkany(L, 1);
    push_det_tostring(L, 1);
    return 1;
}

int og_log(lua_State* L)
{
    ScriptHost::Impl* impl = *extra_slot(L);
    const int n = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= n; i++) {
        push_det_tostring(L, i);
        size_t len = 0;
        const char* s = lua_tolstring(L, -1, &len);
        if (i > 1)
            line.push_back('\t');
        line.append(s, len);
        lua_pop(L, 1);
    }
    TRACE("script", "%s", line.c_str());
    // The trace above is the unconditional live signal; the stored transcript
    // is a bounded tail. A pack logging every tick would otherwise grow this
    // for the life of the GameWorld (the same defect the error store had).
    // Oldest-first eviction keeps log().back() meaning "most recent", which is
    // what callers actually read.
    impl->log_lines.push_back(std::move(line));
    if (impl->log_lines.size() > kMaxStoredScriptLogLines) {
        impl->log_lines.erase(impl->log_lines.begin());
        impl->dropped_log_lines++;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Sandbox construction
// ---------------------------------------------------------------------------

// Base-library functions copied into the sandbox root. Deliberately absent:
// load/loadstring/loadfile/dofile (no dynamic code), collectgarbage (GC
// visibility), require (no package system), print (replaced by og.log alias),
// and pairs/next — hash-part iteration order depends on a per-run seed (Lua
// 5.4 mixes heap addresses into it), so sim scripts iterate arrays only.
// tostring is replaced by an address-free variant below.
constexpr const char* kAllowedBase[] = {
    "assert", "error",    "ipairs",   "pcall",
    "select", "tonumber", "type",     "xpcall", "rawget",
    "rawset", "rawequal", "rawlen",   "setmetatable", "getmetatable",
    "_VERSION",
};

// math members kept: exact/integer-safe only. Transcendentals and random are
// out (libm varies across platforms; RNG must go through the sim).
constexpr const char* kAllowedMath[] = {
    "floor", "ceil", "abs", "min", "max", "tointeger",
    "type",  "maxinteger", "mininteger", "huge", "pi", "ult",
};

void copy_allowed(lua_State* L, int src_idx, int dst_idx,
                  const char* const* names, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        lua_getfield(L, src_idx, names[i]);
        lua_setfield(L, dst_idx, names[i]);
    }
}

const luaL_Reg kOgFuncs[] = {
    {"div", og_div},
    {"mod", og_mod},
    {"fadd", og_fadd},
    {"fsub", og_fsub},
    {"fmul", og_fmul},
    {"fdiv", og_fdiv},
    {"i8", og_narrow<std::int8_t>},
    {"i16", og_narrow<std::int16_t>},
    {"i32", og_narrow<std::int32_t>},
    {"u8", og_narrow<std::uint8_t>},
    {"trunc", og_trunc},
    {"log", og_log},
    {nullptr, nullptr},
};

// Builds the shared sandbox root table and leaves it on the stack.
void build_sandbox_root(lua_State* L)
{
    lua_newtable(L);  // root
    const int root = lua_gettop(L);

    // Base library: open into the globals table, whitelist-copy, then leave
    // the globals table behind (chunks never see it; they get custom _ENV).
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    copy_allowed(L, -1, root, kAllowedBase,
                 sizeof(kAllowedBase) / sizeof(kAllowedBase[0]));
    lua_pop(L, 1);

    // string: shared table, with the bytecode escape hatch removed. The
    // string metatable __index references this same table, so stripping in
    // place also strips method-call access.
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 0);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_setfield(L, root, LUA_STRLIBNAME);

    // table: whole library.
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 0);
    lua_setfield(L, root, LUA_TABLIBNAME);

    // math: whitelist only.
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 0);
    lua_newtable(L);
    copy_allowed(L, -2, lua_gettop(L), kAllowedMath,
                 sizeof(kAllowedMath) / sizeof(kAllowedMath[0]));
    lua_setfield(L, root, LUA_MATHLIBNAME);
    lua_pop(L, 1);  // original math table

    // og namespace.
    luaL_newlib(L, kOgFuncs);
    lua_setfield(L, root, "og");

    // print is og.log under another name (scripts expect print to exist),
    // and tostring is the address-free variant.
    lua_pushcfunction(L, og_log);
    lua_setfield(L, root, "print");
    lua_pushcfunction(L, og_tostring);
    lua_setfield(L, root, "tostring");

    // _G inside the sandbox refers to the root itself (read-only by virtue
    // of the environment fence; direct writes only affect a chunk's env).
    lua_pushvalue(L, root);
    lua_setfield(L, root, "_G");
}

int traceback_handler(lua_State* L)
{
    const char* msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg != nullptr ? msg : "(non-string error)", 1);
    return 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl methods
// ---------------------------------------------------------------------------

ScriptHost::Impl::~Impl()
{
    if (L != nullptr)
        lua_close(L);
}

void ScriptHost::Impl::push_sandbox_root()
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, sandbox_root_ref);
}

void ScriptHost::Impl::push_new_environment()
{
    lua_newtable(L);                    // env
    lua_newtable(L);                    // mt
    push_sandbox_root();
    lua_setfield(L, -2, "__index");     // mt.__index = root
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable"); // fence: getmetatable(env) == false
    lua_setmetatable(L, -2);
}

void ScriptHost::Impl::push_environment(std::string_view env_key)
{
    if (env_key.empty()) {
        push_new_environment();
        return;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, env_cache_ref);
    lua_pushlstring(L, env_key.data(), env_key.size());
    if (lua_rawget(L, -2) == LUA_TTABLE) {
        lua_remove(L, -2);  // cache table
        return;
    }
    lua_pop(L, 1);  // non-table placeholder
    push_new_environment();
    lua_pushlstring(L, env_key.data(), env_key.size());
    lua_pushvalue(L, -2);
    lua_rawset(L, -4);  // cache[env_key] = env
    lua_remove(L, -2);  // cache table
}

void ScriptHost::Impl::record_error(const char* where, const char* message)
{
    const char* w = (where != nullptr) ? where : "?";
    const char* m = (message != nullptr) ? message : "(unknown error)";

    // The trace is the live signal and fires on every occurrence; only the
    // stored vector is bounded.
    TRACE("script_error", "%s: %s", w, m);

    // Collapse repeats. A hook that errors every tick (a cloud effect's
    // on_act, a flag's touch handler) would otherwise append a fresh
    // traceback string per tick for the whole life of the world.
    for (ScriptError& e : errors) {
        if (e.where == w && e.message == m) {
            e.count++;
            return;
        }
    }
    if (errors.size() >= kMaxStoredScriptErrors) {
        dropped_errors++;
        return;
    }
    errors.push_back({w, m, 1});
}

bool ScriptHost::Impl::protected_call(const char* where, int nargs,
                                      int nresults)
{
    // Stack on entry: ... func arg1..argN
    const int handler_pos = lua_gettop(L) - nargs;  // slot under func
    lua_pushcfunction(L, traceback_handler);
    lua_insert(L, handler_pos);

    const bool outermost = (entry_depth == 0);
    if (outermost) {
        instructions_remaining = limits.instructions_per_call;
        // Disabled path is bit-identical to the pre-coverage code: same hook
        // function, same mask, same count. The recorder costs one load of a
        // global bool per host entry — not per instruction, not per line.
        if (coverage::enabled())
            lua_sethook(L, budget_and_coverage_hook,
                        LUA_MASKCOUNT | LUA_MASKLINE, kBudgetCheckInterval);
        else
            lua_sethook(L, budget_hook, LUA_MASKCOUNT, kBudgetCheckInterval);
    }
    entry_depth++;
    const int rc = lua_pcall(L, nargs, nresults, handler_pos);
    entry_depth--;
    if (outermost)
        lua_sethook(L, nullptr, 0, 0);

    if (rc != LUA_OK) {
        record_error(where, lua_tostring(L, -1));
        lua_pop(L, 1);              // error object
        lua_remove(L, handler_pos); // traceback handler
        return false;
    }
    lua_remove(L, handler_pos);     // results shift down into place
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ScriptHost::ScriptHost(ScriptLimits limits) : impl_(std::make_unique<Impl>())
{
    impl_->limits = limits;
    impl_->alloc.cap = limits.memory_bytes;
    impl_->L = lua_newstate(capped_alloc, &impl_->alloc);
    if (impl_->L == nullptr)
        throw std::bad_alloc();
    *extra_slot(impl_->L) = impl_.get();
    build_sandbox_root(impl_->L);
    impl_->sandbox_root_ref = luaL_ref(impl_->L, LUA_REGISTRYINDEX);
    lua_newtable(impl_->L);
    impl_->env_cache_ref = luaL_ref(impl_->L, LUA_REGISTRYINDEX);
}

ScriptHost::~ScriptHost() = default;

ScriptHost::Impl& ScriptHost::impl()
{
    return *impl_;
}

bool ScriptHost::run_chunk(std::string_view chunk_name,
                           std::string_view source,
                           std::string_view env_key)
{
    lua_State* L = impl_->L;
    const std::string name(chunk_name);
    if (luaL_loadbuffer(L, source.data(), source.size(), name.c_str()) !=
        LUA_OK) {
        impl_->record_error(name.c_str(), lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    // The freshly compiled closure is on the stack: bind its prototype tree
    // to THIS generation of the chunk, so every future hit inside it credits
    // the bytes that are actually executing (see script_coverage.h). One
    // bool load when the recorder is off.
    if (coverage::enabled())
        coverage::bind_compiled_chunk(L, -1, chunk_name, source);
    impl_->push_environment(env_key);
    lua_setupvalue(L, -2, 1);  // set the chunk's _ENV
    return impl_->protected_call(name.c_str(), 0, 0);
}

namespace {

// Shared expression-eval plumbing: leaves one result on the stack on success.
bool eval_push(ScriptHost::Impl* impl, std::string_view expr)
{
    lua_State* L = impl->L;
    std::string src = "return (";
    src.append(expr);
    src.push_back(')');
    if (luaL_loadbuffer(L, src.data(), src.size(), "=eval") != LUA_OK) {
        impl->record_error("eval", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    // Eval chunks are never declared, so this binds nothing — but it SCRUBS
    // stale registry entries whose Proto addresses this compile may have
    // reused, which the generation binding's soundness argument relies on
    // (see bind_compiled_chunk in script_coverage.h). Both ScriptHost
    // compile sites must pass through it.
    if (coverage::enabled())
        coverage::bind_compiled_chunk(L, -1, "=eval", src);
    impl->push_new_environment();
    lua_setupvalue(L, -2, 1);
    return impl->protected_call("eval", 0, 1);
}

}  // namespace

std::optional<std::int64_t> ScriptHost::eval_integer(std::string_view expr)
{
    lua_State* L = impl_->L;
    if (!eval_push(impl_.get(), expr))
        return std::nullopt;
    std::optional<std::int64_t> out;
    if (lua_isinteger(L, -1))
        out = static_cast<std::int64_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return out;
}

std::optional<double> ScriptHost::eval_number(std::string_view expr)
{
    lua_State* L = impl_->L;
    if (!eval_push(impl_.get(), expr))
        return std::nullopt;
    std::optional<double> out;
    if (lua_isnumber(L, -1))
        out = static_cast<double>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    return out;
}

std::optional<bool> ScriptHost::eval_boolean(std::string_view expr)
{
    lua_State* L = impl_->L;
    if (!eval_push(impl_.get(), expr))
        return std::nullopt;
    const bool out = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return out;
}

std::optional<std::string> ScriptHost::eval_string(std::string_view expr)
{
    lua_State* L = impl_->L;
    if (!eval_push(impl_.get(), expr))
        return std::nullopt;
    std::optional<std::string> out;
    if (lua_type(L, -1) == LUA_TSTRING) {
        size_t len = 0;
        const char* s = lua_tolstring(L, -1, &len);
        out = std::string(s, len);
    }
    lua_pop(L, 1);
    return out;
}

bool ScriptHost::sandbox_has(std::string_view dotted_path)
{
    lua_State* L = impl_->L;
    impl_->push_sandbox_root();
    size_t start = 0;
    while (start <= dotted_path.size()) {
        const size_t dot = dotted_path.find('.', start);
        const std::string part(dotted_path.substr(
            start, dot == std::string_view::npos ? std::string_view::npos
                                                 : dot - start));
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            return false;
        }
        lua_getfield(L, -1, part.c_str());
        lua_remove(L, -2);
        if (dot == std::string_view::npos)
            break;
        start = dot + 1;
    }
    const bool present = !lua_isnil(L, -1);
    lua_pop(L, 1);
    return present;
}

const std::vector<ScriptError>& ScriptHost::errors() const
{
    return impl_->errors;
}

std::uint64_t ScriptHost::dropped_error_count() const
{
    return impl_->dropped_errors;
}

void ScriptHost::clear_errors()
{
    impl_->errors.clear();
    impl_->dropped_errors = 0;
}

std::size_t ScriptHost::memory_used() const
{
    return impl_->alloc.used;
}

std::size_t ScriptHost::memory_limit() const
{
    return impl_->alloc.cap;
}

const std::vector<std::string>& ScriptHost::log() const
{
    return impl_->log_lines;
}

std::uint64_t ScriptHost::dropped_log_line_count() const
{
    return impl_->dropped_log_lines;
}

}  // namespace og::script
