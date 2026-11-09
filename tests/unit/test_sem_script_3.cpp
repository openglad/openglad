/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch script-3: the rules the review found in world_scripts.cpp.
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
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>
#include <openglad/resources/packs.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace og::script;

namespace {

// The family a throwaway declaration installs into (a free mod slot, the
// SpecialsByIdTest precedent).
constexpr int kModWire = 64;

[[maybe_unused]] std::size_t count_of(const std::string& haystack, const std::string& needle)
{
    std::size_t n = 0;
    for (std::size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size()))
        n++;
    return n;
}

// ---------------------------------------------------------------------------
// The one fixture: every pack store wiped before and after, the failure latch
// reset. Tests that need a world build a TestGameWorld (production loader,
// current_game pointing at it); campaign tests run with current_game null so
// dispatch resolves the shared UI VM.
// ---------------------------------------------------------------------------
class SemScript3 : public ::testing::Test {
protected:
    // Declared first so it outlives the clears below: the shipped pack
    // scripts, family chunks and modules this fixture wipes are what the
    // next test in a --gtest_shuffle order expects to find.
    og::test::ScopedPackStoreState pack_store_restore_;

    void SetUp() override
    {
        previous_ = current_game;
        init_all_registries();
        wipe();
        hooks::reset_hook_failures();
    }

    void TearDown() override
    {
        wipe();
        hooks::reset_hook_failures();
        current_game = previous_;
    }

    static void wipe()
    {
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_pack_lib_modules();
        clear_lua_declared_families();
        clear_failed_pack_declarations();
        clear_all_family_tuning();
        hooks::clear_campaign_providers();
    }

    static void script(const std::string& source,
                       const char* chunk = "sem3/scripts/a.lua",
                       const char* pack = "sem3")
    {
        register_pack_script({pack, chunk, source});
    }

    static WorldScripts& ws() { return active_world_scripts(); }

    static const std::vector<ScriptError>& errors()
    {
        return ws().host().errors();
    }

    static const std::vector<std::string>& vm_log()
    {
        return ws().host().log();
    }

    static std::string all_errors()
    {
        std::string all;
        for (const ScriptError& e : errors()) {
            all += e.where + ": " + e.message;
            all += '\n';
        }
        return all;
    }

    static std::size_t errors_containing(const std::string& needle)
    {
        std::size_t n = 0;
        for (const ScriptError& e : errors())
            if (e.message.find(needle) != std::string::npos)
                n++;
        return n;
    }

    static std::size_t log_count(const std::string& line)
    {
        std::size_t n = 0;
        for (const std::string& l : vm_log())
            if (l == line)
                n++;
        return n;
    }

    static const FamilyDescriptor* soldier_fd()
    {
        const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
        EXPECT_NE(nullptr, fd);
        return fd;
    }

    GameplayContext* previous_ = nullptr;
};

// A world at level 42 (the level-hook tests' id), its walkers built by the
// production loader.
struct World42 : TestGameWorld {
    World42() : TestGameWorld(42) {}

    walker* living(int family, int team, int x, int y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_team_num(static_cast<unsigned char>(team));
        return w;
    }
};

}  // namespace

// ===========================================================================
// resolve_walker (world_scripts.cpp:158) and og_is_alive (:1057)
// ===========================================================================

// A handle kept past the dispatch that made it, whose walker has since left
// the world, is stale: og.is_alive answers false for it and raises nothing
// (the required readers raise; this one does not).
TEST_F(SemScript3, is_alive_answers_false_for_a_stale_handle_without_an_error)
{
    World42 tw;
    walker* victim = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    walker* self = tw.living(FAMILY_SOLDIER, 0, 140, 100);
    ASSERT_NE(nullptr, victim);
    ASSERT_NE(nullptr, self);
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  do_special = function(self)\n"
           "    if saved == nil then\n"
           "      saved = self\n"
           "      og.log('fresh', og.is_alive(saved))\n"
           "      return true\n"
           "    end\n"
           "    og.log('stale', og.is_alive(saved))\n"
           "    return true\n"
           "  end,\n"
           "})\n");
    const auto first = hooks::do_special(soldier_fd(), victim);
    ASSERT_TRUE(first.has_value()) << all_errors();
    ASSERT_EQ(1, tw.world().remove_ob(victim));

    const auto second = hooks::do_special(soldier_fd(), self);
    ASSERT_TRUE(second.has_value())
        << "stale arm: og.is_alive must not raise: " << all_errors();
    EXPECT_TRUE(*second);
    EXPECT_EQ((std::vector<std::string>{"fresh\ttrue", "stale\tfalse"}),
              vm_log())
        << "live arm true, stale arm false";
    EXPECT_TRUE(errors().empty()) << all_errors();
    EXPECT_EQ(0u, hooks::hook_failures().count);
}

// A walker flagged dead is still resolvable inside its own death dispatch,
// and og.is_alive answers false for it there.
TEST_F(SemScript3, is_alive_answers_false_for_a_dead_walker_it_can_still_resolve)
{
    World42 tw;
    script("og.register_level_hooks(42, {\n"
           "  on_entity_death = function(ent)\n"
           "    og.log('alive', og.is_alive(ent), og.entity_id(ent))\n"
           "  end,\n"
           "})\n");
    walker* orc = tw.living(FAMILY_ORC, 1, 200, 200);
    ASSERT_NE(nullptr, orc);
    const std::uint32_t id = orc->entity_id();
    ASSERT_NE(0u, id);
    orc->set_dead(1);
    orc->death();
    EXPECT_EQ((std::vector<std::string>{"alive\tfalse\t" + std::to_string(id)}),
              vm_log())
        << "dead-but-resolvable arm: the handle resolves (its id) and "
           "is_alive is false";
    EXPECT_TRUE(errors().empty()) << all_errors();
}

// ===========================================================================
// walker_eq (world_scripts.cpp:642)
// ===========================================================================

// Handles to two different tracked walkers are unequal; the same walker
// pushed twice is equal.
TEST_F(SemScript3, handles_to_different_tracked_walkers_are_unequal)
{
    World42 tw;
    walker* a = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    walker* b = tw.living(FAMILY_SOLDIER, 0, 140, 100);
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  on_melee_hit = function(self, target)\n"
           "    og.log('eq', self == target)\n"
           "  end,\n"
           "})\n");
    EXPECT_TRUE(hooks::on_melee_hit(soldier_fd(), a, b));
    EXPECT_TRUE(hooks::on_melee_hit(soldier_fd(), a, a));
    EXPECT_EQ((std::vector<std::string>{"eq\tfalse", "eq\ttrue"}), vm_log())
        << "different walkers false, same walker true";
}

// An untracked walker (entity id 0, never in the world) compares by identity
// within one dispatch: itself twice is equal, a second loose walker is not.
TEST_F(SemScript3, untracked_handles_compare_by_identity_within_a_dispatch)
{
    World42 tw;
    walker loose_a;
    loose_a.set_order_family(Order::Living, FAMILY_SOLDIER);
    walker loose_b;
    loose_b.set_order_family(Order::Living, FAMILY_SOLDIER);
    ASSERT_EQ(0u, loose_a.entity_id());
    ASSERT_EQ(0u, loose_b.entity_id());
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  on_melee_hit = function(self, target)\n"
           "    og.log('eq', self == target)\n"
           "  end,\n"
           "})\n");
    EXPECT_TRUE(hooks::on_melee_hit(soldier_fd(), &loose_a, &loose_a));
    EXPECT_TRUE(hooks::on_melee_hit(soldier_fd(), &loose_a, &loose_b));
    EXPECT_EQ((std::vector<std::string>{"eq\ttrue", "eq\tfalse"}), vm_log())
        << "same untracked walker true, another untracked walker false";
}

// ===========================================================================
// og_register_hooks, declaration pass vs bind replay (world_scripts.cpp:830)
// ===========================================================================

// A family chunk may register hooks for the family it declares in the same
// chunk: the declaration pass defers the call, and the bind replay in the
// world VM installs it, so the hook dispatches.
TEST_F(SemScript3, a_family_chunk_registers_hooks_for_its_own_family_in_the_bind_replay)
{
    const char* kChunk =
        "og.family('living', { id = 'sem3:newguy', wire_id = 64,\n"
        "  name = 'NEWGUY' })\n"
        "og.register_hooks('living', 'sem3:newguy', {\n"
        "  on_death = function(self) og.log('newguy died') return true end,\n"
        "})\n";
    og::data::ClasspackData data;
    register_pack_family_chunk({"sem3", "sem3/families/newguy.lua", kChunk});
    const DeclareResult declared = declare_pack_families("sem3", data);
    clear_pack_family_chunks();
    ASSERT_TRUE(declared.ok) << declared.error;
    ASSERT_EQ(1, og::resources::install_classpack_data(std::move(data)));
    const FamilyDescriptor* fd = get_family_descriptor(kModWire);
    ASSERT_NE(nullptr, fd);

    register_pack_family_chunk({"sem3", "sem3/families/newguy.lua", kChunk});
    EXPECT_TRUE(errors().empty()) << all_errors();
    EXPECT_TRUE(ws().has_hook(Order::Living, kModWire, FamilyHook::OnDeath))
        << "bind replay arm: the hook is installed";
    const std::optional<bool> r = hooks::on_death(fd, nullptr);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r);
    EXPECT_EQ((std::vector<std::string>{"newguy died"}), vm_log())
        << "bind replay arm: the hook runs exactly once";
}

