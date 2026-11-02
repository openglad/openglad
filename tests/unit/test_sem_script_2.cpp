/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch script-2: the rules the review found in bindings_kit.cpp, campaign_hooks.cpp, family_decl.cpp, pack_scripts.cpp, script_host.cpp.
// Each test is named after the rule it asserts.

#include <gtest/gtest.h>

#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/packs.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace og::script {
extern std::int64_t g_test_world_instruction_budget;
}

using og::data::ClasspackData;
using og::script::DeclareResult;
using og::script::FamilyHook;
using og::script::ScriptError;
using og::script::ScriptHost;
using og::script::ScriptLimits;

namespace {

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Installs a chosen gameplay context (including none at all) and restores
// whatever was there, even when an ASSERT unwinds the test early.
class ScopedContext {
public:
    explicit ScopedContext(GameplayContext* ctx) : previous_(current_game)
    {
        current_game = ctx;
    }
    ~ScopedContext() { current_game = previous_; }
    ScopedContext(const ScopedContext&) = delete;
    ScopedContext& operator=(const ScopedContext&) = delete;

private:
    GameplayContext* previous_;
};

// The world VM's per-entry instruction budget for VMs built inside the scope.
class ScopedWorldBudget {
public:
    explicit ScopedWorldBudget(std::int64_t v)
    {
        og::script::g_test_world_instruction_budget = v;
    }
    ~ScopedWorldBudget() { og::script::g_test_world_instruction_budget = 0; }
    ScopedWorldBudget(const ScopedWorldBudget&) = delete;
    ScopedWorldBudget& operator=(const ScopedWorldBudget&) = delete;
};

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// A declaration must FAIL, and the message must contain `needle`.
void expect_rejected(const DeclareResult& r, const std::string& needle,
                     const std::string& arm)
{
    EXPECT_FALSE(r.ok) << arm << ": expected a rejection, got a clean "
                       << "declaration";
    EXPECT_TRUE(contains(r.error, needle))
        << arm << ": wanted \"" << needle << "\" in:\n  " << r.error;
}

// ---------------------------------------------------------------------------
// Fixture: the declaration pass (family_decl.cpp), one chunk per call.
// ---------------------------------------------------------------------------

constexpr const char* kDeclPack = "semdecl";

class SemFamilyDecl : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;

    void SetUp() override { og::script::clear_pack_family_chunks(); }
    void TearDown() override { og::script::clear_pack_family_chunks(); }

    static DeclareResult declare(const std::string& lua, ClasspackData& out)
    {
        og::script::clear_pack_family_chunks();
        og::script::register_pack_family_chunk(
            {kDeclPack, "semdecl/families/a.lua", lua});
        return og::script::declare_pack_families(kDeclPack, out);
    }

    static DeclareResult declare(const std::string& lua)
    {
        ClasspackData discarded;
        return declare(lua, discarded);
    }
};

// ---------------------------------------------------------------------------
// Fixture: declare, install, then build a world VM that replays the chunk
// for behaviour (the bind pass).
// ---------------------------------------------------------------------------

class SemFamilyBind : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;

    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
        og::script::clear_pack_family_chunks();
        og::script::clear_lua_declared_families();
    }
    void TearDown() override
    {
        og::script::clear_pack_scripts();
        og::script::clear_pack_family_chunks();
        og::script::clear_lua_declared_families();
    }

    struct Bound {
        bool declared_ok = false;
        int installed = 0;
        int family_id = -1;
        std::string errors;  // every world-VM error's first line
        std::vector<std::string> log;
        std::vector<bool> hooks;  // has_hook per FamilyHook asked for
    };

    static Bound declare_install_bind(const std::string& pack,
                                      const std::string& family,
                                      const std::string& chunk,
                                      const std::vector<FamilyHook>& asked)
    {
        Bound out;
        ClasspackData data;
        og::script::register_pack_family_chunk(
            {pack, pack + "/families/" + family + ".lua", chunk});
        const DeclareResult d = og::script::declare_pack_families(pack, data);
        EXPECT_TRUE(d.ok) << d.error;
        out.declared_ok = d.ok;
        out.installed = og::resources::install_classpack_data(std::move(data));
        out.family_id = og::families::resolve_family_string_id(
            Order::Living, (pack + ":" + family).c_str());
        GameWorld world(12);
        for (const ScriptError& e : world.scripts().host().errors()) {
            out.errors += e.message.substr(0, e.message.find('\n'));
            out.errors += '\n';
        }
        out.log = world.scripts().host().log();
        for (const FamilyHook h : asked)
            out.hooks.push_back(
                world.scripts().has_hook(Order::Living, out.family_id, h));
        return out;
    }
};

// ---------------------------------------------------------------------------
// Fixture: a soldier's do_special hook running a Lua body (bindings_kit.cpp).
// ---------------------------------------------------------------------------

class SemKitBindings : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;

    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
    }
    void TearDown() override { og::script::clear_pack_scripts(); }

    static void register_do_special(const std::string& body)
    {
        og::script::clear_pack_scripts();
        og::script::register_pack_script(
            {"sem.kit", "sem_kit_probe.lua",
             "og.register_hooks('living', 'core:soldier', {\n"
             "  do_special = function(self)\n" +
                 body +
                 "\n    return true\n"
                 "  end,\n"
                 "})\n"});
    }

    static std::optional<SpecialResult> dispatch(walker* self)
    {
        return og::script::hooks::do_special(
            get_family_descriptor(FAMILY_SOLDIER), self);
    }

    static std::string last_error()
    {
        const auto& errors =
            og::script::active_world_scripts().host().errors();
        return errors.empty() ? std::string("(no script error recorded)")
                              : errors.back().message;
    }
};

}  // namespace

// ===========================================================================
// bindings_kit.cpp
// ===========================================================================

