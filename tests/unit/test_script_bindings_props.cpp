/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Entity-side pack bindings: the walker property layer
// (__index/__newindex over the SAME registered accessors), the two fused
// verbs — walker:heal_clamped and og.summon_configured — and og.tuning's
// frozen per-family views.
//
// The property layer's one non-obvious rule is pinned hard here: read
// resolution is METHOD-FIRST. `self:busy()` is sugar for `self.busy(self)`,
// so a name that is both a method and a property can only serve one
// spelling — and the shipped corpus calls the method everywhere. A
// property-first draft of this layer broke cleric.lua's
// `self:current_special()` inside this very test binary; these tests exist
// so no future reordering can get that far again.
//
// The fused verbs pin the EXACT op sequences their doc comments promise
// (orc.lua eat-corpse, soldier.lua boomerang); byte-exact parity depends on
// those sequences never drifting.

#include <gtest/gtest.h>

#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

class ScriptBindingPropsTest : public ::testing::Test {
protected:
    // Declared first so it outlives the clears below: the shipped pack
    // scripts, family chunks and tuning this fixture wipes are what the
    // next test in a --gtest_shuffle order expects to find.
    og::test::ScopedPackStoreState pack_store_restore_;

    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
        og::script::clear_all_family_tuning();
    }
    void TearDown() override
    {
        og::script::clear_pack_scripts();
        og::script::clear_all_family_tuning();
    }

    // Registers `body` as core:soldier's do_special WITHOUT dispatching, so
    // a test can dispatch the same registration several times (chunk-env
    // globals survive between dispatches of one registration — the stale-
    // handle and tuning-regeneration tests depend on that).
    static void register_do_special(const std::string& body)
    {
        og::script::clear_pack_scripts();
        og::script::register_pack_script(
            {"test.props", "props_probe.lua",
             "og.register_hooks('living', 'core:soldier', {\n"
             "  do_special = function(self)\n" +
                 body +
                 "\n    return true\n"
                 "  end,\n"
                 "})\n"});
    }

    static std::optional<SpecialResult> dispatch(walker* self)
    {
        const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
        EXPECT_NE(nullptr, fd);
        if (fd == nullptr)
            return std::nullopt;
        return og::script::hooks::do_special(fd, self);
    }

    static std::optional<SpecialResult> run_do_special(const std::string& body,
                                              walker* self)
    {
        register_do_special(body);
        return dispatch(self);
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

    static void expect_ran_clean(const std::optional<SpecialResult>& handled)
    {
        ASSERT_TRUE(handled.has_value()) << last_error();
        EXPECT_TRUE(*handled);
    }

    static void expect_errored_with(const std::optional<SpecialResult>& handled,
                                    const std::string& fragment)
    {
        EXPECT_FALSE(handled.has_value())
            << "expected the hook to error on: " << fragment;
        ASSERT_FALSE(errors().empty()) << "expected a recorded script error";
        EXPECT_NE(std::string::npos, last_error().find(fragment))
            << "message was: " << last_error();
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
// Property layer — reads
// ---------------------------------------------------------------------------

// Every readable property answers exactly what its method spelling answers,
// because it IS the same lua_CFunction — proven by comparing the two
// spellings inside one dispatch, against values set from C++.
TEST_F(ScriptBindingPropsTest, property_reads_equal_their_method_spellings)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->stats()->set_hitpoints(31.5F);
    self->stats()->set_max_hitpoints(64.25F);
    self->stats()->set_magicpoints(12.0F);
    self->stats()->set_max_magicpoints(80.0F);
    self->stats()->set_level(6);
    self->set_team_num(3);

    expect_ran_clean(run_do_special(
        "    if self.hp ~= self:s_hitpoints() or self.hp ~= 31.5 then\n"
        "      error('hp read')\n"
        "    end\n"
        "    if self.max_hp ~= self:s_max_hitpoints() then\n"
        "      error('max_hp read')\n"
        "    end\n"
        "    if self.magicpoints ~= self:s_magicpoints() then\n"
        "      error('magicpoints read')\n"
        "    end\n"
        "    if self.max_magicpoints ~= self:s_max_magicpoints() then\n"
        "      error('max_magicpoints read')\n"
        "    end\n"
        "    if self.level ~= self:s_level() or self.level ~= 6 then\n"
        "      error('level read')\n"
        "    end\n"
        "    if self.team ~= self:team_num() or self.team ~= 3 then\n"
        "      error('team read')\n"
        "    end",
        self));
}

// Method-first resolution, pinned from both sides: a name shadowed by a
// method still reads as the method (so `self:busy()` keeps working and
// `self.busy` is that same function), while an unshadowed property reads as
// a value. Unknown names still answer nil, and a non-string key is a miss
// like any table miss — never an error.
TEST_F(ScriptBindingPropsTest, read_resolution_is_method_first)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    expect_ran_clean(run_do_special(
        "    if type(self.busy) ~= 'function' then\n"
        "      error('busy must stay a method: got ' .. type(self.busy))\n"
        "    end\n"
        "    if self.busy ~= self.set_busy and type(self.set_busy) ~=\n"
        "        'function' then\n"
        "      error('set_busy must stay a method')\n"
        "    end\n"
        "    if self:busy() ~= 0.0 then\n"
        "      error('method call through the closure must still work')\n"
        "    end\n"
        "    if type(self.hp) ~= 'number' then\n"
        "      error('hp must read as a value')\n"
        "    end\n"
        "    if self.no_such_field ~= nil then\n"
        "      error('unknown names must answer nil')\n"
        "    end\n"
        "    if self[3] ~= nil then\n"
        "      error('non-string keys must answer nil')\n"
        "    end",
        self));
}

// ---------------------------------------------------------------------------
// Property layer — writes
// ---------------------------------------------------------------------------

// Writes run the registered setter, narrowing included: team narrows
// through m_set_team_num's unsigned char (260 → 4), busy through
// m_set_busy's float — and a shadowed-READ name (busy, dead) still WRITES
// as a property, because assignment never resolved methods to begin with.
TEST_F(ScriptBindingPropsTest, property_writes_run_the_same_setters)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    expect_ran_clean(run_do_special(
        "    self.hp = 12.5\n"
        "    if self:s_hitpoints() ~= 12.5 then\n"
        "      error('hp write')\n"
        "    end\n"
        "    self.max_hp = 90.0\n"
        "    self.level = 7\n"
        "    self.team = 260\n"
        "    if self:team_num() ~= 4 then\n"
        "      error('team write must narrow unsigned char: got ' ..\n"
        "            self:team_num())\n"
        "    end\n"
        "    self.busy = 8.25\n"
        "    if self:busy() ~= 8.25 then\n"
        "      error('busy write must reach m_set_busy')\n"
        "    end\n"
        "    self.dead = 1\n"
        "    if self:dead() ~= 1 then\n"
        "      error('dead write must reach m_set_dead')\n"
        "    end\n"
        "    self.dead = 0\n"
        "    self.weapons_left = 5\n"
        "    if self:weapons_left() ~= 5 then\n"
        "      error('weapons_left write')\n"
        "    end",
        self));
    EXPECT_EQ(90.0F, self->stats()->max_hitpoints());
    EXPECT_EQ(7, self->stats()->level());
}