// ===========================================================================
// og_register_hooks specials keys (world_scripts.cpp:929)
// ===========================================================================

// A specials table holding only 'default' is not empty: it registers, and
// every slot dispatches to it.
TEST_F(SemScript3, a_specials_table_with_only_a_default_registers)
{
    World42 tw;
    walker* self = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_NE(nullptr, self);
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  specials = { default = function(self) og.log('d') return true end },\n"
           "})\n");
    ASSERT_TRUE(errors().empty()) << all_errors();
    self->set_current_special(1);
    const std::optional<SpecialResult> r = hooks::do_special(soldier_fd(), self);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r);
    EXPECT_EQ((std::vector<std::string>{"d"}), vm_log())
        << "default-only arm: registered and dispatched";
}

// ===========================================================================
// og_entity_id (world_scripts.cpp:1062)
// ===========================================================================

// og.entity_id answers 0 for a walker that was never added to the world.
TEST_F(SemScript3, entity_id_is_zero_for_an_untracked_walker)
{
    World42 tw;
    walker* tracked = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_NE(nullptr, tracked);
    walker loose;
    loose.set_order_family(Order::Living, FAMILY_SOLDIER);
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  do_special = function(self)\n"
           "    og.log('id', og.entity_id(self))\n"
           "    return true\n"
           "  end,\n"
           "})\n");
    ASSERT_TRUE(hooks::do_special(soldier_fd(), &loose).has_value())
        << all_errors();
    ASSERT_TRUE(hooks::do_special(soldier_fd(), tracked).has_value())
        << all_errors();
    EXPECT_EQ((std::vector<std::string>{
                  "id\t0", "id\t" + std::to_string(tracked->entity_id())}),
              vm_log())
        << "untracked arm 0; the tracked control answers its own id";
}

// ===========================================================================
// og_register_campaign_hooks, one book (world_scripts.cpp:1378)
// ===========================================================================

// A third registration does not move the recorded conflict: the first
// conflicting source (the second registration) is the one reported, once.
TEST_F(SemScript3, a_third_campaign_registration_keeps_the_first_conflicting_source)
{
    current_game = nullptr;
    const char* kBook = "og.register_campaign_hooks({\n"
                        "  picker_menu = function(page_id) return { title = 'B' } end,\n"
                        "})\n";
    script(kBook, "sem3/scripts/c1.lua");
    script(kBook, "sem3/scripts/c2.lua");
    script(kBook, "sem3/scripts/c3.lua");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    hooks::CampaignPage page;
    EXPECT_FALSE(hooks::campaign_picker_page("", page));

    std::size_t records = 0;
    for (const ScriptError& e : errors()) {
        if (e.message.find("duplicate og.register_campaign_hooks") ==
            std::string::npos)
            continue;
        records++;
        EXPECT_EQ(1u, e.count);
        EXPECT_NE(std::string::npos, e.message.find("c1.lua")) << e.message;
        EXPECT_NE(std::string::npos, e.message.find("c2.lua"))
            << "third+ arm: the first conflicter is kept: " << e.message;
        EXPECT_EQ(std::string::npos, e.message.find("c3.lua"))
            << "third+ arm: the third source never replaces it: " << e.message;
    }
    EXPECT_EQ(1u, records) << all_errors();
}

// ===========================================================================
// og_register_default_lineup keys (world_scripts.cpp:1473)
// ===========================================================================

// A misspelled lineup key is a load error that names the key and suggests
// the one it meant.
TEST_F(SemScript3, a_misspelled_default_lineup_key_suggests_power)
{
    current_game = nullptr;
    script("og.register_default_lineup({ powr = function(r) return 1 end })\n");
    ASSERT_EQ(1u, errors().size()) << all_errors();
    const std::string& m = errors()[0].message;
    EXPECT_NE(std::string::npos,
              m.find("og.register_default_lineup: unknown key 'powr'"))
        << m;
    EXPECT_NE(std::string::npos, m.find("did you mean 'power'"))
        << "misspelled arm: the did-you-mean hint: " << m;
    EXPECT_FALSE(hooks::campaign_lineup_registered());
}

// ===========================================================================
// og_family_id (world_scripts.cpp:1527)
// ===========================================================================

// og.family_id answers nil, not an error, for a family string nothing
// installed; the known control answers its wire byte.
TEST_F(SemScript3, family_id_is_nil_for_an_unknown_family)
{
    World42 tw;
    walker* self = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_NE(nullptr, self);
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  do_special = function(self)\n"
           "    local nope = og.family_id('living', 'core:nope')\n"
           "    og.log('nope', nope == nil, type(nope))\n"
           "    og.log('known', og.family_id('living', 'core:soldier'))\n"
           "    return true\n"
           "  end,\n"
           "})\n");
    ASSERT_TRUE(hooks::do_special(soldier_fd(), self).has_value())
        << all_errors();
    EXPECT_EQ((std::vector<std::string>{
                  "nope\ttrue\tnil",
                  "known\t" + std::to_string(FAMILY_SOLDIER)}),
              vm_log());
    EXPECT_TRUE(errors().empty()) << all_errors();
}

// ===========================================================================
// warn_unhandled_castable_specials (world_scripts.cpp:1597)
// ===========================================================================

namespace {

// Builds the VM (which runs the post-load report) and answers what it said
// on stderr and in the host's error store.
struct LoadReport {
    std::string said;
    std::string errors;
};

LoadReport build_and_report()
{
    testing::internal::CaptureStderr();
    WorldScripts& w = active_world_scripts();
    LoadReport r;
    r.said = testing::internal::GetCapturedStderr();
    for (const ScriptError& e : w.host().errors())
        r.errors += e.message + "\n";
    return r;
}

}  // namespace

// A declared family with castable slots but no Lua hook table at all (its
// chunk registered nothing for it) is not reported: there is nothing that
// could have fallen through.
TEST_F(SemScript3, a_family_with_no_hook_table_is_never_reported_unhandled)
{
    current_game = nullptr;
    note_lua_declared_family(Order::Living, FAMILY_SOLDIER, "sem3");
    // Some other family's specials table, so the VM has hook tables to walk.
    script("og.register_hooks('living', 'core:orc', {\n"
           "  specials = { howl = function(self) return true end },\n"
           "})\n");
    const LoadReport r = build_and_report();
    for (const char* name : {"CHARGE", "BOOMERANG", "WHIRLWIND", "DISARM"}) {
        EXPECT_EQ(std::string::npos, r.errors.find(std::string("'") + name + "'"))
            << "no-table arm: " << name << " reported: " << r.errors;
        EXPECT_EQ(std::string::npos, r.said.find(std::string("'") + name + "'"))
            << "no-table arm: " << name << " warned: " << r.said;
    }
    // Control: the orc's own unhandled slot IS reported, so the report ran.
    EXPECT_NE(std::string::npos, r.said.find("'EAT CORPSE'")) << r.said;
}

// A plain do_special function answers for every slot itself, so a declared
// family whose do_special is a function is never reported.
TEST_F(SemScript3, a_plain_do_special_function_counts_as_handling_every_slot)
{
    current_game = nullptr;
    note_lua_declared_family(Order::Living, FAMILY_SOLDIER, "sem3");
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  do_special = function(self) return true end,\n"
           "})\n");
    const LoadReport r = build_and_report();
    EXPECT_EQ(std::string::npos, r.errors.find("nothing handles it"))
        << "plain-function arm: " << r.errors;
    EXPECT_EQ(std::string::npos, r.said.find("has no handler"))
        << "plain-function arm: " << r.said;
    EXPECT_TRUE(ws().has_hook(Order::Living, FAMILY_SOLDIER, FamilyHook::DoSpecial));
}

