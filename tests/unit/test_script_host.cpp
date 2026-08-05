/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_coverage.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>

// The vendored Lua C API, for the P8-A tests alone: they need a REAL
// stripped-bytecode attack artifact, and string.dump is sandbox-stripped, so
// the artifact has to be forged here through lua_dump. tests/ sits outside
// the src/ vendor-leak boundary (scripts/check_vendor_leaks.sh restricts Lua
// headers to src/gameplay/script/); og_unit_script links og_lua for the
// include path.
#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>  // mkdtemp lives here on macOS (stdlib.h on glibc)
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

using og::script::kMaxStoredScriptErrors;
using og::script::kMaxStoredScriptLogLines;
using og::script::ScriptError;
using og::script::ScriptHost;
using og::script::ScriptLimits;

// ---------------------------------------------------------------------------
// Sandbox surface
// ---------------------------------------------------------------------------

TEST(ScriptSandbox, banned_symbols_absent)
{
    ScriptHost host;
    const char* banned[] = {
        "io",        "os",          "package",  "require", "dofile",
        "loadfile",  "load",        "loadstring", "collectgarbage",
        "coroutine", "debug",       "utf8",     "pairs",   "next",
        "string.dump", "math.random", "math.randomseed", "math.sqrt",
        "math.sin",  "math.cos",    "math.exp", "math.log", "math.fmod",
    };
    for (const char* path : banned)
        EXPECT_FALSE(host.sandbox_has(path)) << path << " should be absent";
}

TEST(ScriptSandbox, allowed_symbols_present)
{
    ScriptHost host;
    const char* allowed[] = {
        "assert",       "error",     "ipairs",     "pcall",   "xpcall",
        "select",       "tonumber",  "tostring",   "type",    "print",
        "setmetatable", "getmetatable", "rawget",  "rawset",
        "string.format", "string.sub", "string.rep", "table.insert",
        "table.sort",   "table.concat", "table.unpack",
        "math.floor",   "math.ceil", "math.abs",   "math.min", "math.max",
        "math.tointeger", "math.maxinteger", "math.mininteger",
        "og.div",  "og.mod",  "og.fadd", "og.fsub", "og.fmul", "og.fdiv",
        "og.i8",   "og.i16",  "og.i32",  "og.u8",   "og.trunc", "og.log",
    };
    for (const char* path : allowed)
        EXPECT_TRUE(host.sandbox_has(path)) << path << " should be present";
}

TEST(ScriptSandbox, chunks_are_write_isolated_but_share_root)
{
    ScriptHost host;
    ASSERT_TRUE(host.run_chunk("a", "leak = 42"));
    // Chunk B cannot see chunk A's global.
    auto leaked = host.eval_boolean("leak == nil");
    ASSERT_TRUE(leaked.has_value());
    EXPECT_TRUE(*leaked);
    // But both read the shared root.
    auto fmt = host.eval_string("string.format('%d', 7)");
    ASSERT_TRUE(fmt.has_value());
    EXPECT_EQ("7", *fmt);
}

TEST(ScriptSandbox, chunks_with_same_env_key_share_state)
{
    ScriptHost host;
    ASSERT_TRUE(host.run_chunk("a", "shared = 10", "packX"));
    ASSERT_TRUE(host.run_chunk("b", "og.log(shared + 5)", "packX"));
    ASSERT_TRUE(host.run_chunk("c", "og.log(tostring(shared))", "packY"));
    ASSERT_EQ(2u, host.log().size());
    EXPECT_EQ("15", host.log()[0]) << "same pack shares environment";
    EXPECT_EQ("nil", host.log()[1]) << "different pack is isolated";
}

TEST(ScriptSandbox, environment_metatable_is_fenced)
{
    ScriptHost host;
    auto fenced = host.eval_boolean("getmetatable(_ENV) == false");
    ASSERT_TRUE(fenced.has_value());
    EXPECT_TRUE(*fenced) << "sandbox root must not be reachable via "
                            "getmetatable(_ENV)";
}

TEST(ScriptSandbox, tostring_is_address_free)
{
    ScriptHost host;
    auto s = host.eval_string("tostring({})");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ("table", *s) << "tostring must not leak pointer text";
    auto f = host.eval_string("tostring(print)");
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ("function", *f);
    auto n = host.eval_string("tostring(42)");
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ("42", *n);
    // __tostring metamethods are honored.
    auto m = host.eval_string(
        "tostring(setmetatable({}, {__tostring = function() return 'X' end }))");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ("X", *m);
}

TEST(ScriptSandbox, print_and_og_log_capture_lines_in_order)
{
    ScriptHost host;
    ASSERT_TRUE(host.run_chunk("logtest",
                               "print('alpha', 1)\n"
                               "og.log('beta')\n"
                               "print('gamma', {})\n"));
    ASSERT_EQ(3u, host.log().size());
    EXPECT_EQ("alpha\t1", host.log()[0]);
    EXPECT_EQ("beta", host.log()[1]);
    EXPECT_EQ("gamma\ttable", host.log()[2]);
}

// ---------------------------------------------------------------------------
// Deterministic arithmetic: og.div / og.mod (C semantics)
// ---------------------------------------------------------------------------

TEST(ScriptArith, div_mod_match_cpp_truncation_semantics)
{
    ScriptHost host;
    const std::int64_t cases[][2] = {
        {7, 2},  {-7, 2},  {7, -2},  {-7, -2}, {1, 3},   {-1, 3},
        {0, 5},  {100, 7}, {-100, 7}, {100, -7}, {-100, -7},
        {9223372036854775807LL, 2}, {-9223372036854775807LL, 3},
    };
    for (const auto& c : cases) {
        const std::int64_t a = c[0], b = c[1];
        const std::string div_expr =
            "og.div(" + std::to_string(a) + ", " + std::to_string(b) + ")";
        const std::string mod_expr =
            "og.mod(" + std::to_string(a) + ", " + std::to_string(b) + ")";
        auto dv = host.eval_integer(div_expr);
        auto mv = host.eval_integer(mod_expr);
        ASSERT_TRUE(dv.has_value()) << div_expr;
        ASSERT_TRUE(mv.has_value()) << mod_expr;
        EXPECT_EQ(a / b, *dv) << div_expr;   // C++ truncation is the oracle
        EXPECT_EQ(a % b, *mv) << mod_expr;
    }
}

TEST(ScriptArith, div_differs_from_lua_floor_division_for_negatives)
{
    // This difference is the reason og.div exists (cookbook R1).
    ScriptHost host;
    auto differs = host.eval_boolean("og.div(-7, 2) ~= (-7 // 2)");
    ASSERT_TRUE(differs.has_value());
    EXPECT_TRUE(*differs);
    auto mod_differs = host.eval_boolean("og.mod(-7, 2) ~= (-7 % 2)");
    ASSERT_TRUE(mod_differs.has_value());
    EXPECT_TRUE(*mod_differs);
}

TEST(ScriptArith, div_mod_guard_zero_and_overflow)
{
    ScriptHost host;
    EXPECT_FALSE(host.eval_integer("og.div(1, 0)").has_value());
    EXPECT_FALSE(host.eval_integer("og.mod(1, 0)").has_value());
    EXPECT_FALSE(
        host.eval_integer("og.div(math.mininteger, -1)").has_value());
    EXPECT_FALSE(
        host.eval_integer("og.mod(math.mininteger, -1)").has_value());
    EXPECT_FALSE(host.errors().empty());
    // Rejects non-integral operands rather than silently coercing.
    host.clear_errors();
    EXPECT_FALSE(host.eval_integer("og.div(7.5, 2)").has_value());
    EXPECT_FALSE(host.errors().empty());
}

// ---------------------------------------------------------------------------
// Deterministic arithmetic: og.f* (per-op float rounding)
// ---------------------------------------------------------------------------

TEST(ScriptArith, float_ops_match_cpp_float_rounding)
{
    ScriptHost host;
    struct Case {
        double a, b;
    } cases[] = {
        {0.1, 0.2},  {1.0, 3.0},   {2.5, -0.75}, {1e10, 1.0},
        {3.0, 7.0},  {-19.5, 6.0}, {123456.789, 0.001},
    };
    for (const auto& c : cases) {
        const float fa = static_cast<float>(c.a);
        const float fb = static_cast<float>(c.b);
        const std::string args =
            "(" + std::to_string(c.a) + ", " + std::to_string(c.b) + ")";
        auto add = host.eval_number("og.fadd" + args);
        auto sub = host.eval_number("og.fsub" + args);
        auto mul = host.eval_number("og.fmul" + args);
        auto div = host.eval_number("og.fdiv" + args);
        ASSERT_TRUE(add && sub && mul && div) << args;
        // std::to_string keeps enough digits for these values to round-trip
        // to the same double, so the float casts below match exactly.
        EXPECT_EQ(static_cast<double>(fa + fb), *add) << "fadd" << args;
        EXPECT_EQ(static_cast<double>(fa - fb), *sub) << "fsub" << args;
        EXPECT_EQ(static_cast<double>(fa * fb), *mul) << "fmul" << args;
        EXPECT_EQ(static_cast<double>(fa / fb), *div) << "fdiv" << args;
    }
}

TEST(ScriptArith, fdiv_differs_from_double_division)
{
    // 1/3 in float differs from 1/3 in double — og.fdiv must produce the
    // float result (this is the double-rounding hazard R2 exists for).
    ScriptHost host;
    auto differs = host.eval_boolean("og.fdiv(1, 3) ~= (1.0 / 3.0)");
    ASSERT_TRUE(differs.has_value());
    EXPECT_TRUE(*differs);
}

// ---------------------------------------------------------------------------
// Narrowing helpers
// ---------------------------------------------------------------------------

TEST(ScriptArith, narrowing_matches_cpp_modular_semantics)
{
    ScriptHost host;
    struct Case {
        const char* expr;
        std::int64_t expected;
    } cases[] = {
        {"og.i8(255)", static_cast<std::int8_t>(255)},
        {"og.i8(128)", static_cast<std::int8_t>(128)},
        {"og.i8(-129)", static_cast<std::int8_t>(-129)},
        {"og.u8(-1)", static_cast<std::uint8_t>(-1)},
        {"og.u8(300)", static_cast<std::uint8_t>(300)},
        {"og.i16(65535)", static_cast<std::int16_t>(65535)},
        {"og.i16(40000)", static_cast<std::int16_t>(40000)},
        {"og.i32(4294967296 + 5)", 5},
        {"og.i32(2147483648)", static_cast<std::int32_t>(2147483648LL)},
        {"og.trunc(3.9)", 3},
        {"og.trunc(-3.9)", -3},
        {"og.trunc(0.0)", 0},
        {"og.trunc(-0.5)", 0},
    };
    for (const auto& c : cases) {
        auto v = host.eval_integer(c.expr);
        ASSERT_TRUE(v.has_value()) << c.expr;
        EXPECT_EQ(c.expected, *v) << c.expr;
    }
    // Out-of-range / NaN trunc raises.
    EXPECT_FALSE(host.eval_integer("og.trunc(1e19)").has_value());
    EXPECT_FALSE(host.eval_integer("og.trunc(0/0)").has_value());
}

// ---------------------------------------------------------------------------
// Budgets
// ---------------------------------------------------------------------------

TEST(ScriptBudget, infinite_loop_trips_instruction_budget)
{
    ScriptLimits limits;
    limits.instructions_per_call = 100000;
    ScriptHost host(limits);
    EXPECT_FALSE(host.run_chunk("spin", "while true do end"));
    ASSERT_FALSE(host.errors().empty());
    EXPECT_NE(std::string::npos,
              host.errors().back().message.find("instruction budget"))
        << host.errors().back().message;
    // The VM stays usable after a budget trip.
    host.clear_errors();
    auto ok = host.eval_integer("1 + 1");
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(2, *ok);
}

TEST(ScriptBudget, allocation_cap_trips_deterministically)
{
    ScriptLimits limits;
    limits.memory_bytes = 512 * 1024;
    ScriptHost host(limits);
    EXPECT_FALSE(host.run_chunk(
        "hog", "local t = {} for i = 1, 10000000 do t[i] = i end"));
    ASSERT_FALSE(host.errors().empty());
    EXPECT_LE(host.memory_used(), host.memory_limit());
    // Usable afterwards (the failed allocation was unwound).
    host.clear_errors();
    auto ok = host.eval_integer("2 + 2");
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(4, *ok);
}

// ---------------------------------------------------------------------------
// Host mechanics
// ---------------------------------------------------------------------------

TEST(ScriptHostBasics, syntax_and_runtime_errors_are_recorded)
{
    ScriptHost host;
    EXPECT_FALSE(host.run_chunk("bad_syntax", "this is not lua"));
    ASSERT_EQ(1u, host.errors().size());
    EXPECT_EQ("bad_syntax", host.errors()[0].where);

    EXPECT_FALSE(host.run_chunk("bad_runtime", "error('boom')"));
    ASSERT_EQ(2u, host.errors().size());
    EXPECT_NE(std::string::npos, host.errors()[1].message.find("boom"));
    // Runtime errors carry a traceback.
    EXPECT_NE(std::string::npos,
              host.errors()[1].message.find("stack traceback"));

    host.clear_errors();
    EXPECT_TRUE(host.errors().empty());
}

TEST(ScriptHostBasics, eval_coercions_are_strict)
{
    ScriptHost host;
    auto i = host.eval_integer("7 * 6");
    ASSERT_TRUE(i.has_value());
    EXPECT_EQ(42, *i);
    // A float result is not silently accepted as integer.
    EXPECT_FALSE(host.eval_integer("1.5").has_value());
    auto d = host.eval_number("1.5");
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(1.5, *d);
    auto b = host.eval_boolean("1 == 1");
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(*b);
    auto s = host.eval_string("'x' .. 'y'");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ("xy", *s);
}

TEST(ScriptHostBasics, identical_programs_produce_identical_hosts)
{
    // Two hosts running the same chunks agree on results, logs, and memory
    // accounting — the cheap smoke check for VM-level determinism.
    const char* program =
        "local acc = 0\n"
        "for i = 1, 1000 do acc = acc + og.mod(i * 7, 13) end\n"
        "og.log('acc', acc)\n"
        "local strs = {}\n"
        "for i = 1, 100 do strs[i] = string.format('s%d', i * i) end\n"
        "og.log(table.concat(strs, ','))\n";
    ScriptHost a, b;
    ASSERT_TRUE(a.run_chunk("p", program));
    ASSERT_TRUE(b.run_chunk("p", program));
    EXPECT_EQ(a.log(), b.log());
    EXPECT_EQ(a.memory_used(), b.memory_used());
}

TEST(ScriptHostBasics, integer_arithmetic_is_int64_exact)
{
    ScriptHost host;
    auto v = host.eval_integer("math.maxinteger - 1 + 1");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(9223372036854775807LL, *v);
    // Integer overflow wraps (two's complement), matching C++20 int64 casts.
    auto w = host.eval_integer("math.maxinteger + 1 == math.mininteger");
    EXPECT_FALSE(w.has_value());  // boolean, not integer — strictness check
    auto wb = host.eval_boolean("math.maxinteger + 1 == math.mininteger");
    ASSERT_TRUE(wb.has_value());
    EXPECT_TRUE(*wb);
}

// ---------------------------------------------------------------------------
// Error store bounds
//
// Nothing in production drains errors(); a hook that errors on a hot path
// (an effect's on_act, once per tick per live instance) would otherwise
// append a fresh traceback string for the entire life of the world.
// ---------------------------------------------------------------------------

TEST(ScriptErrorStore, repeated_identical_error_collapses_into_one_record)
{
    ScriptHost host;
    constexpr int kReps = 10000;
    bool all_failed = true;
    for (int i = 0; i < kReps; i++)
        all_failed &= !host.run_chunk("hot_hook", "error('every tick')");
    EXPECT_TRUE(all_failed);

    ASSERT_EQ(1u, host.errors().size()) << "repeats must not allocate records";
    EXPECT_EQ("hot_hook", host.errors()[0].where);
    EXPECT_EQ(static_cast<std::uint64_t>(kReps), host.errors()[0].count);
    EXPECT_NE(std::string::npos, host.errors()[0].message.find("every tick"));
    EXPECT_EQ(0u, host.dropped_error_count())
        << "a collapsed repeat is retained, not dropped";
}

TEST(ScriptErrorStore, distinct_errors_are_capped_with_the_first_ones_kept)
{
    ScriptHost host;
    constexpr std::size_t kExtra = 25;
    const std::size_t total = kMaxStoredScriptErrors + kExtra;
    for (std::size_t i = 0; i < total; i++) {
        const std::string name = "chunk" + std::to_string(i);
        (void)host.run_chunk(name, "error('boom')");
    }

    ASSERT_EQ(kMaxStoredScriptErrors, host.errors().size());
    EXPECT_EQ(static_cast<std::uint64_t>(kExtra), host.dropped_error_count());
    // First-seen order, so the earliest (root-cause) failures are the ones
    // that survive the cap.
    EXPECT_EQ("chunk0", host.errors().front().where);
    EXPECT_EQ("chunk" + std::to_string(kMaxStoredScriptErrors - 1),
              host.errors().back().where);
    for (const ScriptError& e : host.errors())
        EXPECT_EQ(1u, e.count) << e.where;

    // A repeat of an already-stored error still folds, even with the store
    // full — only genuinely new records are dropped.
    (void)host.run_chunk("chunk0", "error('boom')");
    EXPECT_EQ(kMaxStoredScriptErrors, host.errors().size());
    EXPECT_EQ(2u, host.errors().front().count);
    EXPECT_EQ(static_cast<std::uint64_t>(kExtra), host.dropped_error_count());

    host.clear_errors();
    EXPECT_TRUE(host.errors().empty());
    EXPECT_EQ(0u, host.dropped_error_count());
}

TEST(ScriptErrorStore, distinct_where_and_message_are_both_part_of_identity)
{
    ScriptHost host;
    (void)host.run_chunk("a", "error('one')");
    (void)host.run_chunk("a", "error('two')");   // same where, new message
    (void)host.run_chunk("b", "error('one')");   // same message, new where
    (void)host.run_chunk("a", "error('one')");   // exact repeat of the first
    ASSERT_EQ(3u, host.errors().size());
    EXPECT_EQ(2u, host.errors()[0].count);
    EXPECT_EQ(1u, host.errors()[1].count);
    EXPECT_EQ(1u, host.errors()[2].count);
}