// bindings_kit.cpp:57 m_set_kit_state, the HIDDEN-unchanged arm: when the
// HIDDEN bit asked for is the one the walker already has, the hide state and
// the collision table stay as they are and every OTHER bit is replaced (not
// OR-ed in), narrowed to a byte. The binding reads no setting, so both
// values of new_specials must give the same answer.
TEST_F(SemKitBindings, set_kit_state_with_hidden_unchanged_replaces_the_other_bits)
{
    for (const short new_specials : {short{0}, short{1}}) {
        SCOPED_TRACE(testing::Message() << "new_specials=" << new_specials);
        TestGameWorld tw;
        tw.world().new_specials = new_specials;
        walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, self);
        self->setxy(64, 64);
        ASSERT_EQ(1u, tw.world().myobmap->walker_to_pos.count(self));

        // Visible walker, HIDDEN not asked: FEARLESS is replaced by WARD.
        self->set_kit_state(KIT_FEARLESS);
        register_do_special("    self:set_kit_state(og.C.KIT_WARD)");
        ASSERT_TRUE(dispatch(self).has_value()) << last_error();
        EXPECT_FALSE(self->hidden()) << "visible, HIDDEN not asked: stays visible";
        EXPECT_EQ(KIT_WARD, self->kit_state())
            << "visible, HIDDEN not asked: the other bits are REPLACED";
        EXPECT_EQ(1u, tw.world().myobmap->walker_to_pos.count(self))
            << "visible, HIDDEN not asked: still in the collision table";

        // Narrowed to a byte: 256 + WARD lands as WARD.
        self->set_kit_state(KIT_CHANNEL);
        register_do_special("    self:set_kit_state(256 + og.C.KIT_WARD)");
        ASSERT_TRUE(dispatch(self).has_value()) << last_error();
        EXPECT_EQ(KIT_WARD, self->kit_state())
            << "the value is narrowed to a byte, the old bits replaced";
        EXPECT_FALSE(self->hidden());

        // Hidden walker, HIDDEN asked again: FEARLESS replaced by WARD, the
        // walker stays hidden and out of the collision table.
        self->set_kit_state(KIT_FEARLESS);
        self->set_hidden(true);
        ASSERT_TRUE(self->hidden());
        ASSERT_EQ(KIT_HIDDEN | KIT_FEARLESS, self->kit_state());
        ASSERT_EQ(0u, tw.world().myobmap->walker_to_pos.count(self));
        register_do_special(
            "    self:set_kit_state(og.C.KIT_HIDDEN + og.C.KIT_WARD)");
        ASSERT_TRUE(dispatch(self).has_value()) << last_error();
        EXPECT_TRUE(self->hidden()) << "hidden, HIDDEN asked: stays hidden";
        EXPECT_EQ(KIT_HIDDEN | KIT_WARD, self->kit_state())
            << "hidden, HIDDEN asked: the other bits are REPLACED";
        EXPECT_EQ(0u, tw.world().myobmap->walker_to_pos.count(self))
            << "hidden, HIDDEN asked: not re-added to the collision table";
    }
}

// bindings_kit.cpp:155 kit_world, the no-world arm: every kit world verb
// refuses with 'no active world' when the gameplay context has no world.
// The guard is shared, but each verb's call to it is its own site, so each
// verb is dispatched on its own.
TEST_F(SemKitBindings, kit_world_verbs_refuse_to_run_with_no_active_world)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->setxy(64, 64);

    const char* const kVerbs[] = {
        "og.possess(self, self, 1)", "og.release_possession(self)",
        "og.fearless(self, 1)",      "og.map_size()",
        "og.line_clear(self, self)", "og.swap_places(self, self)",
        "og.rejoin_obmap(self)",
    };
    for (const char* verb : kVerbs) {
        SCOPED_TRACE(verb);
        register_do_special(std::string("    ") + verb);
        GameplayContext worldless;  // .world stays nullptr
        ScopedContext scope(&worldless);
        const auto handled = dispatch(self);
        EXPECT_FALSE(handled.has_value())
            << verb << ": no world: the hook must error, not run";
        EXPECT_TRUE(contains(last_error(), "no active world"))
            << verb << ": no world: the error must say 'no active world'; "
            << "got: " << last_error();
    }
}

// ===========================================================================
// campaign_hooks.cpp
// ===========================================================================

// campaign_hooks.cpp:37 valid_campaign_var_name: the empty name is refused
// (with the 1..32 boundary pinned on both sides).
TEST(SemCampaignHooks, an_empty_campaign_var_name_is_refused)
{
    EXPECT_FALSE(og::script::hooks::valid_campaign_var_name(""))
        << "empty: refused";
    EXPECT_TRUE(og::script::hooks::valid_campaign_var_name("a"))
        << "one char: accepted";
    EXPECT_TRUE(og::script::hooks::valid_campaign_var_name(std::string(32, 'a')))
        << "32 chars: accepted";
    EXPECT_FALSE(
        og::script::hooks::valid_campaign_var_name(std::string(33, 'a')))
        << "33 chars: refused";
    EXPECT_FALSE(og::script::hooks::valid_campaign_var_name("a-b"))
        << "'-': refused";
}

// The same empty name through og.register_campaign_hooks: a load error
// naming the 1-32 rule, and no picker registered.
TEST(SemCampaignHooks, register_campaign_hooks_refuses_an_empty_var_name)
{
    og::test::ScopedPackStoreState restore;
    ScopedContext no_game(nullptr);  // dispatch resolves the shared UI VM
    og::script::clear_pack_scripts();
    og::script::clear_pack_family_chunks();
    og::script::clear_pack_lib_modules();
    og::script::hooks::clear_campaign_providers();

    og::script::register_pack_script(
        {"sem.campaign", "semcampaign/scripts/c.lua",
         "og.register_campaign_hooks({\n"
         "  vars = { '' },\n"
         "  picker_menu = function(page_id) return { title = 'X' } end,\n"
         "})\n"});
    EXPECT_FALSE(og::script::hooks::campaign_picker_registered())
        << "an empty var name must refuse the whole registration";
    bool named = false;
    for (const ScriptError& e :
         og::script::active_world_scripts().host().errors())
        named = named || contains(e.message, "vars[1] '' must be 1-32 chars "
                                             "of [a-z0-9_]");
    EXPECT_TRUE(named) << "the error names the empty entry and the rule";

    og::script::clear_pack_scripts();
    og::script::hooks::clear_campaign_providers();
}