// A disabled slot (name NONE) is not castable, so it is skipped: a family
// declaring one special and handling it reports nothing for slots 2..5.
TEST_F(SemScript3, a_disabled_none_slot_is_never_reported_unhandled)
{
    current_game = nullptr;
    og::data::ClasspackData data;
    register_pack_family_chunk(
        {"sem3", "sem3/families/one.lua",
         "og.family('living', { id = 'sem3:one', wire_id = 64, name = 'ONE',\n"
         "  specials = { { id = 'zap', name = 'ZAP', mp_cost = 5 } } })\n"});
    const DeclareResult declared = declare_pack_families("sem3", data);
    clear_pack_family_chunks();
    ASSERT_TRUE(declared.ok) << declared.error;
    ASSERT_EQ(1, og::resources::install_classpack_data(std::move(data)));
    const FamilyDescriptor* fd = get_family_descriptor(kModWire);
    ASSERT_NE(nullptr, fd);
    ASSERT_STREQ("ZAP", fd->special_names[1]);
    for (int slot = 2; slot < FD_NUM_SPECIALS; slot++)
        ASSERT_STREQ(kSpecialNameNone, fd->special_names[slot]) << slot;
    note_lua_declared_family(Order::Living, kModWire, "sem3");

    script("og.register_hooks('living', 'sem3:one', {\n"
           "  specials = { zap = function(self) return true end },\n"
           "})\n");
    const LoadReport r = build_and_report();
    EXPECT_TRUE(r.errors.empty()) << "none-slot arm: " << r.errors;
    EXPECT_EQ(std::string::npos, r.said.find("has no handler"))
        << "none-slot arm: " << r.said;
}

// ===========================================================================
// WorldScripts::WorldScripts lib pass (world_scripts.cpp:1765)
// ===========================================================================

// A module an earlier module already pulled in on demand, and which FAILED
// there, is settled: the eager pass skips it and its chunk never runs again.
TEST_F(SemScript3, a_module_that_failed_on_demand_is_not_loaded_a_second_time)
{
    current_game = nullptr;
    register_pack_lib_module({"sem3", "early", "libfix_sem3_early.lua",
                              "local ok = pcall(og.use, 'late')\n"
                              "og.log('early', ok)\n"
                              "return {}"});
    register_pack_lib_module({"sem3", "late", "libfix_sem3_late.lua",
                              "og.log('late ran')\n"
                              "error('kaput')"});
    script("local quiet = true\n");
    EXPECT_EQ((std::vector<std::string>{"late ran", "early\tfalse"}), vm_log())
        << "settled arm: 'late' ran once, on demand, never again";
    EXPECT_EQ(1u, errors_containing("kaput")) << all_errors();
}

// ===========================================================================
// WorldScripts::WorldScripts family pass (world_scripts.cpp:1785)
// ===========================================================================

// A family chunk that fails to bind is logged for the operator, naming the
// pack, the chunk and the error, and its failure is latched.
TEST_F(SemScript3, a_family_chunk_that_fails_to_bind_is_logged_once_and_latched)
{
    current_game = nullptr;
    register_pack_family_chunk({"sem3", "sem3/families/bad.lua",
                                "local x = og.rand(5)\n"});
    const LoadReport r = build_and_report();
    EXPECT_EQ(1u, count_of(r.said, "[ERROR] class pack 'sem3': family chunk "
                                   "sem3/families/bad.lua failed to bind: "))
        << "bind-fails arm: " << r.said;
    EXPECT_EQ(1u, count_of(r.said, "the world API is dispatch-time only"))
        << r.said;
    EXPECT_EQ(1u, errors_containing("the world API is dispatch-time only"))
        << all_errors();
}

// ===========================================================================
// WorldScripts::WorldScripts script pass (world_scripts.cpp:1802)
// ===========================================================================

// A behaviour chunk that fails to compile registers nothing (the family it
// names has no behaviour), is recorded and logged, and its pack-mate still
// loads.
TEST_F(SemScript3, a_script_chunk_that_fails_registers_nothing_and_is_logged)
{
    current_game = nullptr;
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  do_special = function(self) return true end,\n"
           "}) this is not lua (\n",
           "sem3/scripts/a_broken.lua");
    script("og.register_hooks('living', 'core:orc', {\n"
           "  do_special = function(self) return true end,\n"
           "})\n",
           "sem3/scripts/b_healthy.lua");
    const LoadReport r = build_and_report();
    EXPECT_EQ(1u, count_of(r.said, "[ERROR] class pack 'sem3': chunk "
                                   "sem3/scripts/a_broken.lua failed to load: "))
        << "chunk-fails arm: the operator line: " << r.said;
    ASSERT_EQ(1u, errors().size()) << all_errors();
    EXPECT_NE(std::string::npos, errors()[0].message.find("syntax error"))
        << errors()[0].message;
    EXPECT_FALSE(ws().has_hook(Order::Living, FAMILY_SOLDIER, FamilyHook::DoSpecial))
        << "chunk-fails arm: the broken chunk's family has no behaviour";
    EXPECT_TRUE(ws().has_hook(Order::Living, FAMILY_ORC, FamilyHook::DoSpecial))
        << "chunk-ok arm: the healthy pack-mate still registered";
}

// ===========================================================================
// note_hook_failure (world_scripts.cpp:1900)
// ===========================================================================

// Every failed dispatch counts; the operator line prints once per distinct
// error, and a new error prints its own line.
TEST_F(SemScript3, a_repeated_hook_error_logs_once_and_a_new_error_logs_again)
{
    current_game = nullptr;
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  on_death = function(self) error('boom') end,\n"
           "  on_shoved = function(self) error('other') end,\n"
           "})\n");
    (void)ws();  // build outside the capture
    const FamilyDescriptor* fd = soldier_fd();
    ASSERT_NE(nullptr, fd);

    testing::internal::CaptureStderr();
    EXPECT_FALSE(hooks::on_death(fd, nullptr).has_value());
    EXPECT_FALSE(hooks::on_death(fd, nullptr).has_value());
    const std::string twice = testing::internal::GetCapturedStderr();
    EXPECT_EQ(2u, hooks::hook_failures().count);
    EXPECT_EQ("hook:on_death", hooks::hook_failures().where);
    EXPECT_EQ(1u, count_of(twice, "family hook hook:on_death FAILED"))
        << "repeat arm: one operator line for two failures: " << twice;

    testing::internal::CaptureStderr();
    EXPECT_FALSE(hooks::on_shoved(fd, nullptr));
    const std::string fresh = testing::internal::GetCapturedStderr();
    EXPECT_EQ(3u, hooks::hook_failures().count);
    EXPECT_EQ("hook:on_shoved", hooks::hook_failures().where);
    EXPECT_NE(std::string::npos, hooks::hook_failures().message.find("other"))
        << hooks::hook_failures().message;
    EXPECT_EQ(1u, count_of(fresh, "family hook hook:on_shoved FAILED"))
        << "new-error arm: its own operator line: " << fresh;
}

// ===========================================================================
// HookFrame::call (world_scripts.cpp:2012)
// ===========================================================================

// A result hook's answer is Lua truthiness, not C's: 0 and '' are true, nil
// and false are false.
TEST_F(SemScript3, a_result_hook_answer_is_lua_truthiness)
{
    current_game = nullptr;
    const struct {
        const char* ret;
        bool want;
    } cases[] = {{"0", true}, {"''", true}, {"{}", true}, {"nil", false},
                 {"false", false}};
    for (const auto& c : cases) {
        clear_pack_scripts();
        script(std::string("og.register_hooks('living', 'core:soldier', {\n"
                           "  check_special_ai = function(self) return ") +
               c.ret + " end,\n})\n");
        const std::optional<bool> r = hooks::check_special_ai(soldier_fd(), nullptr);
        ASSERT_TRUE(r.has_value()) << c.ret << ": " << all_errors();
        EXPECT_EQ(c.want, *r) << "returns " << c.ret;
    }
    EXPECT_EQ(0u, hooks::hook_failures().count);
}

// ===========================================================================
// hooks::* wrappers, descriptor gate (world_scripts.cpp:2187)
// ===========================================================================

// An entity whose family has no descriptor has no behaviour: on_kill answers
// false and latches nothing, and hit_response refuses a null statistics
// block even for a family whose hit_response hook IS registered.
TEST_F(SemScript3, a_null_descriptor_or_null_stats_has_no_script_behaviour)
{
    World42 tw;
    walker* self = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    walker* foe = tw.living(FAMILY_ORC, 1, 140, 100);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, foe);
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  on_kill = function(self, victim) og.log('on_kill') end,\n"
           "  hit_response = function(self, foe) og.log('hit_response') end,\n"
           "})\n");
    ASSERT_TRUE(errors().empty()) << all_errors();
    const FamilyDescriptor* none = get_family_descriptor(NUM_FAMILY_SLOTS - 1);
    ASSERT_EQ(nullptr, none);

    EXPECT_FALSE(hooks::on_kill(none, self, foe)) << "null-descriptor arm: on_kill";
    EXPECT_FALSE(hooks::hit_response(soldier_fd(), nullptr, foe))
        << "null-stats arm: hit_response";
    EXPECT_TRUE(vm_log().empty()) << "no hook ran";
    EXPECT_EQ(0u, hooks::hook_failures().count);

    // Control: the same two calls with a descriptor and a stats block run.
    EXPECT_TRUE(hooks::on_kill(soldier_fd(), self, foe));
    EXPECT_TRUE(hooks::hit_response(soldier_fd(), self->stats(), foe));
    EXPECT_EQ((std::vector<std::string>{"on_kill", "hit_response"}), vm_log());
}