TEST(ScriptLogStore, log_keeps_the_most_recent_lines_and_counts_the_rest)
{
    // A pack that logs every tick would otherwise grow this vector for the
    // life of the GameWorld (the defect the error store already fixed). The
    // tail is the useful part, so eviction is oldest-first and log().back()
    // stays "most recent".
    ScriptHost host;
    constexpr std::size_t kExtra = 40;
    const std::size_t total = kMaxStoredScriptLogLines + kExtra;
    std::string chunk;
    for (std::size_t i = 0; i < total; i++)
        chunk += "og.log(" + std::to_string(i) + ")\n";
    ASSERT_TRUE(host.run_chunk("logspam", chunk));

    ASSERT_EQ(kMaxStoredScriptLogLines, host.log().size());
    EXPECT_EQ(static_cast<std::uint64_t>(kExtra),
              host.dropped_log_line_count());
    EXPECT_EQ(std::to_string(total - 1), host.log().back())
        << "the newest line must survive";
    EXPECT_EQ(std::to_string(kExtra), host.log().front())
        << "exactly the oldest kExtra lines were evicted";
}

TEST(ScriptLogStore, an_unfilled_log_drops_nothing)
{
    ScriptHost host;
    ASSERT_TRUE(host.run_chunk("quiet", "og.log('a')\nog.log('b')\n"));
    EXPECT_EQ(2u, host.log().size());
    EXPECT_EQ(0u, host.dropped_log_line_count());
}

// ---------------------------------------------------------------------------
// Pack-script registry
// ---------------------------------------------------------------------------

TEST(PackScriptRegistry, unregister_removes_one_packs_chunks_and_bumps_the_gen)
{
    og::script::clear_pack_scripts();
    og::script::register_pack_script({"pack.a", "a1.lua", "-- a1\n"});
    og::script::register_pack_script({"pack.a", "a2.lua", "-- a2\n"});
    og::script::register_pack_script({"pack.b", "b1.lua", "-- b1\n"});
    ASSERT_EQ(3u, og::script::pack_scripts().size());
    const unsigned before = og::script::pack_scripts_generation();

    // Unmounting one pack must take exactly that pack's chunks with it —
    // this is what keeps a stale family hook from surviving a campaign
    // switch and feeding the deterministic sim.
    og::script::unregister_pack_scripts("pack.a");

    ASSERT_EQ(1u, og::script::pack_scripts().size());
    EXPECT_EQ("pack.b", og::script::pack_scripts()[0].pack_id);
    EXPECT_NE(before, og::script::pack_scripts_generation())
        << "the generation must move so cached VMs rebuild";

    // Unregistering an id that is not present is a no-op, not a crash.
    og::script::unregister_pack_scripts("pack.missing");
    EXPECT_EQ(1u, og::script::pack_scripts().size());

    og::script::clear_pack_scripts();
}

// The family-chunk registry is the same shape and owes the same guarantee,
// for a sharper reason: a family chunk carries DESCRIPTOR DATA, so a chunk
// left behind by an unmounted pack would keep re-installing its family into
// a registry slot on the next pass, and every peer's install order — which
// the wire ids depend on — would depend on what that peer had mounted
// earlier.
TEST(PackFamilyChunkRegistry, unregister_removes_one_packs_chunks)
{
    og::script::clear_pack_family_chunks();
    og::script::register_pack_family_chunk(
        {"pack.a", "a-families.lua", "-- a\n"});
    og::script::register_pack_family_chunk(
        {"pack.a", "a-more-families.lua", "-- a2\n"});
    og::script::register_pack_family_chunk(
        {"pack.b", "b-families.lua", "-- b\n"});
    ASSERT_EQ(3u, og::script::pack_family_chunks().size());
    const unsigned before = og::script::pack_family_generation();

    og::script::unregister_pack_family_chunks("pack.a");

    ASSERT_EQ(1u, og::script::pack_family_chunks().size());
    EXPECT_EQ("pack.b", og::script::pack_family_chunks()[0].pack_id);
    EXPECT_NE(before, og::script::pack_family_generation())
        << "the generation must move so cached VMs rebuild";

    // An id with no chunks removes nothing AND does not move the generation:
    // a declaration-only rebuild of every long-lived VM is expensive, and an
    // unmount of a pack that never had family chunks is not a content change.
    const unsigned after = og::script::pack_family_generation();
    og::script::unregister_pack_family_chunks("pack.missing");
    EXPECT_EQ(1u, og::script::pack_family_chunks().size());
    EXPECT_EQ(after, og::script::pack_family_generation());

    og::script::clear_pack_family_chunks();
}

// ---------------------------------------------------------------------------
// Duplicate hook registration diagnostic
// ---------------------------------------------------------------------------

namespace {

class ScriptHostPackTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
    }
    void TearDown() override { og::script::clear_pack_scripts(); }
};

}  // namespace

TEST_F(ScriptHostPackTest, duplicate_hook_registration_is_reported_and_wins)
{
    // Pack chunks load filename-lexicographically; a second chunk claiming a
    // hook the first already registered used to overwrite it in silence.
    og::script::register_pack_script(
        {"test.pack", "a_first.lua",
         "og.register_hooks('living', 'core:soldier', {\n"
         "  do_special = function(self) og.log('first') return true end,\n"
         "})\n"});
    og::script::register_pack_script(
        {"test.pack", "b_second.lua",
         "og.register_hooks('living', 'core:soldier', {\n"
         "  do_special = function(self) og.log('second') return true end,\n"
         "})\n"});

    og::script::WorldScripts& ws = og::script::active_world_scripts();

    // Reported...
    ASSERT_EQ(1u, ws.host().errors().size());
    const std::string& msg = ws.host().errors()[0].message;
    EXPECT_NE(std::string::npos, msg.find("duplicate hook registration"))
        << msg;
    EXPECT_NE(std::string::npos, msg.find("core:soldier")) << msg;
    EXPECT_NE(std::string::npos, msg.find("do_special")) << msg;
    EXPECT_NE(std::string::npos, ws.host().errors()[0].where.find("b_second"))
        << "the offending chunk is named: " << ws.host().errors()[0].where;

    // ...but last-registration-wins is unchanged.
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    auto result = og::script::hooks::do_special(fd, nullptr);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_FALSE(ws.host().log().empty());
    EXPECT_EQ("second", ws.host().log().back());
    EXPECT_TRUE(ws.has_hook(Order::Living, FAMILY_SOLDIER,
                            og::script::FamilyHook::DoSpecial));
}

TEST_F(ScriptHostPackTest, a_chunk_that_fails_to_compile_is_recorded_not_silent)
{
    // A pack chunk with a syntax error registers nothing: every family it
    // carries silently loses all behavior while the pack still reports
    // installed. The load must land in errors() (and the replay loop logs
    // an operator-facing [ERROR] line through the same channel as the
    // duplicate-hook warning) — a silently-degraded class is how one broken
    // line once took out eleven parity scenarios with no diagnostic at all.
    og::script::register_pack_script(
        {"test.pack", "a_broken.lua", "this is not lua (\n"});
    og::script::register_pack_script(
        {"test.pack", "b_healthy.lua",
         "og.register_hooks('living', 'core:soldier', "
         "{ do_special = function() return true end })\n"});

    og::script::WorldScripts& ws = og::script::active_world_scripts();

    ASSERT_EQ(1u, ws.host().errors().size());
    const og::script::ScriptError& e = ws.host().errors()[0];
    EXPECT_NE(std::string::npos, e.where.find("a_broken")) << e.where;
    EXPECT_NE(std::string::npos, e.message.find("syntax error")) << e.message;

    // The broken chunk must not take its pack-mates down with it.
    EXPECT_TRUE(ws.has_hook(Order::Living, FAMILY_SOLDIER,
                            og::script::FamilyHook::DoSpecial));
}

TEST_F(ScriptHostPackTest, distinct_hooks_for_one_family_are_not_a_duplicate)
{
    og::script::register_pack_script(
        {"test.pack", "a_first.lua",
         "og.register_hooks('living', 'core:soldier', "
         "{ do_special = function() return true end })\n"});
    og::script::register_pack_script(
        {"test.pack", "b_second.lua",
         "og.register_hooks('living', 'core:soldier', "
         "{ on_death = function() return true end })\n"});
    og::script::WorldScripts& ws = og::script::active_world_scripts();
    EXPECT_TRUE(ws.host().errors().empty())
        << ws.host().errors().front().message;
}

// ---------------------------------------------------------------------------
// Pack-Lua coverage recorder
// ---------------------------------------------------------------------------
//
// Family behavior is Lua, so the coverage gate has to measure Lua. These
// pin the recorder's two halves (executed lines, functions whose bodies
// executed), its denominator oracle, and — the load-bearing one — that it is
// completely inert until armed.

namespace cov = og::script::coverage;

namespace {

// A fresh, uniquely named directory under the system temp dir. NEVER a fixed
// name: these tests run concurrently with other checkouts and with second
// instances of this very binary (a coverage collection pass and a plain
// ctest can overlap on a shared machine), and two runs sharing
// temp_directory_path()/"og_lua_cov_test_dump" corrupted each other — one
// run's remove_all() deleted the dump the other was about to read.
std::filesystem::path make_unique_temp_dir(const std::string& prefix)
{
#if !defined(_WIN32)
    std::string templ =
        (std::filesystem::temp_directory_path() / (prefix + "XXXXXX"))
            .string();
    char* made = ::mkdtemp(templ.data());
    return (made != nullptr) ? std::filesystem::path(made)
                             : std::filesystem::path();
#else
    std::random_device rd;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        (prefix + std::to_string(rd()) + "-" + std::to_string(rd()));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return ec ? std::filesystem::path() : dir;
#endif
}

// The probe chunk every line test below uses. Line numbers matter:
//   1 comment  2 code  3 blank  4 'local function'  5 if  6 then-branch
//   7 else     8 else-branch    9 end   10 return   11 end (holds the
//   enclosing chunk's OP_CLOSURE)   12 blank  13 bare local  14 call  15 log
constexpr const char* kProbeChunk =
    "-- a comment\n"                        // 1
    "local counted = 0\n"                   // 2
    "\n"                                    // 3
    "local function branchy(flag)\n"        // 4
    "  if flag then\n"                      // 5
    "    counted = counted + 1\n"           // 6
    "  else\n"                              // 7
    "    counted = counted - 1\n"           // 8
    "  end\n"                               // 9
    "  return counted\n"                    // 10
    "end\n"                                 // 11
    "\n"                                    // 12
    "local bare\n"                          // 13
    "bare = branchy(true)\n"                // 14
    "og.log('done', bare)\n";               // 15

std::vector<int> recorded_lines_for(const std::string& chunk)
{
    std::vector<int> out;
    for (const cov::LineHit& h : cov::line_hits())
        if (h.chunk == chunk)
            out.push_back(h.line);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> split_fields(const std::string& record)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = record.find('\t', start);
        if (tab == std::string::npos) {
            out.push_back(record.substr(start));
            return out;
        }
        out.push_back(record.substr(start, tab - start));
        start = tab + 1;
    }
}

int append_dump_writer(lua_State*, const void* p, std::size_t sz, void* ud)
{
    static_cast<std::string*>(ud)->append(static_cast<const char*>(p), sz);
    return 0;
}

// The REAL P8-A attack artifact: `source` compiled by the vendored Lua and
// dumped STRIPPED of debug info — the shape whose Proto tree keeps every
// function span but carries no line info, so before binary chunks were
// refused everywhere it entered the report as 0 executable lines with all
// of its functions still coverable.
std::string dump_stripped_bytecode(const std::string& source)
{
    lua_State* L = luaL_newstate();
    EXPECT_NE(nullptr, L);
    if (L == nullptr)
        return {};
    EXPECT_EQ(LUA_OK, luaL_loadbufferx(L, source.data(), source.size(),
                                       "generator", "t"));
    std::string bytes;
    lua_dump(L, append_dump_writer, &bytes, /*strip=*/1);
    lua_close(L);
    EXPECT_FALSE(bytes.empty());
    if (!bytes.empty()) {
        EXPECT_EQ(LUA_SIGNATURE[0], bytes.front())
            << "a Lua dump must begin with the binary-chunk signature";
    }
    return bytes;
}

// A pack-shaped source with a called helper and an uncalled one — the
// smallest shape where the P8-A flip is visible: uncovered LINES that
// bytecode would erase from the denominator, while every FUNCTION span
// stays intact in the stripped Proto tree.
constexpr const char* kBytecodeProbeSource =
    "local function helper(x)\n"
    "  local acc = 0\n"
    "  for i = 1, x do\n"
    "    acc = acc + i\n"
    "  end\n"
    "  return acc\n"
    "end\n"
    "local function unused(y)\n"
    "  return y * 2\n"
    "end\n"
    "return helper(4)\n";

}  // namespace

// OPENGLAD_LUA_COVERAGE is a directory path. `=1` reads like a boolean and
// used to be honoured as a directory named `1`, created in whatever cwd the
// process happened to have — measurement scattered across the per-binary
// working directories of one ctest run and no way to tell from the outside.
// Only absolute paths arm the recorder now; everything else is off and says
// so, which the report surfaces as an unmet bar instead of a number.
TEST(ScriptCoverage, output_dir_env_must_be_an_absolute_directory_path)
{
    EXPECT_EQ("/tmp/luacov", cov::validate_output_dir("/tmp/luacov"))
        << "an absolute path is the supported form";

    // Unset/empty is simply "off", and stays silent about it.
    EXPECT_EQ("", cov::validate_output_dir(""));

    // The footgun itself, plus the other relative spellings of it.
    for (const char* bad : {"1", "true", "yes", "on", "0",
                            "luacov", "./luacov", "../luacov"})
    {
        EXPECT_EQ("", cov::validate_output_dir(bad))
            << "relative value " << bad << " must not arm the recorder";
    }
}

TEST(ScriptCoverage, recorder_is_inert_when_disabled)
{
    cov::ScopedRecording recording;      // isolates + restores the store
    cov::set_enabled_for_testing(false); // ...then disarm inside it

    ScriptHost host;
    ASSERT_TRUE(host.run_chunk("cov_off.lua", kProbeChunk));
    ASSERT_EQ(1u, host.log().size()) << "the chunk really ran";

    EXPECT_FALSE(cov::enabled());
    EXPECT_TRUE(cov::line_hits().empty())
        << "a disabled recorder must not observe anything";
    EXPECT_TRUE(cov::function_records().empty());
}

TEST(ScriptCoverage, records_the_lines_a_chunk_actually_ran)
{
    cov::ScopedRecording recording;
    ASSERT_TRUE(cov::enabled());

    ScriptHost host;
    ASSERT_TRUE(host.run_chunk("cov_on.lua", kProbeChunk));
    ASSERT_EQ(1u, host.log().size());
    EXPECT_EQ("done\t1", host.log().back());

    // branchy(true) was called, so the else-branch (line 8) never ran; every
    // other executable line did.
    const std::vector<int> expected{2, 5, 6, 10, 11, 13, 14, 15};
    EXPECT_EQ(expected, recorded_lines_for("cov_on.lua"));

    // The numerator lives on the same grid as the denominator: everything
    // recorded is a line the oracle calls executable.
    const cov::SourceFacts all = cov::source_facts(kProbeChunk, "probe");
    ASSERT_TRUE(all.ok) << all.error;
    for (int line : expected)
        EXPECT_NE(all.lines.end(),
                  std::find(all.lines.begin(), all.lines.end(), line))
            << "line " << line << " recorded but not considered executable";

    // Hits are counted, not just flagged: the loop-free chunk runs each line
    // once, so the recorder must agree.
    for (const cov::LineHit& h : cov::line_hits()) {
        if (h.chunk == "cov_on.lua") {
            EXPECT_EQ(1u, h.count) << "line " << h.line;
        }
    }
}

TEST(ScriptCoverage, executable_lines_are_lua_s_own_breakpoint_lines)
{
    const cov::SourceFacts all = cov::source_facts(kProbeChunk, "probe");
    ASSERT_TRUE(all.ok) << all.error;
    // Comments (1) and blank lines (3, 12) carry no instruction. Neither
    // does the `else` (7) or the `end` that closes a control structure (9).
    // The `end` that closes a FUNCTION (11) does — it carries the enclosing
    // chunk's OP_CLOSURE — and so does a bare `local` (13, an OP_LOADNIL),
    // which is why this is Lua's answer and not a regex's.
    const std::vector<int> expected{2, 5, 6, 8, 10, 11, 13, 14, 15};
    EXPECT_EQ(expected, all.lines);
}

TEST(ScriptCoverage, every_prototype_is_a_function_including_the_chunk)
{
    const cov::SourceFacts all = cov::source_facts(kProbeChunk, "probe");
    ASSERT_TRUE(all.ok) << all.error;
    // The main chunk (0..0) and `branchy` (4..11). Nothing registers either
    // one — that is the point. A denominator built from registered hooks
    // would have called this file functionless.
    const std::vector<cov::FunctionSpan> expected{{0, 0}, {4, 11}};
    EXPECT_EQ(expected, all.functions);
}

TEST(ScriptCoverage, anonymous_and_nested_functions_are_counted)
{
    // Three prototypes nobody could register: an anonymous callback, a
    // closure nested inside it, and a one-line helper.
    const cov::SourceFacts all = cov::source_facts(
        "local outer = function(x)\n"          // 1
        "  local inner = function(y)\n"        // 2
        "    return y + 1\n"                   // 3
        "  end\n"                              // 4
        "  return inner(x)\n"                  // 5
        "end\n"                                // 6
        "local function tiny() return 7 end\n" // 7
        "og.log('n', outer(1) + tiny())\n",    // 8
        "nested.lua");
    ASSERT_TRUE(all.ok) << all.error;
    const std::vector<cov::FunctionSpan> expected{
        {0, 0}, {1, 6}, {2, 4}, {7, 7}};
    EXPECT_EQ(expected, all.functions);
}

