/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Deterministic pack bindings: og.rand0, og.max/min/clamp/sign, the
// og.combat.* wrappers over core/combat_math.h, ob:add_frozen_stun, and
// the og.C constants pack scripts used to hand-copy.
//
// Each test pins the exact C++ semantic the binding exposes. Expected values
// are computed by the same constexpr
// helpers (or the same RNG member) the binding calls, never re-derived by
// hand. If combat_math.h moves, these tests move with it and the bindings
// cannot silently diverge.

#include <gtest/gtest.h>

#include "../test_game_world_fixture.h"

#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

class ScriptBindingMathTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
        // The rand0 stream-position proofs read the world's real LCG state;
        // make sure no earlier test left a thread-local override installed.
        og::sim::set_sim_random_override(nullptr);
    }
    void TearDown() override { og::script::clear_pack_scripts(); }

    // Install `body` as core:soldier's do_special and dispatch it (the same
    // door the sim uses). nullopt = the hook errored.
    static std::optional<bool> run_do_special(const std::string& body,
                                              walker* self)
    {
        og::script::clear_pack_scripts();
        og::script::register_pack_script(
            {"test.pack", "math_probe.lua",
             "og.register_hooks('living', 'core:soldier', {\n"
             "  do_special = function(self)\n" +
                 body +
                 "\n    return true\n"
                 "  end,\n"
                 "})\n"});
        const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
        EXPECT_NE(nullptr, fd);
        if (fd == nullptr)
            return std::nullopt;
        return og::script::hooks::do_special(fd, self);
    }

    static const std::vector<og::script::ScriptError>& errors()
    {
        return og::script::active_world_scripts().host().errors();
    }

    static std::string last_error()
    {
        return errors().empty() ? std::string("(no script error recorded)")
                                : errors().back().message;
    }

    // The hook must have completed; a Lua-side `error('label')` from one of
    // the generated asserts surfaces here with its label and chunk line.
    static void expect_ran_clean(const std::optional<bool>& handled)
    {
        ASSERT_TRUE(handled.has_value()) << last_error();
        EXPECT_TRUE(*handled);
    }

    static void expect_errored_with(const std::optional<bool>& handled,
                                    const std::string& fragment)
    {
        EXPECT_FALSE(handled.has_value())
            << "expected the hook to error on: " << fragment;
        ASSERT_FALSE(errors().empty()) << "expected a recorded script error";
        EXPECT_NE(std::string::npos, last_error().find(fragment))
            << "message was: " << last_error();
    }

    // One generated Lua assert: `expr` must equal the C++-computed value.
    static std::string lua_expect(const std::string& expr, long long expected,
                                  const std::string& label)
    {
        return "    if " + expr + " ~= " + std::to_string(expected) +
               " then\n"
               "      error('" +
               label + ": got ' .. tostring(" + expr + "))\n"
               "    end\n";
    }
};

// Installs a chosen gameplay context (including none at all) and restores
// whatever was there, even when an ASSERT unwinds the test early.
class ScopedContextOverride {
public:
    explicit ScopedContextOverride(GameplayContext* ctx)
        : previous_(current_game)
    {
        current_game = ctx;
    }
    ~ScopedContextOverride() { current_game = previous_; }
    ScopedContextOverride(const ScopedContextOverride&) = delete;
    ScopedContextOverride& operator=(const ScopedContextOverride&) = delete;

private:
    GameplayContext* previous_;
};

}  // namespace

// ---------------------------------------------------------------------------
// og.rand0
// ---------------------------------------------------------------------------

// The headline contract: n <= 0 answers 0 WITHOUT advancing the stream —
// IRandom::next(0)'s early-out, the thing the guard trios hand-encode. The
// world RNG's LCG state is public, so the proof is direct: bytes in, bytes
// unmoved.
TEST_F(ScriptBindingMathTest, rand0_zero_and_negative_do_not_advance_the_stream)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    tw.world().rng_.state_ = 0xC0FFEEu;
    expect_ran_clean(run_do_special(
        "    if og.rand0(0) ~= 0 then\n"
        "      error('rand0(0) must answer 0')\n"
        "    end\n"
        "    if og.rand0(-12) ~= 0 then\n"
        "      error('rand0(negative) must answer 0')\n"
        "    end",
        self));
    EXPECT_EQ(0xC0FFEEu, tw.world().rng_.state_)
        << "rand0(n <= 0) must not advance the generator";
}