TEST_F(ScriptBindingPropsTest, write_to_read_only_property_errors)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    expect_errored_with(run_do_special("    self.xpos = 40", self),
                        "walker property 'xpos' is read-only");
}

TEST_F(ScriptBindingPropsTest, write_to_method_name_errors)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    expect_errored_with(
        run_do_special("    self.death = 1", self),
        "'death' is a walker method, not a writable property");
}

TEST_F(ScriptBindingPropsTest, write_to_unknown_field_errors)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    expect_errored_with(run_do_special("    self.no_such_field = 1", self),
                        "cannot assign unknown walker field 'no_such_field'");
}

// A non-string key can be assigned too; the diagnostic names its type
// instead of a key string.
TEST_F(ScriptBindingPropsTest, write_with_non_string_key_errors)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    expect_errored_with(run_do_special("    self[3] = 1", self),
                        "cannot assign unknown walker field 'number'");
}

// Handle validity is the method path's, verbatim: a stashed handle whose
// entity is gone raises the IDENTICAL "stale or dead entity handle" error
// from a property read, a property write, and a method call — it is the
// same resolve_walker underneath all three.
TEST_F(ScriptBindingPropsTest, stale_handles_raise_the_same_error_text)
{
    TestGameWorld tw;
    walker* victim = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, victim);
    ASSERT_NE(nullptr, self);

    // One registration, three dispatches: stash on the first, then poke the
    // stale handle a different way on each later one. `mode` comes from the
    // chunk env, which persists between dispatches of one registration.
    register_do_special(
        "    if saved == nil then\n"
        "      saved = self\n"
        "      return true\n"
        "    end\n"
        "    mode = (mode or 0) + 1\n"
        "    if mode == 1 then\n"
        "      local _ = saved.hp\n"
        "    elseif mode == 2 then\n"
        "      saved.hp = 1\n"
        "    else\n"
        "      saved:s_hitpoints()\n"
        "    end");
    expect_ran_clean(dispatch(victim));  // stash
    ASSERT_EQ(1, tw.world().remove_ob(victim));

    const char* kStale = "stale or dead entity handle";
    expect_errored_with(dispatch(self), kStale);  // property read
    expect_errored_with(dispatch(self), kStale);  // property write
    expect_errored_with(dispatch(self), kStale);  // method call
}

// ---------------------------------------------------------------------------
// walker:heal_clamped — the orc eat-corpse cluster, fused
// ---------------------------------------------------------------------------

// The documented op sequence, observable end to end: (1) float add of the
// FULL amount, (2) heal marker carrying the int16-narrowed amount — pushed
// even though the clamp follows, (3) clamp to max. With no source, exactly
// one damage number lands (the target's own), matching the corpus
// do_heal_effects(nil, self, ...) call.
TEST_F(ScriptBindingPropsTest, heal_clamped_adds_marks_then_clamps)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->stats()->set_hitpoints(10.0F);
    self->stats()->set_max_hitpoints(25.0F);
    self->damage_numbers.clear();

    expect_ran_clean(run_do_special(
        "    self:heal_clamped(30)\n"
        "    if self:s_hitpoints() ~= 25.0 then\n"
        "      error('must clamp to max: got ' .. self:s_hitpoints())\n"
        "    end",
        self));
    ASSERT_EQ(1u, self->damage_numbers.size())
        << "nil source: exactly the target's own marker";
    EXPECT_EQ(30.0F, self->damage_numbers.back().value)
        << "the marker carries the FULL heal amount, not the clamped delta";
}

// Below max nothing clamps, and the add is the same float op the corpus
// spelled og.fadd(s_hitpoints(), amount): 10.25 + 30 == 40.25 exactly.
TEST_F(ScriptBindingPropsTest, heal_clamped_without_clamp_is_a_float_add)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->stats()->set_max_hitpoints(100.0F);

    expect_ran_clean(run_do_special(
        "    self:s_set_hitpoints(10.25)\n"
        "    self:heal_clamped(30)\n"
        "    if self:s_hitpoints() ~= 40.25 then\n"
        "      error('float add drifted: got ' .. self:s_hitpoints())\n"
        "    end",
        self));
}

// The marker amount narrows through int16 exactly as the corpus og.i16 +
// short parameter did: 65537 wraps to 1 in the marker while the hp add
// receives the full value.
TEST_F(ScriptBindingPropsTest, heal_clamped_marker_narrows_int16)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->stats()->set_hitpoints(1.0F);
    self->stats()->set_max_hitpoints(70000.0F);
    self->damage_numbers.clear();

    expect_ran_clean(run_do_special(
        "    self:heal_clamped(65537)\n"
        "    if self:s_hitpoints() ~= 65538.0 then\n"
        "      error('hp add must receive the full amount')\n"
        "    end",
        self));
    ASSERT_EQ(1u, self->damage_numbers.size());
    EXPECT_EQ(1.0F, self->damage_numbers.back().value)
        << "int16 narrowing must wrap 65537 to 1";
}

// A source walker gets the healer-side marker, exactly like
// do_heal_effects(source, self, n) — one number on each list.
TEST_F(ScriptBindingPropsTest, heal_clamped_with_source_marks_both)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* healer = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, healer);
    self->set_team_num(1);
    healer->set_team_num(2);  // a foe, so the script can find the handle
    self->stats()->set_hitpoints(1.0F);
    self->stats()->set_max_hitpoints(50.0F);
    self->damage_numbers.clear();
    healer->damage_numbers.clear();

    expect_ran_clean(run_do_special(
        "    local other = og.find_near_foe(self)\n"
        "    if not other then\n"
        "      error('fixture: expected to find the healer walker')\n"
        "    end\n"
        "    self:heal_clamped(5, other)",
        self));
    EXPECT_EQ(1u, self->damage_numbers.size());
    EXPECT_EQ(1u, healer->damage_numbers.size())
        << "the source walker carries the healer-side marker";
}