// THE COLLAPSE. Two prototypes that begin on the same line are two entries,
// because identity is the whole SPAN. Keyed on `linedefined` alone they
// merged into one, and covering either marked both — an audit hid an
// uncalled function behind a called one on the same line and the 100 %
// function bar did not notice.
TEST(ScriptCoverage, two_prototypes_on_one_line_stay_two_functions)
{
    const cov::SourceFacts all = cov::source_facts(
        "local function keep(f) return f end\n"  // 1
        "local noop, dead = keep(function() end), keep(function()\n"  // 2
        "    return 'dead'\n"                    // 3
        "  end)\n"                               // 4
        "noop()\n",                              // 5
        "collapse.lua");
    ASSERT_TRUE(all.ok) << all.error;
    // main chunk, `keep`, the empty one-liner on 2, and the multi-line one
    // that ALSO starts on 2.
    const std::vector<cov::FunctionSpan> expected{
        {0, 0}, {1, 1}, {2, 2}, {2, 4}};
    EXPECT_EQ(expected, all.functions);

    int starting_on_two = 0;
    for (const cov::FunctionSpan& span : all.functions) {
        if (span.line_defined == 2)
            starting_on_two++;
    }
    EXPECT_EQ(2, starting_on_two)
        << "both prototypes on line 2 must survive as separate entries";
}

// ...and the runtime half agrees: calling only the empty one leaves the
// other at zero.
TEST(ScriptCoverage, covering_one_of_two_prototypes_on_a_line_leaves_the_other)
{
    cov::ScopedRecording recording;
    ScriptHost host;
    ASSERT_TRUE(host.run_chunk(
        "collapse_run.lua",
        "local function keep(f) return f end\n"  // 1
        "local noop, dead = keep(function() end), keep(function()\n"  // 2
        "    return 'dead'\n"                    // 3
        "  end)\n"                               // 4
        "noop()\n"));                            // 5

    std::map<std::pair<int, int>, std::uint64_t> by_span;
    for (const cov::FunctionRecord& r : cov::function_records()) {
        if (r.chunk == "collapse_run.lua")
            by_span[{r.line_defined, r.last_line_defined}] = r.body_hits;
    }
    const std::uint64_t empty_one_liner = by_span[std::make_pair(2, 2)];
    const std::uint64_t multi_line = by_span[std::make_pair(2, 4)];
    EXPECT_GT(empty_one_liner, 0u) << "the empty function was called";
    EXPECT_EQ(0u, multi_line)
        << "`dead` never ran; sharing a start line must not cover it";
}

TEST(ScriptCoverage, executable_lines_reports_a_compile_error)
{
    const cov::SourceFacts bad = cov::source_facts("if =", "broken.lua");
    EXPECT_FALSE(bad.ok);
    EXPECT_NE(std::string::npos, bad.error.find("broken.lua")) << bad.error;
    EXPECT_TRUE(bad.lines.empty());
    EXPECT_TRUE(bad.functions.empty());
}

TEST(ScriptCoverage, raw_dump_round_trips_through_the_output_directory)
{
    cov::ScopedRecording recording;
    const std::string saved_dir = cov::output_dir();

    ScriptHost host;
    ASSERT_TRUE(host.run_chunk("testfixture/dump.lua", kProbeChunk));
    // The inventory half: a script the engine loaded, with its bytes, so the
    // report can measure one that has no file in the tree. Declared BEFORE it
    // is compiled — exactly the engine's order — so the compile binds its
    // prototypes to this generation and the hits below carry its digest.
    cov::declare_pack_source("packs/inventoried/scripts/x.lua", kProbeChunk,
                             "/somewhere/x.glad");
    ASSERT_TRUE(host.run_chunk("packs/inventoried/scripts/x.lua",
                               kProbeChunk));

    const std::filesystem::path dir =
        make_unique_temp_dir("og_lua_cov_dump_");
    ASSERT_FALSE(dir.empty());

    // No output directory configured: the exit flush is a no-op.
    cov::set_output_dir_for_testing("");
    cov::flush_to_output_dir();
    EXPECT_TRUE(std::filesystem::is_empty(dir));

    cov::set_output_dir_for_testing(dir.string());
    EXPECT_EQ(dir.string(), cov::output_dir());
    cov::flush_to_output_dir();
    cov::set_output_dir_for_testing(saved_dir);

    std::vector<std::filesystem::path> dumps;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".luacov")
            dumps.push_back(entry.path());
    }
    ASSERT_EQ(1u, dumps.size()) << "one dump per flush";

    std::ifstream in(dumps.front());
    std::stringstream body;
    body << in.rdbuf();
    const std::string text = body.str();

    EXPECT_NE(std::string::npos, text.find("# openglad-lua-coverage 5\n"))
        << text;
    // Every dump names the process that wrote it. The report checks the
    // POPULATION of recorder processes against a committed manifest
    // (scripts/coverage/recorder_processes.txt): a suite that quietly loses
    // processes otherwise just reads as slightly lower coverage.
    ASSERT_FALSE(cov::program_name().empty())
        << "an unattributable dump defeats the population check";
    EXPECT_NE(std::string::npos,
              text.find("P\t" + cov::program_name() + "\n"))
        << text;
    // A chunk that ran with NO declared source carries the "-" marker in the
    // generation column — the report measures it only if it is not a packs/
    // chunk (this one is test Lua).
    EXPECT_NE(std::string::npos,
              text.find("L\ttestfixture/dump.lua\t-\t14\t1"))
        << text;
    // A chunk that ran under a declared source carries that generation's
    // digest on every hit: the recorder resolves the executing prototype to
    // the generation it was compiled from, so the report never has to guess
    // which generation executed.
    const std::string probe_digest = cov::sha256_hex(kProbeChunk);
    EXPECT_NE(std::string::npos,
              text.find("L\tpacks/inventoried/scripts/x.lua\t" +
                        probe_digest + "\t14\t1"))
        << text;
    EXPECT_EQ(std::string::npos,
              text.find("L\tpacks/inventoried/scripts/x.lua\t-\t"))
        << "a declared chunk's hits must never carry the no-generation "
           "marker: "
        << text;
    // The declared source travels as a content-addressed sidecar: the S
    // record carries a BARE file name (the reader joins it under sources/
    // itself — a dump must not be able to point at arbitrary paths), the
    // sha256 of the bytes (the identity hits are scored against), and the
    // origin the engine loaded it from (diagnostic).
    const std::size_t s_record =
        text.find("S\tpacks/inventoried/scripts/x.lua\t");
    ASSERT_NE(std::string::npos, s_record) << text;
    const std::size_t s_end = text.find('\n', s_record);
    ASSERT_NE(std::string::npos, s_end);
    const std::vector<std::string> fields =
        split_fields(text.substr(s_record, s_end - s_record));
    ASSERT_EQ(5u, fields.size()) << text;
    const std::string& sidecar_name = fields[2];
    EXPECT_EQ(std::string::npos, sidecar_name.find('/'))
        << "the sidecar field is a bare name, never a path: " << sidecar_name;
    const std::string digest = cov::sha256_hex(kProbeChunk);
    EXPECT_NE(std::string::npos, sidecar_name.find(digest))
        << "the digest is the sidecar's name — two processes declaring "
           "different bytes under one chunk must not collide: "
        << sidecar_name;
    EXPECT_EQ(digest, fields[3])
        << "the S record must carry the sha256 of the declared bytes";
    EXPECT_EQ("/somewhere/x.glad", fields[4]);
    std::ifstream copied(dir / "sources" / sidecar_name);
    ASSERT_TRUE(copied.good()) << sidecar_name;
    std::stringstream copied_body;
    copied_body << copied.rdbuf();
    EXPECT_EQ(std::string(kProbeChunk), copied_body.str())
        << "the sidecar must be the exact bytes the engine compiled";

    std::filesystem::remove_all(dir);

    // write_raw_report reports I/O failure rather than throwing. The path is
    // under a regular FILE, so neither the sidecar directory nor the dump can
    // be created — a merely missing directory is made, since the recorder is
    // handed an output directory that may not exist yet.
    const std::filesystem::path blocker_dir =
        make_unique_temp_dir("og_lua_cov_blocker_");
    ASSERT_FALSE(blocker_dir.empty());
    const std::filesystem::path blocker = blocker_dir / "blocker";
    {
        std::ofstream out(blocker, std::ios::trunc);
        ASSERT_TRUE(out.good());
    }
    EXPECT_FALSE(cov::write_raw_report((blocker / "x.luacov").string()));
    std::filesystem::remove_all(blocker_dir);
}

// Redirecting the output directory forgets the memoized dump path. Without
// that, a test that pointed the recorder at a temp directory and deleted it
// left the process writing its exit dump to a path that no longer exists —
// and the whole binary's Lua coverage vanished from the run with no
// diagnostic. og_unit_script itself was silently contributing nothing.
TEST(ScriptCoverage, redirecting_the_output_directory_starts_a_fresh_dump)
{
    cov::ScopedRecording recording;
    const std::string saved_dir = cov::output_dir();

    ScriptHost host;
    ASSERT_TRUE(host.run_chunk("testfixture/redirect.lua", kProbeChunk));

    const std::filesystem::path first =
        make_unique_temp_dir("og_lua_cov_redirect_a_");
    const std::filesystem::path second =
        make_unique_temp_dir("og_lua_cov_redirect_b_");
    ASSERT_FALSE(first.empty());
    ASSERT_FALSE(second.empty());

    cov::set_output_dir_for_testing(first.string());
    cov::flush_to_output_dir();
    std::filesystem::remove_all(first);  // as a test tidying up would

    cov::set_output_dir_for_testing(second.string());
    cov::flush_to_output_dir();
    cov::set_output_dir_for_testing(saved_dir);

    int dumps = 0;
    for (const auto& entry : std::filesystem::directory_iterator(second)) {
        if (entry.path().extension() == ".luacov")
            dumps++;
    }
    EXPECT_EQ(1, dumps) << "the second directory must get its own dump";
    std::filesystem::remove_all(second);
}

// ---------------------------------------------------------------------------
// Function coverage: registered vs dispatched
// ---------------------------------------------------------------------------

namespace {

const cov::FunctionRecord* find_fn(const std::vector<cov::FunctionRecord>& v,
                                   const std::string& label)
{
    for (const cov::FunctionRecord& r : v)
        if (r.label == label)
            return &r;
    return nullptr;
}

class ScriptCoverageHookTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
    }
    void TearDown() override { og::script::clear_pack_scripts(); }
};

}  // namespace

// Registration names a function; it never covers one. The hit comes from a
// line of the function's own body executing, which is why a hook that is
// registered and never entered stays at zero — and why the metric could not
// be satisfied by dispatching a hook whose body does nothing.
TEST_F(ScriptCoverageHookTest, a_registered_but_uncalled_hook_is_a_miss)
{
    og::script::register_pack_script(
        {"test.pack", "testfixture/fam.lua",
         "og.register_hooks('living', 'core:soldier', {\n"
         "  do_special = function(self)\n"                    // line 2
         "    return true\n"
         "  end,\n"
         "  on_death = function(self)\n"                      // line 5
         "    return true\n"
         "  end,\n"
         "})\n"});

    cov::ScopedRecording recording;
    // Constructing the VM replays the pack, which is where registration —
    // and therefore the function's NAME — is observed.
    og::script::WorldScripts& ws = og::script::active_world_scripts();
    ASSERT_TRUE(ws.host().errors().empty())
        << ws.host().errors().front().message;

    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    ASSERT_TRUE(og::script::hooks::do_special(fd, nullptr).has_value());

    const std::vector<cov::FunctionRecord> fns = cov::function_records();
    const cov::FunctionRecord* special =
        find_fn(fns, "living/core:soldier/do_special");
    const cov::FunctionRecord* death =
        find_fn(fns, "living/core:soldier/on_death");
    ASSERT_NE(nullptr, special);
    ASSERT_NE(nullptr, death) << "an unused hook must still be in the report";

    EXPECT_EQ("testfixture/fam.lua", special->chunk);
    EXPECT_EQ(2, special->line_defined);
    EXPECT_GT(special->body_hits, 0u) << "its body ran";

    EXPECT_EQ(5, death->line_defined);
    EXPECT_EQ(0u, death->body_hits) << "never entered: a function-coverage miss";

    // The chunk itself is a function too, and it ran at load.
    const cov::FunctionRecord* chunk = nullptr;
    for (const cov::FunctionRecord& r : fns) {
        if (r.chunk == "testfixture/fam.lua" && r.line_defined == 0)
            chunk = &r;
    }
    ASSERT_NE(nullptr, chunk) << "the main chunk is a prototype like any other";
    EXPECT_GT(chunk->body_hits, 0u);
}

// The numerator is prototype-attributed, not line-attributed: a function
// nobody registers is covered when it is CALLED, and a one-line function
// sharing its line with the statement that defines it is not covered merely
// because that statement ran. Both are what a registration-counting metric
// got wrong — 17% of the `function` tokens in packs/core were invisible to
// it, and every one of them was a helper.
TEST_F(ScriptCoverageHookTest, unregistered_helpers_are_functions_too)
{
    og::script::register_pack_script(
        {"test.pack", "testfixture/helpers.lua",
         "local called = function(n)\n"          // line 1: called below
         "  return n + 1\n"
         "end\n"
         "local uncalled = function(n)\n"        // line 4: never called
         "  return n - 1\n"
         "end\n"
         "og.register_hooks('living', 'core:soldier', {\n"
         "  do_special = function(self)\n"       // line 8
         "    return called(1) > 0\n"
         "  end,\n"
         "})\n"});

    cov::ScopedRecording recording;
    og::script::WorldScripts& ws = og::script::active_world_scripts();
    ASSERT_TRUE(ws.host().errors().empty())
        << ws.host().errors().front().message;
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    ASSERT_TRUE(og::script::hooks::do_special(fd, nullptr).has_value());

    std::map<int, std::uint64_t> calls_by_line;
    for (const cov::FunctionRecord& r : cov::function_records()) {
        if (r.chunk == "testfixture/helpers.lua")
            calls_by_line[r.line_defined] = r.body_hits;
    }
    EXPECT_GT(calls_by_line[1], 0u)
        << "a helper nothing registered is covered by being called";
    EXPECT_EQ(0u, calls_by_line[4])
        << "...and one nothing calls is a miss, registered or not";
    EXPECT_GT(calls_by_line[8], 0u) << "the hook itself ran";

    // The oracle agrees about the denominator these numerators sit in.
    const cov::SourceFacts facts = cov::source_facts(
        og::script::pack_scripts().front().source, "testfixture/helpers.lua");
    ASSERT_TRUE(facts.ok) << facts.error;
    const std::vector<cov::FunctionSpan> expected{
        {0, 0}, {1, 3}, {4, 6}, {8, 10}};
    EXPECT_EQ(expected, facts.functions);
}

namespace {

class ScriptCoverageLevelTest : public ::testing::Test {
protected:
    ScriptCoverageLevelTest() : world(7)
    {
        init_all_registries();
        world.id = 42;
        world.entity_factory =
            [](Order order, std::int32_t family) -> std::unique_ptr<walker> {
            auto entity = std::make_unique<walker>();
            entity->set_order_family(order, static_cast<char>(family));
            entity->set_sizex(16);
            entity->set_sizey(16);
            return entity;
        };
        context.world = &world;
        context.sim_events = &events;
        previous_ = current_game;
        current_game = &context;
        og::script::clear_pack_scripts();
    }

    ~ScriptCoverageLevelTest() override
    {
        og::script::clear_pack_scripts();
        current_game = previous_;
    }

    GameWorld world;
    GameplayContext context{};
    og::sim::SimEventLog events;
    GameplayContext* previous_ = nullptr;
};

}  // namespace

TEST_F(ScriptCoverageLevelTest, level_and_entity_hooks_count_as_functions)
{
    og::script::register_pack_script(
        {"test.level", "testfixture/lvl.lua",
         "og.register_level_hooks(-1, {\n"
         "  on_entity_spawn = function(ent)\n"                     // line 2
         "    og.set_entity_hooks(ent, { on_death = function(e)\n" // line 3
         "    end })\n"
         "  end,\n"
         "  on_entity_death = function(ent) end,\n"                // line 6
         "})\n"});

    cov::ScopedRecording recording;
    world.tick();
    walker* soldier = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier);

    std::vector<cov::FunctionRecord> fns = cov::function_records();
    const cov::FunctionRecord* spawn = find_fn(fns, "level/-1/on_entity_spawn");
    const cov::FunctionRecord* death = find_fn(fns, "level/-1/on_entity_death");
    const cov::FunctionRecord* per_entity = find_fn(fns, "entity/on_death");
    ASSERT_NE(nullptr, spawn);
    ASSERT_NE(nullptr, death) << "level hooks belong in the denominator too";
    ASSERT_NE(nullptr, per_entity)
        << "og.set_entity_hooks registrations are functions as well";
    EXPECT_GT(spawn->body_hits, 0u);
    EXPECT_EQ(0u, death->body_hits) << "nothing has died yet";
    EXPECT_EQ(0u, per_entity->body_hits);
    EXPECT_EQ(3, per_entity->line_defined);

    // ...and dispatching them flips both to covered.
    soldier->set_dead(1);
    soldier->death();
    fns = cov::function_records();
    EXPECT_GT(find_fn(fns, "level/-1/on_entity_death")->body_hits, 0u);
    EXPECT_GT(find_fn(fns, "entity/on_death")->body_hits, 0u);
}

// ---------------------------------------------------------------------------
// The dump is content-addressed
// ---------------------------------------------------------------------------

