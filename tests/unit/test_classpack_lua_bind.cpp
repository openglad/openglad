/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The BIND pass: the SAME family chunk, replayed in an ordinary world VM.
//
// A families/*.lua file is read twice. Once per content change, in a
// throwaway VM, for its data (tests/unit/test_classpack_lua_decl.cpp). Then
// once per VM build, here, where og.family means something else entirely:
// resolve the declared id to the family the installer already created, and
// hang this chunk's hooks and specials casts in THIS VM's hook tables. Data
// installs once; behavior belongs to whichever VM is going to run it.
//
// What has to be true, and is what the tests below assert:
//
//   * the bind pass touches no registry. If it did, the descriptor a pack
//     installs would depend on how many worlds a session happened to build.
//   * dispatch cannot tell the two spellings apart. An inline
//     `on_death = fn` and an `og.register_hooks` on_death land in the same
//     table under the same key, so every reader downstream is unchanged.
//   * casts are joined to slots by DECLARED ID, never by list position, so
//     re-costing or reordering a family's specials cannot silently move a
//     handler onto a different button.
//   * a castable special nothing handles is a PACK ERROR when a Lua
//     declaration owns the slot (format spec V3) — in one file there is no
//     excuse — and stays a warning for a slot some other front end filled.

#include <gtest/gtest.h>

#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>

#include <string>
#include <vector>

using namespace og::script;

namespace {

constexpr const char* kPack = "v3bind";

// core:soldier is the family every one of these binds against: it is
// installed from the shipped core pack, so the declaration under test is
// pure behavior and the data half is somebody else's business — which is
// exactly the split the bind pass is supposed to make.
constexpr int kSoldier = FAMILY_SOLDIER;

class LuaFamilyBindTest : public ::testing::Test {
protected:
    // Declared first so it outlives the clears below: the shipped pack
    // scripts, family chunks and tuning this fixture wipes are what the
    // next test in a --gtest_shuffle order expects to find.
    og::test::ScopedPackStoreState pack_store_restore_;

    void SetUp() override
    {
        init_all_registries();
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_lua_declared_families();
        clear_failed_pack_declarations();
        hooks::reset_hook_failures();
    }

    void TearDown() override
    {
        clear_pack_scripts();
        clear_pack_family_chunks();
        // Only this pack's lib modules: the shipped core pack's three are
        // what every other test in this binary loads its behavior from.
        unregister_pack_lib_modules(kPack);
        clear_lua_declared_families();
        clear_failed_pack_declarations();
    }

    // Chunk names here are pack-SHAPED but not `packs/`-prefixed, and the
    // whole file follows that rule. A `packs/` name claims the bytes are
    // pack content at that virtual path, which makes registration declare
    // them to the pack-Lua coverage inventory (pack_scripts.h); these
    // fixtures are literals that exist nowhere in the repository, so they
    // are named as what they are and stay unmeasured.
    static void family_chunk(const std::string& lua)
    {
        register_pack_family_chunk({kPack, "v3bind/families/a.lua", lua});
    }