// No world: the verb fails BEFORE its first mutation (R9), so hitpoints
// stay untouched.
TEST_F(ScriptBindingPropsTest, heal_clamped_requires_a_world_before_mutating)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->stats()->set_hitpoints(10.0F);
    self->stats()->set_max_hitpoints(25.0F);

    register_do_special("    self:heal_clamped(5)");
    {
        GameplayContext worldless;  // .world stays nullptr
        ScopedContextOverride scope(&worldless);
        expect_errored_with(dispatch(self), "no active world");
    }
    EXPECT_EQ(10.0F, self->stats()->hitpoints())
        << "a failed heal_clamped must not have touched hitpoints";
}

TEST_F(ScriptBindingPropsTest, heal_clamped_amount_must_be_an_integer)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    expect_errored_with(run_do_special("    self:heal_clamped(1.5)", self),
                        "number has no integer representation");
}

// ---------------------------------------------------------------------------
// og.summon_configured — the soldier boomerang cluster, fused
// ---------------------------------------------------------------------------

// The proof of sequence equivalence: run the corpus spelling (og.summon +
// the five setters, in soldier.lua's exact order) and the fused verb with
// the same inputs, then require every touched field to agree — plus the
// ownership fields summon_entity itself stamps.
TEST_F(ScriptBindingPropsTest, summon_configured_matches_the_corpus_sequence)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->stats()->set_level(3);
    self->set_team_num(2);

    expect_ran_clean(run_do_special(
        "    local fam = og.family_id('fx', 'core:boomerang')\n"
        "    local a = og.summon(self, 'fx', fam)\n"
        "    if not a then\n"
        "      error('corpus-path summon failed')\n"
        "    end\n"
        "    a:set_ani_type(1)\n"
        "    a:set_lifetime(30 + self:s_level() * 12)\n"
        "    a:s_set_hitpoints(og.fadd(a:s_hitpoints(),\n"
        "                              og.fmul(self:s_level(), 12.0)))\n"
        "    a:s_set_max_hitpoints(a:s_hitpoints())\n"
        "    a:set_damage(og.fadd(a:damage(), og.fmul(self:s_level(), 4.0)))\n"
        "    local b = og.summon_configured(self, 'fx', fam, {\n"
        "      ani_type = 1,\n"
        "      lifetime = 30 + self:s_level() * 12,\n"
        "      hp_add = og.fmul(self:s_level(), 12.0),\n"
        "      max_hp_from_hp = true,\n"
        "      damage_add = og.fmul(self:s_level(), 4.0),\n"
        "    })\n"
        "    if not b then\n"
        "      error('fused summon failed')\n"
        "    end\n"
        "    if b:ani_type() ~= a:ani_type() then\n"
        "      error('ani_type differs')\n"
        "    end\n"
        "    if b:lifetime() ~= a:lifetime() then\n"
        "      error('lifetime differs')\n"
        "    end\n"
        "    if b:s_hitpoints() ~= a:s_hitpoints() then\n"
        "      error('hp differs')\n"
        "    end\n"
        "    if b:s_max_hitpoints() ~= a:s_max_hitpoints() then\n"
        "      error('max hp differs')\n"
        "    end\n"
        "    if b:damage() ~= a:damage() then\n"
        "      error('damage differs')\n"
        "    end\n"
        "    if b:team_num() ~= 2 or b:s_level() ~= 3 then\n"
        "      error('summon_entity ownership stamping differs')\n"
        "    end",
        self));
}

// An empty option table is og.summon plus nothing: every field matches a
// bare summon's.
TEST_F(ScriptBindingPropsTest, summon_configured_empty_options_is_bare_summon)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->stats()->set_level(2);

    expect_ran_clean(run_do_special(
        "    local fam = og.family_id('fx', 'core:boomerang')\n"
        "    local a = og.summon(self, 'fx', fam)\n"
        "    local b = og.summon_configured(self, 'fx', fam, {})\n"
        "    if not a or not b then\n"
        "      error('summons failed')\n"
        "    end\n"
        "    if b:ani_type() ~= a:ani_type() or\n"
        "        b:lifetime() ~= a:lifetime() then\n"
        "      error('empty options must configure nothing')\n"
        "    end\n"
        "    if b:s_hitpoints() ~= a:s_hitpoints() or\n"
        "        b:s_max_hitpoints() ~= a:s_max_hitpoints() then\n"
        "      error('empty options must leave stats alone')\n"
        "    end\n"
        "    if b:damage() ~= a:damage() then\n"
        "      error('empty options must leave damage alone')\n"
        "    end",
        self));
}

// max_hp_from_hp = false is a RECOGNIZED key that applies nothing — only a
// truthy value copies hp into max_hp.
TEST_F(ScriptBindingPropsTest, summon_configured_false_max_hp_applies_nothing)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->stats()->set_level(4);

    expect_ran_clean(run_do_special(
        "    local fam = og.family_id('fx', 'core:boomerang')\n"
        "    local a = og.summon(self, 'fx', fam)\n"
        "    local b = og.summon_configured(self, 'fx', fam, {\n"
        "      hp_add = 12.0,\n"
        "      max_hp_from_hp = false,\n"
        "    })\n"
        "    if not a or not b then\n"
        "      error('summons failed')\n"
        "    end\n"
        "    if b:s_hitpoints() ~= og.fadd(a:s_hitpoints(), 12.0) then\n"
        "      error('hp_add must still apply')\n"
        "    end\n"
        "    if b:s_max_hitpoints() ~= a:s_max_hitpoints() then\n"
        "      error('false max_hp_from_hp must not copy hp')\n"
        "    end",
        self));
}

// A typo'd key fails BEFORE the summon (R9: fail before the first sim
// mutation) — the world's object list must not have grown.
TEST_F(ScriptBindingPropsTest, summon_configured_unknown_key_summons_nothing)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    const std::size_t obs_before = tw.world().oblist.size();

    expect_errored_with(
        run_do_special(
            "    og.summon_configured(self, 'fx',\n"
            "        og.family_id('fx', 'core:boomerang'),\n"
            "        { lifetim = 5 })",
            self),
        "unknown option key");
    EXPECT_NE(std::string::npos, last_error().find("max_hp_from_hp"))
        << "the error must list the allowed keys: " << last_error();
    EXPECT_EQ(obs_before, tw.world().oblist.size())
        << "the typo'd call must not have summoned anything";
}