// For n > 0, og.rand0 IS og.rand: same draw, same post-draw state. Proven
// by replaying the identical seed through both spellings.
TEST_F(ScriptBindingMathTest, rand0_positive_is_og_rand_verbatim)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    tw.world().rng_.state_ = 777u;
    expect_ran_clean(run_do_special("    og.log(og.rand0(1000))", self));
    ASSERT_FALSE(og::script::active_world_scripts().host().log().empty());
    const std::string via_rand0 =
        og::script::active_world_scripts().host().log().back();
    const std::uint32_t state_after_rand0 = tw.world().rng_.state_;
    EXPECT_NE(777u, state_after_rand0)
        << "rand0(n > 0) must advance the generator";

    tw.world().rng_.state_ = 777u;
    expect_ran_clean(run_do_special("    og.log(og.rand(1000))", self));
    ASSERT_FALSE(og::script::active_world_scripts().host().log().empty());
    EXPECT_EQ(via_rand0,
              og::script::active_world_scripts().host().log().back())
        << "same seed, same draw";
    EXPECT_EQ(state_after_rand0, tw.world().rng_.state_)
        << "same seed, same post-draw state";
}

// Every rand0 path needs a world — the answer is a property of the world's
// generator, n <= 0 included.
TEST_F(ScriptBindingMathTest, rand0_requires_an_active_world)
{
    GameplayContext worldless;  // .world stays nullptr
    ScopedContextOverride scope(&worldless);
    expect_errored_with(run_do_special("    og.rand0(0)", nullptr),
                        "no active world");
}

// ---------------------------------------------------------------------------
// og.max / og.min / og.clamp / og.sign
// ---------------------------------------------------------------------------

// std::max/std::min exactly: ties answer the FIRST argument, and the winning
// argument comes back unchanged — math.type() makes both visible from Lua.
// The 2^53 probes force Lua's exact mixed integer/float ordering: an
// implementation that compared through doubles would round 2^53+1 onto
// 2^53 and answer the wrong argument.
TEST_F(ScriptBindingMathTest, max_min_follow_std_semantics_exactly)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    expect_ran_clean(run_do_special(
        "    if og.max(2, 3) ~= 3 or og.max(3, 2) ~= 3 then\n"
        "      error('max value')\n"
        "    end\n"
        "    if og.min(2, 3) ~= 2 or og.min(3, 2) ~= 2 then\n"
        "      error('min value')\n"
        "    end\n"
        "    if og.max(-2.5, -7.5) ~= -2.5 then\n"
        "      error('max float')\n"
        "    end\n"
        "    if math.type(og.max(5, 5.0)) ~= 'integer' then\n"
        "      error('max tie must answer the first argument')\n"
        "    end\n"
        "    if math.type(og.max(5.0, 5)) ~= 'float' then\n"
        "      error('max tie must answer the first argument (float first)')\n"
        "    end\n"
        "    if math.type(og.min(5, 5.0)) ~= 'integer' then\n"
        "      error('min tie must answer the first argument')\n"
        "    end\n"
        "    if math.type(og.min(5.0, 5)) ~= 'float' then\n"
        "      error('min tie must answer the first argument (float first)')\n"
        "    end\n"
        "    if math.type(og.max(2, 1.0)) ~= 'integer' then\n"
        "      error('winner subtype must survive')\n"
        "    end\n"
        "    if og.min(9007199254740993, 9007199254740992.0)\n"
        "        ~= 9007199254740992.0 then\n"
        "      error('min 2^53 exactness')\n"
        "    end\n"
        "    if math.type(og.min(9007199254740993, 9007199254740992.0))\n"
        "        ~= 'float' then\n"
        "      error('min 2^53 must answer the float argument')\n"
        "    end\n"
        "    if og.max(9007199254740993, 9007199254740992.0)\n"
        "        ~= 9007199254740993 then\n"
        "      error('max 2^53 exactness')\n"
        "    end",
        self));
}