// ===========================================================================
// family_decl.cpp
// ===========================================================================

// family_decl.cpp:82 push_family_ref: during the declaration pass every use
// of the og.family_id placeholder but its truth raises the 'cannot be READ'
// error, and == is NOT blocked (it answers false, raw identity).
TEST_F(SemFamilyDecl, a_declare_time_family_ref_refuses_every_use_but_truth)
{
    const char* const kUses[] = {
        "local _ = r.x",     "r.x = 1",           "r()",
        "local _ = tostring(r)", "local _ = r .. 'a'", "local _ = #r",
        "local _ = r + 1",   "local _ = r - 1",   "local _ = r * 2",
        "local _ = r / 2",   "local _ = r % 2",   "local _ = r ^ 2",
        "local _ = -r",      "local _ = r // 2",  "local _ = r < 1",
        "local _ = r <= 1",
    };
    for (const char* use : kUses) {
        SCOPED_TRACE(use);
        expect_rejected(
            declare(std::string("local r = og.family_id('living', "
                                "'core:soldier')\n") +
                    use + "\nog.family('living', { id = 'v3:x' })\n"),
            "a family byte cannot be READ while families are being declared",
            use);
    }
}

TEST_F(SemFamilyDecl, a_declare_time_family_ref_compares_equal_only_to_itself)
{
    ClasspackData data;
    const DeclareResult r = declare(
        "local r = og.family_id('living', 'core:soldier')\n"
        "local r2 = og.family_id('living', 'core:archer')\n"
        "assert(not (r == 1), 'ref == 1 must be false')\n"
        "assert(not (r == r2), 'two refs are two values')\n"
        "assert(r == r, 'a ref equals itself')\n"
        "assert(r ~= nil, 'a ref is not nil')\n"
        "assert(getmetatable(r) == false, 'the metatable is locked')\n"
        "og.family('living', { id = 'v3:x' })\n",
        data);
    EXPECT_TRUE(r.ok) << "== is not blocked: " << r.error;
    EXPECT_EQ(1u, data.living.size()) << "== arm: the chunk ran to the end";
}

// family_decl.cpp:334 opt_wire_id: absent leaves the text empty, and an
// integer outside 0..255 is accepted as its decimal text (the range is the
// installer's job, not the declaration's).
TEST_F(SemFamilyDecl, wire_id_is_empty_when_absent_and_any_integer_lands_as_text)
{
    ClasspackData absent;
    ASSERT_TRUE(declare("og.family('living', { id = 'v3:x' })", absent).ok);
    ASSERT_EQ(1u, absent.living.size());
    EXPECT_EQ("", absent.living[0].wire_id) << "absent: wire_id empty";

    ClasspackData big;
    const DeclareResult r300 =
        declare("og.family('living', { id = 'v3:x', wire_id = 300 })", big);
    ASSERT_TRUE(r300.ok) << "300: no declaration error: " << r300.error;
    ASSERT_EQ(1u, big.living.size());
    EXPECT_EQ("300", big.living[0].wire_id) << "300: stored as its text";

    ClasspackData negative;
    const DeclareResult rneg =
        declare("og.family('living', { id = 'v3:x', wire_id = -1 })", negative);
    ASSERT_TRUE(rneg.ok) << "-1: no declaration error: " << rneg.error;
    ASSERT_EQ(1u, negative.living.size());
    EXPECT_EQ("-1", negative.living[0].wire_id) << "-1: stored as its text";
}

// family_decl.cpp:682 harvest_special: an ai that is not a function is a
// load error naming the entry.
TEST_F(SemFamilyDecl, a_special_ai_that_is_not_a_function_is_a_load_error)
{
    expect_rejected(
        declare("og.family('living', { id = 'v3:x', specials = "
                "{ { id = 'a', name = 'A', mp_cost = 1, ai = 3 } } })"),
        "specials[1] 'a'.ai: expected a function, got number", "ai = 3");
    expect_rejected(
        declare("og.family('living', { id = 'v3:x', specials = "
                "{ { id = 'a', name = 'A', mp_cost = 1, ai = false } } })"),
        "specials[1] 'a'.ai: expected a function, got boolean",
        "ai = false (false is the no-op for cast only)");
}

// family_decl.cpp:716 harvest_specials: no specials key leaves the slot's
// specials unset.
TEST_F(SemFamilyDecl, an_absent_specials_list_stays_unset)
{
    ClasspackData data;
    ASSERT_TRUE(declare("og.family('living', { id = 'v3:x' })", data).ok);
    ASSERT_EQ(1u, data.living.size());
    EXPECT_FALSE(data.living[0].specials.has_value())
        << "absent: specials unset (the slot's specials are left alone)";
}

// family_decl.cpp:752 harvest_specials: an explicit slot = 0 is the auto
// slot (previous + 1), not a 'slot 0 does not exist' refusal.
TEST_F(SemFamilyDecl, an_explicit_slot_zero_takes_the_next_slot)
{
    ClasspackData data;
    const DeclareResult r = declare(
        "og.family('living', { id = 'v3:x', specials = {"
        "{ id = 'a', name = 'A', mp_cost = 1 },"
        "{ id = 'b', name = 'B', mp_cost = 1, slot = 0 },"
        "{ id = 'c', name = 'C', mp_cost = 1, slot = 4 },"
        "{ id = 'd', name = 'D', mp_cost = 1, slot = 0 } } })",
        data);
    ASSERT_TRUE(r.ok) << "explicit slot 0: accepted: " << r.error;
    ASSERT_EQ(1u, data.living.size());
    ASSERT_TRUE(data.living[0].specials.has_value());
    const auto& entries = *data.living[0].specials;
    ASSERT_EQ(4u, entries.size());
    EXPECT_EQ(1, entries[0].slot) << "no slot key: slot 1";
    EXPECT_EQ(2, entries[1].slot) << "slot = 0 after slot 1: slot 2";
    EXPECT_EQ(4, entries[2].slot) << "slot given: kept";
    EXPECT_EQ(5, entries[3].slot) << "slot = 0 after slot 4: slot 5";
}