TEST_F(ScriptBindingPropsTest, summon_configured_option_type_errors)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    const char* fam = "og.family_id('fx', 'core:boomerang')";
    expect_errored_with(
        run_do_special("    og.summon_configured(self, 'fx', " +
                           std::string(fam) + ", { ani_type = 'x' })",
                       self),
        "'ani_type' must be an integer");
    expect_errored_with(
        run_do_special("    og.summon_configured(self, 'fx', " +
                           std::string(fam) + ", { lifetime = 1.5 })",
                       self),
        "'lifetime' must be an integer");
    expect_errored_with(
        run_do_special("    og.summon_configured(self, 'fx', " +
                           std::string(fam) + ", { hp_add = 'q' })",
                       self),
        "'hp_add' must be a number");
    expect_errored_with(
        run_do_special("    og.summon_configured(self, 'fx', " +
                           std::string(fam) + ", { damage_add = {} })",
                       self),
        "'damage_add' must be a number");
}

// A summon that fails answers nil exactly like og.summon — no error, no
// options applied. add_ob fails deterministically once the world has no
// entity factory, the same condition that nulls a bare og.summon.
TEST_F(ScriptBindingPropsTest, summon_configured_failed_summon_returns_nil)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    register_do_special(
        "    local b = og.summon_configured(self, 'fx',\n"
        "        og.family_id('fx', 'core:boomerang'), {\n"
        "      lifetime = 5,\n"
        "    })\n"
        "    if b ~= nil then\n"
        "      error('a failed summon must answer nil')\n"
        "    end");
    auto factory = std::move(tw.world().entity_factory);
    tw.world().entity_factory = nullptr;
    expect_ran_clean(dispatch(self));
    tw.world().entity_factory = std::move(factory);
}

// ---------------------------------------------------------------------------
// og.tuning — frozen per-family views
// ---------------------------------------------------------------------------

namespace {

og::script::TuningValue tuning_int(std::int64_t v)
{
    og::script::TuningValue t;
    t.kind = og::script::TuningValue::Kind::Integer;
    t.integer = v;
    return t;
}

og::script::TuningValue tuning_num(double v)
{
    og::script::TuningValue t;
    t.kind = og::script::TuningValue::Kind::Number;
    t.number = v;
    return t;
}

og::script::TuningValue tuning_bool(bool v)
{
    og::script::TuningValue t;
    t.kind = og::script::TuningValue::Kind::Boolean;
    t.boolean = v;
    return t;
}

og::script::TuningValue tuning_str(std::string v)
{
    og::script::TuningValue t;
    t.kind = og::script::TuningValue::Kind::String;
    t.string = std::move(v);
    return t;
}

}  // namespace

// Every value kind arrives with the Lua subtype the author wrote: 7 is an
// integer, 2.5 a float, true a boolean, quoted text a string.
TEST_F(ScriptBindingPropsTest, tuning_serves_every_value_kind)
{
    og::script::TuningMap map;
    map.push_back({"yell_stun", tuning_int(7)});
    map.push_back({"heal_scale", tuning_num(2.5)});
    map.push_back({"eats_corpses", tuning_bool(true)});
    map.push_back({"weapon_tag", tuning_str("knife")});
    og::script::set_family_tuning(Order::Living, FAMILY_SOLDIER,
                                  std::move(map));

    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    expect_ran_clean(run_do_special(
        "    local t = og.tuning(self)\n"
        "    if t.yell_stun ~= 7 or math.type(t.yell_stun) ~= 'integer' then\n"
        "      error('integer kind')\n"
        "    end\n"
        "    if t.heal_scale ~= 2.5 or\n"
        "        math.type(t.heal_scale) ~= 'float' then\n"
        "      error('number kind')\n"
        "    end\n"
        "    if t.eats_corpses ~= true then\n"
        "      error('boolean kind')\n"
        "    end\n"
        "    if t.weapon_tag ~= 'knife' then\n"
        "      error('string kind')\n"
        "    end\n"
        "    if t.absent ~= nil then\n"
        "      error('absent keys answer nil')\n"
        "    end",
        self));
}

// A family that declared no tuning gets an EMPTY frozen table: reads answer
// nil (so scripts can carry defaults), writes still error.
TEST_F(ScriptBindingPropsTest, tuning_undeclared_family_is_empty_and_frozen)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    expect_ran_clean(run_do_special(
        "    local t = og.tuning(self)\n"
        "    if t.anything ~= nil then\n"
        "      error('undeclared family must serve an empty view')\n"
        "    end",
        self));
    expect_errored_with(
        run_do_special("    og.tuning(self).anything = 1", self),
        "attempt to modify a read-only table");
}

TEST_F(ScriptBindingPropsTest, tuning_views_reject_writes)
{
    og::script::TuningMap map;
    map.push_back({"cap", tuning_int(420)});
    og::script::set_family_tuning(Order::Living, FAMILY_SOLDIER,
                                  std::move(map));

    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    expect_errored_with(run_do_special("    og.tuning(self).cap = 1", self),
                        "attempt to modify a read-only table");
    // Assigning an EXISTING key must error too — the proxy shape exists for
    // exactly this case (a metatable on the data table itself would only
    // fence absent keys).
    expect_errored_with(run_do_special("    og.tuning(self).cap = 420", self),
                        "attempt to modify a read-only table");
}

// The per-VM view cache keys on the store's generation: a reinstall (here:
// set_family_tuning between dispatches of ONE VM) must serve the new
// values, while repeated reads within one generation serve the same frozen
// view.
TEST_F(ScriptBindingPropsTest, tuning_cache_follows_the_store_generation)
{
    og::script::TuningMap map;
    map.push_back({"cap", tuning_int(1)});
    og::script::set_family_tuning(Order::Living, FAMILY_SOLDIER,
                                  std::move(map));

    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    register_do_special("    og.log(og.tuning(self).cap)");
    expect_ran_clean(dispatch(self));
    expect_ran_clean(dispatch(self));

    og::script::TuningMap map2;
    map2.push_back({"cap", tuning_int(2)});
    og::script::set_family_tuning(Order::Living, FAMILY_SOLDIER,
                                  std::move(map2));
    expect_ran_clean(dispatch(self));

    const std::vector<std::string>& log =
        og::script::active_world_scripts().host().log();
    ASSERT_GE(log.size(), 3u);
    EXPECT_EQ("1", log[log.size() - 3]);
    EXPECT_EQ("1", log[log.size() - 2]) << "same generation: cached view";
    EXPECT_EQ("2", log.back()) << "bumped generation: rebuilt view";
}

