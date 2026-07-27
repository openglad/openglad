/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_coverage.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
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
// Family behavior is Lua now, so the coverage gate has to measure Lua. These
// pin the recorder's two halves (executed lines, functions whose bodies
// executed), its denominator oracle, and — the load-bearing one — that it is
// completely inert until armed.

namespace cov = og::script::coverage;

namespace {

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

}  // namespace

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
    // report can measure one that has no file in the tree.
    cov::declare_pack_source("packs/inventoried/scripts/x.lua", kProbeChunk,
                             "/somewhere/x.glad");

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        "og_lua_cov_test_dump";
    std::filesystem::remove_all(dir);

    // No output directory configured: the exit flush is a no-op.
    cov::set_output_dir_for_testing("");
    cov::flush_to_output_dir();
    EXPECT_FALSE(std::filesystem::exists(dir));

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

    EXPECT_NE(std::string::npos, text.find("# openglad-lua-coverage 4\n"))
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
    EXPECT_NE(std::string::npos,
              text.find("L\ttestfixture/dump.lua\t14\t1"))
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
    const std::filesystem::path blocker =
        std::filesystem::temp_directory_path() / "og_lua_cov_blocker";
    {
        std::ofstream out(blocker, std::ios::trunc);
        ASSERT_TRUE(out.good());
    }
    EXPECT_FALSE(cov::write_raw_report((blocker / "x.luacov").string()));
    std::filesystem::remove(blocker);
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
        std::filesystem::temp_directory_path() / "og_lua_cov_redirect_a";
    const std::filesystem::path second =
        std::filesystem::temp_directory_path() / "og_lua_cov_redirect_b";
    std::filesystem::remove_all(first);
    std::filesystem::remove_all(second);

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
        std::filesystem::temp_directory_path() / "og_lua_cov_regen_dump";
    std::filesystem::remove_all(dir);
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
        scratch_ = std::filesystem::temp_directory_path() /
                   (std::string("og_cov_gate_") +
                    ::testing::UnitTest::GetInstance()
                        ->current_test_info()
                        ->name());
        std::filesystem::remove_all(scratch_);
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
    // under `chunk` (which need not be src.chunk — that is the attack).
    static std::string forge_full_hits(const std::string& chunk,
                                       const RealSource& src)
    {
        std::string out;
        for (const int line : src.facts.lines)
            out += "L\t" + chunk + "\t" + std::to_string(line) + "\t1\n";
        for (const cov::FunctionSpan& span : src.facts.functions) {
            out += "F\t" + chunk + "\t" + std::to_string(span.line_defined) +
                   "\t" + std::to_string(span.last_line_defined) + "\t1\t\n";
        }
        return out;
    }

    void forge_dump(const std::string& file_name, const std::string& program,
                    const std::string& body)
    {
        write_text_file(scratch_ / "raw" / file_name,
                        "# openglad-lua-coverage 4\nP\t" + program + "\n" +
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
// packs/core/scripts/archmage.lua while recording hits on the real file's
// lines. While declarations pooled across dumps by chunk name, those hits
// were scored against the real bytes some other dump declared and archmage
// went 362/382 -> 382/382 without a line of it running.
TEST_F(CoverageReportGate, hits_bind_to_the_bytes_their_own_process_declared)
{
    const RealSource real = load_real("packs/core/scripts/archmage.lua");
    const std::string stub = "local stub = 1\nreturn stub\n";
    forge_dump("lua-attack.luacov", "og_unit_script",
               forge_s_record(real.chunk, stub) +
                   forge_full_hits(real.chunk, real));

    const ReportRun run =
        run_report(manifest_args({"og_unit_script"}));
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
    const RealSource real = load_real("packs/core/scripts/soldier.lua");
    ASSERT_FALSE(real.facts.lines.empty());
    // Process A declares the real bytes and hits ONE line.
    forge_dump("lua-a.luacov", "procA",
               forge_s_record(real.chunk, real.bytes) + "L\t" + real.chunk +
                   "\t" + std::to_string(real.facts.lines.front()) + "\t1\n");
    // Process B hits every line but declares nothing.
    forge_dump("lua-b.luacov", "procB", forge_full_hits(real.chunk, real));

    const ReportRun run = run_report(manifest_args({"procA", "procB"}));
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

    const ReportRun run = run_report(manifest_args({"og_unit_script"}));
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
    const RealSource real = load_real("packs/core/scripts/soldier.lua");
    // Claim soldier's digest, ship a stub's bytes in the sidecar: the report
    // must refuse rather than score soldier's grid against stub content (or
    // vice versa).
    const std::string stub = "local liar = true\nreturn liar\n";
    const std::string name = "sidecar-" + real.digest + ".lua";
    write_text_file(scratch_ / "raw" / "sources" / name, stub);
    forge_dump("lua-mismatch.luacov", "og_unit_script",
               "S\t" + real.chunk + "\t" + name + "\t" + real.digest +
                   "\tforged\n" + forge_full_hits(real.chunk, real));

    const ReportRun run = run_report(manifest_args({"og_unit_script"}));
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
    // A version-3 dump against this reader once meant every S record was
    // silently skipped and the numerator collapsed to zero with no message.
    write_text_file(scratch_ / "raw" / "lua-old.luacov",
                    "# openglad-lua-coverage 3\n"
                    "P\tog_unit_script\n"
                    "L\tpacks/core/scripts/soldier.lua\t1\t1\n");

    const ReportRun run = run_report(manifest_args({"og_unit_script"}));
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find("not a '# openglad-lua-coverage 4' dump"))
        << run.output;
}

// N2: --cpp-tracefile /dev/null used to read as "C++ 0/0, n/a", the union
// collapsed onto the Lua half, and the gate passed having measured no C++.
TEST_F(CoverageReportGate, an_empty_cpp_half_is_an_error_not_a_smaller_union)
{
    const RealSource real = load_real("packs/core/scripts/soldier.lua");
    forge_dump("lua-good.luacov", "og_unit_script",
               forge_s_record(real.chunk, real.bytes) +
                   forge_full_hits(real.chunk, real));

    const ReportRun run = run_report(manifest_args({"og_unit_script"}) +
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
    const RealSource real = load_real("packs/core/scripts/soldier.lua");
    forge_dump("lua-good.luacov", "og_unit_script",
               forge_s_record(real.chunk, real.bytes) +
                   forge_full_hits(real.chunk, real));

    // C++: enormous and fully covered, so the UNION clears the line bar on
    // C++ slack alone.
    std::string trace = "TN:forged\nSF:src/gameplay/walker.cpp\n";
    for (int fn = 0; fn < 50; fn++) {
        trace += "FN:1,f" + std::to_string(fn) + "\n";
        trace += "FNDA:1,f" + std::to_string(fn) + "\n";
    }
    std::string da;
    da.reserve(16 * 120000);
    for (int line = 1; line <= 120000; line++)
        da += "DA:" + std::to_string(line) + ",1\n";
    trace += da;
    trace += "end_of_record\n";
    write_text_file(scratch_ / "cpp.info.forged", trace);

    const ReportRun run =
        run_report(manifest_args({"og_unit_script"}) + " --cpp-tracefile '" +
                   (scratch_ / "cpp.info.forged").string() + "'");
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
    const RealSource real = load_real("packs/core/scripts/soldier.lua");
    forge_dump("lua-good.luacov", "rogue_process",
               forge_s_record(real.chunk, real.bytes) +
                   forge_full_hits(real.chunk, real));

    // The manifest expects one process that never wrote a dump, and the
    // dump that exists came from a process the manifest never heard of.
    const ReportRun run = run_report(manifest_args({"og_test_parity"}));
    EXPECT_EQ(1, run.exit_code) << run.output;
    EXPECT_NE(std::string::npos, run.output.find("wrote no dump"))
        << run.output;
    EXPECT_NE(std::string::npos, run.output.find("og_test_parity"))
        << run.output;
    EXPECT_NE(std::string::npos,
              run.output.find("rogue_process"))
        << "an unlisted process is named so it gets added: " << run.output;
}