// A family that has a descriptor proceeds to its Lua hook: every wrapper the
// review found uncalled with a real descriptor answers, and its hook logs.
TEST_F(SemScript3, every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor)
{
    World42 tw;
    walker* self = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    walker* foe = tw.living(FAMILY_ORC, 1, 140, 100);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, foe);
    guy g(FAMILY_SOLDIER);
    std::string living_hooks;
    for (const char* h : {"check_special_ai", "hit_response", "set_difficulty",
                          "level_up", "on_death", "on_act_living",
                          "on_act_override", "on_fire_weapon", "handle_teleport",
                          "on_create", "customize_weapon", "on_ani_complete",
                          "on_melee_hit", "on_kill"})
        living_hooks += std::string("  ") + h + " = function() og.log('" + h +
                        "') return true end,\n";
    script("og.register_hooks('living', 'core:soldier', {\n" + living_hooks +
           "})\n"
           "og.register_hooks('weapon', 'core:knife', {\n"
           "  on_animate = function() og.log('weapon.on_animate') return true end,\n"
           "  on_hit_target = function() og.log('weapon.on_hit_target') end,\n"
           "})\n"
           "og.register_hooks('fx', 'core:boomerang', {\n"
           "  on_death = function() og.log('effect.on_death') return true end,\n"
           "})\n"
           "og.register_hooks('treasure', 'core:drumstick', {\n"
           "  on_eat = function() og.log('treasure.on_eat') return true end,\n"
           "})\n");
    ASSERT_TRUE(errors().empty()) << all_errors();
    const FamilyDescriptor* fd = soldier_fd();
    const WeaponFamilyDescriptor* knife = get_weapon_family_descriptor(FAMILY_KNIFE);
    const EffectFamilyDescriptor* boomerang =
        get_effect_family_descriptor(FAMILY_BOOMERANG);
    const TreasureFamilyDescriptor* drumstick =
        get_treasure_family_descriptor(FAMILY_DRUMSTICK);
    ASSERT_NE(nullptr, knife);
    ASSERT_NE(nullptr, boomerang);
    ASSERT_NE(nullptr, drumstick);
    living* me = static_cast<living*>(self);

    EXPECT_EQ(std::optional<bool>(true), hooks::check_special_ai(fd, me));
    EXPECT_TRUE(hooks::hit_response(fd, self->stats(), foe));
    EXPECT_TRUE(hooks::set_difficulty(fd, me, 2));
    EXPECT_TRUE(hooks::level_up(fd, &g, 1));
    EXPECT_EQ(std::optional<bool>(true), hooks::on_death(fd, self));
    EXPECT_TRUE(hooks::on_act_living(fd, me));
    EXPECT_TRUE(hooks::on_act_override(fd, me));
    EXPECT_EQ(std::optional<bool>(true), hooks::on_fire_weapon(fd, self, nullptr));
    EXPECT_EQ(std::optional<bool>(true), hooks::handle_teleport(fd, self));
    EXPECT_TRUE(hooks::on_create(fd, self));
    EXPECT_TRUE(hooks::customize_weapon(fd, self, nullptr));
    EXPECT_EQ(std::optional<bool>(true), hooks::on_ani_complete(fd, self));
    EXPECT_TRUE(hooks::on_melee_hit(fd, self, foe));
    EXPECT_TRUE(hooks::on_kill(fd, self, foe));
    EXPECT_EQ(std::optional<bool>(true), hooks::weapon_on_animate(knife, nullptr));
    EXPECT_TRUE(hooks::weapon_on_hit_target(knife, nullptr, foe, self));
    EXPECT_EQ(std::optional<bool>(true), hooks::effect_on_death(boomerang, nullptr));
    EXPECT_EQ(std::optional<bool>(true),
              hooks::treasure_on_eat(drumstick, nullptr, self));

    EXPECT_EQ((std::vector<std::string>{
                  "check_special_ai", "hit_response", "set_difficulty",
                  "level_up", "on_death", "on_act_living", "on_act_override",
                  "on_fire_weapon", "handle_teleport", "on_create",
                  "customize_weapon", "on_ani_complete", "on_melee_hit",
                  "on_kill", "weapon.on_animate", "weapon.on_hit_target",
                  "effect.on_death", "treasure.on_eat"}),
              vm_log())
        << "descriptor-present arm: each wrapper ran its own hook, in order";
    EXPECT_EQ(0u, hooks::hook_failures().count);
}

// ===========================================================================
// hit_response / set_difficulty / level_up replace the C++ default
// (world_scripts.cpp:2221); a hook that errors is treated as absent, so the
// default runs on top of what the hook changed before its error (#463).
// ===========================================================================

// A set_difficulty hook that runs replaces the built-in 11*level^2 scaling;
// with no hook the built-in (the old game's default arm) applies.
TEST_F(SemScript3, a_set_difficulty_hook_that_runs_replaces_the_default_scaling)
{
    {
        World42 tw;
        tw.world().difficulty = 100;
        walker* w = tw.living(FAMILY_SOLDIER, 0, 100, 100);
        ASSERT_NE(nullptr, w);
        const float base_hp = w->stats()->max_hitpoints();
        const float base_armor = w->stats()->armor();
        script("og.register_hooks('living', 'core:soldier', {\n"
               "  on_death = function() return true end,\n"
               "})\n");
        (void)ws();
        w->set_difficulty(3);
        EXPECT_EQ(base_hp + 99.0f, w->stats()->max_hitpoints())
            << "no-hook arm: the default adds 11*3^2 HP";
        EXPECT_EQ(base_armor + 18.0f, w->stats()->armor())
            << "no-hook arm: the default adds 2*3^2 armor";
    }
    clear_pack_scripts();
    {
        World42 tw;
        tw.world().difficulty = 100;
        walker* w = tw.living(FAMILY_SOLDIER, 0, 100, 100);
        ASSERT_NE(nullptr, w);
        const float base_armor = w->stats()->armor();
        script("og.register_hooks('living', 'core:soldier', {\n"
               "  set_difficulty = function(self, level)\n"
               "    og.log('set_difficulty', level)\n"
               "    self:s_set_max_hitpoints(123)\n"
               "  end,\n"
               "})\n");
        w->set_difficulty(3);
        EXPECT_EQ((std::vector<std::string>{"set_difficulty\t3"}), vm_log());
        EXPECT_EQ(123.0f, w->stats()->max_hitpoints())
            << "hook-ran arm: the default's +99 is not applied";
        EXPECT_EQ(123.0f, w->stats()->hitpoints());
        EXPECT_EQ(base_armor, w->stats()->armor())
            << "hook-ran arm: the default's armor gain is not applied";
    }
}

// A level_up hook that runs replaces the built-in 8/6/8/8/1 gains; with no
// hook the built-in applies.
TEST_F(SemScript3, a_level_up_hook_that_runs_replaces_the_default_gains)
{
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  on_death = function() return true end,\n"
           "})\n");
    guy plain(FAMILY_SOLDIER);
    const guy before_plain(plain);
    plain.upgrade_to_level(static_cast<short>(plain.level + 2), false);
    EXPECT_EQ(before_plain.strength + 16, plain.strength) << "no-hook arm";
    EXPECT_EQ(before_plain.dexterity + 12, plain.dexterity) << "no-hook arm";
    EXPECT_EQ(before_plain.constitution + 16, plain.constitution) << "no-hook arm";
    EXPECT_EQ(before_plain.intelligence + 16, plain.intelligence) << "no-hook arm";
    EXPECT_EQ(before_plain.armor + 2, plain.armor) << "no-hook arm";
    EXPECT_EQ(before_plain.level + 2, plain.level);

    clear_pack_scripts();
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  level_up = function(g, diff)\n"
           "    og.log('level_up', diff)\n"
           "    og.apply_level_up(g, diff, 1, 2, 3, 4, 5)\n"
           "  end,\n"
           "})\n");
    guy hooked(FAMILY_SOLDIER);
    const guy before(hooked);
    hooked.upgrade_to_level(static_cast<short>(hooked.level + 2), false);
    EXPECT_EQ((std::vector<std::string>{"level_up\t2"}), vm_log());
    EXPECT_EQ(before.strength + 2, hooked.strength) << "hook-ran arm";
    EXPECT_EQ(before.dexterity + 4, hooked.dexterity) << "hook-ran arm";
    EXPECT_EQ(before.constitution + 6, hooked.constitution) << "hook-ran arm";
    EXPECT_EQ(before.intelligence + 8, hooked.intelligence) << "hook-ran arm";
    EXPECT_EQ(before.armor + 10, hooked.armor) << "hook-ran arm";
    EXPECT_EQ(before.level + 2, hooked.level);
}