TEST_F(ScriptBindingMathTest, clamp_follows_std_clamp_exactly)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    expect_ran_clean(run_do_special(
        "    if og.clamp(5, 1, 10) ~= 5 then\n"
        "      error('clamp in range answers v')\n"
        "    end\n"
        "    if og.clamp(-3, 1, 10) ~= 1 then\n"
        "      error('clamp below answers lo')\n"
        "    end\n"
        "    if og.clamp(42, 1, 10) ~= 10 then\n"
        "      error('clamp above answers hi')\n"
        "    end\n"
        "    if math.type(og.clamp(5, 5.0, 6.0)) ~= 'integer' then\n"
        "      error('clamp tie with lo answers v itself')\n"
        "    end\n"
        "    if math.type(og.clamp(7, 1.0, 7.0)) ~= 'integer' then\n"
        "      error('clamp tie with hi answers v itself')\n"
        "    end\n"
        "    if og.clamp(3, 4, 4) ~= 4 or og.clamp(5, 4, 4) ~= 4 then\n"
        "      error('clamp degenerate range')\n"
        "    end",
        self));
}

TEST_F(ScriptBindingMathTest, clamp_empty_range_is_an_error)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    expect_errored_with(run_do_special("    og.clamp(1, 10, 2)", self),
                        "hi < lo");
}

TEST_F(ScriptBindingMathTest, sign_is_total_and_answers_an_integer)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    expect_ran_clean(run_do_special(
        "    if og.sign(7) ~= 1 or og.sign(-7) ~= -1 or og.sign(0) ~= 0 then\n"
        "      error('sign integers')\n"
        "    end\n"
        "    if og.sign(2.5) ~= 1 or og.sign(-2.5) ~= -1 then\n"
        "      error('sign floats')\n"
        "    end\n"
        "    if og.sign(0.0) ~= 0 or og.sign(-0.0) ~= 0 then\n"
        "      error('sign zeros')\n"
        "    end\n"
        "    if og.sign(math.maxinteger) ~= 1\n"
        "        or og.sign(math.mininteger) ~= -1 then\n"
        "      error('sign int64 extremes')\n"
        "    end\n"
        "    if og.sign(math.huge) ~= 1 or og.sign(-math.huge) ~= -1 then\n"
        "      error('sign infinities')\n"
        "    end\n"
        "    if math.type(og.sign(2.5)) ~= 'integer' then\n"
        "      error('sign must answer an integer')\n"
        "    end",
        self));
}

// The shared validator: no string coercion (these functions hand an
// argument back) and no NaN (the sim never produces one; a script that
// conjures one fails at the door on every build).
TEST_F(ScriptBindingMathTest, non_numbers_and_nan_are_refused)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    expect_errored_with(run_do_special("    og.max('5', 1)", self),
                        "number expected");
    expect_errored_with(run_do_special("    og.min(1, nil)", self),
                        "number expected");
    expect_errored_with(run_do_special("    og.clamp(1, {}, 3)", self),
                        "number expected");
    expect_errored_with(run_do_special("    og.sign('x')", self),
                        "number expected");
    expect_errored_with(
        run_do_special("    og.max(math.huge - math.huge, 1)", self), "NaN");
    expect_errored_with(
        run_do_special("    og.sign(math.huge - math.huge)", self), "NaN");
}

// ---------------------------------------------------------------------------
// og.combat.* — each binding answers its constexpr, bit for bit
// ---------------------------------------------------------------------------

// Level sweep for the six single-argument helpers. -1 is included on
// purpose: the bindings pass the argument through unclamped, exactly like
// the C++ helpers they wrap.
TEST_F(ScriptBindingMathTest, og_combat_single_arg_helpers_match_their_constexpr)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    std::string body;
    for (int level = -1; level <= 25; level++) {
        const std::string l = std::to_string(level);
        body += lua_expect("og.combat.yell_radius(" + l + ")",
                           og::combat::yell_radius(level), "yell_radius " + l);
        body += lua_expect("og.combat.bomb_damage(" + l + ")",
                           og::combat::bomb_damage(level), "bomb_damage " + l);
        body += lua_expect("og.combat.glow_bonus(" + l + ")",
                           og::combat::glow_bonus(level), "glow_bonus " + l);
        body += lua_expect("og.combat.druid_faerie_lifetime(" + l + ")",
                           og::combat::druid_faerie_lifetime(level),
                           "druid_faerie_lifetime " + l);
        body += lua_expect("og.combat.skeleton_lifetime(" + l + ")",
                           og::combat::skeleton_lifetime(level),
                           "skeleton_lifetime " + l);
        body += lua_expect("og.combat.ghost_raise_lifetime(" + l + ")",
                           og::combat::ghost_raise_lifetime(level),
                           "ghost_raise_lifetime " + l);
    }
    // The sweep must cross every cap/knee so both sides of each policy are
    // exercised through the binding, not just the linear leg.
    ASSERT_LT(og::combat::yell_radius(25), 160 + 20 * 25);
    ASSERT_LT(og::combat::bomb_damage(25), 15 * 26);
    ASSERT_LT(og::combat::glow_bonus(25), 110 * 25);
    expect_ran_clean(run_do_special(body, self));
}