// The store keys on (order, family id) — a weapon family with the same
// numeric id as a living family keeps its own map.
TEST_F(ScriptBindingPropsTest, tuning_is_keyed_per_order_and_family)
{
    og::script::TuningMap living_map;
    living_map.push_back({"who", tuning_str("living")});
    og::script::set_family_tuning(Order::Living, FAMILY_SOLDIER,
                                  std::move(living_map));
    og::script::TuningMap weapon_map;
    weapon_map.push_back({"who", tuning_str("weapon")});
    og::script::set_family_tuning(Order::Weapon,
                                  static_cast<int>(FAMILY_SOLDIER),
                                  std::move(weapon_map));

    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    expect_ran_clean(run_do_special(
        "    if og.tuning(self).who ~= 'living' then\n"
        "      error('living slot must read the living map')\n"
        "    end\n"
        "    local w = og.summon(self, 'weapon', " +
            std::to_string(static_cast<int>(FAMILY_SOLDIER)) +
            ")\n"
            "    if not w then\n"
            "      error('weapon summon failed')\n"
            "    end\n"
            "    if og.tuning(w).who ~= 'weapon' then\n"
            "      error('weapon slot must read the weapon map')\n"
            "    end",
        self));

    // family_tuning() itself distinguishes the slots (the C++-side read the
    // installer uses).
    ASSERT_NE(nullptr,
              og::script::family_tuning(Order::Living, FAMILY_SOLDIER));
    ASSERT_NE(nullptr,
              og::script::family_tuning(Order::Weapon,
                                        static_cast<int>(FAMILY_SOLDIER)));
    EXPECT_EQ(nullptr, og::script::family_tuning(Order::FX, FAMILY_SOLDIER))
        << "an order nobody installed answers null";
}

// og.tuning validates its handle exactly like every other binding.
TEST_F(ScriptBindingPropsTest, tuning_requires_a_valid_handle)
{
    TestGameWorld tw;
    walker* victim = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, victim);
    ASSERT_NE(nullptr, self);

    register_do_special(
        "    if saved == nil then\n"
        "      saved = self\n"
        "      return true\n"
        "    end\n"
        "    og.tuning(saved)");
    expect_ran_clean(dispatch(victim));
    ASSERT_EQ(1, tw.world().remove_ob(victim));
    expect_errored_with(dispatch(self), "stale or dead entity handle");
}

// ---------------------------------------------------------------------------
// og.family_flag — the read-only descriptor view
// ---------------------------------------------------------------------------

// Three of the four flags a script may ask about, each read back for two
// families that DISAGREE on it. Reading the C++ descriptor in the same test
// is what makes this a join and not a transcription: if the binding's
// if-chain ever answered one flag with another's bit, the skeleton/soldier
// pair below would swap and the test would go red.
TEST_F(ScriptBindingPropsTest, family_flag_answers_each_flag_from_the_descriptor)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    const FamilyDescriptor* soldier = get_family_descriptor(FAMILY_SOLDIER);
    const FamilyDescriptor* skeleton = get_family_descriptor(FAMILY_SKELETON);
    const FamilyDescriptor* tower = get_family_descriptor(FAMILY_TOWER1);
    ASSERT_NE(nullptr, soldier);
    ASSERT_NE(nullptr, skeleton);
    ASSERT_NE(nullptr, tower);
    // The pairs really do disagree — otherwise the join below proves nothing.
    ASSERT_NE(soldier->is_undead, skeleton->is_undead);
    ASSERT_NE(soldier->leaves_bloodspot, skeleton->leaves_bloodspot);
    ASSERT_NE(soldier->is_stationary, tower->is_stationary);

    expect_ran_clean(run_do_special(
        "    local function flag(fam, name)\n"
        "      return og.family_flag('living', fam, name) and 1 or 0\n"
        "    end\n"
        "    og.log('undead', flag(" + std::to_string(FAMILY_SOLDIER) +
            ", 'is_undead'), flag(" + std::to_string(FAMILY_SKELETON) +
            ", 'is_undead'))\n"
        "    og.log('blood', flag(" + std::to_string(FAMILY_SOLDIER) +
            ", 'leaves_bloodspot'), flag(" + std::to_string(FAMILY_SKELETON) +
            ", 'leaves_bloodspot'))\n"
        "    og.log('still', flag(" + std::to_string(FAMILY_SOLDIER) +
            ", 'is_stationary'), flag(" + std::to_string(FAMILY_TOWER1) +
            ", 'is_stationary'))\n",
        self));

    const std::vector<std::string>& log =
        og::script::active_world_scripts().host().log();
    ASSERT_EQ(3u, log.size());
    const auto pair = [](bool a, bool b) {
        return std::string(a ? "1" : "0") + "\t" + (b ? "1" : "0");
    };
    EXPECT_EQ("undead\t" + pair(soldier->is_undead, skeleton->is_undead),
              log[0]);
    EXPECT_EQ("blood\t" + pair(soldier->leaves_bloodspot,
                               skeleton->leaves_bloodspot),
              log[1]);
    EXPECT_EQ("still\t" + pair(soldier->is_stationary, tower->is_stationary),
              log[2]);
}

// ---------------------------------------------------------------------------
// The floor-explicit passability probes
// ---------------------------------------------------------------------------