TEST(ScriptCoverage, sha256_matches_the_published_test_vectors)
{
    // The report re-hashes every declared sidecar with Python's hashlib and
    // requires the digests to agree; the whole content-address scheme rests
    // on this function computing real SHA-256.
    EXPECT_EQ(
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        cov::sha256_hex(""));
    EXPECT_EQ(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        cov::sha256_hex("abc"));
    // 56 bytes: the padding no longer fits the final block, so the tail is
    // two blocks — the branch a wrong implementation gets wrong first.
    EXPECT_EQ(
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        cov::sha256_hex(
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
}

// One chunk name really can carry two different sources in one process —
// tests/unit/test_pack_transfer_errors.cpp regenerates a cached pack and
// mounts both generations under packs/org.test.regen/scripts/a.lua. While
// the chunk name alone was the identity, the second declaration silently
// erased the first, one sidecar survived by rename() ordering, and the
// first generation's digest in runtime_only_lua.txt was unreachable — a
// reviewed-holes list with a dead entry.
TEST(ScriptCoverage, one_chunk_name_carrying_two_sources_keeps_both)
{
    cov::ScopedRecording recording;
    const std::string saved_dir = cov::output_dir();

    const std::string gen1 = "local first = 1\nreturn first\n";
    const std::string gen2 = "local second = 2\nreturn second\n";
    cov::declare_pack_source("packs/regen/scripts/a.lua", gen1, "cache-v1");
    cov::declare_pack_source("packs/regen/scripts/a.lua", gen2, "cache-v2");
    // Re-declaring identical bytes still folds onto one record.
    cov::declare_pack_source("packs/regen/scripts/a.lua", gen1, "cache-v1");

    int for_chunk = 0;
    bool saw_gen1 = false, saw_gen2 = false;
    for (const cov::PackSourceRecord& r : cov::pack_sources()) {
        if (r.chunk != "packs/regen/scripts/a.lua")
            continue;
        for_chunk++;
        EXPECT_EQ(cov::sha256_hex(r.source), r.digest);
        saw_gen1 |= (r.source == gen1);
        saw_gen2 |= (r.source == gen2);
    }
    EXPECT_EQ(2, for_chunk)
        << "two generations, two records — never an overwrite";
    EXPECT_TRUE(saw_gen1);
    EXPECT_TRUE(saw_gen2);

    // And the dump makes both observable: two S records, two sidecars.
    const std::filesystem::path dir =
        make_unique_temp_dir("og_lua_cov_regen_dump_");
    ASSERT_FALSE(dir.empty());
    cov::set_output_dir_for_testing(dir.string());
    cov::flush_to_output_dir();
    cov::set_output_dir_for_testing(saved_dir);

    std::string text;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".luacov") {
            std::ifstream in(entry.path());
            std::stringstream body;
            body << in.rdbuf();
            text = body.str();
        }
    }
    int s_records = 0;
    std::size_t at = 0;
    while ((at = text.find("S\tpacks/regen/scripts/a.lua\t", at)) !=
           std::string::npos) {
        s_records++;
        at++;
    }
    EXPECT_EQ(2, s_records) << text;
    EXPECT_NE(std::string::npos, text.find(cov::sha256_hex(gen1))) << text;
    EXPECT_NE(std::string::npos, text.find(cov::sha256_hex(gen2))) << text;

    int sidecars = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(dir / "sources")) {
        std::ifstream in(entry.path());
        std::stringstream body;
        body << in.rdbuf();
        EXPECT_TRUE(body.str() == gen1 || body.str() == gen2)
            << entry.path();
        sidecars++;
    }
    EXPECT_EQ(2, sidecars) << "one content-addressed sidecar per generation";
    std::filesystem::remove_all(dir);
}

// THE GENERATION BINDING. When one chunk name carries two sources in one
// process, every hit is stored under the generation whose COMPILED CODE
// executed — each compile binds its prototype tree to the (chunk, digest)
// it was compiled from, and the hook resolves the executing prototype. The
// report used to receive digest-less hits and guess: it credited a hit to
// EVERY declared generation whose grid contained the line, which let
// execution of one generation's bytes mark the other generation's lines
// covered. A hit belongs to exactly one (chunk, digest).
TEST(ScriptCoverage, hits_bind_to_the_generation_that_executed)
{
    cov::ScopedRecording recording;
    const std::string saved_dir = cov::output_dir();
    ScriptHost host;

    // Both generations put code on lines 1-2, so digest-less pooling (or
    // grid-based guessing) would cross-credit them; gen2 alone has line 3.
    const std::string gen1 = "local a = 1\nog.log('gen1', a)\n";
    const std::string gen2 =
        "local b = 2\nlocal c = 3\nog.log('gen2', b + c)\n";
    const char* chunk = "packs/org.test.gen/scripts/a.lua";
    const std::string d1 = cov::sha256_hex(gen1);
    const std::string d2 = cov::sha256_hex(gen2);

    cov::declare_pack_source(chunk, gen1, "cache-v1");
    ASSERT_TRUE(host.run_chunk(chunk, gen1));
    // gen2 is declared, then compiled: ITS prototypes bind to its digest,
    // and hits inside them accumulate there.
    cov::declare_pack_source(chunk, gen2, "cache-v2");
    ASSERT_TRUE(host.run_chunk(chunk, gen2));
    ASSERT_TRUE(host.run_chunk(chunk, gen2));

    std::map<std::pair<std::string, int>, std::uint64_t> by_gen_line;
    for (const cov::LineHit& h : cov::line_hits()) {
        if (h.chunk == chunk) {
            EXPECT_FALSE(h.digest.empty())
                << "declared chunks never record digest-less hits (line "
                << h.line << ")";
            by_gen_line[{h.digest, h.line}] += h.count;
        }
    }
    EXPECT_EQ(1u, by_gen_line[std::make_pair(d1, 1)]);
    EXPECT_EQ(1u, by_gen_line[std::make_pair(d1, 2)]);
    EXPECT_EQ(0u, by_gen_line[std::make_pair(d1, 3)])
        << "gen1 has no line 3; a hit here would be cross-credit";
    EXPECT_EQ(2u, by_gen_line[std::make_pair(d2, 1)]) << "gen2 ran twice";
    EXPECT_EQ(2u, by_gen_line[std::make_pair(d2, 2)]);
    EXPECT_EQ(2u, by_gen_line[std::make_pair(d2, 3)]);

    // The function metric binds identically: each generation's main chunk is
    // its own (digest, span 0..0) record.
    std::map<std::string, std::uint64_t> chunk_fn_hits;
    for (const cov::FunctionRecord& r : cov::function_records()) {
        if (r.chunk == chunk && r.line_defined == 0)
            chunk_fn_hits[r.digest] += r.body_hits;
    }
    EXPECT_GT(chunk_fn_hits[d1], 0u);
    EXPECT_GT(chunk_fn_hits[d2], 0u);
    EXPECT_EQ(2u, chunk_fn_hits.size())
        << "one main-chunk record per generation, none digest-less";

    // And the dump carries the binding: every L record for this chunk names
    // its generation, so the report never guesses.
    const std::filesystem::path dir =
        make_unique_temp_dir("og_lua_cov_gen_dump_");
    ASSERT_FALSE(dir.empty());
    cov::set_output_dir_for_testing(dir.string());
    cov::flush_to_output_dir();
    cov::set_output_dir_for_testing(saved_dir);
    std::string text;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".luacov") {
            std::ifstream in(entry.path());
            std::stringstream body;
            body << in.rdbuf();
            text = body.str();
        }
    }
    EXPECT_NE(std::string::npos,
              text.find("L\t" + std::string(chunk) + "\t" + d1 + "\t1\t1"))
        << text;
    EXPECT_NE(std::string::npos,
              text.find("L\t" + std::string(chunk) + "\t" + d2 + "\t3\t2"))
        << text;
    EXPECT_EQ(std::string::npos,
              text.find("L\t" + std::string(chunk) + "\t-\t"))
        << "no digest-less hits for a declared chunk: " << text;
    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// The report is the gate: attack reproductions, pinned failing-closed
// ---------------------------------------------------------------------------
//
// Each test forges the raw dumps an attack (or a broken collection run)
// would leave behind and requires scripts/coverage/coverage_report.py to
// reject them. They shell out to the REAL script against the REAL repository
// inventory: the property under test belongs to the pipeline, not to a
// reimplementation of it.

namespace {

std::filesystem::path test_exe_dir()
{
#if defined(__linux__)
    std::error_code ec;
    const std::filesystem::path exe =
        std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : exe.parent_path();
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size + 1, '\0');
    if (size == 0 || _NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};
    return std::filesystem::path(buffer.c_str()).parent_path();
#else
    return {};
#endif
}

std::filesystem::path find_repo_root()
{
    std::filesystem::path dir = test_exe_dir();
    for (int hops = 0; hops < 8 && !dir.empty(); hops++) {
        if (std::filesystem::exists(
                dir / "scripts" / "coverage" / "coverage_report.py"))
            return dir;
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir)
            break;
        dir = parent;
    }
    return {};
}

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_text_file(const std::filesystem::path& path,
                     const std::string& body)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good()) << path;
    out << body;
}

struct ReportRun {
    int exit_code = -1;
    std::string output;        // report stdout+stderr
    std::string summary_json;  // summary.json, when one was written
};

// "lines_hit" of one file's row in summary.json's lua_files array. -1 when
// the row is absent. json.dumps preserves insertion order, so the value
// directly after the matching "path" belongs to that row.
int summary_lines_hit(const std::string& json, const std::string& path)
{
    const std::size_t at = json.find("\"path\": \"" + path + "\"");
    if (at == std::string::npos)
        return -1;
    const std::string key = "\"lines_hit\": ";
    const std::size_t k = json.find(key, at);
    if (k == std::string::npos)
        return -1;
    return std::atoi(json.c_str() + k + key.size());
}

// The per-half line verdict inside summary.json's "gate" object.
std::string gate_line_verdict(const std::string& json, const std::string& half)
{
    const std::size_t gate = json.find("\"gate\": {");
    if (gate == std::string::npos)
        return "<no gate>";
    const std::size_t at = json.find("\"" + half + "\": {", gate);
    if (at == std::string::npos)
        return "<no half>";
    const std::string key = "\"line\": \"";
    const std::size_t k = json.find(key, at);
    if (k == std::string::npos)
        return "<no line>";
    const std::size_t start = k + key.size();
    return json.substr(start, json.find('"', start) - start);
}

class CoverageReportGate : public ::testing::Test {
protected:
    void SetUp() override
    {
        repo_ = find_repo_root();
        ASSERT_FALSE(repo_.empty())
            << "cannot locate the repository from the test binary; these "
               "tests drive scripts/coverage/coverage_report.py in-tree";
        lines_tool_ = test_exe_dir() / "og_lua_lines";
        ASSERT_TRUE(std::filesystem::exists(lines_tool_))
            << lines_tool_ << " must be built beside the test binaries "
                              "(target og_lua_lines)";
        // mkdtemp, never a name derived from the test: two concurrent runs
        // of this binary with a shared temp dir each remove_all()'d the
        // scratch the other was reading.
        scratch_ = make_unique_temp_dir(
            std::string("og_cov_gate_") +
            ::testing::UnitTest::GetInstance()->current_test_info()->name() +
            "_");
        ASSERT_FALSE(scratch_.empty());
        std::filesystem::create_directories(scratch_ / "raw" / "sources");
    }

    void TearDown() override { std::filesystem::remove_all(scratch_); }

    struct RealSource {
        std::string chunk;   // repo-relative path, used as the chunk name
        std::string bytes;
        std::string digest;
        cov::SourceFacts facts;
    };

    // A real inventory entry, with the grid the oracle computes for it —
    // forged dumps built from this are self-consistent by construction.
    RealSource load_real(const std::string& rel)
    {
        RealSource out;
        out.chunk = rel;
        out.bytes = read_text_file(repo_ / rel);
        EXPECT_FALSE(out.bytes.empty()) << rel;
        out.digest = cov::sha256_hex(out.bytes);
        out.facts = cov::source_facts(out.bytes, rel);
        EXPECT_TRUE(out.facts.ok) << out.facts.error;
        return out;
    }

    // S record declaring `bytes` for `chunk`, sidecar written for real.
    std::string forge_s_record(const std::string& chunk,
                               const std::string& bytes)
    {
        const std::string digest = cov::sha256_hex(bytes);
        const std::string name = "sidecar-" + digest + ".lua";
        write_text_file(scratch_ / "raw" / "sources" / name, bytes);
        return "S\t" + chunk + "\t" + name + "\t" + digest + "\tforged\n";
    }

    // L and F records covering every line and prototype of `src`, recorded
    // under `chunk` (which need not be src.chunk — that is the attack) and
    // bound to `generation` — the digest the recorder stamps on every hit,
    // or "-" for "no declared source was active". Defaults to the digest of
    // the bytes the hits actually describe, which is what an honest recorder
    // writes.
    static std::string forge_full_hits(const std::string& chunk,
                                       const RealSource& src,
                                       const std::string& generation = "")
    {
        const std::string gen =
            generation.empty() ? src.digest : generation;
        std::string out;
        for (const int line : src.facts.lines)
            out += "L\t" + chunk + "\t" + gen + "\t" + std::to_string(line) +
                   "\t1\n";
        for (const cov::FunctionSpan& span : src.facts.functions) {
            out += "F\t" + chunk + "\t" + gen + "\t" +
                   std::to_string(span.line_defined) + "\t" +
                   std::to_string(span.last_line_defined) + "\t1\t\n";
        }
        return out;
    }

    void forge_dump(const std::string& file_name, const std::string& program,
                    const std::string& body)
    {
        write_text_file(scratch_ / "raw" / file_name,
                        "# openglad-lua-coverage 5\nP\t" + program + "\n" +
                            body);
    }

    // The population check runs against a manifest each test forges to
    // match its dumps, so the committed one cannot mask the defect under
    // test (nor fail it for unrelated reasons).
    std::string manifest_args(const std::vector<std::string>& names)
    {
        std::string body;
        for (const std::string& name : names)
            body += name + "\n";
        write_text_file(scratch_ / "manifest.txt", body);
        return " --processes-manifest '" + (scratch_ / "manifest.txt").string() +
               "'";
    }

    // A fixtures ledger the test controls. Forged runs observe none of the
    // committed runtime_only_lua.txt digests, and an unobserved fixture is
    // an ERROR by design — so every test supplies its own ledger (usually
    // empty) and the one that tests the staleness rule supplies a stale one.
    std::string fixtures_args(const std::string& body = "")
    {
        write_text_file(scratch_ / "fixtures.txt", body);
        return " --fixture-digests '" + (scratch_ / "fixtures.txt").string() +
               "'";
    }

    // Git-tracked src/**/*.cpp paths, repo-relative — what the C++
    // completeness check runs against. Cached: the answer cannot change
    // within one test process.
    const std::vector<std::string>& tracked_src_cpp()
    {
        static std::vector<std::string> cached = [this] {
            std::vector<std::string> out;
            const std::filesystem::path list =
                scratch_ / "tracked_src_cpp.txt";
            const std::string cmd = "git -C '" + repo_.string() +
                                    "' ls-files -- src > '" + list.string() +
                                    "'";
            if (std::system(cmd.c_str()) != 0)
                return out;
            std::ifstream in(list);
            std::string line;
            while (std::getline(in, line)) {
                if (line.size() > 4 &&
                    line.compare(line.size() - 4, 4, ".cpp") == 0)
                    out.push_back(line);
            }
            return out;
        }();
        return cached;
    }

    // Paths named in the committed cpp_excluded.txt (the TUs the coverage
    // build genuinely cannot measure).
    std::vector<std::string> committed_cpp_exclusions()
    {
        std::vector<std::string> out;
        std::ifstream in(repo_ / "scripts" / "coverage" / "cpp_excluded.txt");
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            out.push_back(line.substr(0, line.find(' ')));
        }
        return out;
    }

    // A tracefile that measures every tracked src/ TU except the committed
    // exclusions — the fully-covered, fully-accounted C++ half a forged run
    // needs so the completeness check judges only what a test perturbs.
    // `omit` names TUs to leave out; `extra_paths` appends records for paths
    // that are not tracked at all. `lines_per_file` scales the forged line
    // count (all covered).
    std::string forge_cpp_tracefile(
        const std::vector<std::string>& omit = {},
        const std::vector<std::string>& extra_paths = {},
        int lines_per_file = 3)
    {
        const std::vector<std::string> excluded = committed_cpp_exclusions();
        std::string trace;
        int fn = 0;
        auto add_record = [&](const std::string& path) {
            trace += "TN:forged\nSF:" + path + "\n";
            const std::string name = "f" + std::to_string(fn++);
            trace += "FN:1," + name + "\nFNDA:1," + name + "\n";
            for (int line = 1; line <= lines_per_file; line++)
                trace += "DA:" + std::to_string(line) + ",1\n";
            trace += "end_of_record\n";
        };
        for (const std::string& path : tracked_src_cpp()) {
            if (std::find(omit.begin(), omit.end(), path) != omit.end())
                continue;
            if (std::find(excluded.begin(), excluded.end(), path) !=
                excluded.end())
                continue;
            add_record(path);
        }
        for (const std::string& path : extra_paths)
            add_record(path);
        const std::filesystem::path file = scratch_ / "cpp.info.forged";
        write_text_file(file, trace);
        return " --cpp-tracefile '" + file.string() + "'";
    }

    ReportRun run_report(const std::string& extra_args)
    {
        ReportRun result;
        const std::filesystem::path out_dir = scratch_ / "report-out";
        std::filesystem::remove_all(out_dir);
        const std::filesystem::path log = scratch_ / "report.log";
        // OPENGLAD_LUA_COVERAGE is scrubbed so that, under an ARMED suite
        // run, the og_lua_lines child (which embeds the recorder) does not
        // write its own dump into the real collection directory.
        const std::string cmd =
            "env -u OPENGLAD_LUA_COVERAGE python3 '" +
            (repo_ / "scripts" / "coverage" / "coverage_report.py").string() +
            "' --repo-root '" + repo_.string() + "' --lua-raw-dir '" +
            (scratch_ / "raw").string() + "' --lines-tool '" +
            lines_tool_.string() + "' --output-dir '" + out_dir.string() +
            "'" + extra_args + " > '" + log.string() + "' 2>&1";
        const int rc = std::system(cmd.c_str());
#if defined(WIFEXITED)
        result.exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#else
        result.exit_code = rc;
#endif
        result.output = read_text_file(log);
        result.summary_json = read_text_file(out_dir / "summary.json");
        return result;
    }

    std::filesystem::path repo_, scratch_, lines_tool_;
};

}  // namespace