    // Everything the VM recorded while loading, as one string — the load
    // errors these rules produce are host error records, and a test that
    // searched only the first one would miss the second slot's.
    static std::string load_errors(const WorldScripts& ws)
    {
        std::string all;
        for (const auto& e : ws.host().errors()) {
            all += e.message;
            all += '\n';
        }
        return all;
    }
};

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

// ---------------------------------------------------------------------------
// V1 — a declaration is legal only in families/
// ---------------------------------------------------------------------------

// Not a style rule. families/ is the directory the installer evaluates, so
// a declaration written anywhere else would bind behavior in every VM while
// its data half never installed at all — a family whose hooks exist and
// whose descriptor does not.
TEST_F(LuaFamilyBindTest, declaring_outside_families_is_a_load_error)
{
    register_pack_script(
        {kPack, "v3bind/scripts/wrong.lua",
         "og.family('living', { id = 'core:soldier', name = 'X' })\n"});
    const std::string errors = load_errors(active_world_scripts());
    EXPECT_TRUE(contains(errors, "only a packs/<id>/families/*.lua chunk may "
                                 "declare pack data"))
        << errors;
}

TEST_F(LuaFamilyBindTest, anims_and_pack_headers_are_families_only_too)
{
    register_pack_script({kPack, "v3bind/scripts/a.lua",
                          "og.anims('x', { frames = { {0} } })\n"});
    register_pack_script({kPack, "v3bind/scripts/b.lua",
                          "og.pack{ id = 'v3bind' }\n"});
    const std::string errors = load_errors(active_world_scripts());
    EXPECT_TRUE(contains(errors, "og.anims: only a packs/<id>/families/"))
        << errors;
    EXPECT_TRUE(contains(errors, "og.pack: only a packs/<id>/families/"))
        << errors;
}

// A lib module is the other place a pack's Lua runs, and it is shared: a
// declaration inside one would install (or not) depending on which family
// file happened to og.use it first.
TEST_F(LuaFamilyBindTest, declaring_inside_a_lib_module_is_a_load_error)
{
    register_pack_lib_module(
        {kPack, "sneaky", "v3bind/lib/sneaky.lua",
         "og.family('living', { id = 'core:soldier', name = 'X' })\n"
         "return {}\n"});
    register_pack_script({kPack, "v3bind/scripts/pull.lua",
                          "local m = og.use('sneaky')\n"});
    const std::string errors = load_errors(active_world_scripts());
    EXPECT_TRUE(contains(errors, "og.family: only a packs/<id>/families/"))
        << errors;
}

// ---------------------------------------------------------------------------
// The bind pass installs behavior and NOTHING else
// ---------------------------------------------------------------------------

TEST_F(LuaFamilyBindTest, an_inline_hook_binds_and_dispatches)
{
    family_chunk("og.family('living', {\n"
                 "  id = 'core:soldier',\n"
                 "  on_death = function(self)\n"
                 "    og.log('declared on_death ran')\n"
                 "    return true\n"
                 "  end,\n"
                 "})\n");
    const FamilyDescriptor* fd = get_family_descriptor(kSoldier);
    ASSERT_NE(nullptr, fd);

    const auto result = hooks::on_death(fd, nullptr);
    ASSERT_TRUE(result.has_value()) << "the declared hook did not dispatch";
    EXPECT_TRUE(*result);

    WorldScripts& ws = active_world_scripts();
    EXPECT_TRUE(load_errors(ws).empty()) << load_errors(ws);
    EXPECT_TRUE(ws.has_hook(Order::Living, kSoldier, FamilyHook::OnDeath));
    ASSERT_FALSE(ws.host().log().empty());
    EXPECT_EQ("declared on_death ran", ws.host().log().back());
    EXPECT_EQ(0u, hooks::hook_failures().count);
}

// The registry is the installer's. A declaration replayed for its behavior
// must not re-state data — otherwise the descriptor a session ends up with
// would depend on how many world VMs it built along the way.
TEST_F(LuaFamilyBindTest, binding_never_writes_to_the_registry)
{
    const FamilyDescriptor* fd = get_family_descriptor(kSoldier);
    ASSERT_NE(nullptr, fd);
    const float hp_before = fd->combat.hp;
    const std::int32_t hire_before = fd->hiring_cost;
    const unsigned short charge_cost_before = fd->special_cost[1];
    ASSERT_GT(hp_before, 0.0f) << "the shipped soldier must be installed";

    family_chunk("og.family('living', {\n"
                 "  id = 'core:soldier',\n"
                 "  combat = { hp = 1 },\n"
                 "  costs = { hire = 1 },\n"
                 "  specials = { { id = 'charge', name = 'CHARGE',\n"
                 "                 mp_cost = 1 } },\n"
                 "})\n");
    (void)active_world_scripts();

    EXPECT_FLOAT_EQ(hp_before, fd->combat.hp);
    EXPECT_EQ(hire_before, fd->hiring_cost);
    EXPECT_EQ(charge_cost_before, fd->special_cost[1]);
}

TEST_F(LuaFamilyBindTest, binding_a_family_nothing_installed_is_a_load_error)
{
    family_chunk("og.family('living', { id = 'v3bind:ghostfamily' })\n");
    const std::string errors = load_errors(active_world_scripts());
    EXPECT_TRUE(contains(errors, "no such family is installed")) << errors;
    EXPECT_TRUE(contains(errors, "v3bind:ghostfamily")) << errors;
}

// A pack the declaration pass threw out installed nothing, so replaying its
// chunks would report that same casualty again in every VM the process ever
// builds. The installer names it once; the bind replay stays quiet.
TEST_F(LuaFamilyBindTest, chunks_of_a_rejected_pack_are_not_replayed)
{
    family_chunk("og.family('living', { id = 'v3bind:ghostfamily' })\n");
    note_failed_pack_declaration(kPack);
    const std::string errors = load_errors(active_world_scripts());
    EXPECT_TRUE(errors.empty())
        << "a rejected pack's chunks must not be bound: " << errors;
}

// ---------------------------------------------------------------------------
// V2 — specials, joined by id
// ---------------------------------------------------------------------------

namespace {

// The soldier's four castable slots, and the fact that the descriptor names
// them: everything below joins against these ids, so a test that assumed
// them silently would be pinning the wrong thing.
void expect_soldier_special_ids()
{
    const FamilyDescriptor* fd = get_family_descriptor(kSoldier);
    ASSERT_NE(nullptr, fd);
    const char* const want[] = {nullptr, "charge", "boomerang", "whirlwind",
                                "disarm"};
    for (int slot = 1; slot <= 4; slot++) {
        ASSERT_NE(nullptr, fd->special_ids[slot]) << "slot " << slot;
        ASSERT_STREQ(want[slot], fd->special_ids[slot]) << "slot " << slot;
    }
}

}  // namespace

// The V3 diagnostic is the clearest window onto WHICH slots a declaration
// answered for: it names every castable slot nothing handles. Declaring a
// cast for one special by id and reading back the complaints proves the
// join landed on that special and not on list position 1.
TEST_F(LuaFamilyBindTest, a_cast_lands_on_the_slot_its_id_names)
{
    expect_soldier_special_ids();
    // `disarm` is the FOURTH slot. Declared alone, it is the first (and
    // only) entry of the list — position would put it in slot 1.
    family_chunk("og.family('living', { id = 'core:soldier', specials = {\n"
                 "  { id = 'disarm', name = 'DISARM', mp_cost = 150,\n"
                 "    cast = function() return true end },\n"
                 "} })\n");
    note_lua_declared_family(Order::Living, kSoldier, kPack);

    const std::string errors = load_errors(active_world_scripts());
    EXPECT_FALSE(contains(errors, "'DISARM'"))
        << "the declared cast did not reach the disarm slot: " << errors;
    for (const char* unhandled : {"'CHARGE' (slot 1)", "'BOOMERANG' (slot 2)",
                                  "'WHIRLWIND' (slot 3)"}) {
        EXPECT_TRUE(contains(errors, unhandled))
            << unhandled << " should be unhandled: " << errors;
    }
}

// `cast = false` is the charged no-op said out loud: the special fires,
// spends its MP and does nothing, because that is what the family means. It
// has to count as an ANSWER, or the author cannot express it without also
// tripping the unhandled-special rule.
TEST_F(LuaFamilyBindTest, cast_false_is_a_handler_and_silences_the_rule)
{
    family_chunk("og.family('living', { id = 'core:soldier', specials = {\n"
                 "  { id = 'charge', name = 'CHARGE', mp_cost = 25,\n"
                 "    cast = function() return true end },\n"
                 "  { id = 'boomerang', name = 'BOOMERANG', mp_cost = 100,\n"
                 "    cast = false },\n"
                 "} })\n");
    note_lua_declared_family(Order::Living, kSoldier, kPack);

    const std::string errors = load_errors(active_world_scripts());
    EXPECT_FALSE(contains(errors, "'BOOMERANG'"))
        << "cast = false is an answer: " << errors;
    EXPECT_FALSE(contains(errors, "'CHARGE'")) << errors;
    EXPECT_TRUE(contains(errors, "'WHIRLWIND'")) << errors;
    EXPECT_TRUE(contains(errors, "'DISARM'")) << errors;
}

TEST_F(LuaFamilyBindTest, a_default_cast_answers_every_slot)
{
    family_chunk("og.family('living', { id = 'core:soldier', specials = {\n"
                 "  { id = 'charge', name = 'CHARGE', mp_cost = 25 },\n"
                 "  default_cast = function() return true end,\n"
                 "} })\n");
    note_lua_declared_family(Order::Living, kSoldier, kPack);

    WorldScripts& ws = active_world_scripts();
    EXPECT_TRUE(load_errors(ws).empty())
        << "a default_cast handles the slots nothing else does: "
        << load_errors(ws);
    EXPECT_TRUE(ws.has_hook(Order::Living, kSoldier, FamilyHook::DoSpecial));
    const FamilyDescriptor* fd = get_family_descriptor(kSoldier);
    const auto result = hooks::do_special(fd, nullptr);
    ASSERT_TRUE(result.has_value()) << "the default should have caught it";
    EXPECT_TRUE(*result);
}

// V3's other half. A slot some OTHER front end filled may be served by a
// C++ do_special or by a script the operator has not written yet, so it
// warns. A slot a Lua declaration owns has no such excuse, and only the
// error stops the pack.
TEST_F(LuaFamilyBindTest, an_unhandled_castable_special_warns_unless_declared)
{
    family_chunk("og.family('living', { id = 'core:soldier', specials = {\n"
                 "  { id = 'charge', name = 'CHARGE', mp_cost = 25,\n"
                 "    cast = function() return true end },\n"
                 "} })\n");
    // No note_lua_declared_family: as far as the engine knows, some other
    // front end owns this family.
    EXPECT_TRUE(load_errors(active_world_scripts()).empty())
        << "an undeclared slot warns, it does not stop the pack";

    clear_pack_family_chunks();
    family_chunk("og.family('living', { id = 'core:soldier', specials = {\n"
                 "  { id = 'charge', name = 'CHARGE', mp_cost = 25,\n"
                 "    cast = function() return true end },\n"
                 "} })\n");
    note_lua_declared_family(Order::Living, kSoldier, kPack);
    const std::string errors = load_errors(active_world_scripts());
    EXPECT_TRUE(contains(errors, "class pack 'v3bind'")) << errors;
    EXPECT_TRUE(contains(errors, "is castable for 100 MP and nothing handles "
                                 "it")) << errors;
    EXPECT_TRUE(contains(errors, "say `cast = false` if the no-op is "
                                 "deliberate")) << errors;
}

// The per-entry `ai =` sugar lowers to exactly ONE family-level
// check_special_ai — a closure that selects on the slot being cast. Call
// count matters: the engine asks this question in a hot AI path, and the
// sugar must not turn one call into five.
TEST_F(LuaFamilyBindTest, per_entry_ai_lowers_to_one_family_level_hook)
{
    family_chunk("og.family('living', { id = 'core:soldier', specials = {\n"
                 "  { id = 'charge', name = 'CHARGE', mp_cost = 25,\n"
                 "    cast = false, ai = function() return true end },\n"
                 "  { id = 'boomerang', name = 'BOOMERANG', mp_cost = 100,\n"
                 "    cast = false },\n"
                 "  { id = 'whirlwind', name = 'WHIRLWIND', mp_cost = 120,\n"
                 "    cast = false },\n"
                 "  { id = 'disarm', name = 'DISARM', mp_cost = 150,\n"
                 "    cast = false },\n"
                 "} })\n");
    WorldScripts& ws = active_world_scripts();
    EXPECT_TRUE(load_errors(ws).empty()) << load_errors(ws);
    EXPECT_TRUE(
        ws.has_hook(Order::Living, kSoldier, FamilyHook::CheckSpecialAi))
        << "the sugar must present as the family-level hook";

    // A slot with no opinion answers true, which is what the engine does
    // when no hook is registered at all.
    const FamilyDescriptor* fd = get_family_descriptor(kSoldier);
    const auto answer = hooks::check_special_ai(fd, nullptr);
    ASSERT_TRUE(answer.has_value());
    EXPECT_TRUE(*answer);
    EXPECT_EQ(0u, hooks::hook_failures().count);
}

// ---------------------------------------------------------------------------
// V4 / V7 — og.register_hooks survives as the override seam
// ---------------------------------------------------------------------------

// A mod that wants to change one family's death behavior should not have to
// restate its stats. og.register_hooks over a declaration is the SUPPORTED
// way to do that, so the duplicate-registration report must not fire on it
// — the report exists for two scripts racing, and this is not a race.
TEST_F(LuaFamilyBindTest, register_hooks_overrides_a_declaration_quietly)
{
    family_chunk("og.family('living', { id = 'core:soldier',\n"
                 "  on_death = function() og.log('declared') return true end,\n"
                 "})\n");
    register_pack_script(
        {kPack, "v3bind/scripts/override.lua",
         "og.register_hooks('living', 'core:soldier', { on_death = "
         "function() og.log('override') return true end })\n"});

    WorldScripts& ws = active_world_scripts();
    EXPECT_TRUE(load_errors(ws).empty())
        << "overriding a declaration is the seam, not a collision: "
        << load_errors(ws);

    // The override WINS: families/ is replayed before scripts/, so the last
    // registration is the script's.
    const FamilyDescriptor* fd = get_family_descriptor(kSoldier);
    const auto result = hooks::on_death(fd, nullptr);
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(ws.host().log().empty());
    EXPECT_EQ("override", ws.host().log().back());
}

TEST_F(LuaFamilyBindTest, a_second_override_of_one_hook_is_still_reported)
{
    // One override of a declaration is the seam. Two scripts overriding the
    // same hook is the collision the report was written for, and the
    // declaration must not buy them an exemption each.
    family_chunk("og.family('living', { id = 'core:soldier',\n"
                 "  on_death = function() return true end })\n");
    register_pack_script(
        {kPack, "v3bind/scripts/a-first.lua",
         "og.register_hooks('living', 'core:soldier', "
         "{ on_death = function() return true end })\n"});
    register_pack_script(
        {kPack, "v3bind/scripts/b-second.lua",
         "og.register_hooks('living', 'core:soldier', "
         "{ on_death = function() return true end })\n"});

    const std::string errors = load_errors(active_world_scripts());
    EXPECT_TRUE(contains(errors, "duplicate hook registration")) << errors;
    EXPECT_TRUE(contains(errors, "on_death")) << errors;
}

// ---------------------------------------------------------------------------
// Ordering
// ---------------------------------------------------------------------------

// lib/ → families/ → scripts/. A family declaration may og.use a module,
// and a behavior script must be able to layer over a declaration; both
// follow from that order and neither would work from any other.
TEST_F(LuaFamilyBindTest, a_declaration_may_use_a_lib_module_at_bind_time)
{
    // Registered alongside the shipped pack's modules, not instead of them:
    // dropping those would leave every later test in this binary with a
    // core pack whose scripts cannot og.use anything. TearDown takes this
    // one back out by pack id.
    register_pack_lib_module(
        {kPack, "shared", "v3bind/lib/shared.lua",
         "return { die = function() og.log('from lib') return true end }\n"});
    family_chunk("local shared = og.use('shared')\n"
                 "og.family('living', { id = 'core:soldier',\n"
                 "  on_death = shared.die })\n");

    WorldScripts& ws = active_world_scripts();
    EXPECT_TRUE(load_errors(ws).empty()) << load_errors(ws);

    const FamilyDescriptor* fd = get_family_descriptor(kSoldier);
    const auto result = hooks::on_death(fd, nullptr);
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(ws.host().log().empty());
    EXPECT_EQ("from lib", ws.host().log().back());
}

// The load-time fence covers the bind replay exactly as it covers the
// declaration pass. Without it, a family chunk's top level could draw from
// the RNG during a mid-session VM rebuild and desynchronise the peers.
TEST_F(LuaFamilyBindTest, the_world_api_is_closed_during_the_bind_replay)
{
    family_chunk("local x = og.rand(5)\n"
                 "og.family('living', { id = 'core:soldier' })\n");
    const std::string errors = load_errors(active_world_scripts());
    EXPECT_TRUE(contains(errors, "the world API is dispatch-time only"))
        << errors;
}

// ---------------------------------------------------------------------------
// Two declarations of one family
// ---------------------------------------------------------------------------

// og.register_hooks over a declaration is the supported override seam and
// stays quiet (above). TWO declarations of the same family's same hook is the
// other case: two authors editing one family, where the loser's code silently
// never runs. That has to be reported, and the LATER one has to be the one
// that survives — last-registration-wins is the rule everything else in the
// loader follows.
TEST_F(LuaFamilyBindTest, two_declarations_of_one_hook_report_and_the_later_wins)
{
    family_chunk("og.family('living', { id = 'core:soldier',\n"
                 "  on_death = function() og.log('first') return true end })\n"
                 "og.family('living', { id = 'core:soldier',\n"
                 "  on_death = function() og.log('second') return true end })\n");

    WorldScripts& ws = active_world_scripts();
    const std::string errors = load_errors(ws);
    EXPECT_TRUE(contains(errors, "duplicate hook registration")) << errors;
    EXPECT_TRUE(contains(errors, "on_death")) << errors;

    const FamilyDescriptor* fd = get_family_descriptor(kSoldier);
    const auto result = hooks::on_death(fd, nullptr);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(1u, ws.host().log().size());
    EXPECT_EQ("second", ws.host().log().back())
        << "the later declaration must be the one that runs";
}

// ---------------------------------------------------------------------------
// The id join, and what it refuses
// ---------------------------------------------------------------------------

// Casts join to slots by DECLARED ID against the installed descriptor, so an
// id the family does not have cannot be silently dropped: the author would be
// left with a special that never fires and no reason why. The message lists
// the ids that DO exist, because "charge" against "chrage" is the whole class
// of mistake this catches.
TEST_F(LuaFamilyBindTest, a_special_id_the_family_lacks_names_the_ids_it_has)
{
    expect_soldier_special_ids();
    family_chunk("og.family('living', { id = 'core:soldier', specials = {\n"
                 "  { id = 'chrage', name = 'CHARGE', mp_cost = 25,\n"
                 "    cast = function() og.log('bound') return true end },\n"
                 "} })\n");

    WorldScripts& ws = active_world_scripts();
    const std::string errors = load_errors(ws);
    EXPECT_TRUE(contains(errors, "special 'chrage' names no slot of living "
                                 "family 'core:soldier'")) << errors;
    EXPECT_TRUE(contains(errors, "declared ids: charge, boomerang, whirlwind, "
                                 "disarm")) << errors;
    // The chunk was abandoned at the bad id, so nothing it declared bound.
    EXPECT_FALSE(ws.has_hook(Order::Living, kSoldier, FamilyHook::DoSpecial));
    EXPECT_TRUE(ws.host().log().empty());
}

// ---------------------------------------------------------------------------
// Dispatch through the lowered forms
//
// These two drive a REAL living through the same funnel walker::special()
// uses, because the slot the closure picks is read off the walker: a
// nullptr self can only ever prove the no-opinion arm.
// ---------------------------------------------------------------------------

namespace {

// core:soldier, alive, in a headless world — the caster every dispatch below
// selects its slot on.
living* spawn_soldier(TestGameWorld& tw)
{
    walker* w = tw.world().add_ob(Order::Living, kSoldier);
    EXPECT_NE(nullptr, w) << "the shipped soldier must be spawnable";
    return static_cast<living*>(w);
}

}  // namespace

// The `ai =` sugar lowers to ONE closure over a private slot → function
// table, and the closure has to pick by the slot the walker is actually
// casting. Forwarding the wrong entry would make a class refuse the spell its
// own ai approved and cast the one it declined.
TEST_F(LuaFamilyBindTest, per_slot_ai_answers_for_the_slot_being_cast)
{
    expect_soldier_special_ids();
    family_chunk("og.family('living', { id = 'core:soldier', specials = {\n"
                 "  { id = 'charge', name = 'CHARGE', mp_cost = 25,\n"
                 "    cast = false,\n"
                 "    ai = function(self) og.log('slot1 asked') return false end },\n"
                 "  { id = 'boomerang', name = 'BOOMERANG', mp_cost = 100,\n"
                 "    cast = false },\n"
                 "} })\n");
    TestGameWorld tw;
    living* self = spawn_soldier(tw);
    ASSERT_NE(nullptr, self);

    WorldScripts& ws = active_world_scripts();
    EXPECT_TRUE(load_errors(ws).empty()) << load_errors(ws);

    const FamilyDescriptor* fd = get_family_descriptor(kSoldier);
    self->set_current_special(1);
    const auto slot1 = hooks::check_special_ai(fd, self);
    ASSERT_TRUE(slot1.has_value());
    EXPECT_FALSE(*slot1) << "slot 1's own ai said no";
    ASSERT_EQ(1u, ws.host().log().size());
    EXPECT_EQ("slot1 asked", ws.host().log().back());

    // Slot 2 declared no ai. The closure answers "no opinion" — true, what
    // the engine does with no hook at all — and slot 1's function must NOT
    // have been asked on its behalf.
    self->set_current_special(2);
    const auto slot2 = hooks::check_special_ai(fd, self);
    ASSERT_TRUE(slot2.has_value());
    EXPECT_TRUE(*slot2);
    EXPECT_EQ(1u, ws.host().log().size())
        << "another slot's ai answered for slot 2";
    EXPECT_EQ(0u, hooks::hook_failures().count);
}

// `cast = false` is stored as a value, not left absent, so it BEATS a
// default_cast rather than falling through to it. The slot was written to
// spend its MP and do nothing on purpose; a default that swallowed it would
// make the spelling unusable in any family that also has a default.
TEST_F(LuaFamilyBindTest, cast_false_beats_the_default_for_its_own_slot)
{
    expect_soldier_special_ids();
    family_chunk("og.family('living', { id = 'core:soldier', specials = {\n"
                 "  { id = 'charge', name = 'CHARGE', mp_cost = 25,\n"
                 "    cast = function() og.log('charge ran') return true end },\n"
                 "  { id = 'boomerang', name = 'BOOMERANG', mp_cost = 100,\n"
                 "    cast = false },\n"
                 "  default_cast = function() og.log('default ran') return true end,\n"
                 "} })\n");
    TestGameWorld tw;
    living* self = spawn_soldier(tw);
    ASSERT_NE(nullptr, self);

    WorldScripts& ws = active_world_scripts();
    EXPECT_TRUE(load_errors(ws).empty()) << load_errors(ws);

    const FamilyDescriptor* fd = get_family_descriptor(kSoldier);
    self->set_current_special(1);
    const auto charged = hooks::do_special(fd, self);
    ASSERT_TRUE(charged.has_value());
    EXPECT_TRUE(*charged);
    ASSERT_EQ(1u, ws.host().log().size());
    EXPECT_EQ("charge ran", ws.host().log().back());

    // Slot 2 is the charged no-op: consumed as a successful dispatch with
    // nothing called, and the default left untouched.
    self->set_current_special(2);
    const auto noop = hooks::do_special(fd, self);
    ASSERT_TRUE(noop.has_value()) << "the no-op is a handled dispatch";
    EXPECT_TRUE(*noop);
    EXPECT_EQ(1u, ws.host().log().size())
        << "the default_cast swallowed the explicit no-op";

    // Slot 3 declared nothing, so it is the default's to answer.
    self->set_current_special(3);
    const auto defaulted = hooks::do_special(fd, self);
    ASSERT_TRUE(defaulted.has_value());
    EXPECT_TRUE(*defaulted);
    ASSERT_EQ(2u, ws.host().log().size());
    EXPECT_EQ("default ran", ws.host().log().back());
    EXPECT_EQ(0u, hooks::hook_failures().count);
}