// A pack that plans a jump or a summon onto ANOTHER floor has to be able to
// ask about that floor without moving anything there first. The optional
// fourth argument is that ask, and the point is that it does not answer for
// the prober's own floor: floor 1 below carries a wall where floor 0 is open,
// so a probe that dropped the argument would report "clear" for a spot that
// would wall the summon in.
//
// The OBJECT probe is the documented exception (game_world.cpp: obmap's floor
// band keys off ob->floor(), not the argument), so its arm pins what it
// really promises — the 4-arg form answers its own floor's occupancy, open
// spot against blocked spot.
TEST_F(ScriptBindingPropsTest, an_explicit_floor_probes_that_floor_not_the_probers)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    ASSERT_EQ(2, w.floor_count());

    // Floor 1: grass everywhere but one wall cell, at the grid cell the
    // prober's own floor leaves open.
    constexpr int kTileX = 6;
    constexpr int kTileY = 6;
    const int gw = w.grid.w;
    const int gh = w.grid.h;
    ASSERT_GT(gw, kTileX);
    ASSERT_GT(gh, kTileY);
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) *
                                  static_cast<std::size_t>(gh)];
    std::fill(buf, buf + static_cast<std::size_t>(gw) *
                             static_cast<std::size_t>(gh),
              static_cast<unsigned char>(PIX_GRASS1));
    buf[kTileX + kTileY * gw] = static_cast<unsigned char>(PIX_H_WALL1);
    w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                    static_cast<unsigned char>(gh), buf);
    w.smoother_for_floor(1).set_target(w.grid_for_floor(1));

    constexpr int kProbeX = kTileX * GRID_SIZE;
    constexpr int kProbeY = kTileY * GRID_SIZE;
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->set_floor(0);
    self->setxy(16, 16);

    // A blocker on the prober's OWN floor, well away from the wall cell, for
    // the object arm's blocked half.
    constexpr int kBlockedX = 10 * GRID_SIZE;
    constexpr int kBlockedY = 10 * GRID_SIZE;
    walker* blocker = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, blocker);
    blocker->set_floor(0);
    blocker->setxy(kBlockedX, kBlockedY);

    // The C++ answers this test's Lua must reproduce exactly.
    ASSERT_TRUE(w.query_grid_passable(kProbeX, kProbeY, self));
    ASSERT_FALSE(w.query_grid_passable(kProbeX, kProbeY, self, 1));
    ASSERT_TRUE(w.query_grid_passable(kProbeX, kProbeY, self, 0));
    ASSERT_TRUE(w.query_passable(kProbeX, kProbeY, self));
    ASSERT_FALSE(w.query_passable(kProbeX, kProbeY, self, 1));
    ASSERT_TRUE(w.query_passable(kProbeX, kProbeY, self, 0));
    ASSERT_TRUE(w.query_object_passable(kProbeX, kProbeY, self, 0));
    ASSERT_FALSE(w.query_object_passable(kBlockedX, kBlockedY, self, 0));

    const std::string probe = std::to_string(kProbeX) + ", " +
                              std::to_string(kProbeY) + ", self";
    const std::string blocked = std::to_string(kBlockedX) + ", " +
                                std::to_string(kBlockedY) + ", self";
    expect_ran_clean(run_do_special(
        "    local function b(v) return v and 1 or 0 end\n"
        "    og.log('grid', b(og.query_grid_passable(" + probe + ")),\n"
        "           b(og.query_grid_passable(" + probe + ", 1)),\n"
        "           b(og.query_grid_passable(" + probe + ", 0)))\n"
        "    og.log('both', b(og.query_passable(" + probe + ")),\n"
        "           b(og.query_passable(" + probe + ", 1)),\n"
        "           b(og.query_passable(" + probe + ", 0)))\n"
        "    og.log('object', b(og.query_object_passable(" + probe + ", 0)),\n"
        "           b(og.query_object_passable(" + blocked + ", 0)))\n",
        self));

    // Each triple is: the prober's own floor (open), floor 1 (walled), floor 0
    // named explicitly (open again — so the middle answer is the argument
    // talking and not a probe that simply broke).
    const std::vector<std::string>& log =
        og::script::active_world_scripts().host().log();
    ASSERT_EQ(3u, log.size());
    EXPECT_EQ("grid\t1\t0\t1", log[0]);
    EXPECT_EQ("both\t1\t0\t1", log[1]);
    EXPECT_EQ("object\t1\t0", log[2]);
}

// ---------------------------------------------------------------------------
// New Specials kit verbs (bindings_kit.cpp). Each answer is read back in C++
// against the engine function the binding wraps.
// ---------------------------------------------------------------------------

namespace {

std::size_t obmap_entries(GameWorld& w, walker* ob)
{
    return w.myobmap->walker_to_pos.count(ob);
}

// A copy of the script log: re-registering a script rebuilds the host, so a
// reference into the old log would not survive the next run.
std::vector<std::string> logged()
{
    return og::script::active_world_scripts().host().log();
}

}  // namespace

// og.swap_places(a, b): the two walkers trade positions and floors and both
// stay in the collision table. It probes nothing, so a drumstick lying on
// the spot a walker swaps onto is NOT eaten (a query_passable probe would
// have eaten it). It refuses, moving nobody, for the same walker twice and
// for a hidden walker.
TEST_F(ScriptBindingPropsTest, kit_swap_places_trades_spots_and_floors_and_eats_nothing)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* other = w.add_ob(Order::Living, FAMILY_ORC);
    walker* food = w.add_ob(Order::Treasure, FAMILY_DRUMSTICK);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, other);
    ASSERT_NE(nullptr, food);
    self->setxy(64, 64);
    other->set_floor(1);
    other->setxy(192, 96);
    food->set_floor(1);
    food->setxy(192, 96);
    other->set_team_num(1);
    self->stats()->set_hitpoints(5.0f);
    self->set_foe(other);

    expect_ran_clean(run_do_special(
        "    local function b(v) return v and 1 or 0 end\n"
        "    og.log('same', b(og.swap_places(self, self)))\n"
        "    og.log('swap', b(og.swap_places(self, self:foe())))\n",
        self));
    std::vector<std::string> log = logged();
    ASSERT_EQ(2u, log.size());
    EXPECT_EQ("same\t0", log[0]) << "a walker cannot swap with itself";
    EXPECT_EQ("swap\t1", log[1]);
    EXPECT_EQ(192, self->xpos());
    EXPECT_EQ(96, self->ypos());
    EXPECT_EQ(1, self->floor());
    EXPECT_EQ(64, other->xpos());
    EXPECT_EQ(64, other->ypos());
    EXPECT_EQ(0, other->floor());
    EXPECT_EQ(1u, obmap_entries(w, self));
    EXPECT_EQ(1u, obmap_entries(w, other));
    EXPECT_FALSE(food->dead()) << "the swap must not eat what lies underfoot";
    EXPECT_FLOAT_EQ(5.0f, self->stats()->hitpoints());

    // A hidden walker cannot be swapped: nothing moves.
    other->set_hidden(true);
    expect_ran_clean(run_do_special(
        "    local function b(v) return v and 1 or 0 end\n"
        "    og.log('hidden', b(og.swap_places(self, self:foe())))\n",
        self));
    log = logged();
    ASSERT_EQ(1u, log.size());
    EXPECT_EQ("hidden\t0", log[0]);
    EXPECT_EQ(192, self->xpos());
    EXPECT_EQ(64, other->xpos());
}