// family_decl.cpp:754 harvest_specials: a negative slot is outside 1..5.
TEST_F(SemFamilyDecl, a_negative_special_slot_does_not_exist)
{
    expect_rejected(
        declare("og.family('living', { id = 'v3:x', specials = "
                "{ { id = 'a', name = 'A', mp_cost = 1, slot = -1 } } })"),
        "specials[1]: slot -1 does not exist (a family has slots 1..5)",
        "slot = -1");
}

// family_decl.cpp:884 harvest_combat: an absent combat block leaves combat
// unset (while a present stats block lands), and a combat block that is not
// a table is a load error.
TEST_F(SemFamilyDecl, an_absent_combat_block_stays_unset_and_a_bad_one_is_refused)
{
    ClasspackData data;
    const DeclareResult r = declare(
        "og.family('living', { id = 'v3:x', stats = { strength = 1, "
        "dexterity = 2, constitution = 3, intelligence = 4, armor = 5, "
        "level = 6 } })",
        data);
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(1u, data.living.size());
    ASSERT_TRUE(data.living[0].stats.has_value()) << "stats present: filled";
    EXPECT_EQ(6, data.living[0].stats->level);
    EXPECT_FALSE(data.living[0].combat.has_value()) << "absent combat: unset";

    expect_rejected(declare("og.family('living', { id = 'v3:x', combat = 7 })"),
                    "'v3:x'.combat: expected a table, got number",
                    "bad combat");
}

// family_decl.cpp:949 harvest_costs: no costs block leaves costs unset, and
// a costs block without train leaves train unset.
TEST_F(SemFamilyDecl, absent_costs_and_absent_train_stay_unset)
{
    ClasspackData none;
    ASSERT_TRUE(declare("og.family('living', { id = 'v3:x' })", none).ok);
    ASSERT_EQ(1u, none.living.size());
    EXPECT_FALSE(none.living[0].costs.has_value()) << "no costs: unset";

    ClasspackData hire_only;
    const DeclareResult r = declare(
        "og.family('living', { id = 'v3:x', costs = { hire = 10 } })",
        hire_only);
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(1u, hire_only.living.size());
    ASSERT_TRUE(hire_only.living[0].costs.has_value());
    EXPECT_EQ(10, hire_only.living[0].costs->hire);
    EXPECT_FALSE(hire_only.living[0].costs->train.has_value())
        << "train absent: costs.train unset";
}

// family_decl.cpp:1067 the per-order key vocabularies: a key legal on one
// order is an unknown key on another. One test per declaring order; each
// pairs the refusal with the owning order's acceptance.
TEST_F(SemFamilyDecl, a_living_refuses_keys_of_the_other_orders)
{
    for (const char* key : {"radar_landmark = true", "rally_radius = 2",
                            "init_frame = 1", "editor_label = 'X'",
                            "hp = 10"}) {
        SCOPED_TRACE(key);
        const std::string name = std::string(key).substr(
            0, std::string(key).find(' '));
        expect_rejected(
            declare(std::string("og.family('living', { id = 'v3:x', ") + key +
                    " })"),
            "unknown key '" + name + "'", std::string("living + ") + key);
    }
    ClasspackData data;
    ASSERT_TRUE(declare("og.family('living', { id = 'v3:x', combat = { hp = 10,"
                        " melee_damage = 1, stepsize = 1, fire_delay = 1, "
                        "fire_mp_cost = 1 } })",
                        data)
                    .ok)
        << "living hp lives in the combat block";
    ASSERT_TRUE(data.living[0].combat.has_value());
    EXPECT_EQ(10.0F, data.living[0].combat->hp);
}

TEST_F(SemFamilyDecl, a_weapon_refuses_keys_of_the_other_orders)
{
    for (const char* key : {"playable = true", "radar_landmark = true",
                            "init_frame = 1", "editor_label = 'X'",
                            "stats = {}"}) {
        SCOPED_TRACE(key);
        const std::string name = std::string(key).substr(
            0, std::string(key).find(' '));
        expect_rejected(
            declare(std::string("og.family('weapon', { id = 'v3:w', ") + key +
                    " })"),
            "unknown key '" + name + "'", std::string("weapon + ") + key);
    }
    ClasspackData data;
    ASSERT_TRUE(
        declare("og.family('weapon', { id = 'v3:w', rally_radius = 3 })", data)
            .ok)
        << "weapon owns rally_radius";
    ASSERT_EQ(1u, data.weapons.size());
}

TEST_F(SemFamilyDecl, an_effect_refuses_keys_of_the_other_orders)
{
    for (const char* key : {"rally_radius = 2", "init_frame = 1",
                            "playable = true", "editor_label = 'X'"}) {
        SCOPED_TRACE(key);
        const std::string name = std::string(key).substr(
            0, std::string(key).find(' '));
        expect_rejected(
            declare(std::string("og.family('effect', { id = 'v3:e', ") + key +
                    " })"),
            "unknown key '" + name + "'", std::string("effect + ") + key);
    }
    ClasspackData data;
    ASSERT_TRUE(declare("og.family('effect', { id = 'v3:e', radar_landmark = "
                        "true })",
                        data)
                    .ok)
        << "effect owns radar_landmark";
    ASSERT_EQ(1u, data.effects.size());
    EXPECT_TRUE(data.effects[0].presentation.radar_landmark);
}