// A hit_response hook that runs replaces the built-in "attack the attacker"
// response; with no hook the built-in retargets the victim onto its attacker.
TEST_F(SemScript3, a_hit_response_hook_that_runs_replaces_attack_the_attacker)
{
    for (const bool hooked : {false, true}) {
        clear_pack_scripts();
        World42 tw;
        walker* victim = tw.living(FAMILY_SOLDIER, 0, 100, 100);
        walker* attacker = tw.living(FAMILY_ORC, 1, 140, 100);
        ASSERT_NE(nullptr, victim);
        ASSERT_NE(nullptr, attacker);
        victim->set_act_type(ACT_GUARD);
        victim->stats()->set_magicpoints(0.0f);  // no counter-special to buy
        script(hooked ? "og.register_hooks('living', 'core:soldier', {\n"
                        "  hit_response = function(self, foe)\n"
                        "    og.log('hit', og.entity_id(foe))\n"
                        "  end,\n"
                        "})\n"
                      : "og.register_hooks('living', 'core:soldier', {\n"
                        "  on_death = function() return true end,\n"
                        "})\n");
        (void)ws();
        ASSERT_EQ(nullptr, victim->foe());
        victim->stats()->hit_response(attacker);
        if (hooked) {
            EXPECT_EQ((std::vector<std::string>{
                          "hit\t" + std::to_string(attacker->entity_id())}),
                      vm_log());
            EXPECT_EQ(nullptr, victim->foe())
                << "hook-ran arm: the default retarget is skipped";
            EXPECT_EQ(nullptr, attacker->foe());
        } else {
            EXPECT_EQ(attacker, victim->foe())
                << "no-hook arm: the default attacks the attacker";
            EXPECT_EQ(victim, attacker->foe());
        }
    }
}

// #463: a set_difficulty hook that errors is treated as absent. What it
// changed before the error stays, and the built-in 11*level^2 scaling runs on
// top of it; the operator line says the default ran.
TEST_F(SemScript3, a_set_difficulty_hook_that_errors_keeps_its_change_and_the_default_scaling_runs_too)
{
    World42 tw;
    tw.world().difficulty = 100;
    walker* w = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_NE(nullptr, w);
    const float base_hp = w->stats()->max_hitpoints();
    const float base_armor = w->stats()->armor();
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  set_difficulty = function(self, level)\n"
           "    self:s_set_max_hitpoints(self:s_max_hitpoints() + 1000)\n"
           "    error('oops')\n"
           "  end,\n"
           "})\n");
    (void)ws();  // build outside the capture

    testing::internal::CaptureStderr();
    w->set_difficulty(3);
    const std::string said = testing::internal::GetCapturedStderr();
    EXPECT_EQ(1u, hooks::hook_failures().count);
    EXPECT_EQ("hook:set_difficulty", hooks::hook_failures().where);
    EXPECT_EQ(base_hp + 1000.0f + 99.0f, w->stats()->max_hitpoints())
        << "the hook's +1000 stays and the default's 11*3^2 is added";
    EXPECT_EQ(base_armor + 18.0f, w->stats()->armor())
        << "the default's 2*3^2 armor is added";
    EXPECT_EQ(1u, count_of(said, "family hook hook:set_difficulty FAILED")) << said;
    EXPECT_EQ(1u, count_of(said, "default ran in its place")) << said;
    EXPECT_EQ(0u, count_of(said, "nothing ran")) << said;
}

// #463: a level_up hook that errors keeps the gains it applied, and the
// built-in 8/6/8/8/1 gains run too.
TEST_F(SemScript3, a_level_up_hook_that_errors_keeps_its_gains_and_the_default_gains_run_too)
{
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  level_up = function(g, diff)\n"
           "    og.apply_level_up(g, diff, 1, 2, 3, 4, 5)\n"
           "    error('oops')\n"
           "  end,\n"
           "})\n");
    (void)ws();
    guy hooked(FAMILY_SOLDIER);
    const guy before(hooked);

    testing::internal::CaptureStderr();
    hooked.upgrade_to_level(static_cast<short>(hooked.level + 2), false);
    const std::string said = testing::internal::GetCapturedStderr();
    EXPECT_EQ(1u, hooks::hook_failures().count);
    EXPECT_EQ("hook:level_up", hooks::hook_failures().where);
    EXPECT_EQ(before.strength + 2 + 16, hooked.strength) << "hook's 1*2, default's 8*2";
    EXPECT_EQ(before.dexterity + 4 + 12, hooked.dexterity) << "hook's 2*2, default's 6*2";
    EXPECT_EQ(before.constitution + 6 + 16, hooked.constitution)
        << "hook's 3*2, default's 8*2";
    EXPECT_EQ(before.intelligence + 8 + 16, hooked.intelligence)
        << "hook's 4*2, default's 8*2";
    EXPECT_EQ(before.armor + 10 + 2, hooked.armor) << "hook's 5*2, default's 1*2";
    EXPECT_EQ(before.level + 2, hooked.level);
    EXPECT_EQ(1u, count_of(said, "family hook hook:level_up FAILED")) << said;
    EXPECT_EQ(1u, count_of(said, "default ran in its place")) << said;
}

// #463: a hit_response hook that errors after it ran still leaves the victim
// attacking its attacker: the built-in response runs in the hook's place.
TEST_F(SemScript3, a_hit_response_hook_that_errors_still_attacks_the_attacker)
{
    World42 tw;
    walker* victim = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    walker* attacker = tw.living(FAMILY_ORC, 1, 140, 100);
    ASSERT_NE(nullptr, victim);
    ASSERT_NE(nullptr, attacker);
    victim->set_act_type(ACT_GUARD);
    victim->stats()->set_magicpoints(0.0f);  // no counter-special to buy
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  hit_response = function(self, foe)\n"
           "    og.log('hit', og.entity_id(foe))\n"
           "    error('oops')\n"
           "  end,\n"
           "})\n");
    (void)ws();
    ASSERT_EQ(nullptr, victim->foe());

    testing::internal::CaptureStderr();
    victim->stats()->hit_response(attacker);
    const std::string said = testing::internal::GetCapturedStderr();
    EXPECT_EQ((std::vector<std::string>{
                  "hit\t" + std::to_string(attacker->entity_id())}),
              vm_log())
        << "the hook ran up to its error";
    EXPECT_EQ(1u, hooks::hook_failures().count);
    EXPECT_EQ(attacker, victim->foe()) << "the default attacks the attacker";
    EXPECT_EQ(victim, attacker->foe());
    EXPECT_EQ(1u, count_of(said, "family hook hook:hit_response FAILED")) << said;
    EXPECT_EQ(1u, count_of(said, "default ran in its place")) << said;
}