// og.line_clear(a, b): GameWorld::clear_sight_line — a wall cell between
// the two walkers blocks it, an open row does not, and walkers on different
// floors never see each other.
TEST_F(ScriptBindingPropsTest, kit_line_clear_is_blocked_by_a_wall_and_by_floors)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    const int gw = w.grid.w;
    const int gh = w.grid.h;
    ASSERT_GT(gw, 12);
    ASSERT_GT(gh, 8);
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) *
                                  static_cast<std::size_t>(gh)];
    std::fill(buf, buf + static_cast<std::size_t>(gw) *
                             static_cast<std::size_t>(gh),
              static_cast<unsigned char>(PIX_GRASS1));
    buf[6 + 2 * gw] = static_cast<unsigned char>(PIX_H_WALL1);  // row 2 only
    w.grid_for_floor(0) = PixieData(1, static_cast<unsigned char>(gw),
                                    static_cast<unsigned char>(gh), buf);

    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* walled = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* open = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* upstairs = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, walled);
    ASSERT_NE(nullptr, open);
    ASSERT_NE(nullptr, upstairs);
    // Cell centres: row 2 crosses the wall at column 6, row 6 is open.
    self->setxy(2 * GRID_SIZE, 2 * GRID_SIZE);
    walled->setxy(10 * GRID_SIZE, 2 * GRID_SIZE);
    open->setxy(2 * GRID_SIZE, 6 * GRID_SIZE);
    upstairs->set_floor(1);
    upstairs->setxy(2 * GRID_SIZE, 4 * GRID_SIZE);
    ASSERT_FALSE(w.clear_sight_line(self, walled));
    ASSERT_TRUE(w.clear_sight_line(self, open));

    self->set_foe(walled);
    self->set_leader(open);
    walled->set_foe(upstairs);
    expect_ran_clean(run_do_special(
        "    local function b(v) return v and 1 or 0 end\n"
        "    local walled = self:foe()\n"
        "    og.log('line', b(og.line_clear(self, walled)),\n"
        "           b(og.line_clear(self, self:leader())),\n"
        "           b(og.line_clear(self, walled:foe())))\n",
        self));
    const std::vector<std::string> log = logged();
    ASSERT_EQ(1u, log.size());
    EXPECT_EQ("line\t0\t1\t0", log[0]);
}

// og.map_size() answers the world's pixel bounds and its floor count.
TEST_F(ScriptBindingPropsTest, kit_map_size_answers_pixel_bounds_and_floors)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(3);
    w.pixmaxx = 1234;
    w.pixmaxy = 567;
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    expect_ran_clean(run_do_special(
        "    local x, y, floors = og.map_size()\n"
        "    og.log('size', x, y, floors)\n",
        self));
    const std::vector<std::string> log = logged();
    ASSERT_EQ(1u, log.size());
    EXPECT_EQ("size\t1234\t567\t3", log[0]);
}

// og.rejoin_obmap(ent) puts a walker a kit took out of the collision table
// back at its spot, and never re-registers a dead one.
TEST_F(ScriptBindingPropsTest, kit_rejoin_obmap_registers_a_live_walker_only)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* corpse = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, corpse);
    self->setxy(64, 64);
    corpse->setxy(128, 64);
    w.myobmap->remove(self);
    w.myobmap->remove(corpse);
    corpse->set_dead(1);
    self->set_foe(corpse);
    ASSERT_EQ(0u, obmap_entries(w, self));
    expect_ran_clean(run_do_special(
        "    og.rejoin_obmap(self)\n"
        "    og.rejoin_obmap(self:foe())\n",
        self));
    EXPECT_EQ(1u, obmap_entries(w, self));
    EXPECT_EQ(0u, obmap_entries(w, corpse)) << "a dead walker stays out";
    bool found = false;
    for (const walker* ob : w.myobmap->obmap_get_list(64, 64, 0))
        found = found || ob == self;
    EXPECT_TRUE(found) << "registered at its own spot";
}

// og.fearless(ent) answers og::sim::fearless: the walker's KIT_FEARLESS mark
// (the banner's aura is pinned in test_kit_seams_small.cpp).
TEST_F(ScriptBindingPropsTest, kit_fearless_answers_the_kit_mark)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    register_do_special(
        "    og.log('fearless', og.fearless(self) and 1 or 0)\n");
    expect_ran_clean(dispatch(self));
    self->set_kit_state(KIT_FEARLESS);
    expect_ran_clean(dispatch(self));
    const std::vector<std::string> log = logged();
    ASSERT_EQ(2u, log.size());
    EXPECT_EQ("fearless\t0", log[0]);
    EXPECT_EQ("fearless\t1", log[1]);
}

// walker:set_hidden takes a boolean or an integer (0 clears, anything else
// sets); a string or a fraction is a script error and leaves the walker as
// it was.
TEST_F(ScriptBindingPropsTest, kit_set_hidden_refuses_a_non_integer)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    expect_ran_clean(run_do_special("    self:set_hidden(true)\n", self));
    EXPECT_TRUE(self->hidden());
    expect_errored_with(run_do_special("    self:set_hidden('x')\n", self),
                        "walker:set_hidden: expected a boolean or 0|1");
    EXPECT_TRUE(self->hidden()) << "a refused call changes nothing";
    expect_errored_with(run_do_special("    self:set_hidden(0.5)\n", self),
                        "walker:set_hidden: expected a boolean or 0|1");
    EXPECT_TRUE(self->hidden());
    expect_ran_clean(run_do_special("    self:set_hidden(0)\n", self));
    EXPECT_FALSE(self->hidden());
}

// walker:last_attacker_id() answers the id walker::note_attacker stamped (0
// before anyone hit); walker:init_fire() starts the attack row once and is
// refused while the walker is busy from it.
TEST_F(ScriptBindingPropsTest, kit_last_attacker_id_and_init_fire)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* hitter = w.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, hitter);
    self->setxy(64, 64);
    self->set_curdir(static_cast<signed char>(FACE_RIGHT));
    self->set_enddir(static_cast<char>(FACE_RIGHT));
    self->set_lastx(1.0f);
    self->set_lasty(0.0f);
    self->set_busy(0.0f);
    self->set_ani_type(ANI_WALK);

    const std::string body =
        "    og.log('hit', self:last_attacker_id())\n"
        "    local function b(v) return v and 1 or 0 end\n"
        "    og.log('fire', b(self:init_fire()), b(self:init_fire()))\n";
    expect_ran_clean(run_do_special(body, self));
    EXPECT_EQ(ANI_ATTACK, self->ani_type()) << "the attack row started";
    EXPECT_GT(self->busy(), 0.0f);

    self->note_attacker(hitter, 7u);
    ASSERT_NE(0u, hitter->entity_id());
    expect_ran_clean(dispatch(self));
    const std::vector<std::string> log = logged();
    ASSERT_EQ(4u, log.size());
    EXPECT_EQ("hit\t0", log[0]) << "nobody has hit it yet";
    EXPECT_EQ("fire\t1\t0", log[1]) << "the second start is refused: busy";
    EXPECT_EQ("hit\t" + std::to_string(hitter->entity_id()), log[2]);
}