TEST_F(SemFamilyDecl, a_treasure_refuses_keys_of_the_other_orders)
{
    for (const char* key : {"flags = {}", "loops_animation = true",
                            "rally_radius = 2", "editor_label = 'X'"}) {
        SCOPED_TRACE(key);
        const std::string name = std::string(key).substr(
            0, std::string(key).find(' '));
        expect_rejected(
            declare(std::string("og.family('treasure', { id = 'v3:t', ") +
                    key + " })"),
            "unknown key '" + name + "'", std::string("treasure + ") + key);
    }
    ClasspackData data;
    ASSERT_TRUE(declare("og.family('treasure', { id = 'v3:t', radar_landmark = "
                        "true })",
                        data)
                    .ok)
        << "treasure owns radar_landmark";
    ASSERT_EQ(1u, data.treasures.size());
    EXPECT_TRUE(data.treasures[0].presentation.radar_landmark);
}

TEST_F(SemFamilyDecl, a_generator_refuses_keys_of_the_other_orders)
{
    for (const char* key : {"flags = {}", "radar_landmark = true",
                            "playable = true", "init_frame = 1"}) {
        SCOPED_TRACE(key);
        const std::string name = std::string(key).substr(
            0, std::string(key).find(' '));
        expect_rejected(
            declare(std::string("og.family('generator', { id = 'v3:g', ") +
                    key + " })"),
            "unknown key '" + name + "'", std::string("generator + ") + key);
    }
    ClasspackData data;
    ASSERT_TRUE(declare("og.family('generator', { id = 'v3:g', editor_label = "
                        "'G' })",
                        data)
                    .ok)
        << "generator owns editor_label";
    ASSERT_EQ(1u, data.generators.size());
    EXPECT_EQ("G", data.generators[0].editor_label.value_or(""));
}

// family_decl.cpp:1213 harvest_entry, the reader-failure arm: a declaration
// that fails to harvest pushes NO entry and NO provenance, whatever its
// order; a good declaration before it in the same chunk stays.
TEST_F(SemFamilyDecl, a_failed_harvest_leaves_no_entry_and_no_provenance)
{
    struct Case {
        const char* lua;
        const char* needle;
    };
    const Case kCases[] = {
        {"og.family('living', { id = 'v3:l', combat = 3 })",
         "'v3:l'.combat: expected a table"},
        {"og.family('weapon', { id = 'v3:w', gravity = 'x' })",
         "'v3:w'.gravity"},
        {"og.family('effect', { id = 'v3:e', loops_animation = 'x' })",
         "'v3:e'.loops_animation"},
        {"og.family('treasure', { id = 'v3:t', init_frame = 'x' })",
         "'v3:t'.init_frame"},
        {"og.family('generator', { id = 'v3:g', editor_label = 7 })",
         "'v3:g'.editor_label"},
    };
    for (const Case& c : kCases) {
        SCOPED_TRACE(c.lua);
        ClasspackData data;
        expect_rejected(declare(c.lua, data), c.needle, c.lua);
        EXPECT_TRUE(data.living.empty()) << c.lua << ": no living entry";
        EXPECT_TRUE(data.weapons.empty()) << c.lua << ": no weapon entry";
        EXPECT_TRUE(data.effects.empty()) << c.lua << ": no effect entry";
        EXPECT_TRUE(data.treasures.empty()) << c.lua << ": no treasure entry";
        EXPECT_TRUE(data.generators.empty())
            << c.lua << ": no generator entry";
        EXPECT_TRUE(data.lua_declarations.empty())
            << c.lua << ": no provenance";
    }

    ClasspackData data;
    expect_rejected(declare("og.family('living', { id = 'v3:good' })\n"
                            "og.family('living', { id = 'v3:bad', combat = 3 })\n",
                            data),
                    "'v3:bad'.combat", "good then bad");
    ASSERT_EQ(1u, data.living.size()) << "only the good declaration landed";
    EXPECT_EQ("v3:good", data.living[0].id);
    ASSERT_EQ(1u, data.lua_declarations.size());
}

// family_decl.cpp:1343 harvest_pack_header: a field a later og.pack header
// leaves out keeps its earlier value; a field it gives wins.
TEST_F(SemFamilyDecl, a_later_pack_header_keeps_the_fields_it_leaves_out)
{
    ClasspackData data;
    const DeclareResult r = declare(
        "og.pack({ id = 'semdecl', title = 'A', version = '1', "
        "authors = { 'X', 'Y' } })\n"
        "og.pack({ version = '2' })\n"
        "og.pack({ title = 'B' })\n",
        data);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ("semdecl", data.pack) << "id absent later: kept";
    EXPECT_EQ("2", data.version) << "version given later: last wins";
    EXPECT_EQ("B", data.title) << "title given last: last wins";
    EXPECT_EQ("X, Y", data.authors) << "authors absent later: kept";
}

// family_decl.cpp:1587 bind_specials, the none arm: a specials list whose
// entries carry neither a cast, a default_cast nor an ai binds NO
// do_special and NO check_special_ai hook.
TEST_F(SemFamilyBind, specials_without_cast_or_ai_bind_no_hook)
{
    const Bound none = declare_install_bind(
        "semnone", "plain",
        "og.family('living', { id = 'semnone:plain', name = 'PLAIN',\n"
        "  specials = { { id = 'zap', name = 'ZAP', mp_cost = 5 } } })\n",
        {FamilyHook::DoSpecial, FamilyHook::CheckSpecialAi});
    ASSERT_EQ(1, none.installed);
    ASSERT_GE(none.family_id, 0);
    EXPECT_EQ("", none.errors);
    EXPECT_FALSE(none.hooks[0]) << "none: no DoSpecial hook";
    EXPECT_FALSE(none.hooks[1]) << "none: no CheckSpecialAi hook";

    // Control: the same list with a cast and an ai binds both.
    const Bound both = declare_install_bind(
        "semboth", "plain",
        "og.family('living', { id = 'semboth:plain', name = 'PLAIN',\n"
        "  specials = { { id = 'zap', name = 'ZAP', mp_cost = 5,\n"
        "    cast = function(self) return true end,\n"
        "    ai = function(self) return false end } } })\n",
        {FamilyHook::DoSpecial, FamilyHook::CheckSpecialAi});
    EXPECT_EQ("", both.errors);
    EXPECT_TRUE(both.hooks[0]) << "cast: DoSpecial bound";
    EXPECT_TRUE(both.hooks[1]) << "ai: CheckSpecialAi bound";
}