// N1's demonstrated attack: a dump declares a 2-line stub as the source of
// packs/core/families/living-17-archmage.lua while recording hits on the real file's
// lines. While declarations pooled across dumps by chunk name, those hits
// were scored against the real bytes some other dump declared and archmage
// went 362/382 -> 382/382 without a line of it running.
TEST_F(CoverageReportGate, hits_bind_to_the_bytes_their_own_process_declared)
{
    const RealSource real = load_real("packs/core/families/living-17-archmage.lua");
    const std::string stub = "local stub = 1\nreturn stub\n";
    forge_dump("lua-attack.luacov", "og_unit_script",
               forge_s_record(real.chunk, stub) +
                   forge_full_hits(real.chunk, real,
                                   cov::sha256_hex(stub)));

    const ReportRun run =
        run_report(manifest_args({"og_unit_script"}) + fixtures_args());
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find("not repository content"))
        << run.output;
    EXPECT_NE(std::string::npos, run.output.find(cov::sha256_hex(stub)))
        << "the unknown digest is named: " << run.output;
    EXPECT_EQ(0, summary_lines_hit(run.summary_json, real.chunk))
        << "archmage must gain nothing from hits bound to a stub";
}

// The same defect from the other side: a dump records hits on a pack chunk
// it never declared. "Did SOMEONE declare this chunk" used to be the
// question; the answer must come from the dump whose hits they are.
TEST_F(CoverageReportGate, a_dump_cannot_borrow_another_dumps_declaration)
{
    const RealSource real = load_real("packs/core/families/living-00-soldier.lua");
    ASSERT_FALSE(real.facts.lines.empty());
    // Process A declares the real bytes and hits ONE line.
    forge_dump("lua-a.luacov", "procA",
               forge_s_record(real.chunk, real.bytes) + "L\t" + real.chunk +
                   "\t" + real.digest + "\t" +
                   std::to_string(real.facts.lines.front()) + "\t1\n");
    // Process B hits every line — under the right digest, even — but never
    // declares the source itself.
    forge_dump("lua-b.luacov", "procB", forge_full_hits(real.chunk, real));

    const ReportRun run =
        run_report(manifest_args({"procA", "procB"}) + fixtures_args());
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find("own process never declared"))
        << run.output;
    EXPECT_EQ(1, summary_lines_hit(run.summary_json, real.chunk))
        << "only the declaring process's one hit may count";
}

TEST_F(CoverageReportGate, sidecar_paths_cannot_escape_the_dump_directory)
{
    const std::string zeros(64, '0');
    forge_dump("lua-escape.luacov", "og_unit_script",
               "S\tpacks/x/scripts/a.lua\t/etc/hostname\t" + zeros +
                   "\tforged\n"
                   "S\tpacks/x/scripts/b.lua\t../../outside.lua\t" +
                   zeros + "\tforged\n");

    const ReportRun run =
        run_report(manifest_args({"og_unit_script"}) + fixtures_args());
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find("'/etc/hostname' is a path, not a bare file "
                              "name"))
        << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find("'../../outside.lua' is a path, not a bare "
                              "file name"))
        << run.output;
}

TEST_F(CoverageReportGate, sidecar_bytes_must_hash_to_the_declared_digest)
{
    const RealSource real = load_real("packs/core/families/living-00-soldier.lua");
    // Claim soldier's digest, ship a stub's bytes in the sidecar: the report
    // must refuse rather than score soldier's grid against stub content (or
    // vice versa).
    const std::string stub = "local liar = true\nreturn liar\n";
    const std::string name = "sidecar-" + real.digest + ".lua";
    write_text_file(scratch_ / "raw" / "sources" / name, stub);
    forge_dump("lua-mismatch.luacov", "og_unit_script",
               "S\t" + real.chunk + "\t" + name + "\t" + real.digest +
                   "\tforged\n" + forge_full_hits(real.chunk, real));

    const ReportRun run =
        run_report(manifest_args({"og_unit_script"}) + fixtures_args());
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find("does not hash to the digest the dump "
                              "declared"))
        << run.output;
    EXPECT_EQ(0, summary_lines_hit(run.summary_json, real.chunk))
        << "a declaration that fails verification must credit nothing";
}

TEST_F(CoverageReportGate, an_unknown_dump_format_version_is_an_error)
{
    // A version-3 dump against a version-4 reader once meant every S record
    // was silently skipped and the numerator collapsed to zero with no
    // message. Version 4 itself is now the refused past: its digest-less
    // L/F records are ambiguous by construction and must not be
    // reinterpreted as anything.
    write_text_file(scratch_ / "raw" / "lua-old.luacov",
                    "# openglad-lua-coverage 4\n"
                    "P\tog_unit_script\n"
                    "L\tpacks/core/families/living-00-soldier.lua\t1\t1\n");

    const ReportRun run =
        run_report(manifest_args({"og_unit_script"}) + fixtures_args());
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find("not a '# openglad-lua-coverage 5' dump"))
        << run.output;
}

// N2: --cpp-tracefile /dev/null used to read as "C++ 0/0, n/a", the union
// collapsed onto the Lua half, and the gate passed having measured no C++.
TEST_F(CoverageReportGate, an_empty_cpp_half_is_an_error_not_a_smaller_union)
{
    const RealSource real = load_real("packs/core/families/living-00-soldier.lua");
    forge_dump("lua-good.luacov", "og_unit_script",
               forge_s_record(real.chunk, real.bytes) +
                   forge_full_hits(real.chunk, real));

    const ReportRun run = run_report(manifest_args({"og_unit_script"}) +
                                     fixtures_args() +
                                     " --cpp-tracefile /dev/null");
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find(
                  "the C++ half was requested but measured nothing"))
        << run.output;
}

// F: a partial suite kept the union above the bar while the Lua half sat at
// 94.50%. The bar applies to each half alone AND to the union.
TEST_F(CoverageReportGate, each_half_must_meet_the_bar_not_only_the_union)
{
    // Lua: one fully-covered file out of the whole inventory — far below
    // 95% for the half.
    const RealSource real = load_real("packs/core/families/living-00-soldier.lua");
    forge_dump("lua-good.luacov", "og_unit_script",
               forge_s_record(real.chunk, real.bytes) +
                   forge_full_hits(real.chunk, real));

    // C++: every tracked TU present, enormous and fully covered, so the
    // UNION clears the line bar on C++ slack alone (and the completeness
    // check has nothing to say — this test is about the bars).
    ASSERT_FALSE(tracked_src_cpp().empty());
    const std::string cpp_args =
        forge_cpp_tracefile({}, {}, /*lines_per_file=*/1000);

    const ReportRun run =
        run_report(manifest_args({"og_unit_script"}) + fixtures_args() +
                   cpp_args);
    EXPECT_EQ("PASS", gate_line_verdict(run.summary_json, "combined"))
        << "the forged union really is above the line bar — that is the "
           "attack: "
        << run.summary_json.substr(0, 400);
    EXPECT_EQ("PASS", gate_line_verdict(run.summary_json, "cpp"));
    EXPECT_EQ("FAIL", gate_line_verdict(run.summary_json, "lua"))
        << "the Lua half alone must be held to the same bar";
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos, run.summary_json.find("\"status\": \"FAIL\""))
        << run.summary_json.substr(0, 400);
}

// A suite that quietly loses recorder processes does not look broken, it
// looks like slightly lower coverage; with slack above the bar it looks
// like nothing at all. The population is structural, not a percentage.
TEST_F(CoverageReportGate, the_recorder_process_population_is_checked)
{
    const RealSource real = load_real("packs/core/families/living-00-soldier.lua");
    forge_dump("lua-good.luacov", "rogue_process",
               forge_s_record(real.chunk, real.bytes) +
                   forge_full_hits(real.chunk, real));

    // The manifest expects one process that never wrote a dump, and the
    // dump that exists came from a process the manifest never heard of.
    const ReportRun run =
        run_report(manifest_args({"og_test_parity"}) + fixtures_args());
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos, run.output.find("wrote no dump"))
        << run.output;
    EXPECT_NE(std::string::npos, run.output.find("og_test_parity"))
        << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find("rogue_process"))
        << "an unlisted process is named so it gets added: " << run.output;
}

// P5-A, the druid-override shape. One chunk name, TWO declared generations
// (both real repository files, so neither declaration is itself an error),
// and hits from only one of them. While the report credited a hit to every
// declared generation whose grid contained the line, execution of one file's
// bytes marked the other 85% covered — a never-loaded byte-variant of core
// druid.lua went from an honest 0/101+0/4 gate FAIL to a PASS. Hits carry
// their generation now, so the other generation gets exactly nothing.
TEST_F(CoverageReportGate, hits_credit_only_the_generation_that_executed)
{
    const RealSource ran = load_real("packs/core/families/living-13-druid.lua");
    const RealSource bystander = load_real("packs/core/families/living-00-soldier.lua");
    // The two grids overlap heavily by line NUMBER (both files put code on
    // most early lines) — that overlap is what the deleted guess credited.
    forge_dump("lua-twogen.luacov", "og_unit_script",
               forge_s_record(ran.chunk, ran.bytes) +
                   forge_s_record(ran.chunk, bystander.bytes) +
                   forge_full_hits(ran.chunk, ran));

    const ReportRun run =
        run_report(manifest_args({"og_unit_script"}) + fixtures_args());
    EXPECT_EQ(1, run.exit_code)
        << "most of the inventory is still uncovered: " << run.output;
    EXPECT_EQ(static_cast<int>(ran.facts.lines.size()),
              summary_lines_hit(run.summary_json, ran.chunk))
        << "the generation that executed gets full credit";
    EXPECT_EQ(0, summary_lines_hit(run.summary_json, bystander.chunk))
        << "the declared-but-never-executed generation must gain NOTHING "
           "from the other generation's hits";
    EXPECT_EQ(std::string::npos,
              run.output.find("credited to every declared generation"))
        << "the multi-generation guess (and its warning) must be gone: "
        << run.output;
}

// P5-G: runtime_only_lua.txt is a reviewed ledger of holes in the metric.
// An entry no run observes any more is rot — its test is gone, or something
// stopped making those bytes reachable — and rot in the ledger used to be a
// warning nobody reads. It fails the run now.
TEST_F(CoverageReportGate, a_stale_fixture_digest_is_an_error)
{
    const RealSource real = load_real("packs/core/families/living-00-soldier.lua");
    forge_dump("lua-good.luacov", "og_unit_script",
               forge_s_record(real.chunk, real.bytes) +
                   forge_full_hits(real.chunk, real));

    const std::string stale(64, 'a');
    const ReportRun run = run_report(
        manifest_args({"og_unit_script"}) +
        fixtures_args(stale + "  some test that no longer exists\n"));
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos, run.output.find("never observed"))
        << run.output;
    EXPECT_NE(std::string::npos, run.output.find(stale.substr(0, 12)))
        << "the stale digest is named: " << run.output;
    EXPECT_NE(std::string::npos, run.output.find("ERROR"))
        << "staleness is an error, not a warning: " << run.output;
}

// P5-J: a gcov record for a file that no longer exists is dropped — but
// never in silence, and in CI never at all. In a fresh build directory
// (--strict-cpp, what coverage.yml passes) there is no such thing as a
// legitimately stale record, so any drop is an error; locally it is a
// warning that recommends a clean build. Deleting a badly-covered src/ file
// without a clean rebuild must not quietly inflate the number.
TEST_F(CoverageReportGate, a_dropped_cpp_record_is_an_error_under_strict_cpp)
{
    const RealSource real = load_real("packs/core/families/living-00-soldier.lua");
    forge_dump("lua-good.luacov", "og_unit_script",
               forge_s_record(real.chunk, real.bytes) +
                   forge_full_hits(real.chunk, real));
    ASSERT_FALSE(tracked_src_cpp().empty());
    const std::string cpp_args = forge_cpp_tracefile(
        {}, {"src/gameplay/families/no_such_family.cpp"});

    const ReportRun strict = run_report(manifest_args({"og_unit_script"}) +
                                        fixtures_args() + cpp_args +
                                        " --strict-cpp");
    EXPECT_EQ(1, strict.exit_code) << strict.output;
    EXPECT_NE(std::string::npos,
              strict.output.find("ERROR: 1 C++ record(s)"))
        << strict.output;
    EXPECT_NE(std::string::npos, strict.output.find("no_such_family.cpp"))
        << strict.output;

    const ReportRun loose = run_report(manifest_args({"og_unit_script"}) +
                                       fixtures_args() + cpp_args);
    EXPECT_NE(std::string::npos,
              loose.output.find("warning: dropped 1 C++ record(s)"))
        << loose.output;
    EXPECT_NE(std::string::npos, loose.output.find("clean build"))
        << "the warning tells the reader how to get rid of the litter: "
        << loose.output;
    EXPECT_EQ(std::string::npos, loose.output.find("ERROR: 1 C++ record(s)"))
        << "without --strict-cpp an untracked stale record is a warning: "
        << loose.output;
}

// P5-F: the C++ denominator is whatever the build emitted .gcno for, so a
// tracked TU that silently stops being compiled just vanishes. Every
// git-tracked src/**/*.cpp must be measured or listed — with a reason — in
// scripts/coverage/cpp_excluded.txt.
TEST_F(CoverageReportGate, an_absent_tracked_tu_must_be_listed_or_measured)
{
    const RealSource real = load_real("packs/core/families/living-00-soldier.lua");
    forge_dump("lua-good.luacov", "og_unit_script",
               forge_s_record(real.chunk, real.bytes) +
                   forge_full_hits(real.chunk, real));
    ASSERT_FALSE(tracked_src_cpp().empty());
    // Leave one stable, definitely-tracked TU out of the forged data.
    const std::string missing = "src/gameplay/walker.cpp";
    ASSERT_NE(tracked_src_cpp().end(),
              std::find(tracked_src_cpp().begin(), tracked_src_cpp().end(),
                        missing));
    const std::string cpp_args = forge_cpp_tracefile({missing});

    const ReportRun run = run_report(manifest_args({"og_unit_script"}) +
                                     fixtures_args() + cpp_args);
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find("absent from the gcov data and not listed"))
        << run.output;
    EXPECT_NE(std::string::npos, run.output.find(missing)) << run.output;
    EXPECT_NE(std::string::npos, run.output.find("cpp_excluded.txt"))
        << "the fix is named: " << run.output;

    // The converse: with every TU measured or committed-excluded, the
    // completeness check is silent.
    const ReportRun clean = run_report(manifest_args({"og_unit_script"}) +
                                       fixtures_args() +
                                       forge_cpp_tracefile());
    EXPECT_EQ(std::string::npos,
              clean.output.find("absent from the gcov data"))
        << clean.output;
}

// ...and the exclusion ledger itself must not rot: a listed TU the data DOES
// measure, or one git no longer tracks, is an error until the line is
// deleted — the same rule the Lua fixture ledger lives under.
TEST_F(CoverageReportGate, a_stale_cpp_exclusion_is_an_error)
{
    const RealSource real = load_real("packs/core/families/living-00-soldier.lua");
    forge_dump("lua-good.luacov", "og_unit_script",
               forge_s_record(real.chunk, real.bytes) +
                   forge_full_hits(real.chunk, real));
    ASSERT_FALSE(tracked_src_cpp().empty());
    const std::string cpp_args = forge_cpp_tracefile();

    write_text_file(scratch_ / "excluded.txt",
                    "src/gameplay/walker.cpp measured-yet-listed\n"
                    "src/never/was/here.cpp not-even-tracked\n");
    const ReportRun run = run_report(
        manifest_args({"og_unit_script"}) + fixtures_args() + cpp_args +
        " --cpp-excluded '" + (scratch_ / "excluded.txt").string() + "'");
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find("TUs the gcov data DOES measure"))
        << run.output;
    EXPECT_NE(std::string::npos, run.output.find("src/gameplay/walker.cpp"))
        << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find("not git-tracked src/ .cpp files"))
        << run.output;
    EXPECT_NE(std::string::npos, run.output.find("src/never/was/here.cpp"))
        << run.output;

    // A reason is not optional either: the ledger documents WHY each TU is
    // unmeasurable, and a bare path documents nothing.
    write_text_file(scratch_ / "noreason.txt",
                    "src/platform/emscripten/web_touch_bridge.cpp\n");
    const ReportRun bare = run_report(
        manifest_args({"og_unit_script"}) + fixtures_args() + cpp_args +
        " --cpp-excluded '" + (scratch_ / "noreason.txt").string() + "'");
    EXPECT_EQ(1, bare.exit_code) << bare.output;
    EXPECT_NE(std::string::npos,
              bare.output.find("exclusion without a reason"))
        << bare.output;
}

// ---------------------------------------------------------------------------
// Proto-true generation binding (P6-A/P6-B) and the atomic dump (P6-D)
// ---------------------------------------------------------------------------