// #463: the failure line of every one of the 23 family hooks says what its
// caller did in its place: the engine's default ran (the twelve hooks whose
// caller has one), or nothing ran (do_special, whose cast is refused free,
// and the notification hooks).
TEST_F(SemScript3, the_failure_line_says_nothing_ran_only_for_a_hook_with_no_engine_default)
{
    World42 tw;
    walker* self = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    walker* foe = tw.living(FAMILY_ORC, 1, 140, 100);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, foe);
    guy g(FAMILY_SOLDIER);
    std::string living_hooks;
    for (const char* h : {"do_special", "check_special_ai", "hit_response",
                          "set_difficulty", "level_up", "on_death",
                          "on_act_living", "on_act_override", "on_shoved",
                          "on_fire_weapon", "handle_teleport", "on_create",
                          "customize_weapon", "on_ani_complete", "on_melee_hit",
                          "on_kill"})
        living_hooks += std::string("  ") + h + " = function() error('" + h +
                        " broke') end,\n";
    script("og.register_hooks('living', 'core:soldier', {\n" + living_hooks +
           "})\n"
           "og.register_hooks('weapon', 'core:knife', {\n"
           "  on_death = function() error('weapon on_death broke') end,\n"
           "  on_animate = function() error('weapon on_animate broke') end,\n"
           "  on_hit_target = function() error('weapon on_hit_target broke') end,\n"
           "})\n"
           "og.register_hooks('fx', 'core:boomerang', {\n"
           "  on_act = function() error('effect on_act broke') end,\n"
           "  on_death = function() error('effect on_death broke') end,\n"
           "})\n"
           "og.register_hooks('treasure', 'core:drumstick', {\n"
           "  on_eat = function() error('treasure on_eat broke') end,\n"
           "})\n"
           "og.register_hooks('generator', 'core:tent', {\n"
           "  customize_spawn = function() error('customize_spawn broke') end,\n"
           "})\n");
    ASSERT_TRUE(errors().empty()) << all_errors();
    (void)ws();
    const FamilyDescriptor* fd = soldier_fd();
    const WeaponFamilyDescriptor* knife = get_weapon_family_descriptor(FAMILY_KNIFE);
    const EffectFamilyDescriptor* boomerang =
        get_effect_family_descriptor(FAMILY_BOOMERANG);
    const TreasureFamilyDescriptor* drumstick =
        get_treasure_family_descriptor(FAMILY_DRUMSTICK);
    ASSERT_NE(nullptr, fd);
    ASSERT_NE(nullptr, knife);
    ASSERT_NE(nullptr, boomerang);
    ASSERT_NE(nullptr, drumstick);
    living* me = static_cast<living*>(self);

    const struct {
        const char* where;
        bool default_ran;
        std::function<void()> dispatch;
    } cases[] = {
        {"hook:do_special", false, [&] { (void)hooks::do_special(fd, self); }},
        {"hook:check_special_ai", true, [&] { (void)hooks::check_special_ai(fd, me); }},
        {"hook:hit_response", true, [&] { (void)hooks::hit_response(fd, self->stats(), foe); }},
        {"hook:set_difficulty", true, [&] { (void)hooks::set_difficulty(fd, me, 2); }},
        {"hook:level_up", true, [&] { (void)hooks::level_up(fd, &g, 1); }},
        {"hook:on_death", true, [&] { (void)hooks::on_death(fd, self); }},
        {"hook:on_act_living", false, [&] { (void)hooks::on_act_living(fd, me); }},
        {"hook:on_act_override", true, [&] { (void)hooks::on_act_override(fd, me); }},
        {"hook:on_shoved", false, [&] { (void)hooks::on_shoved(fd, self); }},
        {"hook:on_fire_weapon", true, [&] { (void)hooks::on_fire_weapon(fd, self, nullptr); }},
        {"hook:handle_teleport", true, [&] { (void)hooks::handle_teleport(fd, self); }},
        {"hook:on_create", false, [&] { (void)hooks::on_create(fd, self); }},
        {"hook:customize_weapon", false, [&] { (void)hooks::customize_weapon(fd, self, nullptr); }},
        {"hook:on_ani_complete", true, [&] { (void)hooks::on_ani_complete(fd, self); }},
        {"hook:on_melee_hit", false, [&] { (void)hooks::on_melee_hit(fd, self, foe); }},
        {"hook:on_kill", false, [&] { (void)hooks::on_kill(fd, self, foe); }},
        {"hook:weapon.on_death", false, [&] { (void)hooks::weapon_on_death(knife, nullptr); }},
        {"hook:weapon.on_animate", true, [&] { (void)hooks::weapon_on_animate(knife, nullptr); }},
        {"hook:weapon.on_hit_target", false,
         [&] { (void)hooks::weapon_on_hit_target(knife, nullptr, foe, self); }},
        {"hook:effect.on_act", true, [&] { (void)hooks::effect_on_act(boomerang, nullptr); }},
        {"hook:effect.on_death", false, [&] { (void)hooks::effect_on_death(boomerang, nullptr); }},
        {"hook:treasure.on_eat", true,
         [&] { (void)hooks::treasure_on_eat(drumstick, nullptr, self); }},
        {"hook:generator.customize_spawn", false,
         [&] { (void)hooks::generator_customize_spawn(0 /*FAMILY_TENT*/, nullptr, self); }},
    };
    ASSERT_EQ(static_cast<std::size_t>(FamilyHook::Count), std::size(cases))
        << "every family hook has its case";
    std::uint64_t failures = 0;
    for (const auto& c : cases) {
        testing::internal::CaptureStderr();
        c.dispatch();
        const std::string said = testing::internal::GetCapturedStderr();
        ++failures;
        EXPECT_EQ(failures, hooks::hook_failures().count) << c.where;
        EXPECT_EQ(c.where, hooks::hook_failures().where);
        EXPECT_EQ(1u, count_of(said, std::string("family hook ") + c.where + " FAILED"))
            << c.where << ": " << said;
        EXPECT_EQ(c.default_ran ? 1u : 0u, count_of(said, "default ran in its place"))
            << c.where << ": " << said;
        EXPECT_EQ(c.default_ran ? 0u : 1u,
                  count_of(said, "it has no engine default, so nothing ran in its place"))
            << c.where << ": " << said;
    }
}

// ===========================================================================
// Notification hooks (world_scripts.cpp:2253)
// ===========================================================================

namespace {

// The eight notification wrappers, called on one rig; each answers whether
// its hook ran without error.
struct NotifyRig {
    walker* self = nullptr;
    walker* foe = nullptr;
    walker* knife = nullptr;
    walker* tent = nullptr;
};

std::vector<bool> call_notifications(const NotifyRig& r)
{
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    const WeaponFamilyDescriptor* knife = get_weapon_family_descriptor(FAMILY_KNIFE);
    living* me = static_cast<living*>(r.self);
    return {hooks::on_act_living(fd, me),
            hooks::on_shoved(fd, r.self),
            hooks::on_create(fd, r.self),
            hooks::customize_weapon(fd, r.self, r.knife),
            hooks::on_melee_hit(fd, r.self, r.foe),
            hooks::on_kill(fd, r.self, r.foe),
            hooks::weapon_on_hit_target(knife, r.knife, r.foe, r.self),
            hooks::generator_customize_spawn(FAMILY_TENT, r.tent, r.foe)};
}

const char* const kNotifyWhere[] = {
    "hook:on_act_living", "hook:on_shoved",     "hook:on_create",
    "hook:customize_weapon", "hook:on_melee_hit", "hook:on_kill",
    "hook:weapon.on_hit_target", "hook:generator.customize_spawn"};

}  // namespace

// Each notification hook that runs answers true and its Lua side effects
// land on the walkers it was handed.
TEST_F(SemScript3, a_notification_hook_that_runs_answers_true_with_its_side_effects)
{
    World42 tw;
    NotifyRig r;
    r.self = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    r.foe = tw.living(FAMILY_ORC, 1, 140, 100);
    r.knife = tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    r.tent = tw.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, r.self);
    ASSERT_NE(nullptr, r.foe);
    ASSERT_NE(nullptr, r.knife);
    ASSERT_NE(nullptr, r.tent);
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  on_act_living = function(self) self:s_set_hitpoints(11) end,\n"
           "  on_shoved = function(self) self:s_set_hitpoints(12) end,\n"
           "  on_create = function(self) self:s_set_hitpoints(13) end,\n"
           "  customize_weapon = function(self, w) w:s_set_hitpoints(14) end,\n"
           "  on_melee_hit = function(self, t) t:s_set_hitpoints(15) end,\n"
           "  on_kill = function(self, v) v:s_set_hitpoints(16) end,\n"
           "})\n"
           "og.register_hooks('weapon', 'core:knife', {\n"
           "  on_hit_target = function(w, t, o) o:s_set_hitpoints(17) end,\n"
           "})\n"
           "og.register_hooks('generator', 'core:tent', {\n"
           "  customize_spawn = function(g, s) s:s_set_hitpoints(18) end,\n"
           "})\n");
    ASSERT_TRUE(errors().empty()) << all_errors();
    const FamilyDescriptor* fd = soldier_fd();
    const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(FAMILY_KNIFE);
    living* me = static_cast<living*>(r.self);

    EXPECT_TRUE(hooks::on_act_living(fd, me));
    EXPECT_EQ(11.0f, r.self->stats()->hitpoints()) << "on_act_living ran";
    EXPECT_TRUE(hooks::on_shoved(fd, r.self));
    EXPECT_EQ(12.0f, r.self->stats()->hitpoints()) << "on_shoved ran";
    EXPECT_TRUE(hooks::on_create(fd, r.self));
    EXPECT_EQ(13.0f, r.self->stats()->hitpoints()) << "on_create ran";
    EXPECT_TRUE(hooks::customize_weapon(fd, r.self, r.knife));
    EXPECT_EQ(14.0f, r.knife->stats()->hitpoints()) << "customize_weapon ran";
    EXPECT_TRUE(hooks::on_melee_hit(fd, r.self, r.foe));
    EXPECT_EQ(15.0f, r.foe->stats()->hitpoints()) << "on_melee_hit ran";
    EXPECT_TRUE(hooks::on_kill(fd, r.self, r.foe));
    EXPECT_EQ(16.0f, r.foe->stats()->hitpoints()) << "on_kill ran";
    EXPECT_TRUE(hooks::weapon_on_hit_target(wfd, r.knife, r.foe, r.self));
    EXPECT_EQ(17.0f, r.self->stats()->hitpoints()) << "weapon on_hit_target ran";
    EXPECT_TRUE(hooks::generator_customize_spawn(FAMILY_TENT, r.tent, r.foe));
    EXPECT_EQ(18.0f, r.foe->stats()->hitpoints()) << "customize_spawn ran";
    EXPECT_EQ(0u, hooks::hook_failures().count);
}