// family_decl.cpp:1634 bind_family, the absent arm: a hook the declaration
// does not spell stays unbound while the one it spells binds.
TEST_F(SemFamilyBind, a_hook_the_declaration_leaves_out_stays_unbound)
{
    const Bound b = declare_install_bind(
        "semabs", "shifty",
        "og.family('living', { id = 'semabs:shifty', name = 'SHIFTY',\n"
        "  on_death = function(self) return true end })\n",
        {FamilyHook::OnDeath, FamilyHook::HitResponse, FamilyHook::OnActLiving,
         FamilyHook::DoSpecial, FamilyHook::LevelUp});
    ASSERT_EQ(1, b.installed);
    ASSERT_GE(b.family_id, 0);
    EXPECT_EQ("", b.errors);
    EXPECT_TRUE(b.hooks[0]) << "on_death given: bound";
    EXPECT_FALSE(b.hooks[1]) << "hit_response absent: unbound";
    EXPECT_FALSE(b.hooks[2]) << "on_act absent: unbound";
    EXPECT_FALSE(b.hooks[3]) << "do_special absent: unbound";
    EXPECT_FALSE(b.hooks[4]) << "level_up absent: unbound";
}

// family_decl.cpp:1675 og.family / og.anims / og.pack argument shapes.
TEST_F(SemFamilyDecl, og_family_anims_and_pack_refuse_a_wrong_shaped_call)
{
    for (const char* call : {"og.family('living', 'x')", "og.family('living')"}) {
        SCOPED_TRACE(call);
        const DeclareResult r = declare(call);
        expect_rejected(r, "bad argument #2", call);
        EXPECT_TRUE(contains(r.error, "table expected")) << r.error;
    }
    expect_rejected(declare("og.family(5, { id = 'v3:x' })"),
                    "og.family: unknown order '5'",
                    "a number order coerces to its text");
    const DeclareResult anims = declare("og.anims('x', 3)");
    expect_rejected(anims, "bad argument #2", "og.anims('x', 3)");
    EXPECT_TRUE(contains(anims.error, "table expected")) << anims.error;
    const DeclareResult pack = declare("og.pack(7)");
    expect_rejected(pack, "bad argument #1", "og.pack(7)");
    EXPECT_TRUE(contains(pack.error, "table expected")) << pack.error;
}

// family_decl.cpp:1684, the world-VM arm of og.anims and og.pack: in the
// bind replay both are silent no-ops answering nothing, and the chunk runs
// on past them.
TEST_F(SemFamilyBind, og_anims_and_og_pack_do_nothing_in_a_world_vm)
{
    const Bound b = declare_install_bind(
        "semworld", "shifty",
        "local declaring =\n"
        "  type(og.family_id('living', 'core:soldier')) == 'userdata'\n"
        "og.family('living', { id = 'semworld:shifty', name = 'SHIFTY' })\n"
        "if not declaring then\n"
        "  local n = select('#', og.pack({ title = 'WORLD' }))\n"
        "  local m = select('#', og.anims('sparkle', { rows = 1, "
        "frames = { {9} } }))\n"
        "  og.log('world pass', n, m)\n"
        "end\n",
        {});
    ASSERT_EQ(1, b.installed);
    EXPECT_EQ("", b.errors) << "world VM: og.pack/og.anims raise nothing";
    ASSERT_EQ(1u, b.log.size()) << "world VM: the chunk ran past both calls";
    EXPECT_EQ("world pass\t0\t0", b.log[0])
        << "world VM: og.pack and og.anims answer nothing";
}

// ===========================================================================
// pack_scripts.cpp
// ===========================================================================

// pack_scripts.cpp:60 / :122 register_pack_script and
// register_pack_family_chunk: the same (pack, chunk name) replaces in place,
// a new one appends last (even a name another pack uses), and every
// registration bumps the generation.
TEST(SemPackScripts, a_pack_script_registered_twice_replaces_in_place)
{
    og::test::ScopedPackStoreState restore;
    og::script::clear_pack_scripts();
    const auto& v = og::script::pack_scripts();

    unsigned g = og::script::pack_scripts_generation();
    og::script::register_pack_script({"p", "a.lua", "-- 1"});
    ASSERT_EQ(1u, v.size()) << "new: appended";
    EXPECT_EQ(g + 1, og::script::pack_scripts_generation()) << "new: bumped";

    g = og::script::pack_scripts_generation();
    og::script::register_pack_script({"p", "b.lua", "-- 2"});
    ASSERT_EQ(2u, v.size()) << "new: size + 1";
    EXPECT_EQ("b.lua", v[1].chunk_name) << "new: appended LAST";
    EXPECT_EQ(g + 1, og::script::pack_scripts_generation()) << "new: bumped";

    g = og::script::pack_scripts_generation();
    og::script::register_pack_script({"p", "a.lua", "-- 3"});
    ASSERT_EQ(2u, v.size()) << "same pack+name: size unchanged";
    EXPECT_EQ("a.lua", v[0].chunk_name) << "same pack+name: same index";
    EXPECT_EQ("-- 3", v[0].source) << "same pack+name: the new source";
    EXPECT_EQ("b.lua", v[1].chunk_name);
    EXPECT_EQ("-- 2", v[1].source);
    EXPECT_EQ(g + 1, og::script::pack_scripts_generation())
        << "same pack+name: bumped";

    og::script::register_pack_script({"q", "a.lua", "-- 4"});
    ASSERT_EQ(3u, v.size()) << "same name, other pack: appended";
    EXPECT_EQ("q", v[2].pack_id);
    EXPECT_EQ("-- 3", v[0].source) << "the other pack's a.lua untouched";

    og::script::clear_pack_scripts();
}