// P6-A: THE PROBE, pinned. Hits bind to the generation whose compiled code
// is EXECUTING — never to the most recently declared digest. The engine
// sequence this models: the boot mount declares gen1; a GameWorld compiles
// it and keeps closures; a campaign mount re-declares the same chunk as
// gen2; the still-live gen1 closure runs again. Before the Proto-true
// binding, those late hits were stored under gen2 — silently covering lines
// of a source that never ran where the grids overlapped (a dishonest pass),
// and hard-failing the report with an off-grid error where they did not
// (P6-B's false failure). Same defect, both directions.
TEST(ScriptCoverage, stale_closures_credit_their_own_generation_not_the_newest)
{
    cov::ScopedRecording recording;
    ScriptHost host;

    const char* chunk = "packs/org.test.stale/scripts/a.lua";
    // gen1 stores a closure in the shared pack environment; its body (lines
    // 2-3) is what the stale call below must credit to gen1.
    const std::string gen1 =
        "stale = function()\n"        // 1
        "  local x = 1\n"             // 2
        "  og.log('gen1', x)\n"       // 3
        "end\n";                      // 4
    const std::string gen2 = "og.log('gen2 loaded')\n";  // 1
    const std::string d1 = cov::sha256_hex(gen1);
    const std::string d2 = cov::sha256_hex(gen2);

    // Engine order: declare, then compile. Loading gen1 defines `stale` but
    // does not enter its body.
    cov::declare_pack_source(chunk, gen1, "probe/disk");
    ASSERT_TRUE(host.run_chunk(chunk, gen1, "org.test.stale"));
    // The campaign override: the SAME chunk name re-declared and re-compiled
    // with different bytes, while gen1's closure stays alive in the shared
    // environment.
    cov::declare_pack_source(chunk, gen2, "probe/campaign.glad");
    ASSERT_TRUE(host.run_chunk(chunk, gen2, "org.test.stale"));

    // The surviving gen1 closure executes AFTER gen2 became the newest
    // declaration. The trigger chunk is undeclared test Lua.
    ASSERT_TRUE(host.run_chunk("trigger.lua", "stale()\n", "org.test.stale"));

    std::map<std::pair<std::string, int>, std::uint64_t> by_gen_line;
    for (const cov::LineHit& h : cov::line_hits()) {
        if (h.chunk == chunk)
            by_gen_line[{h.digest, h.line}] += h.count;
    }
    EXPECT_EQ(1u, by_gen_line[std::make_pair(d1, 2)])
        << "the stale body's hits belong to gen1, exactly once (the load "
           "defined the function without entering it)";
    EXPECT_EQ(1u, by_gen_line[std::make_pair(d1, 3)]);
    EXPECT_EQ(0u, by_gen_line[std::make_pair(d2, 2)])
        << "gen2 has no code on lines 2-3: a hit here is the off-grid false "
           "failure — and, where grids overlap, the dishonest pass";
    EXPECT_EQ(0u, by_gen_line[std::make_pair(d2, 3)]);

    // The function metric binds the same way: gen1's 1..4 prototype ran,
    // and no phantom 1..4 record appears under gen2.
    bool gen1_body_ran = false;
    bool gen2_phantom_span = false;
    for (const cov::FunctionRecord& r : cov::function_records()) {
        if (r.chunk != chunk)
            continue;
        if (r.digest == d1 && r.line_defined == 1 && r.last_line_defined == 4)
            gen1_body_ran = (r.body_hits > 0);
        if (r.digest == d2 && r.line_defined == 1 && r.last_line_defined == 4)
            gen2_phantom_span = true;
    }
    EXPECT_TRUE(gen1_body_ran);
    EXPECT_FALSE(gen2_phantom_span)
        << "gen2 defines no prototype spanning 1..4";

    // The undeclared trigger chunk stays under the no-generation marker —
    // the fallback path for test Lua is unchanged.
    std::uint64_t trigger_hits = 0;
    bool trigger_no_generation = true;
    for (const cov::LineHit& h : cov::line_hits()) {
        if (h.chunk == "trigger.lua") {
            trigger_hits += h.count;
            trigger_no_generation = trigger_no_generation && h.digest.empty();
        }
    }
    EXPECT_GT(trigger_hits, 0u);
    EXPECT_TRUE(trigger_no_generation);
}

// P6-D: the dump used to be opened with trunc and streamed IN PLACE while
// its own sidecars were already temp+rename — so a SIGKILL mid-flush left a
// torn dump the report rejects as a malformed record, indistinguishable
// from tampering; and the SECOND flush some harnesses perform (explicit
// pre-_exit plus exit-time destructor) re-opened an already-complete dump
// with trunc, recreating the window. The dump now publishes by rename().
// This forks (gtest fast-style death test), kills the child between writing
// the temp file and renaming it, and requires the published path to still
// hold the PREVIOUS complete dump byte-for-byte.
TEST(ScriptCoverage, a_kill_during_publish_leaves_the_previous_complete_dump)
{
    cov::ScopedRecording recording;
    const std::string saved_dir = cov::output_dir();
    ScriptHost host;
    ASSERT_TRUE(host.run_chunk("testfixture/torn_a.lua", kProbeChunk));

    const std::filesystem::path dir = make_unique_temp_dir("og_lua_cov_torn_");
    ASSERT_FALSE(dir.empty());
    cov::set_output_dir_for_testing(dir.string());
    cov::flush_to_output_dir();  // complete dump #1

    std::filesystem::path dump;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".luacov")
            dump = entry.path();
    }
    ASSERT_FALSE(dump.empty());
    const std::string complete = read_text_file(dump);
    ASSERT_NE(std::string::npos, complete.find("# openglad-lua-coverage 5\n"));
    ASSERT_NE(std::string::npos, complete.find("torn_a.lua"));

    // More recorded data, so the interrupted second flush is writing a
    // LONGER file: the torn outcome would have been a prefix of #2, not a
    // stale-but-complete #1.
    ASSERT_TRUE(host.run_chunk("testfixture/torn_b.lua", kProbeChunk));

    EXPECT_EXIT(
        {
            cov::detail::g_between_dump_write_and_publish = [] {
                raise(SIGKILL);
            };
            cov::flush_to_output_dir();
            _exit(0);  // unreachable: the seam killed us mid-publish
        },
        ::testing::KilledBySignal(SIGKILL), "");

    // The kill landed after the temp write and before the rename. The
    // published dump is still #1, byte for byte: old and complete — never
    // torn, never truncated.
    EXPECT_EQ(complete, read_text_file(dump));

    // And a clean re-flush atomically replaces it with an equally-complete
    // successor carrying the new data — the double-flush shape harnesses
    // actually hit.
    cov::flush_to_output_dir();
    const std::string replaced = read_text_file(dump);
    EXPECT_NE(std::string::npos, replaced.find("# openglad-lua-coverage 5\n"));
    EXPECT_NE(std::string::npos, replaced.find("torn_a.lua"));
    EXPECT_NE(std::string::npos, replaced.find("torn_b.lua"));

    cov::set_output_dir_for_testing(saved_dir);
    std::filesystem::remove_all(dir);
}

// P6-B, at the report level: one dump carries a chunk's TWO generations —
// the campaign-override shape, where a mount re-declares a core chunk while
// a world compiled from the old bytes is alive — with hits under BOTH. Each
// generation's hits land on its OWN grid, so the run has no structural
// error. This exact dump shape used to die with "hit(s) on lines the
// declared source has no code on" the first time the recorder credited a
// stale closure's hits to the newest declaration.
TEST_F(CoverageReportGate, two_live_generations_score_cleanly_on_their_own_grids)
{
    const RealSource old_gen = load_real("packs/core/families/living-00-soldier.lua");
    const RealSource new_gen = load_real("packs/core/families/living-14-orc.lua");
    // One chunk name; the override generation is real repository content
    // (exactly as a campaign-embedded script is), so neither declaration is
    // itself an error.
    forge_dump("lua-live-override.luacov", "og_unit_script",
               forge_s_record(old_gen.chunk, old_gen.bytes) +
                   forge_s_record(old_gen.chunk, new_gen.bytes) +
                   forge_full_hits(old_gen.chunk, old_gen) +
                   forge_full_hits(old_gen.chunk, new_gen));

    // Thresholds at zero: two files of the whole inventory are covered, and
    // the property under test is the absence of structural errors, not the
    // percentage.
    const ReportRun run = run_report(
        manifest_args({"og_unit_script"}) + fixtures_args() +
        " --line-threshold 0 --function-threshold 0");
    EXPECT_EQ(0, run.exit_code) << run.output;
    EXPECT_EQ(std::string::npos, run.output.find("no code on"))
        << "stale-generation hits belong on their own grid, not off the "
           "newest one: "
        << run.output;
    EXPECT_EQ(std::string::npos, run.output.find("ERROR")) << run.output;
    EXPECT_EQ(static_cast<int>(old_gen.facts.lines.size()),
              summary_lines_hit(run.summary_json, old_gen.chunk));
    EXPECT_EQ(static_cast<int>(new_gen.facts.lines.size()),
              summary_lines_hit(run.summary_json, new_gen.chunk))
        << "the override generation's hits attribute to its content entry";
}