// A family with a descriptor but no hook of that kind answers false and
// latches nothing, for every notification wrapper.
TEST_F(SemScript3, a_notification_with_no_hook_of_its_kind_answers_false)
{
    World42 tw;
    NotifyRig r;
    r.self = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    r.foe = tw.living(FAMILY_ORC, 1, 140, 100);
    r.knife = tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    r.tent = tw.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, r.tent);
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  on_death = function() og.log('on_death') return true end,\n"
           "})\n"
           "og.register_hooks('weapon', 'core:knife', {\n"
           "  on_death = function() return true end,\n"
           "})\n");
    ASSERT_TRUE(errors().empty()) << all_errors();
    const std::vector<bool> got = call_notifications(r);
    ASSERT_EQ(8u, got.size());
    for (std::size_t i = 0; i < got.size(); i++)
        EXPECT_FALSE(got[i]) << "no-hook arm: " << kNotifyWhere[i];
    EXPECT_EQ(0u, hooks::hook_failures().count);
    EXPECT_TRUE(vm_log().empty());
    // Control: the one registered kind does answer.
    EXPECT_EQ(std::optional<bool>(true), hooks::on_death(soldier_fd(), r.self));
}

// A notification hook that errors answers false and latches the failure
// under its own where label.
TEST_F(SemScript3, a_notification_hook_that_errors_answers_false_and_latches)
{
    World42 tw;
    NotifyRig r;
    r.self = tw.living(FAMILY_SOLDIER, 0, 100, 100);
    r.foe = tw.living(FAMILY_ORC, 1, 140, 100);
    r.knife = tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    r.tent = tw.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, r.tent);
    script("og.register_hooks('living', 'core:soldier', {\n"
           "  on_act_living = function() error('e1') end,\n"
           "  on_shoved = function() error('e2') end,\n"
           "  on_create = function() error('e3') end,\n"
           "  customize_weapon = function() error('e4') end,\n"
           "  on_melee_hit = function() error('e5') end,\n"
           "  on_kill = function() error('e6') end,\n"
           "})\n"
           "og.register_hooks('weapon', 'core:knife', {\n"
           "  on_hit_target = function() error('e7') end,\n"
           "})\n"
           "og.register_hooks('generator', 'core:tent', {\n"
           "  customize_spawn = function() error('e8') end,\n"
           "})\n");
    ASSERT_TRUE(errors().empty()) << all_errors();
    (void)ws();
    testing::internal::CaptureStderr();
    const FamilyDescriptor* fd = soldier_fd();
    const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(FAMILY_KNIFE);
    living* me = static_cast<living*>(r.self);
    const std::vector<std::pair<bool, std::string>> got = {
        {hooks::on_act_living(fd, me), hooks::hook_failures().where},
        {hooks::on_shoved(fd, r.self), hooks::hook_failures().where},
        {hooks::on_create(fd, r.self), hooks::hook_failures().where},
        {hooks::customize_weapon(fd, r.self, r.knife), hooks::hook_failures().where},
        {hooks::on_melee_hit(fd, r.self, r.foe), hooks::hook_failures().where},
        {hooks::on_kill(fd, r.self, r.foe), hooks::hook_failures().where},
        {hooks::weapon_on_hit_target(wfd, r.knife, r.foe, r.self),
         hooks::hook_failures().where},
        {hooks::generator_customize_spawn(FAMILY_TENT, r.tent, r.foe),
         hooks::hook_failures().where}};
    (void)testing::internal::GetCapturedStderr();
    for (std::size_t i = 0; i < got.size(); i++) {
        EXPECT_FALSE(got[i].first) << "errored arm: " << kNotifyWhere[i];
        EXPECT_EQ(kNotifyWhere[i], got[i].second) << "errored arm: where label";
    }
    EXPECT_EQ(8u, hooks::hook_failures().count);
    EXPECT_NE(std::string::npos, hooks::hook_failures().message.find("e8"))
        << hooks::hook_failures().message;
}

// ===========================================================================
// hooks::on_act_override fall-through (world_scripts.cpp:2263)
// ===========================================================================

// An on_act_override that errors, or a family with no override at all,
// falls through to the classic act: the frozen delay ticks down.
TEST_F(SemScript3, an_erroring_or_absent_act_override_falls_through_to_the_default_act)
{
    for (const bool erroring : {true, false}) {
        clear_pack_scripts();
        World42 tw;
        tw.world().type = GameWorld::TYPE_SCRIPTED;
        script(erroring ? "og.register_hooks('living', 'core:soldier', {\n"
                          "  on_act_override = function(self)\n"
                          "    og.log('override', og.entity_id(self))\n"
                          "    error('ovr boom')\n"
                          "  end,\n"
                          "})\n"
                        : "og.register_hooks('living', 'core:soldier', {\n"
                          "  on_death = function() return true end,\n"
                          "})\n");
        walker* w = tw.living(FAMILY_SOLDIER, 0, 160, 160);
        ASSERT_NE(nullptr, w);
        w->set_real_team_num(255);
        w->set_act_type(ACT_CONTROL);
        w->set_ani_type(ANI_WALK);
        w->stats()->set_frozen_delay(3);
        const std::string id = std::to_string(w->entity_id());
        testing::internal::CaptureStderr();
        tw.world().tick();
        (void)testing::internal::GetCapturedStderr();
        EXPECT_EQ(2, w->stats()->frozen_delay())
            << (erroring ? "errors" : "absent") << " arm: the default act ran";
        if (erroring) {
            EXPECT_EQ(1u, log_count("override\t" + id)) << "the override was asked";
            EXPECT_EQ(1u, errors_containing("ovr boom")) << all_errors();
        } else {
            EXPECT_TRUE(vm_log().empty());
            EXPECT_TRUE(errors().empty()) << all_errors();
        }
    }
}

// ===========================================================================
// push_level_hook_fn (world_scripts.cpp:2460)
// ===========================================================================

// A level's own hook takes precedence over the wildcard of the same kind:
// only the exact one runs.
TEST_F(SemScript3, a_levels_own_hook_takes_precedence_over_the_wildcard)
{
    World42 tw;
    script("og.register_level_hooks(42, {\n"
           "  on_load = function(l) og.log('exact', l) end,\n"
           "})\n"
           "og.register_level_hooks(-1, {\n"
           "  on_load = function(l) og.log('wild', l) end,\n"
           "})\n");
    tw.world().tick();
    tw.world().tick();
    EXPECT_EQ((std::vector<std::string>{"exact\t42"}), vm_log())
        << "exact arm: the level's own on_load ran once, the wildcard never";
    EXPECT_TRUE(errors().empty()) << all_errors();
}

// ===========================================================================
// level_entity_death attribution (world_scripts.cpp:2545)
// ===========================================================================

// A fresh stamp whose killer has been swept from the world passes a nil
// killer but keeps the stamped team.
TEST_F(SemScript3, a_swept_killer_passes_nil_but_keeps_the_stamped_team)
{
    World42 tw;
    script("og.register_level_hooks(42, {\n"
           "  on_entity_death = function(ent, killer, killer_team)\n"
           "    og.log('death', killer == nil and 1 or 0, killer_team)\n"
           "  end,\n"
           "})\n");
    walker* soldier = tw.living(FAMILY_SOLDIER, 0, 160, 160);
    walker* orc = tw.living(FAMILY_ORC, 1, 176, 160);
    ASSERT_NE(nullptr, soldier);
    ASSERT_NE(nullptr, orc);
    orc->stats()->set_hitpoints(100.0f);
    const std::uint32_t soldier_id = soldier->entity_id();
    soldier->attack(orc);
    ASSERT_EQ(soldier_id, orc->last_attacker_id()) << "the hit stamped";
    ASSERT_EQ(1, tw.world().remove_ob(soldier));
    ASSERT_EQ(nullptr, tw.world().find_by_id(soldier_id)) << "the killer is swept";
    orc->set_dead(1);
    orc->death();
    EXPECT_EQ((std::vector<std::string>{"death\t1\t0"}), vm_log())
        << "swept arm: nil killer, stamped team 0";
}