TEST(SemPackScripts, a_family_chunk_registered_twice_replaces_in_place)
{
    og::test::ScopedPackStoreState restore;
    og::script::clear_pack_family_chunks();
    const auto& v = og::script::pack_family_chunks();

    unsigned g = og::script::pack_family_generation();
    og::script::register_pack_family_chunk({"p", "p/families/a.lua", "-- 1"});
    ASSERT_EQ(1u, v.size()) << "new: appended";
    EXPECT_EQ(g + 1, og::script::pack_family_generation()) << "new: bumped";

    g = og::script::pack_family_generation();
    og::script::register_pack_family_chunk({"p", "p/families/b.lua", "-- 2"});
    ASSERT_EQ(2u, v.size()) << "new: size + 1";
    EXPECT_EQ("p/families/b.lua", v[1].chunk_name) << "new: appended LAST";
    EXPECT_EQ(g + 1, og::script::pack_family_generation()) << "new: bumped";

    g = og::script::pack_family_generation();
    og::script::register_pack_family_chunk({"p", "p/families/a.lua", "-- 3"});
    ASSERT_EQ(2u, v.size()) << "same pack+name: size unchanged";
    EXPECT_EQ("p/families/a.lua", v[0].chunk_name)
        << "same pack+name: same index";
    EXPECT_EQ("-- 3", v[0].source) << "same pack+name: the new source";
    EXPECT_EQ("-- 2", v[1].source);
    EXPECT_EQ(g + 1, og::script::pack_family_generation())
        << "same pack+name: bumped";

    og::script::register_pack_family_chunk({"q", "p/families/a.lua", "-- 4"});
    ASSERT_EQ(3u, v.size()) << "same name, other pack: appended";
    EXPECT_EQ("q", v[2].pack_id);

    og::script::clear_pack_family_chunks();
}

// ===========================================================================
// script_host.cpp
// ===========================================================================

// script_host.cpp:40 capped_alloc: an allocation within the cap is counted
// in memory_used, and a free gives it back to the budget.
TEST(SemScriptHost, memory_within_the_cap_is_counted_and_a_free_returns_it)
{
    ScriptHost h;
    const std::size_t m0 = h.memory_used();
    ASSERT_TRUE(h.run_chunk("grow", "t = {} for i = 1, 65536 do t[i] = i end",
                            "sem"))
        << (h.errors().empty() ? "" : h.errors().back().message);
    const std::size_t peak = h.memory_used();
    // 65536 array slots of a value each are at least 512 KiB (a Lua 5.4
    // array slot is 9 bytes at its tightest, 16 as a plain TValue).
    EXPECT_GE(peak - m0, 512u * 1024u)
        << "within cap: the grown table is counted (m0=" << m0
        << " peak=" << peak << ")";
    EXPECT_LE(peak, h.memory_limit());

    ASSERT_TRUE(h.run_chunk("drop", "t = nil", "sem"));
    ASSERT_TRUE(h.run_chunk(
        "churn",
        "for i = 1, 200000 do local s = string.rep('x', 64) end", "sem"))
        << (h.errors().empty() ? "" : h.errors().back().message);
    const std::size_t after = h.memory_used();
    EXPECT_LT(after + 512u * 1024u, peak)
        << "free: the collected table went back to the budget (peak=" << peak
        << " after=" << after << ")";
}

// script_host.cpp:102 og.div: a zero divisor and INT64_MIN / -1 are script
// errors naming the reason, never a value.
TEST(SemScriptHost, og_div_by_zero_and_its_overflow_are_script_errors)
{
    ScriptHost h;
    EXPECT_EQ(std::nullopt, h.eval_integer("og.div(1, 0)")) << "b == 0";
    ASSERT_EQ(1u, h.errors().size());
    EXPECT_TRUE(contains(h.errors()[0].message, "og.div: division by zero"))
        << h.errors()[0].message;

    h.clear_errors();
    EXPECT_EQ(std::nullopt, h.eval_integer("og.div(math.mininteger, -1)"))
        << "INT64_MIN / -1";
    ASSERT_EQ(1u, h.errors().size());
    EXPECT_TRUE(contains(h.errors()[0].message, "og.div: overflow"))
        << h.errors()[0].message;

    h.clear_errors();
    EXPECT_EQ(std::optional<std::int64_t>(-3), h.eval_integer("og.div(-7, 2)"))
        << "control: C truncation";
    EXPECT_EQ(std::optional<std::int64_t>(INT64_MIN + 1),
              h.eval_integer("og.div(math.mininteger + 1, 1)"))
        << "control: INT64_MIN alone is not the overflow";
    EXPECT_TRUE(h.errors().empty());
}

// script_host.cpp:114 og.mod: the same two refusals.
TEST(SemScriptHost, og_mod_by_zero_and_its_overflow_are_script_errors)
{
    ScriptHost h;
    EXPECT_EQ(std::nullopt, h.eval_integer("og.mod(1, 0)")) << "b == 0";
    ASSERT_EQ(1u, h.errors().size());
    EXPECT_TRUE(contains(h.errors()[0].message, "og.mod: division by zero"))
        << h.errors()[0].message;

    h.clear_errors();
    EXPECT_EQ(std::nullopt, h.eval_integer("og.mod(math.mininteger, -1)"))
        << "INT64_MIN % -1";
    ASSERT_EQ(1u, h.errors().size());
    EXPECT_TRUE(contains(h.errors()[0].message, "og.mod: overflow"))
        << h.errors()[0].message;

    h.clear_errors();
    EXPECT_EQ(std::optional<std::int64_t>(-1), h.eval_integer("og.mod(-7, 2)"))
        << "control: sign of the dividend";
    EXPECT_TRUE(h.errors().empty());
}