// og.possess(rider, host, ticks) hands back the engine's answer: true, or
// false plus a short HUD reason; a dead host is always refused. ticks must
// fit the host's countdown. og.release_possession(ent) answers nil for a
// walker that is in no possession.
TEST_F(ScriptBindingPropsTest, kit_possess_and_release_hand_back_the_engine_answer)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* host = w.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, host);
    host->set_team_num(1);
    self->set_foe(host);
    host->set_dead(1);

    expect_ran_clean(run_do_special(
        "    local ok, reason = og.possess(self, self:foe(), 30)\n"
        "    og.log('possess', ok and 1 or 0, type(reason), #reason)\n"
        "    og.log('release', og.release_possession(self) == nil and 1 or 0)\n",
        self));
    const std::vector<std::string> log = logged();
    ASSERT_EQ(2u, log.size());
    ASSERT_EQ(0u, log[0].rfind("possess\t0\tstring\t", 0)) << log[0];
    const int reason_len = std::stoi(log[0].substr(log[0].rfind('\t') + 1));
    EXPECT_GT(reason_len, 0) << "a refusal names its reason";
    EXPECT_LE(reason_len, 24) << "and it fits the HUD";
    EXPECT_EQ("release\t1", log[1]);
    EXPECT_EQ(0u, self->possess_link());
    EXPECT_EQ(0u, host->possess_link());

    expect_errored_with(
        run_do_special("    og.possess(self, self:foe(), 40000)\n", self),
        "og.possess: ticks out of range");
    expect_errored_with(
        run_do_special("    og.possess(self, self:foe(), -1)\n", self),
        "og.possess: ticks out of range");

    // A live host on another team is taken: true and no reason, both sides
    // linked, the rider hidden, the host on the rider's team with the
    // countdown set. Releasing from the rider's side hands the rider back
    // and the host goes home.
    walker* live = w.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, live);
    live->set_team_num(1);
    live->setxy(64, 64);
    self->setxy(96, 64);
    self->set_foe(live);
    expect_ran_clean(run_do_special(
        "    local ok, reason = og.possess(self, self:foe(), 30)\n"
        "    og.log('possess', ok and 1 or 0, reason == nil and 1 or 0)\n",
        self));
    const std::vector<std::string> taken = logged();
    ASSERT_EQ(1u, taken.size());
    EXPECT_EQ("possess\t1\t1", taken[0]) << "a live foe is possessed";
    EXPECT_EQ(live->entity_id(), self->possess_link());
    EXPECT_EQ(self->entity_id(), live->possess_link());
    EXPECT_EQ(30, live->possess_ticks());
    EXPECT_TRUE(self->hidden()) << "the rider rides hidden";
    EXPECT_EQ(self->team_num(), live->team_num())
        << "the host now fights for the rider's team";

    expect_ran_clean(run_do_special(
        "    og.log('release', og.release_possession(self) == self and 1 or 0)\n",
        self));
    const std::vector<std::string> released = logged();
    ASSERT_EQ(1u, released.size());
    EXPECT_EQ("release\t1", released[0]) << "release hands back the rider";
    EXPECT_EQ(0u, self->possess_link());
    EXPECT_EQ(0u, live->possess_link());
    EXPECT_FALSE(self->hidden());
    EXPECT_EQ(1, live->team_num()) << "the host goes home";
}

// walker:alternate_cost(slot) answers the price of the slot's shifted
// alternate as the session sees it: the declared price with the New
// Specials setting on, 0 for a new-kit alternate the setting hides, 0 for
// an alternate with no price of its own, and an error for a slot outside
// the table. walker:blocks_placement() answers whether a weapon's family is
// solid scenery (never a living).
// RED (C++ mutation, rebuilt: blocks_placement answering false, and
// alternate_cost reading the setting as always on): "solid 0 0 0" and,
// with the setting off, "alt 0 77 40".
TEST_F(ScriptBindingPropsTest, kit_alternate_cost_and_blocks_placement_read_the_family)
{
    // The descriptors are put back even when an assertion ends the test.
    struct Restore {
        FamilyDescriptor soldier = *get_family_descriptor(FAMILY_SOLDIER);
        WeaponFamilyDescriptor arrow = *get_weapon_family_descriptor(FAMILY_ARROW);
        ~Restore()
        {
            set_family_descriptor(FAMILY_SOLDIER, soldier);
            set_weapon_family_descriptor(FAMILY_ARROW, arrow);
        }
    } restore;
    FamilyDescriptor priced = restore.soldier;
    priced.alternate_cost[1] = 0;            // no price of its own
    priced.alternate_new_kit[1] = false;
    priced.alternate_cost[2] = 77;           // a new-kit alternate
    priced.alternate_new_kit[2] = true;
    priced.special_new_kit[2] = false;
    priced.alternate_cost[3] = 40;           // a classic priced alternate
    priced.alternate_new_kit[3] = false;
    priced.special_new_kit[3] = false;
    ASSERT_TRUE(set_family_descriptor(FAMILY_SOLDIER, priced));
    WeaponFamilyDescriptor scenery = restore.arrow;
    scenery.blocks_placement = true;
    ASSERT_TRUE(set_weapon_family_descriptor(FAMILY_ARROW, scenery));

    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* knife = w.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    walker* wall = w.add_weap_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, knife);
    ASSERT_NE(nullptr, wall);
    self->set_foe(knife);
    self->set_leader(wall);
    const std::string body =
        "    local function b(v) return v and 1 or 0 end\n"
        "    og.log('alt', self:alternate_cost(1), self:alternate_cost(2),\n"
        "           self:alternate_cost(3))\n"
        "    og.log('solid', b(self:blocks_placement()),\n"
        "           b(self:foe():blocks_placement()),\n"
        "           b(self:leader():blocks_placement()))\n";

    w.new_specials = 1;
    expect_ran_clean(run_do_special(body, self));
    const std::vector<std::string> on = logged();
    ASSERT_EQ(2u, on.size());
    EXPECT_EQ("alt\t0\t77\t40", on[0]) << "the declared prices, setting on";
    EXPECT_EQ("solid\t0\t0\t1", on[1])
        << "a living and a knife are not scenery; the walled weapon is";

    w.new_specials = 0;
    expect_ran_clean(run_do_special(body, self));
    const std::vector<std::string> off = logged();
    ASSERT_EQ(2u, off.size());
    EXPECT_EQ("alt\t0\t0\t40", off[0])
        << "setting off: the new-kit alternate is not in play";

    expect_errored_with(run_do_special("    self:alternate_cost(6)\n", self),
                        "alternate_cost index out of range");
    expect_errored_with(run_do_special("    self:alternate_cost(-1)\n", self),
                        "alternate_cost index out of range");
}