// P6-F: the engine loads only lowercase ".lua" (src/resources/packs.cpp
// compares the literal bytes), but the inventory used to lowercase suffixes
// — so packs/.../PROBE.LUA entered the denominator as a file nothing could
// ever load: an unfixable red. Inside .glad archives the same typo was
// silently ignored instead. Both paths now match the engine byte-for-byte
// AND surface the case-variant as a named enumeration problem. Runs the
// real scripts/lua_inventory.py over a scratch git repository.
TEST(LuaInventory, a_case_variant_lua_suffix_is_a_problem_not_an_entry)
{
    const std::filesystem::path repo = find_repo_root();
    ASSERT_FALSE(repo.empty());
    const std::filesystem::path scratch =
        make_unique_temp_dir("og_lua_case_");
    ASSERT_FALSE(scratch.empty());
    std::filesystem::create_directories(scratch / "repo");

    write_text_file(scratch / "driver.py", std::string(R"PY(
import pathlib
import subprocess
import sys
import zipfile

sys.path.insert(0, sys.argv[1])  # <repo>/scripts
import lua_inventory

# The scratch repository must not inherit the REAL repository's embedded-Lua
# declarations (they name C++ files that do not exist here, which is its own
# problem class); the raw-string sniffer is not under test.
lua_inventory.embedded_lua_dispositions = lambda *a, **k: {}

root = pathlib.Path(sys.argv[2])
scripts = root / "packs" / "core" / "scripts"
scripts.mkdir(parents=True)
(scripts / "good.lua").write_text("return 1\n")
(scripts / "PROBE.LUA").write_text("return 2\n")
outside = root / "tests"
outside.mkdir()
(outside / "IGNORED.LUA").write_text("return 3\n")
with zipfile.ZipFile(root / "camp.glad", "w") as z:
    z.writestr("packs/z/scripts/member.lua", "return 4\n")
    z.writestr("packs/z/scripts/SHOUT.LUA", "return 5\n")
subprocess.run(["git", "init", "-q"], cwd=root, check=True)

scan = lua_inventory.scan(root)
paths = sorted(s.path for s in scan.sources)
assert "packs/core/scripts/good.lua" in paths, paths
assert "camp.glad!packs/z/scripts/member.lua" in paths, paths
assert not any("PROBE" in p for p in paths), paths       # engine-exact match
assert not any("SHOUT" in p for p in paths), paths
assert not any("IGNORED" in p for p in paths), paths
text = "\n".join(scan.problems)
assert "packs/core/scripts/PROBE.LUA" in text, text      # named, on disk
assert "camp.glad!packs/z/scripts/SHOUT.LUA" in text, text  # named, member
assert "will never load" in text, text
assert "IGNORED" not in text, text  # outside shipped roots: still silent
assert len(scan.problems) == 2, text
print("SUFFIX-CASE-OK")
)PY"));

    const std::filesystem::path log = scratch / "driver.log";
    const std::string cmd =
        "python3 '" + (scratch / "driver.py").string() + "' '" +
        (repo / "scripts").string() + "' '" + (scratch / "repo").string() +
        "' > '" + log.string() + "' 2>&1";
    const int rc = std::system(cmd.c_str());
#if defined(WIFEXITED)
    const int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#else
    const int exit_code = rc;
#endif
    const std::string output = read_text_file(log);
    EXPECT_EQ(0, exit_code) << output;
    EXPECT_NE(std::string::npos, output.find("SUFFIX-CASE-OK")) << output;
    std::filesystem::remove_all(scratch);
}

// P7-A: a compile while the recorder was DISABLED used to skip
// bind_compiled_chunk entirely — neither registering nor scrubbing — so a
// Proto address that died under a declared generation's binding and was
// reused by that compile RESURRECTED the dead generation once recording was
// re-enabled: fresh, undeclared test Lua executed and its hits were credited
// to a digest whose code no longer existed anywhere (the attack measured 181
// such hits with this exact toggle sequence). Production arming is
// process-lifetime, so only the test seam can toggle — but the invariant
// must not depend on that. The registry maintenance is unconditional on the
// compile path now; this repeats the attacker's sequence and requires every
// resulting hit to carry the no-generation marker.
TEST(ScriptCoverage, a_compile_while_disabled_scrubs_stale_proto_bindings)
{
    // Identical sources and equal-length chunk names keep the phase-B
    // compile's allocations the same sizes as the freed phase-A blocks, so
    // the allocator hands the dead addresses straight back (LIFO reuse).
    const std::string body =
        "local function helper_a(x)\n"
        "  local t = 0\n"
        "  for i = 1, x do\n"
        "    t = t + i\n"
        "  end\n"
        "  return t\n"
        "end\n"
        "local function helper_b(x)\n"
        "  local t = 1\n"
        "  for i = 1, x do\n"
        "    t = t * 2\n"
        "  end\n"
        "  return t\n"
        "end\n"
        "function probe_run()\n"
        "  local a = helper_a(9)\n"
        "  local b = helper_b(9)\n"
        "  return a + b\n"
        "end\n"
        "return 0\n";
    const std::string chunk_a = "probe/gen_a.lua";
    const std::string chunk_b = "probe/gen_b.lua";  // same length as chunk_a

    // host2 exists BEFORE host1's prototypes are freed, so host2's own
    // construction cannot consume the freed blocks first.
    auto host1 = std::make_unique<ScriptHost>();
    ScriptHost host2;

    const std::string dead_digest = cov::sha256_hex(body);
    {
        cov::ScopedRecording recording;
        cov::declare_pack_source(chunk_a, body, "p7a-toggle-probe");
        ASSERT_TRUE(host1->run_chunk(chunk_a, body, "env-a"));
        // Prove the declared generation really bound: its own execution
        // recorded hits under (chunk_a, digest).
        bool declared_hit = false;
        for (const auto& hit : cov::line_hits()) {
            declared_hit |=
                (hit.chunk == chunk_a && hit.digest == dead_digest);
        }
        ASSERT_TRUE(declared_hit);
    }  // scoped store discarded; the Proto registry deliberately persists

    // Free every prototype the phase-A binding registered.
    host1.reset();

    // Recorder OFF — the state P7-A exploits: compile the same bytes,
    // undeclared now, over the freed addresses. The old code skipped the
    // registry entirely here.
    const bool was_enabled = cov::enabled();
    cov::set_enabled_for_testing(false);
    ASSERT_TRUE(host2.run_chunk(chunk_b, body, "env-b"));
    cov::set_enabled_for_testing(was_enabled);

    // Recorder back ON: execute the phase-B functions. Every hit must carry
    // the no-generation marker — none may resurrect the dead (chunk_a,
    // digest) generation.
    {
        cov::ScopedRecording recording;
        ASSERT_TRUE(
            host2.run_chunk("probe/runner.lua", "return probe_run()",
                            "env-b"));
        const std::vector<cov::LineHit> hits = cov::line_hits();
        ASSERT_FALSE(hits.empty());
        for (const auto& hit : hits) {
            EXPECT_EQ(std::string(), hit.digest)
                << "hit on " << hit.chunk << ":" << hit.line
                << " was credited to a generation instead of the "
                   "no-generation marker";
            EXPECT_NE(chunk_a, hit.chunk)
                << "a dead generation's chunk resurfaced";
        }
        for (const auto& fn : cov::function_records()) {
            EXPECT_EQ(std::string(), fn.digest)
                << "function record on " << fn.chunk << " spanning ["
                << fn.line_defined << "," << fn.last_line_defined
                << "] was credited to a generation";
        }
    }
}

// P7-B: the engine loads only lowercase ".glad" (the campaign filter and
// builtin restore in src/resources/io/platform_io_common.cpp compare
// case-sensitively, and every mount path is built as "<id>.glad"), but the
// inventory matched the ARCHIVE suffix case-insensitively — the exact
// asymmetry the .lua rule above closed — so a case-variant archive's .lua
// members entered the denominator as entries nothing could ever load: an
// unfixable red, with problem_count still 0. Archive membership is now the
// engine's, byte-for-byte, and the case-variant archive is a named
// enumeration problem wherever it sits. Runs the real
// scripts/lua_inventory.py over a scratch git repository.
TEST(LuaInventory, a_case_variant_glad_suffix_is_a_problem_not_a_denominator)
{
    const std::filesystem::path repo = find_repo_root();
    ASSERT_FALSE(repo.empty());
    const std::filesystem::path scratch =
        make_unique_temp_dir("og_glad_case_");
    ASSERT_FALSE(scratch.empty());
    std::filesystem::create_directories(scratch / "repo");

    write_text_file(scratch / "driver.py", std::string(R"PY(
import pathlib
import subprocess
import sys
import zipfile

sys.path.insert(0, sys.argv[1])  # <repo>/scripts
import lua_inventory

# The scratch repository must not inherit the REAL repository's embedded-Lua
# declarations; the raw-string sniffer is not under test.
lua_inventory.embedded_lua_dispositions = lambda *a, **k: {}

root = pathlib.Path(sys.argv[2])
scripts = root / "packs" / "core" / "scripts"
scripts.mkdir(parents=True)
(scripts / "good.lua").write_text("return 1\n")
with zipfile.ZipFile(root / "camp.glad", "w") as z:
    z.writestr("packs/z/scripts/member.lua", "return 4\n")
builtin = root / "builtin"
builtin.mkdir()
with zipfile.ZipFile(builtin / "PROBE.GLAD", "w") as z:
    z.writestr("packs/p/scripts/hidden.lua", "return 9\n")
with zipfile.ZipFile(root / "probe2.Glad", "w") as z:
    z.writestr("packs/q/scripts/hidden2.lua", "return 8\n")
subprocess.run(["git", "init", "-q"], cwd=root, check=True)

scan = lua_inventory.scan(root)
paths = sorted(s.path for s in scan.sources)
assert "packs/core/scripts/good.lua" in paths, paths
assert "camp.glad!packs/z/scripts/member.lua" in paths, paths  # exact: kept
assert not any("hidden" in p for p in paths), paths  # members: never read
assert not any("PROBE" in p or "probe2" in p for p in paths), paths
text = "\n".join(scan.problems)
assert "builtin/PROBE.GLAD" in text, text          # named, nested location
assert "probe2.Glad" in text, text                 # named, repo root
assert "will never load" in text, text
assert "'.glad'" in text, text
assert len(scan.problems) == 2, text
print("GLAD-CASE-OK")
)PY"));

    const std::filesystem::path log = scratch / "driver.log";
    const std::string cmd =
        "python3 '" + (scratch / "driver.py").string() + "' '" +
        (repo / "scripts").string() + "' '" + (scratch / "repo").string() +
        "' > '" + log.string() + "' 2>&1";
    const int rc = std::system(cmd.c_str());
#if defined(WIFEXITED)
    const int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#else
    const int exit_code = rc;
#endif
    const std::string output = read_text_file(log);
    EXPECT_EQ(0, exit_code) << output;
    EXPECT_NE(std::string::npos, output.find("GLAD-CASE-OK")) << output;
    std::filesystem::remove_all(scratch);
}

// P7-C: a top-level directory that case-folds to a shipped root without
// equaling it (Packs/) was silently excluded — correct membership for the
// gate platform, where PhysFS is case-sensitive and the engine cannot load
// it either, but a case-insensitive dev filesystem (macOS/Windows) WOULD
// load it while the Linux gate never measures it. Membership stays
// case-sensitive (engine-true); the spelling is now a named enumeration
// problem. Same treatment for a case-variant scripts/ segment inside a pack
// (packs/x/Scripts/), which — per the no-narrower-pattern policy — stays a
// denominator entry, one that could never be covered on the gate platform.
// Runs the real scripts/lua_inventory.py over a scratch git repository.
TEST(LuaInventory, a_case_variant_shipped_root_is_a_problem_not_a_silence)
{
    const std::filesystem::path repo = find_repo_root();
    ASSERT_FALSE(repo.empty());
    const std::filesystem::path scratch =
        make_unique_temp_dir("og_root_case_");
    ASSERT_FALSE(scratch.empty());
    std::filesystem::create_directories(scratch / "repo");

    write_text_file(scratch / "driver.py", std::string(R"PY(
import pathlib
import subprocess
import sys

sys.path.insert(0, sys.argv[1])  # <repo>/scripts
import lua_inventory

lua_inventory.embedded_lua_dispositions = lambda *a, **k: {}

root = pathlib.Path(sys.argv[2])

# Case-sensitivity probe: the full matrix needs variant spellings to coexist
# with the real ones, which only a case-sensitive filesystem (the gate
# platform) can hold. On a case-insensitive dev filesystem exercise the
# reduced shape: the variant root alone, with no lowercase twin.
(root / "CaseProbe").mkdir()
case_sensitive = not (root / "caseprobe").exists()
(root / "CaseProbe").rmdir()

if case_sensitive:
    scripts = root / "packs" / "core" / "scripts"
    scripts.mkdir(parents=True)
    (scripts / "good.lua").write_text("return 1\n")
    shadow = root / "Packs" / "core" / "scripts"
    shadow.mkdir(parents=True)
    (shadow / "shadow.lua").write_text("return 2\n")
    docs = root / "Docs" / "modding"
    docs.mkdir(parents=True)
    (docs / "example.lua").write_text("return 3\n")
    seg = root / "packs" / "core" / "Scripts"
    seg.mkdir()
    (seg / "misplaced.lua").write_text("return 4\n")
    other = root / "unrelated"
    other.mkdir()
    (other / "junk.lua").write_text("return 5\n")
    subprocess.run(["git", "init", "-q"], cwd=root, check=True)

    scan = lua_inventory.scan(root)
    paths = sorted(s.path for s in scan.sources)
    assert "packs/core/scripts/good.lua" in paths, paths
    # Engine-true membership: the case-variant roots' files stay OUT...
    assert not any(p.startswith(("Packs/", "Docs/")) for p in paths), paths
    # ...while the case-variant scripts/ segment stays IN (any .lua under a
    # shipped root is an entry — the no-narrower-pattern policy).
    assert "packs/core/Scripts/misplaced.lua" in paths, paths
    assert not any("junk" in p for p in paths), paths
    text = "\n".join(scan.problems)
    assert "Packs/core/scripts/shadow.lua" in text, text
    assert "Docs/modding/example.lua" in text, text
    assert "will not ship" in text, text
    assert "packs/core/Scripts/misplaced.lua" in text, text
    assert "rename the directory" in text, text
    assert "unrelated" not in text, text  # plain non-shipped top: silent
    assert len(scan.problems) == 3, text
else:
    shadow = root / "Packs" / "core" / "scripts"
    shadow.mkdir(parents=True)
    (shadow / "shadow.lua").write_text("return 2\n")
    subprocess.run(["git", "init", "-q"], cwd=root, check=True)

    scan = lua_inventory.scan(root)
    assert not scan.sources, [s.path for s in scan.sources]
    text = "\n".join(scan.problems)
    assert "Packs/core/scripts/shadow.lua" in text, text
    assert "will not ship" in text, text
    assert len(scan.problems) == 1, text
print("ROOT-CASE-OK")
)PY"));

    const std::filesystem::path log = scratch / "driver.log";
    const std::string cmd =
        "python3 '" + (scratch / "driver.py").string() + "' '" +
        (repo / "scripts").string() + "' '" + (scratch / "repo").string() +
        "' > '" + log.string() + "' 2>&1";
    const int rc = std::system(cmd.c_str());
#if defined(WIFEXITED)
    const int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#else
    const int exit_code = rc;
#endif
    const std::string output = read_text_file(log);
    EXPECT_EQ(0, exit_code) << output;
    EXPECT_NE(std::string::npos, output.find("ROOT-CASE-OK")) << output;
    std::filesystem::remove_all(scratch);
}

// ---------------------------------------------------------------------------
// P8-A: precompiled bytecode is refused at every layer
// ---------------------------------------------------------------------------
//
// The demonstrated attack: commit a pack script as STRIPPED Lua bytecode
// instead of text. Pre-fix, both luaL_loadbuffer sites accepted binary
// chunks (default "bt" mode), and a stripped Proto tree keeps every
// function span while carrying no line info — measured at fc39e79a, an
// 11-line 3-function script read as 0 lines / 3 functions with exit 0. The
// file's uncovered lines left the denominator while the 100% function bar
// stayed satisfiable, flipping gate FAIL to PASS on identical logic. Text-
// only loading is also the canonical Lua security posture: lundump does no
// consistency checking, so crafted bytecode is an arbitrary-code vector
// through the sandbox. These tests pin all the layers: the engine refuses
// the chunk, the oracle refuses to grid it, the inventory refuses to admit
// it, and the end-to-end report FAILS rather than shrinking.

TEST(ScriptHostSecurity, a_precompiled_binary_chunk_is_refused_not_run)
{
    const std::string text = kBytecodeProbeSource;
    const std::string bytecode = dump_stripped_bytecode(text);
    ASSERT_FALSE(bytecode.empty());

    ScriptHost host;
    // The text form compiles and runs...
    ASSERT_TRUE(host.run_chunk("textual", text));
    ASSERT_TRUE(host.errors().empty());
    // ...its bytecode is a LOAD ERROR through the normal script-error
    // channel, exactly like a syntax error — never a second way to run.
    EXPECT_FALSE(host.run_chunk("packs/evil/scripts/boss.lua", bytecode));
    ASSERT_EQ(1u, host.errors().size());
    EXPECT_EQ("packs/evil/scripts/boss.lua", host.errors()[0].where);
    EXPECT_NE(std::string::npos,
              host.errors()[0].message.find("binary chunk"))
        << host.errors()[0].message;

    // The BOM-wearing variant is refused too (as unparseable text — the
    // engine never strips a BOM, so it can never reach the binary loader).
    EXPECT_FALSE(host.run_chunk("packs/evil/scripts/bom.lua",
                                "\xef\xbb\xbf" + bytecode));
    ASSERT_EQ(2u, host.errors().size());
}

TEST(ScriptCoverage, source_facts_rejects_bytecode_not_a_truncated_grid)
{
    const std::string text = kBytecodeProbeSource;
    const cov::SourceFacts honest =
        cov::source_facts(text, "packs/core/scripts/boss.lua");
    ASSERT_TRUE(honest.ok) << honest.error;
    ASSERT_GT(honest.lines.size(), 0u);
    ASSERT_GT(honest.functions.size(), 1u);

    // The same logic as bytecode: not a 0-line grid — a NAMED error, the
    // same phrase the inventory uses, so a blob that slipped enumeration
    // still cannot mint a truncated denominator through og_lua_lines.
    const std::string bytecode = dump_stripped_bytecode(text);
    const cov::SourceFacts rejected =
        cov::source_facts(bytecode, "packs/core/scripts/boss.lua");
    EXPECT_FALSE(rejected.ok);
    EXPECT_NE(std::string::npos,
              rejected.error.find("precompiled Lua bytecode"))
        << rejected.error;
    EXPECT_NE(std::string::npos,
              rejected.error.find("packs/core/scripts/boss.lua"))
        << rejected.error;
    EXPECT_NE(std::string::npos, rejected.error.find("commit the .lua text"))
        << rejected.error;
    EXPECT_TRUE(rejected.lines.empty());
    EXPECT_TRUE(rejected.functions.empty());

    // A UTF-8 BOM in front of the signature is the same artifact wearing a
    // disguise and gets the same named answer.
    const cov::SourceFacts bom = cov::source_facts(
        "\xef\xbb\xbf" + bytecode, "packs/core/scripts/boss.lua");
    EXPECT_FALSE(bom.ok);
    EXPECT_NE(std::string::npos, bom.error.find("precompiled Lua bytecode"))
        << bom.error;
}

// Runs the real scripts/lua_inventory.py over a scratch git repository
// holding the full plant matrix: bytecode as an on-disk .lua, as a
// BOM-disguised .lua, as a .glad archive member, and as an embedded
// R"LUA(...)LUA" literal. Each is a named problem; NONE is a denominator
// entry (admitting one would carry the erased line grid the problem exists
// to keep out); a bytecode blob OUTSIDE the shipped roots stays unread and
// unnamed, like every other non-shipped file.
TEST(LuaInventory, precompiled_bytecode_is_a_problem_not_a_denominator_entry)
{
    const std::filesystem::path repo = find_repo_root();
    ASSERT_FALSE(repo.empty());
    const std::filesystem::path scratch =
        make_unique_temp_dir("og_lua_bytecode_");
    ASSERT_FALSE(scratch.empty());
    const std::filesystem::path root = scratch / "repo";
    const std::filesystem::path scripts =
        root / "packs" / "core" / "scripts";
    std::filesystem::create_directories(scripts);

    const std::string bytecode =
        dump_stripped_bytecode(kBytecodeProbeSource);
    ASSERT_FALSE(bytecode.empty());
    write_text_file(scripts / "good.lua", "return 1\n");
    write_text_file(scripts / "evil.lua", bytecode);
    write_text_file(scripts / "evil_bom.lua", "\xef\xbb\xbf" + bytecode);
    std::filesystem::create_directories(root / "tests");
    write_text_file(root / "tests" / "outside.lua", bytecode);
    // The embedded-literal plant only needs the classification prefix (the
    // predicate is the loader's first-byte test); real dump bytes could
    // contain the literal's own ")LUA" terminator by chance.
    write_text_file(root / "src" / "gen.cpp",
                    std::string("const char* k = R\"LUA(") +
                        LUA_SIGNATURE[0] + "Lua fake)LUA\";\n");

    write_text_file(scratch / "driver.py", std::string(R"PY(
import pathlib
import subprocess
import sys
import zipfile

sys.path.insert(0, sys.argv[1])  # <repo>/scripts
import lua_inventory

# The scratch repository must not inherit the REAL repository's embedded-Lua
# declarations; the one scratch C++ file is declared shipped so its literal
# is collected and classified.
lua_inventory.embedded_lua_dispositions = (
    lambda *a, **k: {"src/gen.cpp": "shipped"})

root = pathlib.Path(sys.argv[2])
bytecode = (root / "packs" / "core" / "scripts" / "evil.lua").read_bytes()
assert bytecode.startswith(b"\x1bLua"), bytecode[:8]
with zipfile.ZipFile(root / "camp.glad", "w") as z:
    z.writestr("packs/z/scripts/member.lua", "return 4\n")
    z.writestr("packs/z/scripts/evil_member.lua", bytecode)
subprocess.run(["git", "init", "-q"], cwd=root, check=True)

scan = lua_inventory.scan(root)
paths = sorted(s.path for s in scan.sources)
# The denominator is exactly what the plants left alone.
assert paths == ["camp.glad!packs/z/scripts/member.lua",
                 "packs/core/scripts/good.lua"], paths
text = "\n".join(scan.problems)
assert len(scan.problems) == 4, text
assert "packs/core/scripts/evil.lua" in text, text
assert "packs/core/scripts/evil_bom.lua" in text, text
assert "UTF-8 BOM" in text, text
assert "camp.glad!packs/z/scripts/evil_member.lua" in text, text
assert 'src/gen.cpp:R"LUA"@1' in text, text
assert text.count("recompiled Lua is not shipped source") == 4, text
assert text.count("commit the .lua text") == 4, text
assert "outside.lua" not in text, text  # non-shipped: unread, unnamed
print("BYTECODE-INVENTORY-OK")
)PY"));

    const std::filesystem::path log = scratch / "driver.log";
    const std::string cmd =
        "python3 '" + (scratch / "driver.py").string() + "' '" +
        (repo / "scripts").string() + "' '" + root.string() + "' > '" +
        log.string() + "' 2>&1";
    const int rc = std::system(cmd.c_str());
#if defined(WIFEXITED)
    const int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#else
    const int exit_code = rc;
#endif
    const std::string output = read_text_file(log);
    EXPECT_EQ(0, exit_code) << output;
    EXPECT_NE(std::string::npos, output.find("BYTECODE-INVENTORY-OK"))
        << output;
    std::filesystem::remove_all(scratch);
}

// The end-to-end P8-A shape, against the REAL report over a scratch
// repository: the same collection run PASSES on a text pack script and
// FAILS the moment a bytecode pack script is committed next to it — with
// the denominator unchanged, so the failure can never be the "erased
// denominator" PASS the attack aimed for. (The pre-fix behavior is pinned
// in the banner above: 0 lines, intact spans, exit 0.)
TEST_F(CoverageReportGate, a_bytecode_pack_script_fails_the_gate)
{
    // A scratch repository with one honest, fully covered text script.
    const std::filesystem::path scratch_repo = scratch_ / "repo";
    const std::string warden_rel = "packs/core/scripts/warden.lua";
    const std::string warden_text =
        "local function tick(n)\n"
        "  return n + 1\n"
        "end\n"
        "return tick(1)\n";
    write_text_file(scratch_repo / warden_rel, warden_text);
    {
        const std::string cmd =
            "git init -q '" + scratch_repo.string() + "'";
        ASSERT_EQ(0, std::system(cmd.c_str()));
    }

    RealSource warden;
    warden.chunk = warden_rel;
    warden.bytes = warden_text;
    warden.digest = cov::sha256_hex(warden_text);
    warden.facts = cov::source_facts(warden_text, warden_rel);
    ASSERT_TRUE(warden.facts.ok) << warden.facts.error;
    forge_dump("honest.luacov", "og_forged",
               forge_s_record(warden.chunk, warden.bytes) +
                   forge_full_hits(warden.chunk, warden));
    manifest_args({"og_forged"});
    fixtures_args();

    // The report is driven through a wrapper so the scratch repository does
    // not inherit the REAL repository's embedded-Lua declarations (they
    // name C++ files that do not exist in the scratch tree).
    write_text_file(scratch_ / "driver.py", std::string(R"PY(
import json
import pathlib
import sys

sys.path.insert(0, sys.argv[1])   # <repo>/scripts
sys.path.insert(0, sys.argv[2])   # <repo>/scripts/coverage
import lua_inventory
lua_inventory.embedded_lua_dispositions = lambda *a, **k: {}
import coverage_report

out_dir = sys.argv[6]
sys.argv = ["coverage_report.py",
            "--repo-root", sys.argv[3],
            "--lua-raw-dir", sys.argv[4],
            "--lines-tool", sys.argv[5],
            "--output-dir", out_dir,
            "--processes-manifest", sys.argv[7],
            "--fixture-digests", sys.argv[8]]
rc = coverage_report.main()
summary = json.loads(
    (pathlib.Path(out_dir) / "summary.json").read_text())
print("LUA-LINES-FOUND", summary["lua"]["lines_found"])
print("LUA-FILES", ";".join(f["path"] for f in summary["lua_files"]))
print("REPORT-STATUS", summary["status"])
sys.exit(rc)
)PY"));

    const auto run_driver = [&](const std::string& out_name) -> ReportRun {
        ReportRun result;
        const std::filesystem::path log = scratch_ / (out_name + ".log");
        const std::string cmd =
            "env -u OPENGLAD_LUA_COVERAGE python3 '" +
            (scratch_ / "driver.py").string() + "' '" +
            (repo_ / "scripts").string() + "' '" +
            (repo_ / "scripts" / "coverage").string() + "' '" +
            scratch_repo.string() + "' '" + (scratch_ / "raw").string() +
            "' '" + lines_tool_.string() + "' '" +
            (scratch_ / out_name).string() + "' '" +
            (scratch_ / "manifest.txt").string() + "' '" +
            (scratch_ / "fixtures.txt").string() + "' > '" + log.string() +
            "' 2>&1";
        const int rc = std::system(cmd.c_str());
#if defined(WIFEXITED)
        result.exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#else
        result.exit_code = rc;
#endif
        result.output = read_text_file(log);
        return result;
    };
    const auto lines_found = [](const ReportRun& run) -> int {
        const std::string key = "LUA-LINES-FOUND ";
        const std::size_t at = run.output.find(key);
        return (at == std::string::npos)
                   ? -1
                   : std::atoi(run.output.c_str() + at + key.size());
    };

    // Control: the text-only scratch repository passes.
    const ReportRun before = run_driver("out-before");
    EXPECT_EQ(0, before.exit_code) << before.output;
    EXPECT_NE(std::string::npos, before.output.find("REPORT-STATUS PASS"))
        << before.output;
    EXPECT_NE(std::string::npos, before.output.find(warden_rel))
        << before.output;
    ASSERT_GT(lines_found(before), 0) << before.output;

    // The attack: the same logic, committed as stripped bytecode.
    const std::string boss_rel = "packs/core/scripts/boss.lua";
    write_text_file(scratch_repo / boss_rel,
                    dump_stripped_bytecode(kBytecodeProbeSource));

    const ReportRun after = run_driver("out-after");
    EXPECT_EQ(1, after.exit_code) << after.output;
    EXPECT_NE(std::string::npos, after.output.find("REPORT-STATUS FAIL"))
        << after.output;
    EXPECT_NE(std::string::npos,
              after.output.find("precompiled Lua bytecode"))
        << after.output;
    EXPECT_NE(std::string::npos, after.output.find(boss_rel))
        << after.output;
    // Never the erased-denominator PASS: the denominator is unchanged by
    // the plant (the problem, not a truncated entry, is what the report
    // sees) and the bytecode path is not among the measured files.
    EXPECT_EQ(lines_found(before), lines_found(after)) << after.output;
    const std::size_t files_at = after.output.find("LUA-FILES");
    ASSERT_NE(std::string::npos, files_at);
    EXPECT_EQ(std::string::npos, after.output.find(boss_rel, files_at))
        << "the bytecode plant must not become a measured file";
}

// ---------------------------------------------------------------------------
// Generated API stubs (docs/modding/og-api.d.lua)
// ---------------------------------------------------------------------------
// The stub file is the lua-language-server surface of the og.* API,
// generated from the binding registration tables by
// scripts/modding/gen_api_stubs.py. It lives under a shipped-Lua root, so
// scripts/lua_inventory.py puts it in the coverage denominator like any
// other shipped .lua — and the file is designed for that: annotation-only
// stubs (`---@field`, never `function ... end`), so its whole grid is ONE
// function record (the main chunk) and ONE executable line (`og = {}`),
// both covered by executing the chunk right here.
//
// The shape assertions are the contract, not a snapshot: if a regeneration
// ever emitted real function statements, every one would be a permanently
// uncovered function record and the coverage gate would go red far from
// the cause. Fail HERE instead, naming the rule. Freshness is deliberately
// NOT asserted: the api_stub_check CMake target owns drift and gates
// coverage_report.

TEST(ApiStubs, stub_file_is_annotation_only_and_loads_in_the_sandbox)
{
    // Unit-test WORKING_DIRECTORY is the repo root (og_add_unit_group).
    const char* stub_path = "docs/modding/og-api.d.lua";
    std::ifstream in(stub_path, std::ios::binary);
    ASSERT_TRUE(in.is_open())
        << stub_path
        << " missing; regenerate: python3 scripts/modding/gen_api_stubs.py";
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string source = buf.str();
    ASSERT_EQ(0u, source.rfind("---@meta", 0))
        << "stub file must start with the ---@meta marker";

    const og::script::coverage::SourceFacts facts =
        og::script::coverage::source_facts(source, stub_path);
    ASSERT_TRUE(facts.ok) << facts.error;
    EXPECT_EQ(std::size_t{1}, facts.functions.size())
        << "annotation-only contract: the main chunk must be the only "
           "function span — a `function ... end` stub would enter the "
           "coverage denominator as a permanently uncovered record";
    EXPECT_EQ(std::size_t{1}, facts.lines.size())
        << "annotation-only contract: `og = {}` must be the only "
           "executable line";

    // Loads and runs cleanly under the sandbox: same text-only mode, same
    // environment fence a pack chunk gets. Running it is also what covers
    // the file's main chunk when the coverage recorder is armed — PROVIDED
    // the (chunk, digest) pair is declared first, exactly as registering a
    // pack chunk declares it before any VM can compile it. Declared by hand
    // here because this file is shipped Lua that is not pack content: it
    // lives under docs/ and reaches no registry.
    // Without the declaration the recorder files the hits under the
    // no-generation marker and the shipped file scores 0%, failing the Lua
    // function bar by exactly one record (found the hard way: 163/164).
    og::script::coverage::declare_pack_source(stub_path, source,
                                              "docs/modding");
    ScriptHost host;
    EXPECT_TRUE(host.run_chunk(stub_path, source, "og-api-stubs"))
        << "stub file failed to load";
    EXPECT_TRUE(host.errors().empty());
}

// ---------------------------------------------------------------------------
// og.use — the pack lib module system
// ---------------------------------------------------------------------------
// A pack ships shared helpers as packs/<id>/lib/<name>.lua; the resources
// layer registers them here and every new VM loads each once — eagerly, in
// registration order, before any pack script — memoizing a FROZEN export.
// og.use is pack-relative and load-time-only. These tests drive the whole
// contract through the shared UI WorldScripts instance (no world in
// context), whose rebuild-on-generation behavior is itself part of the
// contract under test.
//
// Fixture chunk names deliberately avoid the "packs/" prefix: the coverage
// recorder treats packs/-named chunks as shipped pack code that must match
// the repository inventory or the runtime-only ledger, and these
// throwaway strings are neither (the real-path fixture in
// test_pack_lua_paths.cpp owns that spelling).

namespace {

class PackLibTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
        og::script::clear_pack_lib_modules();
        og::script::clear_pack_family_chunks();
        og::script::clear_lua_declared_families();
        // active_world_scripts() must serve the shared UI instance, not
        // some world a previous test left in the gameplay context.
        previous_game_ = current_game;
        current_game = nullptr;
    }
    void TearDown() override
    {
        og::script::clear_pack_scripts();
        og::script::clear_pack_lib_modules();
        og::script::clear_pack_family_chunks();
        og::script::clear_lua_declared_families();
        current_game = previous_game_;
    }

    static og::script::WorldScripts& ws()
    {
        return og::script::active_world_scripts();
    }

    static std::string last_error()
    {
        return ws().host().errors().empty()
                   ? std::string("(no script error recorded)")
                   : ws().host().errors().back().message;
    }

    static bool any_error_contains(const std::string& fragment)
    {
        for (const ScriptError& e : ws().host().errors()) {
            if (e.message.find(fragment) != std::string::npos)
                return true;
        }
        return false;
    }

    static void register_module(const char* pack, const char* name,
                                const std::string& source)
    {
        og::script::register_pack_lib_module(
            {pack, name, std::string("libfix_") + pack + "_" + name + ".lua",
             source});
    }

private:
    GameplayContext* previous_game_ = nullptr;
};

}  // namespace

// The registry itself: append order is load order; a duplicate
// (pack_id, name) replaces in place; unregister takes exactly one pack's
// modules; every mutation bumps the generation — except a no-op
// unregister, which must NOT invalidate long-lived hosts.
TEST_F(PackLibTest, registry_roundtrip_order_and_generation)
{
    const unsigned gen0 = og::script::pack_lib_generation();
    register_module("a.pack", "m1", "return { tag = 'a1' }");
    register_module("b.pack", "m1", "return { tag = 'b1' }");
    register_module("a.pack", "m2", "return { tag = 'a2' }");
    EXPECT_EQ(gen0 + 3, og::script::pack_lib_generation());

    ASSERT_EQ(3u, og::script::pack_lib_modules().size());
    EXPECT_EQ("m1", og::script::pack_lib_modules()[0].name);
    EXPECT_EQ("b.pack", og::script::pack_lib_modules()[1].pack_id);
    EXPECT_EQ("m2", og::script::pack_lib_modules()[2].name);

    // Replacement keeps the slot, swaps the source.
    register_module("a.pack", "m1", "return { tag = 'a1-v2' }");
    ASSERT_EQ(3u, og::script::pack_lib_modules().size());
    EXPECT_EQ("return { tag = 'a1-v2' }",
              og::script::pack_lib_modules()[0].source);
    EXPECT_EQ(gen0 + 4, og::script::pack_lib_generation());

    // A pack nobody registered: no change, no generation bump.
    og::script::unregister_pack_lib_modules("ghost.pack");
    EXPECT_EQ(gen0 + 4, og::script::pack_lib_generation());
    EXPECT_EQ(3u, og::script::pack_lib_modules().size());

    og::script::unregister_pack_lib_modules("a.pack");
    ASSERT_EQ(1u, og::script::pack_lib_modules().size());
    EXPECT_EQ("b.pack", og::script::pack_lib_modules()[0].pack_id);
    EXPECT_EQ(gen0 + 5, og::script::pack_lib_generation());

    og::script::clear_pack_lib_modules();
    EXPECT_TRUE(og::script::pack_lib_modules().empty());
    EXPECT_EQ(gen0 + 6, og::script::pack_lib_generation());
}

// Eager loading runs each module ONCE, in registration order, before any
// pack script — and a script binding the export at load time reads it.
TEST_F(PackLibTest, modules_load_eagerly_in_order_before_scripts)
{
    register_module("t.pack", "alpha", "og.log('alpha loaded')\n"
                                       "return { n = 1 }");
    register_module("t.pack", "beta", "og.log('beta loaded')\n"
                                      "return { n = 2 }");
    og::script::register_pack_script(
        {"t.pack", "s.lua",
         "local alpha = og.use('alpha')\n"
         "local beta = og.use('beta')\n"
         "og.log('script sees', alpha.n + beta.n)"});

    const std::vector<std::string>& log = ws().host().log();
    ASSERT_EQ(3u, log.size()) << "each module loads exactly once";
    EXPECT_EQ("alpha loaded", log[0]);
    EXPECT_EQ("beta loaded", log[1]);
    EXPECT_EQ("script sees\t3", log[2]);
    EXPECT_TRUE(ws().host().errors().empty()) << last_error();
}

// A module may og.use a LATER module: it loads on demand inside the
// earlier load, and the eager pass then skips the settled entry instead of
// running it twice.
TEST_F(PackLibTest, on_demand_load_inside_the_eager_pass_settles_once)
{
    register_module("t.pack", "early",
                    "local late = og.use('late')\n"
                    "og.log('early sees', late.v)\n"
                    "return { v = late.v + 1 }");
    register_module("t.pack", "late", "og.log('late loaded')\n"
                                      "return { v = 10 }");
    og::script::register_pack_script(
        {"t.pack", "s.lua", "og.log('sum', og.use('early').v)"});

    const std::vector<std::string>& log = ws().host().log();
    ASSERT_EQ(3u, log.size()) << "'late' must not run a second time";
    EXPECT_EQ("late loaded", log[0]);
    EXPECT_EQ("early sees\t10", log[1]);
    EXPECT_EQ("sum\t11", log[2]);
}

// Exports are frozen: writes to fresh AND existing keys raise, # forwards
// through to an array-shaped export, and the metatable is fenced. The
// freeze is shallow BY DESIGN; R6 forbids nested mutable state, but the
// freeze cannot detect mutable closure upvalues. This test prevents a future
// "deep freeze" from silently changing what modules may do.
TEST_F(PackLibTest, exports_are_frozen_shallow_views)
{
    register_module("t.pack", "consts",
                    "return { 10, 20, 30, cap = 7, inner = { x = 1 } }");
    og::script::register_pack_script(
        {"t.pack", "s.lua",
         "local c = og.use('consts')\n"
         "if #c ~= 3 then\n"
         "  error('__len must forward: got ' .. #c)\n"
         "end\n"
         "if c[2] ~= 20 or c.cap ~= 7 then\n"
         "  error('reads must reach the data')\n"
         "end\n"
         "if getmetatable(c) ~= false then\n"
         "  error('metatable must be fenced')\n"
         "end\n"
         "local ok1 = pcall(function() c.fresh = 1 end)\n"
         "local ok2 = pcall(function() c.cap = 8 end)\n"
         "if ok1 or ok2 then\n"
         "  error('writes must raise on fresh and existing keys')\n"
         "end\n"
         "c.inner.x = 5\n"
         "if c.inner.x ~= 5 then\n"
         "  error('the freeze is shallow by design')\n"
         "end\n"
         "og.log('frozen ok')"});

    ASSERT_FALSE(ws().host().log().empty()) << last_error();
    EXPECT_EQ("frozen ok", ws().host().log().back());
    EXPECT_TRUE(ws().host().errors().empty()) << last_error();
}

// A non-table export (a bare constant) passes through unwrapped — it is
// immutable in Lua already.
TEST_F(PackLibTest, non_table_exports_pass_through)
{
    register_module("t.pack", "answer", "return 42");
    og::script::register_pack_script(
        {"t.pack", "s.lua", "og.log(og.use('answer'))"});
    ASSERT_FALSE(ws().host().log().empty()) << last_error();
    EXPECT_EQ("42", ws().host().log().back());
}

// og.use resolves against the pack whose chunk is loading: two packs each
// see their own 'util', and a name only one pack ships is invisible to the
// other (with the expected-path spelling in the error).
TEST_F(PackLibTest, resolution_is_pack_relative)
{
    register_module("a.pack", "util", "return { tag = 'from a' }");
    register_module("b.pack", "util", "return { tag = 'from b' }");
    og::script::register_pack_script(
        {"a.pack", "sa.lua", "og.log('a reads', og.use('util').tag)"});
    og::script::register_pack_script(
        {"b.pack", "sb.lua", "og.log('b reads', og.use('util').tag)"});
    og::script::register_pack_script(
        {"b.pack", "sc.lua", "og.use('only_in_a')"});

    const std::vector<std::string>& log = ws().host().log();
    ASSERT_GE(log.size(), 2u);
    EXPECT_EQ("a reads\tfrom a", log[0]);
    EXPECT_EQ("b reads\tfrom b", log[1]);
    EXPECT_TRUE(any_error_contains(
        "no module 'only_in_a' in pack 'b.pack' (expected "
        "packs/b.pack/lib/only_in_a.lua)"))
        << last_error();
}

// Load-time-only: a hook that calls og.use at DISPATCH time errors with
// the bind-at-load-time instruction. (Outside pack load there is no pack
// to resolve against, and a dispatch-time og.use would be hidden coupling
// the reader cannot see.)
TEST_F(PackLibTest, og_use_is_load_time_only)
{
    register_module("t.pack", "util", "return { n = 1 }");
    og::script::register_pack_script(
        {"t.pack", "s.lua",
         "og.register_hooks('living', 'core:soldier', {\n"
         "  do_special = function(self)\n"
         "    og.use('util')\n"
         "    return true\n"
         "  end,\n"
         "})"});
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    const std::optional<bool> handled =
        og::script::hooks::do_special(fd, nullptr);
    EXPECT_FALSE(handled.has_value());
    EXPECT_TRUE(any_error_contains(
        "og.use: only callable while a pack chunk loads"))
        << last_error();
}

// A dependency cycle is an error naming the module, not a hang or a stack
// blowout — and both participants latch as failed.
TEST_F(PackLibTest, dependency_cycles_are_detected)
{
    register_module("t.pack", "m1", "local m2 = og.use('m2')\n"
                                    "return { via = m2 }");
    register_module("t.pack", "m2", "local m1 = og.use('m1')\n"
                                    "return { via = m1 }");
    og::script::register_pack_script(
        {"t.pack", "s.lua", "og.use('m1')"});

    EXPECT_TRUE(any_error_contains("circular dependency on module 'm1'"))
        << last_error();
    EXPECT_TRUE(any_error_contains("og.use: module 'm1' failed to load"))
        << "the script's og.use must see the latched failure: "
        << last_error();
}

// A broken module runs ONCE: later og.use calls answer the latched
// failure deterministically instead of re-running the chunk.
TEST_F(PackLibTest, failures_latch_and_do_not_rerun)
{
    register_module("t.pack", "boom", "og.log('boom ran')\n"
                                      "error('kaput')");
    og::script::register_pack_script(
        {"t.pack", "s1.lua", "og.use('boom')"});
    og::script::register_pack_script(
        {"t.pack", "s2.lua", "og.use('boom')"});

    int boom_runs = 0;
    for (const std::string& line : ws().host().log()) {
        if (line == "boom ran")
            boom_runs++;
    }
    EXPECT_EQ(1, boom_runs) << "a failed chunk must never re-run";
    EXPECT_TRUE(any_error_contains("kaput")) << last_error();
    EXPECT_TRUE(any_error_contains("og.use: module 'boom' failed to load"))
        << last_error();
}

// A module must RETURN its exports; falling off the end is an error that
// says so.
TEST_F(PackLibTest, module_returning_nothing_is_an_error)
{
    register_module("t.pack", "silent", "local x = 1");
    og::script::register_pack_script(
        {"t.pack", "s.lua", "og.use('silent')"});
    EXPECT_TRUE(any_error_contains("module returned no exports"))
        << last_error();
    EXPECT_TRUE(any_error_contains("og.use: module 'silent' failed to load"))
        << last_error();
}

// Modules get FRESH isolated environments: chunk-level globals do not leak
// between modules, or from the pack script environment into a module.
TEST_F(PackLibTest, module_environments_are_isolated)
{
    register_module("t.pack", "first", "leaky = 99\n"
                                       "return { ok = true }");
    register_module("t.pack", "second",
                    "return { saw = tostring(leaky) }");
    og::script::register_pack_script(
        {"t.pack", "s.lua",
         "og.log('second saw', og.use('second').saw)"});
    ASSERT_FALSE(ws().host().log().empty()) << last_error();
    EXPECT_EQ("second saw\tnil", ws().host().log().back());
}

// Module chunks run under the same instruction budget as every other
// chunk; a runaway module fails its load and latches.
TEST_F(PackLibTest, modules_are_budget_metered)
{
    register_module("t.pack", "spin", "while true do end");
    og::script::register_pack_script(
        {"t.pack", "s.lua", "og.use('spin')"});
    EXPECT_TRUE(any_error_contains("instruction budget")) << last_error();
    EXPECT_TRUE(any_error_contains("og.use: module 'spin' failed to load"))
        << last_error();
}

// Module chunks are text-only, like every compile in the process: a
// planted binary chunk is refused, never run.
TEST_F(PackLibTest, module_chunks_are_text_only)
{
    register_module("t.pack", "blob", "\x1bLua fake bytecode");
    og::script::register_pack_script(
        {"t.pack", "s.lua", "og.use('blob')"});
    EXPECT_TRUE(any_error_contains("og.use: module 'blob' failed to load"))
        << last_error();
    EXPECT_FALSE(any_error_contains("attempt to call"))
        << "the blob must be refused at compile, not executed: "
        << last_error();
}

// A module-only mutation rebuilds long-lived hosts exactly like a script
// mutation: the combined build generation moves, and the rebuilt VM serves
// the new module.
TEST_F(PackLibTest, module_mutations_rebuild_the_shared_host)
{
    register_module("t.pack", "vals", "return { v = 1 }");
    og::script::register_pack_script(
        {"t.pack", "s.lua", "og.log('v', og.use('vals').v)"});
    ASSERT_FALSE(ws().host().log().empty()) << last_error();
    EXPECT_EQ("v\t1", ws().host().log().back());
    const unsigned built = ws().built_generation();
    EXPECT_EQ(built, og::script::pack_scripts_build_generation());

    // No mutation: the SAME instance answers (no rebuild, log intact).
    EXPECT_EQ(built, ws().built_generation());
    EXPECT_EQ("v\t1", ws().host().log().back());

    // Module-only mutation: generation moves, next access rebuilds, the
    // replayed script reads the replacement export.
    register_module("t.pack", "vals", "return { v = 2 }");
    EXPECT_NE(built, og::script::pack_scripts_build_generation());
    EXPECT_EQ("v\t2", ws().host().log().back());
}