// script_host.cpp:173 og.trunc: a value whose truncation does not fit int64,
// or NaN, is a script error.
TEST(SemScriptHost, og_trunc_out_of_integer_range_is_a_script_error)
{
    ScriptHost h;
    for (const char* expr : {"og.trunc(1e19)", "og.trunc(-1e19)",
                             "og.trunc(0/0)", "og.trunc(2^63)",
                             "og.trunc(-(2^63))"}) {
        SCOPED_TRACE(expr);
        h.clear_errors();
        EXPECT_EQ(std::nullopt, h.eval_integer(expr)) << expr;
        ASSERT_EQ(1u, h.errors().size()) << expr;
        EXPECT_TRUE(contains(h.errors()[0].message,
                             "og.trunc: value out of integer range"))
            << h.errors()[0].message;
    }
    h.clear_errors();
    EXPECT_EQ(std::optional<std::int64_t>(4611686018427387904LL),
              h.eval_integer("og.trunc(2^62)"))
        << "boundary control: 2^62 is in range";
    EXPECT_TRUE(h.errors().empty());
}

// script_host.cpp:340 traceback_handler: an error raised with a non-string
// value records '(non-string error)' and a traceback.
TEST(SemScriptHost, a_non_string_error_is_recorded_as_non_string_error)
{
    for (const char* src : {"error({})", "error(nil)"}) {
        SCOPED_TRACE(src);
        ScriptHost h;
        EXPECT_FALSE(h.run_chunk("e", src));
        ASSERT_EQ(1u, h.errors().size());
        const std::string& m = h.errors()[0].message;
        EXPECT_EQ(0u, m.rfind("(non-string error)", 0))
            << "non-string error: the record starts with the placeholder: "
            << m;
        EXPECT_TRUE(contains(m, "stack traceback")) << m;
    }
}

// script_host.cpp:374 push_environment, the empty-key arm: two chunks run
// with no env key do not see each other's globals.
TEST(SemScriptHost, chunks_with_no_env_key_get_isolated_environments)
{
    ScriptHost h;
    ASSERT_TRUE(h.run_chunk("a", "leak = 42"));
    ASSERT_TRUE(h.run_chunk("b", "og.log(tostring(leak))"));
    ASSERT_EQ(1u, h.log().size());
    EXPECT_EQ("nil", h.log()[0])
        << "empty key: a second chunk must not see the first one's global";
}

// script_host.cpp:426 protected_call, the nested arm: a host entry made from
// inside another (a module og.use'd while an earlier module loads) shares
// the outer entry's instruction budget instead of getting a fresh one.
TEST(SemScriptHost, a_nested_script_entry_shares_the_outer_instruction_budget)
{
    og::test::ScopedPackStoreState restore;
    init_all_registries();
    ScopedContext no_game(nullptr);
    ScopedWorldBudget budget(1000000);
    const std::string spin =
        "local x = 0 for i = 1, 300000 do x = x + i end\n";

    struct Case {
        bool outer_spins;
        bool inner_spins;
        bool expect_trip;
        const char* arm;
    };
    const Case kCases[] = {
        {true, false, false, "control: the outer spin alone fits"},
        {false, true, false, "control: the inner spin alone fits"},
        {true, true, true, "nested: both spins share ONE budget and trip"},
    };
    for (const Case& c : kCases) {
        SCOPED_TRACE(c.arm);
        og::script::clear_pack_scripts();
        og::script::clear_pack_family_chunks();
        og::script::clear_pack_lib_modules();
        og::script::register_pack_lib_module(
            {"sem.budget", "outer", "sembudget_outer.lua",
             std::string(c.outer_spins ? spin : "") +
                 "local inner = og.use('inner')\n"
                 "og.log('outer done')\n"
                 "return { n = inner.n + 1 }\n"});
        og::script::register_pack_lib_module(
            {"sem.budget", "inner", "sembudget_inner.lua",
             std::string(c.inner_spins ? spin : "") +
                 "og.log('inner done')\n"
                 "return { n = 1 }\n"});
        og::script::WorldScripts ws;
        bool tripped = false;
        for (const ScriptError& e : ws.host().errors())
            tripped = tripped || contains(e.message, "instruction budget");
        EXPECT_EQ(c.expect_trip, tripped) << c.arm;
        const bool outer_done =
            std::find(ws.host().log().begin(), ws.host().log().end(),
                      "outer done") != ws.host().log().end();
        EXPECT_EQ(!c.expect_trip, outer_done) << c.arm;
    }
    og::script::clear_pack_lib_modules();
    og::script::clear_pack_scripts();
}

// script_host.cpp:495 run_chunk, the syntax-error arm: a chunk that does
// not compile is recorded under its name, with no traceback, and nothing in
// it runs.
TEST(SemScriptHost, a_chunk_with_a_syntax_error_is_recorded_and_never_runs)
{
    ScriptHost h;
    EXPECT_FALSE(h.run_chunk("bad", "og.log('ran')\nthis is not lua"));
    ASSERT_EQ(1u, h.errors().size()) << "syntax error: one record";
    EXPECT_EQ("bad", h.errors()[0].where) << "syntax error: named by chunk";
    EXPECT_FALSE(contains(h.errors()[0].message, "stack traceback"))
        << "syntax error: a load error carries no traceback: "
        << h.errors()[0].message;
    EXPECT_TRUE(h.log().empty()) << "syntax error: nothing in the chunk ran";
}

// script_host.cpp:555 eval coercions: a number read refuses a non-numeric
// string (but takes a numeric one), a string read refuses a number, and a
// refusal is not a script error.
TEST(SemScriptHost, eval_number_and_eval_string_refuse_the_wrong_type)
{
    ScriptHost h;
    EXPECT_EQ(std::nullopt, h.eval_number("'x'")) << "eval_number of 'x'";
    EXPECT_EQ(std::optional<double>(12.0), h.eval_number("'12'"))
        << "control: lua_isnumber takes a numeric string";
    EXPECT_EQ(std::nullopt, h.eval_string("42")) << "eval_string of 42";
    EXPECT_EQ(std::optional<std::string>("xy"), h.eval_string("'x' .. 'y'"));
    EXPECT_TRUE(h.errors().empty()) << "a type refusal is not a script error";
}
