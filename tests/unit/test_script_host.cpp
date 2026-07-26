/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include <openglad/gameplay/script/script_host.h>

#include <cstdint>
#include <string>

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