// The two accumulators, over every policy arm: thaw-immunity discard,
// negative-add-as-zero, plain add, cap crossing, and the monotonic
// already-over-cap answer.
TEST_F(ScriptBindingMathTest, og_combat_accumulators_match_their_constexpr)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    const int stun_cases[][2] = {
        {-5, 40},   // thaw immunity: add discarded, -5 answered unchanged
        {0, 10},    // plain add
        {10, -3},   // negative add counts as 0
        {140, 60},  // crosses kFrozenStunStackCap
        {200, 5},   // already over cap: answered unchanged, never reduced
        {0, 0},     // no-op
    };
    const int cloak_cases[][2] = {
        {0, 100},   // plain gain
        {300, 100}, // crosses kInvisibilityCloakCap
        {400, 10},  // potion-granted overcap: never reduced
        {10, 0},    // no-op
    };

    std::string body;
    for (const auto& c : stun_cases) {
        const std::string args =
            std::to_string(c[0]) + ", " + std::to_string(c[1]);
        body += lua_expect("og.combat.stun_total(" + args + ")",
                           og::combat::stun_total(c[0], c[1]),
                           "stun_total(" + args + ")");
    }
    for (const auto& c : cloak_cases) {
        const std::string args =
            std::to_string(c[0]) + ", " + std::to_string(c[1]);
        body += lua_expect("og.combat.cloak_total(" + args + ")",
                           og::combat::cloak_total(c[0], c[1]),
                           "cloak_total(" + args + ")");
    }
    expect_ran_clean(run_do_special(body, self));
}

// ---------------------------------------------------------------------------
// ob:add_frozen_stun — the fused setter
// ---------------------------------------------------------------------------

// For each starting raw value, the fused verb must land exactly where the
// hand-written orc sequence lands:
//   ob:s_set_frozen_delay(stun_total(ob:s_frozen_delay_raw(), n))
// — including the thaw-immunity case, where the RAW read matters (the
// masked getter would hide the negative and wrongly re-open the add).
TEST_F(ScriptBindingMathTest, add_frozen_stun_is_stun_total_over_raw_then_set)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, self->stats());

    const int cases[][2] = {
        {0, 40},    // fresh victim
        {140, 60},  // crosses the stack cap
        {-12, 40},  // thaw immunity: DISCARDED, raw stays -12
        {20, -5},   // negative add counts as 0
        {200, 5},   // legacy overcap value: unchanged, never reduced
    };
    for (const auto& c : cases) {
        self->stats()->set_frozen_delay(static_cast<short>(c[0]));
        expect_ran_clean(run_do_special(
            "    self:add_frozen_stun(" + std::to_string(c[1]) + ")", self));
        EXPECT_EQ(og::combat::stun_total(c[0], c[1]),
                  self->stats()->frozen_delay_raw())
            << "add_frozen_stun(" << c[1] << ") over raw " << c[0];
    }
}

// ---------------------------------------------------------------------------
// og.C — the constants pack scripts used to hand-copy
// ---------------------------------------------------------------------------

TEST_F(ScriptBindingMathTest, og_C_combat_constants_match_the_header)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    std::string body;
    body += lua_expect("og.C.STARBURST_ADD_CAP", og::combat::kStarburstAddCap,
                       "STARBURST_ADD_CAP");
    body += lua_expect("og.C.MACE_LIFE_CAP", og::combat::kMaceLifeCap,
                       "MACE_LIFE_CAP");
    body += lua_expect("og.C.SPRINKLE_REFRESH_OWNER_LEVEL",
                       og::combat::kSprinkleRefreshOwnerLevel,
                       "SPRINKLE_REFRESH_OWNER_LEVEL");
    body += lua_expect("og.C.SPRINKLE_REFRESH_FLOOR",
                       og::combat::kSprinkleRefreshFloor,
                       "SPRINKLE_REFRESH_FLOOR");
    expect_ran_clean(run_do_special(body, self));
}