// The attribution window is inclusive: a stamp exactly 48 ticks old still
// credits its (live) killer.
TEST_F(SemScript3, a_stamp_exactly_at_the_attribution_window_still_credits_the_killer)
{
    World42 tw;
    script("og.register_level_hooks(42, {\n"
           "  on_entity_death = function(ent, killer, killer_team)\n"
           "    og.log('death', killer and og.entity_id(killer) or -1, killer_team)\n"
           "  end,\n"
           "})\n");
    walker* soldier = tw.living(FAMILY_SOLDIER, 0, 160, 160);
    walker* orc = tw.living(FAMILY_ORC, 1, 176, 160);
    ASSERT_NE(nullptr, soldier);
    ASSERT_NE(nullptr, orc);
    orc->stats()->set_hitpoints(100.0f);
    soldier->attack(orc);
    ASSERT_EQ(soldier->entity_id(), orc->last_attacker_id());
    tw.world().tick_count_ += og::sim::kKillAttributionTicks;
    orc->set_dead(1);
    orc->death();
    EXPECT_EQ((std::vector<std::string>{
                  "death\t" + std::to_string(soldier->entity_id()) + "\t0"}),
              vm_log());
}

// ===========================================================================
// level_mode_tick (world_scripts.cpp:2650)
// ===========================================================================

// An active mode whose level registers no on_mode_tick runs none, even when
// another level's on_mode_tick has set the kind bit.
TEST_F(SemScript3, an_active_mode_without_on_mode_tick_runs_no_mode_tick)
{
    World42 tw;
    tw.world().type = GameWorld::TYPE_SCRIPTED;
    script("og.register_level_hooks(42, {\n"
           "  on_mode_init = function(l) og.log('mode_init', l) end,\n"
           "  on_tick = function(l, t) og.log('tick', t) end,\n"
           "})\n",
           "sem3/scripts/a.lua");
    script("og.register_level_hooks(43, {\n"
           "  on_mode_tick = function(l, t) og.log('mode_tick', t) end,\n"
           "})\n",
           "sem3/scripts/b.lua");
    constexpr std::uint32_t kModeTickBit =
        1u << 5;  // LevelHook::ModeTick (script_internal.h: "ModeTick = 5")
    ASSERT_EQ(kModeTickBit, hooks::level_hook_kinds_for(43) & kModeTickBit)
        << "precondition: the kind bit is set";
    walker* orc = tw.living(FAMILY_ORC, 1, 320, 320);  // keeps classic cold
    ASSERT_NE(nullptr, orc);
    tw.world().tick();
    tw.world().tick();
    EXPECT_TRUE(tw.world().mode.active);
    EXPECT_EQ(1u, log_count("mode_init\t42"));
    EXPECT_EQ(1u, log_count("tick\t1"));
    EXPECT_EQ(1u, log_count("tick\t2"));
    EXPECT_EQ(0u, log_count("mode_tick\t1") + log_count("mode_tick\t2"))
        << "absent arm: no on_mode_tick ran";
    EXPECT_TRUE(errors().empty()) << all_errors();
}

// ===========================================================================
// level_lineup_stage (world_scripts.cpp:2683)
// ===========================================================================

// A lineup-stage hook that raises half way still reports that it ran (what it
// fielded stays fielded) and its error is recorded under its own label.
TEST_F(SemScript3, a_lineup_stage_hook_that_raises_still_reports_it_ran)
{
    World42 tw;
    script("og.register_level_hooks(42, {\n"
           "  on_lineup_stage = function(l) og.log('lineup', l) error('half') end,\n"
           "})\n");
    EXPECT_TRUE(hooks::level_lineup_stage(&tw.world()))
        << "raises arm: a hook ran";
    EXPECT_EQ((std::vector<std::string>{"lineup\t42"}), vm_log());
    ASSERT_EQ(1u, errors().size()) << all_errors();
    EXPECT_EQ("level:on_lineup_stage", errors()[0].where);
    EXPECT_NE(std::string::npos, errors()[0].message.find("half"));
}

// ===========================================================================
// read_page_entry id (world_scripts.cpp:2947)
// ===========================================================================

// A picker row's id must be a non-empty STRING: a number id and an empty id
// each reject the page, naming the field; a string id is accepted.
TEST_F(SemScript3, a_page_entry_id_must_be_a_non_empty_string)
{
    current_game = nullptr;
    const struct {
        const char* id;
        bool ok;
    } cases[] = {{"5", false}, {"''", false}, {"'five'", true}};
    for (const auto& c : cases) {
        clear_pack_scripts();
        script(std::string("og.register_campaign_hooks({\n"
                           "  picker_menu = function(page_id)\n"
                           "    return { title = 'X', entries = {\n"
                           "      { id = ") +
               c.id +
               ", kind = 'page' } } }\n"
               "  end,\n"
               "})\n");
        hooks::CampaignPage page;
        EXPECT_EQ(c.ok, hooks::campaign_picker_page("", page)) << "id = " << c.id;
        EXPECT_EQ(c.ok ? 0u : 1u,
                  errors_containing("entries[1].id is missing or not a string"))
            << "id = " << c.id << ": " << all_errors();
        if (c.ok) {
            ASSERT_EQ(1u, page.entries.size());
            EXPECT_EQ("five", page.entries[0].id);
        }
    }
}

// ===========================================================================
// parse_zone_assign labels (world_scripts.cpp:3182)
// ===========================================================================

// An assign chip label that is not a string rejects the zone, naming the
// label; two string labels are accepted.
TEST_F(SemScript3, a_non_string_assign_label_rejects_the_zone)
{
    current_game = nullptr;
    const struct {
        const char* labels;
        bool ok;
    } cases[] = {{"{ 5, 'B' }", false}, {"{ 'A', 'B' }", true}};
    for (const auto& c : cases) {
        clear_pack_scripts();
        script(std::string("og.register_campaign_hooks({\n"
                           "  base_camp = function()\n"
                           "    return { widgets = { { kind = 'roster',\n"
                           "      assign = { key = 'road', labels = ") +
               c.labels +
               " } } } }\n"
               "  end,\n"
               "})\n");
        hooks::CampaignZone zone;
        EXPECT_EQ(c.ok, hooks::campaign_zone(zone)) << c.labels;
        EXPECT_EQ(c.ok ? 0u : 1u,
                  errors_containing(
                      "widgets[1].assign.labels[1] must be a non-empty string"))
            << c.labels << ": " << all_errors();
        if (c.ok) {
            ASSERT_EQ(1u, zone.widgets.size());
            EXPECT_TRUE(zone.widgets[0].assign.active);
            EXPECT_EQ((std::vector<std::string>{"A", "B"}),
                      zone.widgets[0].assign.labels);
        }
    }
}

// ===========================================================================
// parse_zone_readout items (world_scripts.cpp:3349)
// ===========================================================================

// A readout with no items is accepted and shows none.
TEST_F(SemScript3, a_readout_without_items_is_accepted_and_shows_none)
{
    current_game = nullptr;
    script("og.register_campaign_hooks({\n"
           "  base_camp = function()\n"
           "    return { widgets = { { kind = 'roster' }, { kind = 'readout' } } }\n"
           "  end,\n"
           "})\n");
    hooks::CampaignZone zone;
    ASSERT_TRUE(hooks::campaign_zone(zone)) << "nil-items arm: " << all_errors();
    ASSERT_EQ(2u, zone.widgets.size());
    EXPECT_EQ(hooks::CampaignZoneWidget::Kind::Readout, zone.widgets[1].kind);
    EXPECT_TRUE(zone.widgets[1].items.empty());
    EXPECT_TRUE(errors().empty()) << all_errors();
}

// ===========================================================================
// campaign_fighter_power error label (world_scripts.cpp:3893)
// ===========================================================================

// A pricer that errors is no price, and the one error recorded names WHICH
// pricer refused: the campaign book's or the default.
TEST_F(SemScript3, a_pricer_error_is_recorded_under_the_pricer_that_raised_it)
{
    current_game = nullptr;
    const struct {
        const char* source;
        const char* chunk;
        const char* where;
    } cases[] = {
        {"og.register_campaign_hooks({\n"
         "  lineup = { power = function(row) error('book refuses') end },\n"
         "})\n",
         "sem3/scripts/c.lua", "campaign:lineup_power"},
        {"og.register_default_lineup({\n"
         "  power = function(row) error('default refuses') end,\n"
         "})\n",
         "sem3/scripts/z_default.lua", "default:lineup_power"},
    };
    for (const auto& c : cases) {
        clear_pack_scripts();
        script(c.source, c.chunk);
        hooks::LineupPowerRow row;
        long long power = 12345;
        EXPECT_FALSE(hooks::campaign_fighter_power(row, power)) << c.where;
        EXPECT_EQ(12345, power) << c.where;
        ASSERT_EQ(1u, errors().size()) << all_errors();
        EXPECT_EQ(c.where, errors()[0].where) << "error arm: which pricer";
    }
}
