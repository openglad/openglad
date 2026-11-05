/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch script-1: the rules the review found in bindings_entity.cpp.
// Each test is named after the rule it asserts.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/order.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>
#include <openglad/resources/gloader.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// guy.cpp's experience ladder (external linkage; no gameplay header
// declares it).
std::uint32_t calculate_exp(std::int32_t level);

namespace {

using og::script::ScriptError;

// Splits one og.log line into its tab-separated fields.
std::vector<std::string> fields(const std::string& line)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::string to_s(int v) { return std::to_string(v); }

// ---------------------------------------------------------------------------
// Fixture 1: a TestGameWorld (production loader) and a core:soldier
// do_special hook that runs the body under test. No tick runs.
// ---------------------------------------------------------------------------
class SemScriptEntity : public ::testing::Test {
protected:
    // Declared first so it outlives the clears below: the shipped pack
    // scripts and tuning this fixture wipes are what the next test in a
    // --gtest_shuffle order expects to find.
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

    static void register_do_special(const std::string& body)
    {
        og::script::clear_pack_scripts();
        og::script::register_pack_script(
            {"test.sem", "sem_script_1.lua",
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

    // Runs `body` once and requires it to finish without a script error.
    static void run_clean(const std::string& body, walker* self)
    {
        register_do_special(body);
        const std::optional<SpecialResult> r = dispatch(self);
        ASSERT_TRUE(r.has_value()) << last_error();
        EXPECT_TRUE(*r);
    }

    static const std::vector<ScriptError>& errors()
    {
        return og::script::active_world_scripts().host().errors();
    }

    static std::string last_error()
    {
        return errors().empty() ? std::string("(no script error recorded)")
                                : errors().back().message;
    }

    static const std::vector<std::string>& vm_log()
    {
        return og::script::active_world_scripts().host().log();
    }

    // The last log line whose first field is `tag`, split; empty if none.
    static std::vector<std::string> logged(const std::string& tag)
    {
        const auto& log = vm_log();
        for (auto it = log.rbegin(); it != log.rend(); ++it) {
            std::vector<std::string> f = fields(*it);
            if (!f.empty() && f[0] == tag)
                return f;
        }
        return {};
    }
};

std::size_t entity_total(const GameWorld& w)
{
    return w.oblist.size() + w.fxlist.size() + w.weaplist.size();
}

}  // namespace

// ===========================================================================
// order_from_string (bindings_entity.cpp:135)
// ===========================================================================

// A script names an entity's order by word. living/weapon/fx and the unknown
// word are pinned elsewhere; these are the other three spellings.
TEST_F(SemScriptEntity, the_order_words_treasure_generator_and_effect_make_those_orders)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    run_clean("    local t = og.add_ob('treasure', " + to_s(FAMILY_STAIN) + ")\n"
              "    local g = og.add_ob('generator', 0)\n"
              "    local e = og.add_fx_ob('effect', " + to_s(FAMILY_DOOR_OPEN) + ")\n"
              "    local f = og.add_fx_ob('fx', " + to_s(FAMILY_DOOR_OPEN) + ")\n"
              "    og.log('ord', t:order(), g:order(), e:order(), f:order())\n"
              "    og.log('ids', og.entity_id(t), og.entity_id(g),\n"
              "           og.entity_id(e), og.entity_id(f))\n",
              self);

    const std::vector<std::string> ord = logged("ord");
    ASSERT_EQ(5u, ord.size()) << last_error();
    EXPECT_EQ(to_s(static_cast<int>(Order::Treasure)), ord[1])
        << "'treasure' names the treasure order";
    EXPECT_EQ(to_s(static_cast<int>(Order::Generator)), ord[2])
        << "'generator' names the generator order";
    EXPECT_EQ(to_s(static_cast<int>(Order::FX)), ord[3])
        << "'effect' is an accepted alias of 'fx'";
    EXPECT_EQ(to_s(static_cast<int>(Order::FX)), ord[4])
        << "'fx' names the effect order";

    // The C++ side agrees: each entity really carries that order.
    const std::vector<std::string> ids = logged("ids");
    ASSERT_EQ(5u, ids.size());
    const Order want[] = {Order::Treasure, Order::Generator, Order::FX,
                          Order::FX};
    for (std::size_t i = 0; i < 4; i++) {
        walker* e = w.find_by_id(static_cast<std::uint32_t>(std::stoul(ids[i + 1])));
        ASSERT_NE(nullptr, e) << "entity " << i;
        EXPECT_EQ(want[i], e->query_order()) << "entity " << i;
    }
}

// ===========================================================================
// list_from_selector (bindings_entity.cpp:149)
// ===========================================================================

// 'fx' scans the effect list (where stains and other drops live), and the
// 'ob' scan never sees an entry of that list.
TEST_F(SemScriptEntity, a_range_scan_of_fx_reads_the_effect_list_only)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->setxy(100, 100);
    self->set_team_num(0);
    walker* near_fx = w.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    walker* far_fx = w.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, near_fx);
    ASSERT_NE(nullptr, far_fx);
    near_fx->setxy(105, 100);
    far_fx->setxy(400, 400);
    const std::uint32_t self_id = self->entity_id();
    const std::uint32_t near_id = near_fx->entity_id();

    run_clean("    local l, n = og.find_in_range('fx', 50, self)\n"
              "    og.log('fx', #l, n, og.entity_id(l[1]))\n"
              "    local o, m = og.find_in_range('ob', 50, self)\n"
              "    og.log('ob', #o, m, og.entity_id(o[1]))\n",
              self);

    EXPECT_EQ((std::vector<std::string>{"fx", "1", "1", to_s(static_cast<int>(near_id))}),
              logged("fx"))
        << "'fx' returns exactly the near effect-list entry";
    EXPECT_EQ((std::vector<std::string>{"ob", "1", "1", to_s(static_cast<int>(self_id))}),
              logged("ob"))
        << "'ob' sees the body list only (self), never the effect entry";
}

// ===========================================================================
// m_special (bindings_entity.cpp:432)
// ===========================================================================

// walker:special() from Lua answers (true, nil) on a cast that succeeds, and
// the nested success charges its cost.
TEST_F(SemScriptEntity, a_lua_cast_that_succeeds_answers_true_and_nil)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    og::script::clear_pack_scripts();
    og::script::register_pack_script({"test.sem", "sem_specials.lua", R"LUA(
og.register_hooks('living', 'core:soldier', {
  specials = {
    boomerang = function(self)
      self:set_current_special(4)
      local ok, reason = self:special()
      self:set_current_special(2)
      og.log('inner', tostring(ok), tostring(reason), type(reason))
      return true
    end,
    disarm = function(self)
      return true
    end,
  },
})
)LUA"});
    self->stats()->set_magicpoints(500.0f);
    self->stats()->set_special_cost(2, 0);
    self->stats()->set_special_cost(4, 20);
    self->set_current_special(2);
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
    ASSERT_TRUE(self->special(&why, &reason)) << last_error();
    EXPECT_EQ((std::vector<std::string>{"inner", "true", "nil", "nil"}),
              logged("inner"))
        << "a succeeded cast returns true and a nil reason";
    EXPECT_EQ(480.0f, self->stats()->magicpoints())
        << "the nested success charged its 20";
}

// ===========================================================================
// s_refresh_front (bindings_entity.cpp:839)
// ===========================================================================

// Refreshing a held order rewrites its lease and payload in place: same
// command type, queue never grows.
TEST_F(SemScriptEntity, refreshing_the_head_order_rewrites_lease_and_payload_in_place)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->stats()->commands.clear();

    run_clean("    self:s_force_command(og.C.COMMAND_GOTO, 10, 1, 1)\n"
              "    self:s_refresh_front(30, 7, 8)\n",
              self);

    const auto& cmds = self->stats()->commands;
    ASSERT_EQ(1u, cmds.size()) << "the refresh never grows the queue";
    EXPECT_EQ(COMMAND_GOTO, cmds.front().commandtype)
        << "the refresh keeps the command type";
    EXPECT_EQ(30, cmds.front().commandcount) << "new lease";
    EXPECT_EQ(7, cmds.front().com1) << "new payload com1";
    EXPECT_EQ(8, cmds.front().com2) << "new payload com2";
}

// ===========================================================================
// og_summon_configured (bindings_entity.cpp:1032)
// ===========================================================================

// Every option is type-checked BEFORE the summon: a wrong type is an error
// and nothing new exists in the world.
TEST_F(SemScriptEntity, a_wrongly_typed_summon_option_summons_nothing)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    const std::string fam = "og.family_id('fx', 'core:boomerang')";
    const std::pair<const char*, const char*> bad[] = {
        {"{ ani_type = 'x' }", "'ani_type' must be an integer"},
        {"{ lifetime = 1.5 }", "'lifetime' must be an integer"},
        {"{ hp_add = 'q' }", "'hp_add' must be a number"},
        {"{ damage_add = {} }", "'damage_add' must be a number"},
    };
    for (const auto& [opts, msg] : bad) {
        const std::size_t before = entity_total(w);
        register_do_special("    og.summon_configured(self, 'fx', " + fam +
                            ", " + opts + ")");
        const std::optional<SpecialResult> r = dispatch(self);
        EXPECT_FALSE(r.has_value()) << opts;
        EXPECT_NE(std::string::npos, last_error().find(msg)) << last_error();
        EXPECT_EQ(before, entity_total(w))
            << opts << ": a refused option must leave nothing summoned";
    }

    // Control: the same summon with a right-typed option adds one entity.
    const std::size_t before = entity_total(w);
    run_clean("    og.summon_configured(self, 'fx', " + fam +
                  ", { lifetime = 5 })",
              self);
    EXPECT_EQ(before + 1, entity_total(w));
}

// ===========================================================================
// og_query_genre (bindings_entity.cpp:1263) — row deferred on #457 (the
// door never names its own floor); the floor arms and the classic door arm
// are pinned here.
// ===========================================================================

TEST_F(SemScriptEntity, a_terrain_lookup_reads_floor_zero_unless_a_floor_is_named)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    ASSERT_EQ(2, w.floor_count());
    const int gw = w.grid.w;
    const int gh = w.grid.h;
    ASSERT_GT(gw, 8);
    ASSERT_GT(gh, 8);
    // Floor 0: wall at (5,4), grass at (7,4).
    w.grid.data[static_cast<std::size_t>(4 * gw + 5)] =
        static_cast<unsigned char>(PIX_WALL4);
    w.grid.data[static_cast<std::size_t>(4 * gw + 7)] =
        static_cast<unsigned char>(PIX_GRASS1);
    // Floor 1: grass everywhere but a wall at (7,4).
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) *
                                  static_cast<std::size_t>(gh)];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh),
              static_cast<unsigned char>(PIX_GRASS1));
    buf[7 + 4 * gw] = static_cast<unsigned char>(PIX_WALL4);
    w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                    static_cast<unsigned char>(gh), buf);
    w.smoother_for_floor(1).set_target(w.grid_for_floor(1));

    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    run_clean("    og.log('g', og.query_genre(5, 4), og.query_genre(7, 4),\n"
              "           og.query_genre(5, 4, 0), og.query_genre(5, 4, 1),\n"
              "           og.query_genre(7, 4, 1), og.query_genre(-1, -1))\n",
              self);
    EXPECT_EQ((std::vector<std::string>{"g", to_s(TYPE_WALL), to_s(TYPE_GRASS),
                                        to_s(TYPE_WALL), to_s(TYPE_GRASS),
                                        to_s(TYPE_WALL), to_s(TYPE_GRASS)}),
              logged("g"))
        << "omitted floor = floor 0; a named floor reads that floor's "
           "terrain; off-map reads grass";
}

// Classic (weap.cpp FAMILY_DOOR death at 7ff1654a): with no wall above, the
// ORIGINAL writes the dying door's own facing (curdir = FACE_UP), not the new
// effect's. The wall-above arm is PackLuaWeaponDoor.a_door_under_a_wall_opens_sideways.
TEST_F(SemScriptEntity, classic_a_door_with_no_wall_above_turns_itself_up_not_the_opening)
{
    og::test::mount_core_pack();
    const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(FAMILY_DOOR);
    ASSERT_NE(nullptr, wfd);
    og::test::ScopedHookFailureGuard guard;

    TestGameWorld tw;
    GameWorld& w = tw.world();
    PixieData& grid = w.grid_for_floor(0);
    ASSERT_GT(grid.w, 10);
    ASSERT_GT(grid.h, 10);
    grid.data[static_cast<std::size_t>(9) * grid.w + 10] =
        static_cast<unsigned char>(PIX_GRASS1);

    // The opening effect's own default facing, from an identical spawn.
    walker* probe = w.add_weap_ob(Order::FX, FAMILY_DOOR_OPEN);
    ASSERT_NE(nullptr, probe);
    const signed char opening_default = probe->curdir();
    probe->set_dead(1);
    const std::uint32_t probe_id = probe->entity_id();

    walker* door = w.add_ob(Order::Weapon, FAMILY_DOOR);
    ASSERT_NE(nullptr, door);
    door->setxy(10 * GRID_SIZE, 10 * GRID_SIZE);
    door->set_curdir(static_cast<signed char>(FACE_DOWN));
    ASSERT_TRUE(og::test::on_death(*wfd, static_cast<weap*>(door)));

    walker* opened = nullptr;
    for (const auto& ob : w.weaplist) {
        if (ob && !ob->dead() && ob->entity_id() != probe_id &&
            ob->query_order() == Order::FX && ob->family() == FAMILY_DOOR_OPEN)
            opened = ob.get();
    }
    ASSERT_NE(nullptr, opened) << "the door must leave an opened-door effect";
    EXPECT_EQ(FACE_UP, static_cast<int>(door->curdir()))
        << "classic: no wall above sets the DOOR's own facing up";
    EXPECT_EQ(static_cast<int>(opening_default), static_cast<int>(opened->curdir()))
        << "classic: the opening effect keeps its spawn facing";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// og_cosmetic_rand (bindings_entity.cpp:1281, 1286)
// ===========================================================================

namespace {

struct ScopedCosmeticOverride {
    explicit ScopedCosmeticOverride(IRandom** ref) { set_cosmetic_rng_override(ref); }
    ~ScopedCosmeticOverride() { set_cosmetic_rng_override(nullptr); }
};

}  // namespace

// A bound in [1, INT32_MAX] draws, and with no cosmetic stream installed it
// draws from the sim RNG: one LCG step per roll.
TEST_F(SemScriptEntity, a_cosmetic_roll_in_range_draws_one_step_of_the_sim_stream)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    ASSERT_EQ(nullptr, cosmetic_rng_override());
    w.rng_.state_ = 0x5EEDu;
    og::sim::SimRandom probe(0x5EEDu);
    const std::uint32_t a = probe.next(1000u);
    const std::uint32_t b = probe.next(2147483647u);

    run_clean("    og.log('c', og.cosmetic_rand(1000), og.cosmetic_rand(2147483647))\n",
              self);
    EXPECT_EQ((std::vector<std::string>{"c", std::to_string(a), std::to_string(b)}),
              logged("c"))
        << "bounds 1000 and INT32_MAX are accepted and draw the sim RNG";
    EXPECT_EQ(probe.state_, w.rng_.state_) << "exactly two sim steps";
}

// With a cosmetic stream installed the roll reads it, and the sim stream does
// not move.
TEST_F(SemScriptEntity, a_cosmetic_roll_reads_the_installed_cosmetic_stream_and_leaves_the_sim_alone)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    FixedRandom fixed(7);
    IRandom* ref = &fixed;
    ScopedCosmeticOverride scope(&ref);
    w.rng_.state_ = 0x5EEDu;

    run_clean("    og.log('c', og.cosmetic_rand(10), og.cosmetic_rand(1000))\n", self);
    EXPECT_EQ((std::vector<std::string>{"c", "7", "7"}), logged("c"))
        << "the value comes from the cosmetic override";
    EXPECT_EQ(0x5EEDu, w.rng_.state_) << "the sim stream did not advance";
}

// ===========================================================================
// g_upgrade_to_level (bindings_entity.cpp:1469)
// ===========================================================================

// Promoting by script sets experience to the level's threshold by default;
// passing false promotes without touching experience.
TEST_F(SemScriptEntity, a_scripted_promotion_sets_experience_unless_told_not_to)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    auto character = std::make_unique<guy>(FAMILY_SOLDIER);
    character->level = 1;
    character->exp = 500;
    self->set_owned_myguy(std::move(character));
    ASSERT_NE(nullptr, self->myguy);

    run_clean("    self:g_upgrade_to_level(5)\n", self);
    EXPECT_EQ(5, self->myguy->level) << "default: level promoted";
    EXPECT_EQ(calculate_exp(5), self->myguy->exp)
        << "default: experience set to the level-5 threshold";
    ASSERT_NE(500u, calculate_exp(5));

    self->myguy->level = 1;
    self->myguy->exp = 500;
    run_clean("    self:g_upgrade_to_level(5, false)\n", self);
    EXPECT_EQ(5, self->myguy->level) << "false: level promoted";
    EXPECT_EQ(500u, self->myguy->exp) << "false: experience untouched";
}

// ===========================================================================
// og_exp_from_action (bindings_entity.cpp:1570)
// ===========================================================================

// Each of the ten deed names answers exactly the C++ exp_from_action amount
// for that deed (the binding computes, it does not award).
TEST_F(SemScriptEntity, each_named_deed_answers_its_own_experience_amount)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* actor = w.add_ob(Order::Living, FAMILY_CLERIC);
    walker* target = w.add_ob(Order::Living, FAMILY_SKELETON);
    ASSERT_NE(nullptr, actor);
    ASSERT_NE(nullptr, target);
    actor->stats()->set_level(3);
    target->stats()->set_level(2);
    const std::uint32_t target_id = target->entity_id();

    struct Deed {
        const char* name;
        ExpAction action;
    };
    const Deed deeds[] = {
        {"attack", ExpAction::Attack},
        {"kill", ExpAction::Kill},
        {"heal", ExpAction::Heal},
        {"turn_undead", ExpAction::TurnUndead},
        {"raise_skeleton", ExpAction::RaiseSkeleton},
        {"raise_ghost", ExpAction::RaiseGhost},
        {"resurrect", ExpAction::Resurrect},
        {"resurrect_penalty", ExpAction::ResurrectPenalty},
        {"protection", ExpAction::Protection},
        {"eat_corpse", ExpAction::EatCorpse},
    };
    // combat_rng() answers the fixture's gameplay RNG (TestGameWorld::rng,
    // a FixedRandom) — only heal draws. A nonzero fixed value keeps heal's
    // amount distinct from the others.
    tw.rng = FixedRandom(123);
    FixedRandom probe(123);
    std::vector<std::string> want;
    std::string body = "    local t = og.find_by_id(" + std::to_string(target_id) + ")\n";
    for (const Deed& d : deeds) {
        want.push_back(std::to_string(compute_xp_from_action(d.action, 3, 2, 10, probe)));
        body += std::string("    og.log('deed', '") + d.name +
                "', og.exp_from_action(self, t, '" + d.name + "', 10))\n";
    }
    // The ten amounts are pairwise distinct, so a transposed name arm shows.
    std::vector<std::string> sorted = want;
    std::sort(sorted.begin(), sorted.end());
    ASSERT_EQ(sorted.end(), std::adjacent_find(sorted.begin(), sorted.end()))
        << "fixture: two deeds share an amount";

    run_clean(body, actor);
    std::vector<std::vector<std::string>> rows;
    for (const std::string& line : vm_log()) {
        std::vector<std::string> f = fields(line);
        if (f.size() == 3 && f[0] == "deed")
            rows.push_back(f);
    }
    ASSERT_EQ(10u, rows.size()) << last_error();
    for (std::size_t i = 0; i < 10; i++) {
        EXPECT_EQ(deeds[i].name, rows[i][1]);
        EXPECT_EQ(want[i], rows[i][2]) << "deed '" << deeds[i].name << "'";
    }
}

// ===========================================================================
// og_tuning (bindings_entity.cpp:1662)
// ===========================================================================

namespace {

og::script::TuningValue tuning_int(std::int64_t v)
{
    og::script::TuningValue t;
    t.kind = og::script::TuningValue::Kind::Integer;
    t.integer = v;
    return t;
}

}  // namespace

// The tuning view is cached: the same table (identity) comes back within a
// store generation, across dispatches too, and a new one after the store
// changes.
TEST_F(SemScriptEntity, a_family_tuning_view_is_one_cached_table_per_generation)
{
    og::script::TuningMap map;
    map.push_back({"cap", tuning_int(1)});
    og::script::set_family_tuning(Order::Living, FAMILY_SOLDIER, std::move(map));

    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    register_do_special(
        "    local a = og.tuning(self)\n"
        "    local b = og.tuning(self)\n"
        "    og.log('same', tostring(rawequal(a, b)))\n"
        "    if stash ~= nil then\n"
        "      og.log('kept', tostring(rawequal(stash, a)), a.cap)\n"
        "    end\n"
        "    stash = a\n");
    ASSERT_TRUE(dispatch(self).has_value()) << last_error();
    EXPECT_EQ((std::vector<std::string>{"same", "true"}), logged("same"))
        << "two reads in one dispatch are the same table";
    ASSERT_TRUE(dispatch(self).has_value()) << last_error();
    EXPECT_EQ((std::vector<std::string>{"kept", "true", "1"}), logged("kept"))
        << "same generation: the cached table survives the dispatch";

    og::script::TuningMap map2;
    map2.push_back({"cap", tuning_int(2)});
    og::script::set_family_tuning(Order::Living, FAMILY_SOLDIER, std::move(map2));
    ASSERT_TRUE(dispatch(self).has_value()) << last_error();
    EXPECT_EQ((std::vector<std::string>{"kept", "false", "2"}), logged("kept"))
        << "a new generation builds a new table";
}

// ===========================================================================
// og_ani_frame (bindings_entity.cpp:1740)
// ===========================================================================

// A negative frame index reads nil (the row itself exists).
TEST_F(SemScriptEntity, a_negative_animation_frame_index_reads_nil)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    // The row starts one byte into the buffer, so a missing index guard
    // would read the 9 in front of it.
    static const signed char kBuf[] = {9, 5, 6, 7, -1};
    static const signed char* const kTable[] = {kBuf + 1};
    const signed char* const* saved_ani = self->ani;
    const auto saved_count = self->ani_count;
    self->ani = kTable;
    self->ani_count = 1;

    run_clean("    og.log('f', tostring(og.ani_frame(self, 0, -1)),\n"
              "           tostring(og.ani_frame(self, 0, 0)))\n",
              self);
    self->ani = saved_ani;
    self->ani_count = saved_count;
    EXPECT_EQ((std::vector<std::string>{"f", "nil", "5"}), logged("f"))
        << "index -1 is nil; index 0 reads the frame";
}

// ===========================================================================
// pad_weapon_family (bindings_entity.cpp:3191)
// ===========================================================================

// fire_pad_* take nil/-1 (current weapon) or a family slot; below -1 is an
// error naming the id.
TEST_F(SemScriptEntity, a_pad_probe_refuses_a_weapon_family_below_minus_one)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->setxy(160, 160);
    self->set_current_weapon(FAMILY_KNIFE);

    run_clean(
        "    local ok1, e1 = pcall(function() return self:fire_pad_clear(-2) end)\n"
        "    local ok2, e2 = pcall(function() return self:fire_pad_answer(-2) end)\n"
        "    og.log('bad', tostring(ok1), tostring(ok2))\n"
        "    og.log('e1', e1)\n"
        "    og.log('e2', e2)\n"
        "    og.log('cur', tostring(self:fire_pad_clear(-1) == self:fire_pad_clear()),\n"
        "           tostring(self:fire_pad_answer(-1) == self:fire_pad_answer()))\n",
        self);
    EXPECT_EQ((std::vector<std::string>{"bad", "false", "false"}), logged("bad"))
        << "-2 is refused by both pad probes";
    const auto e1 = logged("e1");
    const auto e2 = logged("e2");
    ASSERT_EQ(2u, e1.size());
    ASSERT_EQ(2u, e2.size());
    EXPECT_NE(std::string::npos, e1[1].find("weapon family -2 out of range")) << e1[1];
    EXPECT_NE(std::string::npos, e2[1].find("weapon family -2 out of range")) << e2[1];
    EXPECT_EQ((std::vector<std::string>{"cur", "true", "true"}), logged("cur"))
        << "-1 asks the current weapon, like nil";
}

// ===========================================================================
// Fixture 2: a TestGameWorld wired to the production loader, with a level
// on_load hook (ticks the world once). The ModeBindings shape.
// ===========================================================================

namespace {

loader& sem_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

struct SemModeWorld : TestGameWorld
{
    og::test::ScopedPackStoreState pack_store_restore;

    SemModeWorld() : TestGameWorld(42)
    {
        init_all_registries();
        og::script::clear_pack_scripts();
        loader* game_loader = &sem_loader();
        world().entity_factory = [game_loader](Order order, std::int32_t family) {
            return game_loader->create_walker_owned(order, family);
        };
        world().entity_configurator =
            [game_loader](walker& entity, Order order,
                          std::int32_t family) -> const PixieData* {
            game_loader->set_walker(&entity, order, family);
            return game_loader->graphics_for(entity.query_order(), entity.family());
        };
        world().entity_derived_stats =
            [game_loader](walker* entity, Order order, std::int32_t family) {
                if (entity != nullptr)
                    game_loader->set_derived_stats(entity, order, family);
            };
    }

    ~SemModeWorld() { og::script::clear_pack_scripts(); }

    // Registers `body` as level 42's on_load hook (plus `extra` chunk
    // source, e.g. family hooks) and runs one tick.
    void run_on_load(const std::string& body, const std::string& extra = "")
    {
        og::script::clear_pack_scripts();
        og::script::register_pack_script(
            {"test.sem", "sem_mode.lua",
             extra + "og.register_level_hooks(42, {\n"
                     "  on_load = function(level)\n" +
                 body +
                 "\n  end,\n"
                 "})\n"});
        world().tick();
    }

    const std::vector<std::string>& vm_log()
    {
        return world().scripts().host().log();
    }

    std::vector<std::string> logged(const std::string& tag)
    {
        const auto& log = vm_log();
        for (auto it = log.rbegin(); it != log.rend(); ++it) {
            std::vector<std::string> f = fields(*it);
            if (!f.empty() && f[0] == tag)
                return f;
        }
        return {};
    }

    bool logged_line(const std::string& needle)
    {
        for (const auto& line : vm_log())
            if (line.find(needle) != std::string::npos)
                return true;
        return false;
    }

    std::string script_errors()
    {
        std::string out;
        for (const auto& e : world().scripts().host().errors())
            out += e.where + ": " + e.message + "\n";
        for (const auto& line : vm_log())
            out += "log: " + line + "\n";
        return out;
    }

    walker* spawn_living(int family, int team, int x, int y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_team_num(static_cast<unsigned char>(team));
        w->set_real_team_num(255);
        w->set_act_type(ACT_CONTROL);
        return w;
    }

    walker* spawn_marker(int team, int x, int y)
    {
        walker* marker = world().add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        if (marker == nullptr)
            return nullptr;
        marker->setxy(static_cast<short>(x), static_cast<short>(y));
        marker->set_team_num(static_cast<unsigned char>(team));
        return marker;
    }

    // Two-floor world: floor 1 is grass with a wall at grid cell (tx, ty).
    void add_floor_one_with_wall_at(int tx, int ty)
    {
        GameWorld& w = world();
        w.set_floor_count(2);
        const int gw = w.grid.w;
        const int gh = w.grid.h;
        auto* buf = new unsigned char[static_cast<std::size_t>(gw) *
                                      static_cast<std::size_t>(gh)];
        std::fill(buf, buf + static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh),
                  static_cast<unsigned char>(PIX_GRASS1));
        buf[tx + ty * gw] = static_cast<unsigned char>(PIX_H_WALL1);
        w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                        static_cast<unsigned char>(gh), buf);
        w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
    }

    bool alive(std::uint32_t id)
    {
        walker* e = world().find_by_id(id);
        return e != nullptr && !e->dead();
    }
};

}  // namespace

// ===========================================================================
// og_end_level (bindings_entity.cpp:2570)
// ===========================================================================

TEST(SemScriptMode, an_ending_other_than_win_or_loss_is_refused_and_never_latches)
{
    SemModeWorld fx;
    fx.run_on_load(
        "    local ok, e = pcall(og.end_level, 2, 43)\n"
        "    og.log('r', tostring(ok))\n"
        "    og.log('e', e)\n");
    EXPECT_EQ((std::vector<std::string>{"r", "false"}), fx.logged("r")) << fx.script_errors();
    EXPECT_TRUE(fx.logged_line("ending must be 0 (win) or 1 (loss)")) << fx.script_errors();
    EXPECT_FALSE(fx.world().mode.win_latched) << "a refused ending never latches";
    EXPECT_EQ(-1, fx.world().mode.winner_team);
}

TEST(SemScriptMode, a_next_level_outside_minus_one_to_32767_is_refused_and_never_latches)
{
    SemModeWorld fx;
    fx.run_on_load(
        "    local ok1 = pcall(og.end_level, 0, -2)\n"
        "    local ok2, e2 = pcall(og.end_level, 0, 32768)\n"
        "    og.log('r', tostring(ok1), tostring(ok2))\n"
        "    og.log('e', e2)\n");
    EXPECT_EQ((std::vector<std::string>{"r", "false", "false"}), fx.logged("r"))
        << fx.script_errors();
    EXPECT_TRUE(fx.logged_line("next_level out of range [-1, 32767]")) << fx.script_errors();
    EXPECT_FALSE(fx.world().mode.win_latched) << "a refused next level never latches";
    EXPECT_EQ(-1, fx.world().mode.winner_team);
}

// ===========================================================================
// og_declare_winner (bindings_entity.cpp:2592)
// ===========================================================================

TEST(SemScriptMode, a_negative_winner_team_is_refused_and_nothing_latches)
{
    SemModeWorld fx;
    fx.run_on_load(
        "    local ok, e = pcall(og.declare_winner, -1)\n"
        "    og.log('neg', tostring(ok))\n"
        "    og.log('e', e)\n");
    EXPECT_EQ((std::vector<std::string>{"neg", "false"}), fx.logged("neg"))
        << fx.script_errors();
    EXPECT_TRUE(fx.logged_line("team -1 not in [0, 3] or band [16, 31]"))
        << fx.script_errors();
    EXPECT_EQ(-1, fx.world().mode.winner_team);
    EXPECT_FALSE(fx.world().mode.win_latched);
}

// ===========================================================================
// og_mode_get / og_mode_set (bindings_entity.cpp:2614, 2628)
// ===========================================================================

TEST(SemScriptMode, a_mode_var_write_truncates_to_int32)
{
    SemModeWorld fx;
    fx.run_on_load(
        "    og.mode_set(1, 4294967303)\n"
        "    og.mode_set(2, 2147483648)\n"
        "    og.log('v', og.mode_get(1), og.mode_get(2))\n");
    EXPECT_EQ((std::vector<std::string>{"v", "7", "-2147483648"}), fx.logged("v"))
        << fx.script_errors();
    EXPECT_EQ(7, fx.world().mode.vars[1]);
    EXPECT_EQ(-2147483648LL, static_cast<long long>(fx.world().mode.vars[2]));
}

TEST(SemScriptMode, a_negative_mode_var_write_is_refused_and_writes_nothing)
{
    SemModeWorld fx;
    fx.run_on_load(
        "    local ok, e = pcall(og.mode_set, -1, 5)\n"
        "    og.log('neg', tostring(ok))\n"
        "    og.log('e', e)\n");
    EXPECT_EQ((std::vector<std::string>{"neg", "false"}), fx.logged("neg"))
        << fx.script_errors();
    EXPECT_TRUE(fx.logged_line("mode var index -1 out of range [0, 63]"))
        << fx.script_errors();
    for (std::size_t i = 0; i < fx.world().mode.vars.size(); i++)
        EXPECT_EQ(0, fx.world().mode.vars[i]) << "var " << i;
}

// ===========================================================================
// og_set_hud_line / og_clear_hud_line (bindings_entity.cpp:2659, 2665)
// ===========================================================================

TEST(SemScriptMode, hud_slots_below_zero_and_past_three_are_refused_and_write_nothing)
{
    SemModeWorld fx;
    fx.run_on_load(
        "    og.set_hud_line(0, 'A')\n"
        "    og.set_hud_line(3, 'KEEP', 1)\n"
        "    local a, ea = pcall(og.set_hud_line, -1, 'x')\n"
        "    local b, eb = pcall(og.clear_hud_line, 4)\n"
        "    og.log('r', tostring(a), tostring(b))\n"
        "    og.log('ea', ea)\n"
        "    og.log('eb', eb)\n");
    EXPECT_EQ((std::vector<std::string>{"r", "false", "false"}), fx.logged("r"))
        << "set_hud_line(-1) and clear_hud_line(4) are refused: " << fx.script_errors();
    EXPECT_TRUE(fx.logged_line("slot -1 out of range [0, 3]")) << fx.script_errors();
    EXPECT_TRUE(fx.logged_line("slot 4 out of range [0, 3]")) << fx.script_errors();
    const auto& hud = fx.world().mode.hud;
    EXPECT_STREQ("A", hud[0].text.data());
    EXPECT_EQ(255, hud[0].team);
    EXPECT_STREQ("", hud[1].text.data());
    EXPECT_STREQ("", hud[2].text.data());
    EXPECT_STREQ("KEEP", hud[3].text.data()) << "slot 3 was not cleared";
    EXPECT_EQ(1, hud[3].team);
}

TEST(SemScriptMode, a_negative_hud_tint_is_refused_without_half_writing_the_line)
{
    SemModeWorld fx;
    fx.run_on_load(
        "    local ok, e = pcall(og.set_hud_line, 2, 'x', -1)\n"
        "    og.log('r', tostring(ok))\n"
        "    og.log('e', e)\n");
    EXPECT_EQ((std::vector<std::string>{"r", "false"}), fx.logged("r")) << fx.script_errors();
    EXPECT_TRUE(fx.logged_line("team -1 not in [0, 3] or band [16, 31]"))
        << fx.script_errors();
    EXPECT_STREQ("", fx.world().mode.hud[2].text.data());
    EXPECT_EQ(255, fx.world().mode.hud[2].team);
}

// ===========================================================================
// og_set_beacon (bindings_entity.cpp:2702)
// ===========================================================================

TEST(SemScriptMode, a_nil_beacon_clears_the_slot_whatever_team_rides_along)
{
    SemModeWorld fx;
    walker* mutant = fx.spawn_living(FAMILY_SOLDIER, 1, 160, 160);
    ASSERT_NE(nullptr, mutant);
    const auto id = static_cast<std::int32_t>(mutant->entity_id());
    fx.run_on_load(
        "    local obs = og.oblist()\n"
        "    og.set_beacon(1, obs[1], 1)\n"
        "    local ok = pcall(og.set_beacon, 1, obs[1], 99)\n"
        "    og.log('bad', tostring(ok), og.mode_get(0))\n"
        "    og.set_beacon(0, obs[1], 1)\n"
        "    og.set_beacon(0, nil, 99)\n"
        "    og.log('cleared')\n");
    EXPECT_EQ((std::vector<std::string>{"bad", "false", "0"}), fx.logged("bad"))
        << "control: an entity with team 99 is refused: " << fx.script_errors();
    EXPECT_EQ(id, fx.world().mode.beacons[1].entity_id) << "the refused call left slot 1";
    EXPECT_EQ(1, fx.world().mode.beacons[1].team);
    EXPECT_TRUE(fx.logged_line("cleared"))
        << "nil with an invalid team raises no error: " << fx.script_errors();
    EXPECT_EQ(0, fx.world().mode.beacons[0].entity_id) << "nil clears slot 0";
    EXPECT_EQ(255, fx.world().mode.beacons[0].team);
}

TEST(SemScriptMode, a_negative_beacon_slot_is_refused_and_writes_nothing)
{
    SemModeWorld fx;
    walker* mutant = fx.spawn_living(FAMILY_SOLDIER, 1, 160, 160);
    ASSERT_NE(nullptr, mutant);
    fx.run_on_load(
        "    local ok, e = pcall(og.set_beacon, -1, og.oblist()[1])\n"
        "    og.log('r', tostring(ok))\n"
        "    og.log('e', e)\n");
    EXPECT_EQ((std::vector<std::string>{"r", "false"}), fx.logged("r")) << fx.script_errors();
    EXPECT_TRUE(fx.logged_line("slot -1 out of range [0, 3]")) << fx.script_errors();
    for (std::size_t i = 0; i < fx.world().mode.beacons.size(); i++) {
        EXPECT_EQ(0, fx.world().mode.beacons[i].entity_id) << "beacon " << i;
        EXPECT_EQ(255, fx.world().mode.beacons[i].team) << "beacon " << i;
    }
}

TEST(SemScriptMode, a_negative_beacon_tint_is_refused_and_the_slot_kept)
{
    SemModeWorld fx;
    walker* mutant = fx.spawn_living(FAMILY_SOLDIER, 1, 160, 160);
    ASSERT_NE(nullptr, mutant);
    const auto id = static_cast<std::int32_t>(mutant->entity_id());
    fx.run_on_load(
        "    local obs = og.oblist()\n"
        "    og.set_beacon(2, obs[1], 1)\n"
        "    local ok, e = pcall(og.set_beacon, 2, obs[1], -1)\n"
        "    og.log('r', tostring(ok))\n"
        "    og.log('e', e)\n");
    EXPECT_EQ((std::vector<std::string>{"r", "false"}), fx.logged("r")) << fx.script_errors();
    EXPECT_TRUE(fx.logged_line("team -1 not in [0, 3] or band [16, 31]"))
        << fx.script_errors();
    EXPECT_EQ(id, fx.world().mode.beacons[2].entity_id);
    EXPECT_EQ(1, fx.world().mode.beacons[2].team);
}

// ===========================================================================
// og_team_score / og_respawn_pending_count / og_respawn_anchor_count
// (bindings_entity.cpp:2798, 2999, 3014)
// ===========================================================================

TEST(SemScriptMode, score_and_respawn_queries_refuse_a_negative_team)
{
    SemModeWorld fx;
    fx.run_on_load(
        "    local a, ea = pcall(og.team_score, -1)\n"
        "    local b, eb = pcall(og.respawn_pending_count, -1)\n"
        "    local c, ec = pcall(og.respawn_anchor_count, -1)\n"
        "    og.log('r', tostring(a), tostring(b), tostring(c))\n"
        "    og.log('ea', ea)\n"
        "    og.log('eb', eb)\n"
        "    og.log('ec', ec)\n");
    const auto r = fx.logged("r");
    ASSERT_EQ(4u, r.size()) << fx.script_errors();
    EXPECT_EQ("false", r[1]) << "og.team_score(-1) is refused";
    EXPECT_EQ("false", r[2]) << "og.respawn_pending_count(-1) is refused";
    EXPECT_EQ("false", r[3]) << "og.respawn_anchor_count(-1) is refused";
    for (const char* tag : {"ea", "eb", "ec"}) {
        const auto e = fx.logged(tag);
        ASSERT_EQ(2u, e.size()) << tag;
        EXPECT_NE(std::string::npos, e[1].find("team -1 out of range [0, 3]"))
            << tag << ": " << e[1];
    }
}

// og_award_score (#454): the team is range-checked as the Lua integer, so a
// number a byte would wrap onto a real team (256 -> 0, 257 -> 1, -255 -> 1)
// is outside the score table and silently ignored, as -1 and 4 are.
TEST(SemScriptMode, a_score_award_to_a_team_a_byte_would_wrap_onto_a_real_team_is_ignored)
{
    SemModeWorld fx;
    fx.run_on_load(
        "    og.award_score(256, 10)\n"
        "    og.award_score(257, 20)\n"
        "    og.award_score(-255, 30)\n"
        "    og.award_score(-1, 40)\n"
        "    og.award_score(4, 50)\n"
        "    og.award_score(3, 5)\n"
        "    og.log('done')\n");
    ASSERT_EQ((std::vector<std::string>{"done"}), fx.logged("done")) << fx.script_errors();
    EXPECT_TRUE(fx.world().scripts().host().errors().empty()) << fx.script_errors();
    EXPECT_EQ(0u, fx.world().m_score[0]) << "256 is not team 0";
    EXPECT_EQ(0u, fx.world().m_score[1]) << "257 and -255 are not team 1";
    EXPECT_EQ(0u, fx.world().m_score[2]);
    EXPECT_EQ(5u, fx.world().m_score[3]) << "the in-range control is credited";

    std::vector<og::sim::Event> scores;
    for (const auto& e : fx.events.events())
        if (e.kind == og::sim::EventKind::ScoreChange)
            scores.push_back(e);
    ASSERT_EQ(1u, scores.size()) << "only the control announces a score";
    EXPECT_EQ(3u, scores[0].a);
    EXPECT_EQ(5u, scores[0].b);
}

// ===========================================================================
// og_match_setting (bindings_entity.cpp:2899) — the difficulty arm, under
// both Gameplay Fixes values.
// ===========================================================================

TEST(SemScriptMode, the_sim_reads_the_difficulty_setting_at_its_live_value)
{
    for (int fixes : {0, 1}) {
        for (short pct : {short{200}, short{50}}) {
            SemModeWorld fx;
            fx.world().gameplay_fixes = static_cast<decltype(fx.world().gameplay_fixes)>(fixes);
            fx.world().difficulty = pct;
            fx.run_on_load(
                "    og.log('d', og.match_setting('difficulty'),\n"
                "           og.match_setting('gameplay_fixes'))\n");
            EXPECT_EQ((std::vector<std::string>{"d", std::to_string(pct), std::to_string(fixes)}),
                      fx.logged("d"))
                << "fixes " << fixes << " difficulty " << pct << ": " << fx.script_errors();
        }
    }
}

// ===========================================================================
// og_respawn_schedule (bindings_entity.cpp:2970)
// ===========================================================================

TEST(SemScriptMode, a_respawn_with_no_delay_uses_the_match_delay_and_65536_is_refused)
{
    SemModeWorld fx;
    fx.world().respawn.respawn_ticks = 77;
    fx.world().ctf_requested_respawn_ticks = 77;
    walker* a = fx.spawn_living(FAMILY_SOLDIER, 1, 160, 160);
    walker* b = fx.spawn_living(FAMILY_ARCHER, 1, 224, 160);
    walker* c = fx.spawn_living(FAMILY_CLERIC, 1, 288, 160);
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    ASSERT_NE(nullptr, c);
    a->set_dead(1);
    b->set_dead(1);
    c->set_dead(1);
    const std::uint32_t ida = a->entity_id();
    const std::uint32_t idb = b->entity_id();
    const std::uint32_t idc = c->entity_id();
    fx.run_on_load(
        "    local a = og.find_by_id(" + std::to_string(ida) + ")\n"
        "    local b = og.find_by_id(" + std::to_string(idb) + ")\n"
        "    local c = og.find_by_id(" + std::to_string(idc) + ")\n"
        "    og.log('plain', tostring(og.respawn_schedule(a)))\n"
        "    local ok, e = pcall(og.respawn_schedule, b, 65536)\n"
        "    og.log('wide', tostring(ok))\n"
        "    og.log('e', e)\n"
        "    og.log('edge', tostring(og.respawn_schedule(c, 65535)))\n");
    EXPECT_EQ((std::vector<std::string>{"plain", "true"}), fx.logged("plain"))
        << fx.script_errors();
    EXPECT_EQ((std::vector<std::string>{"wide", "false"}), fx.logged("wide"))
        << "65536 ticks is refused";
    EXPECT_TRUE(fx.logged_line("ticks out of range [1, 65535]")) << fx.script_errors();
    EXPECT_EQ((std::vector<std::string>{"edge", "true"}), fx.logged("edge"));
    const auto& q = fx.world().respawn.respawn_queue;
    ASSERT_EQ(2u, q.size()) << "the refused schedule queued nothing";
    EXPECT_EQ(ida, q[0].walker_entity_id);
    EXPECT_EQ(77, q[0].ticks_left) << "no ticks: the resolved match delay";
    EXPECT_EQ(idc, q[1].walker_entity_id);
    EXPECT_EQ(65535, q[1].ticks_left) << "65535 is the top accepted delay";
}

// ===========================================================================
// og_respawn_anchor (bindings_entity.cpp:3035)
// ===========================================================================

TEST(SemScriptMode, a_negative_respawn_anchor_index_is_refused)
{
    SemModeWorld fx;
    fx.spawn_marker(0, 128, 96);
    fx.spawn_marker(0, 192, 96);
    og::sim::respawn_scan_anchors(fx.world());
    fx.run_on_load(
        "    local ok, e = pcall(og.respawn_anchor, 0, -1)\n"
        "    og.log('r', tostring(ok))\n"
        "    og.log('e', e)\n"
        "    og.log('a0', og.respawn_anchor(0, 0))\n");
    EXPECT_EQ((std::vector<std::string>{"r", "false"}), fx.logged("r")) << fx.script_errors();
    EXPECT_TRUE(fx.logged_line("og.respawn_anchor: index -1 out of range [0, 1]"))
        << fx.script_errors();
    EXPECT_EQ((std::vector<std::string>{"a0", "128", "96"}), fx.logged("a0"));
}

// ===========================================================================
// og_spawn_spot_clear (bindings_entity.cpp:3057)
// ===========================================================================

// The probe eats nothing: a drop on the probed spot whose on_eat hook is live
// is not eaten, while the eating route (og.query_passable) over the same
// spot does eat it.
TEST(SemScriptMode, the_spawn_spot_probe_eats_nothing_on_the_spot)
{
    SemModeWorld fx;
    walker* prober = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, prober);
    walker* drop = fx.world().add_fx_ob(Order::Treasure, FAMILY_SILVER_BAR);
    ASSERT_NE(nullptr, drop);
    drop->setxy(224, 224);
    const std::uint32_t prober_id = prober->entity_id();
    const std::string eat_hook =
        "og.register_hooks('treasure', 'core:silver_bar', {\n"
        "  on_eat = function(self, eater)\n"
        "    og.log('EATEN')\n"
        "    return true\n"
        "  end,\n"
        "})\n";
    fx.run_on_load(
        "    local w = og.find_by_id(" + std::to_string(prober_id) + ")\n"
        "    og.log('probe', tostring(og.spawn_spot_clear(w, 224, 224)))\n"
        "    og.log('after_probe')\n"
        "    og.query_passable(224, 224, w)\n"
        "    og.log('after_route')\n",
        eat_hook);
    EXPECT_EQ((std::vector<std::string>{"probe", "true"}), fx.logged("probe"))
        << fx.script_errors();
    // The order of the log lines: no EATEN before the probe ends; the eating
    // route is what produces it.
    const auto& log = fx.vm_log();
    std::size_t probe_at = log.size(), route_at = log.size(), eaten_at = log.size();
    int eaten = 0;
    for (std::size_t i = 0; i < log.size(); i++) {
        if (log[i] == "after_probe") probe_at = i;
        if (log[i] == "after_route") route_at = i;
        if (log[i] == "EATEN") {
            eaten++;
            if (eaten_at == log.size()) eaten_at = i;
        }
    }
    ASSERT_NE(log.size(), probe_at) << fx.script_errors();
    ASSERT_NE(log.size(), route_at) << fx.script_errors();
    EXPECT_EQ(1, eaten) << "control: the eating route eats the drop exactly once: "
                        << fx.script_errors();
    EXPECT_GT(eaten_at, probe_at) << "the probe itself ate the drop";
    EXPECT_LT(eaten_at, route_at);
    EXPECT_NE(nullptr, fx.world().find_by_id(prober_id));
}

// An explicit floor answers for THAT floor, not the prober's.
TEST(SemScriptMode, the_spawn_spot_probe_answers_for_the_floor_it_names)
{
    SemModeWorld fx;
    constexpr int kTx = 6;
    constexpr int kTy = 6;
    fx.add_floor_one_with_wall_at(kTx, kTy);
    walker* prober = fx.spawn_living(FAMILY_SOLDIER, 0, 16, 16);
    ASSERT_NE(nullptr, prober);
    prober->set_floor(0);
    const std::uint32_t prober_id = prober->entity_id();
    const int px = kTx * GRID_SIZE;
    const int py = kTy * GRID_SIZE;
    fx.run_on_load(
        "    local w = og.find_by_id(" + std::to_string(prober_id) + ")\n"
        "    og.log('s', tostring(og.spawn_spot_clear(w, " + to_s(px) + ", " + to_s(py) + ", 1)),\n"
        "           tostring(og.spawn_spot_clear(w, " + to_s(px) + ", " + to_s(py) + ", 0)),\n"
        "           tostring(og.spawn_spot_clear(w, " + to_s(px) + ", " + to_s(py) + ")))\n");
    EXPECT_EQ((std::vector<std::string>{"s", "false", "true", "true"}), fx.logged("s"))
        << "floor 1 is walled there, floor 0 (named or the prober's own) is open: "
        << fx.script_errors();
}

// ===========================================================================
// og_scrub_corpse_stain (bindings_entity.cpp:3074)
// ===========================================================================

TEST(SemScriptMode, a_stain_scrub_with_no_floor_kills_drops_on_every_floor)
{
    SemModeWorld fx;
    fx.add_floor_one_with_wall_at(20, 20);
    walker* s0 = fx.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    walker* s1 = fx.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, s0);
    ASSERT_NE(nullptr, s1);
    s0->set_floor(0);
    s1->set_floor(1);
    s0->setxy(160, 160);
    s1->setxy(160, 160);
    const std::uint32_t id0 = s0->entity_id();
    const std::uint32_t id1 = s1->entity_id();
    fx.run_on_load("    og.scrub_corpse_stain(160, 160)\n");
    EXPECT_FALSE(fx.alive(id0)) << "the floor-0 stain is scrubbed: " << fx.script_errors();
    EXPECT_FALSE(fx.alive(id1)) << "the floor-1 stain at the same spot is scrubbed too";
}

TEST(SemScriptMode, a_stain_scrub_on_a_named_floor_spares_other_floors)
{
    SemModeWorld fx;
    fx.add_floor_one_with_wall_at(20, 20);
    walker* s0 = fx.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, s0);
    s0->set_floor(0);
    s0->setxy(160, 160);
    const std::uint32_t id0 = s0->entity_id();
    fx.run_on_load(
        "    og.scrub_corpse_stain(160, 160, 1)\n"
        "    og.log('after1', tostring(og.find_by_id(" + std::to_string(id0) +
        "):dead()))\n"
        "    og.scrub_corpse_stain(160, 160, 0)\n");
    EXPECT_EQ((std::vector<std::string>{"after1", "0"}), fx.logged("after1"))
        << "a floor-1 scrub at the same spot spares the floor-0 stain: "
        << fx.script_errors();
    EXPECT_FALSE(fx.alive(id0)) << "the floor-0 scrub kills it";
}

// ===========================================================================
// Fixture 3: campaign hooks on the shared UI VM (no world).
// ===========================================================================

namespace {

class SemScriptCampaign : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;

    SemScriptCampaign()
    {
        previous_ = current_game;
        current_game = nullptr;  // dispatch resolves the shared UI VM
        og::script::clear_pack_scripts();
        og::script::clear_pack_family_chunks();
        og::script::clear_pack_lib_modules();
        og::script::hooks::clear_campaign_providers();
    }

    ~SemScriptCampaign() override
    {
        og::script::clear_pack_scripts();
        og::script::clear_pack_family_chunks();
        og::script::clear_pack_lib_modules();
        og::script::hooks::clear_campaign_providers();
        current_game = previous_;
    }

    static void register_script(const std::string& source)
    {
        og::script::register_pack_script({"test.semcampaign", "semcampaign/scripts/c.lua", source});
    }

    static const std::vector<ScriptError>& vm_errors()
    {
        return og::script::active_world_scripts().host().errors();
    }

    static const std::vector<std::string>& vm_log()
    {
        return og::script::active_world_scripts().host().log();
    }

    static std::string dump()
    {
        std::string out;
        for (const auto& e : vm_errors())
            out += "error: " + e.message + "\n";
        for (const auto& l : vm_log())
            out += "log: " + l + "\n";
        return out;
    }

    static bool run_action()
    {
        og::script::hooks::CampaignActionResult result;
        return og::script::hooks::campaign_picker_action("x", result);
    }

    GameplayContext* previous_ = nullptr;
};

}  // namespace

// ===========================================================================
// og_campaign_state_set (bindings_entity.cpp:1945)
// ===========================================================================

TEST_F(SemScriptCampaign, a_campaign_value_below_int32_never_reaches_the_store)
{
    std::map<std::string, std::int32_t> store;
    int calls = 0;
    og::script::hooks::CampaignProviders providers;
    providers.state_set = [&](const std::string& key, std::int32_t value) {
        calls++;
        store[key] = value;
        return true;
    };
    og::script::hooks::install_campaign_providers(std::move(providers));
    register_script(R"LUA(og.register_campaign_hooks({
  picker_action = function(entry_id)
    local ok, err = pcall(og.campaign_state_set, "low", -3000000000)
    og.log("low " .. tostring(ok))
    og.log("msg " .. err)
    og.campaign_state_set("edge", -2147483648)
    og.campaign_state_set("top", 2147483647)
    return nil
  end,
}))LUA");
    ASSERT_TRUE(run_action()) << dump();
    const auto& log = vm_log();
    ASSERT_EQ(2u, log.size()) << dump();
    EXPECT_EQ("low false", log[0]);
    EXPECT_NE(std::string::npos,
              log[1].find("og.campaign_state_set: value out of int32 range"))
        << log[1];
    EXPECT_EQ(2, calls) << "only the two in-range writes reached the store";
    EXPECT_EQ(0u, store.count("low"));
    EXPECT_EQ(-2147483647 - 1, store.at("edge"));
    EXPECT_EQ(2147483647, store.at("top"));
}

// ===========================================================================
// og_campaign_team (bindings_entity.cpp:2046)
// ===========================================================================

TEST_F(SemScriptCampaign, each_roster_row_says_whether_the_hero_marches_tonight)
{
    og::script::hooks::CampaignProviders providers;
    providers.team_snapshot = [] {
        std::vector<og::script::hooks::CampaignRosterEntry> team(2);
        team[0].name = "MILO";
        team[0].family = "SOLDIER";
        team[0].deployed = true;
        team[1].name = "WYRD";
        team[1].family = "MAGE";
        team[1].deployed = false;
        return team;
    };
    og::script::hooks::install_campaign_providers(std::move(providers));
    register_script(R"LUA(og.register_campaign_hooks({
  picker_action = function(entry_id)
    local team = og.campaign_team()
    og.log("dep " .. team[1].name .. " " .. tostring(team[1].deployed) ..
           " " .. team[2].name .. " " .. tostring(team[2].deployed))
    return nil
  end,
}))LUA");
    ASSERT_TRUE(run_action()) << dump();
    ASSERT_EQ(1u, vm_log().size()) << dump();
    EXPECT_EQ("dep MILO true WYRD false", vm_log()[0])
        << "deployed hero reads true, benched hero false";
}

// ===========================================================================
// known_match_setting / og_campaign_match_get (bindings_entity.cpp:2097)
// ===========================================================================

TEST_F(SemScriptCampaign, the_retired_match_knobs_are_unknown_to_campaign_scripts)
{
    int calls = 0;
    og::script::hooks::CampaignProviders providers;
    providers.match_get = [&](const std::string& name) -> std::int32_t {
        calls++;
        return name == "score_limit" ? 5 : 3;
    };
    og::script::hooks::install_campaign_providers(std::move(providers));
    register_script(R"LUA(og.register_campaign_hooks({
  picker_action = function(entry_id)
    local a, ea = pcall(og.campaign_match_get, "team_count")
    local b, eb = pcall(og.campaign_match_get, "strip_troops")
    og.log("ret " .. tostring(a) .. " " .. tostring(b))
    og.log("ea " .. ea)
    og.log("eb " .. eb)
    og.log("live " .. og.campaign_match_get("score_limit"))
    return nil
  end,
}))LUA");
    ASSERT_TRUE(run_action()) << dump();
    const auto& log = vm_log();
    ASSERT_EQ(4u, log.size()) << dump();
    EXPECT_EQ("ret false false", log[0]) << "both retired names raise";
    EXPECT_NE(std::string::npos, log[1].find("unknown setting 'team_count'")) << log[1];
    EXPECT_NE(std::string::npos, log[2].find("unknown setting 'strip_troops'")) << log[2];
    EXPECT_EQ("live 5", log[3]) << "control: a live knob reads the provider";
    EXPECT_EQ(1, calls) << "the retired names never reached the provider";
}

// ===========================================================================
// og_campaign_match_set (bindings_entity.cpp:2136)
// ===========================================================================

TEST_F(SemScriptCampaign, a_match_value_below_int32_never_reaches_the_provider)
{
    std::vector<std::pair<std::string, std::int32_t>> writes;
    og::script::hooks::CampaignProviders providers;
    providers.match_set = [&](const std::string& name, std::int32_t value) {
        writes.emplace_back(name, value);
        return true;
    };
    og::script::hooks::install_campaign_providers(std::move(providers));
    register_script(R"LUA(og.register_campaign_hooks({
  picker_action = function(entry_id)
    local ok, err = pcall(og.campaign_match_set, "score_limit", -3000000000)
    og.log("low " .. tostring(ok))
    og.log("msg " .. err)
    og.log("edge " .. tostring(og.campaign_match_set("score_limit", -2147483648)))
    return nil
  end,
}))LUA");
    ASSERT_TRUE(run_action()) << dump();
    const auto& log = vm_log();
    ASSERT_EQ(3u, log.size()) << dump();
    EXPECT_EQ("low false", log[0]);
    EXPECT_NE(std::string::npos,
              log[1].find("og.campaign_match_set: value out of int32 range"))
        << log[1];
    EXPECT_EQ("edge true", log[2]);
    ASSERT_EQ(1u, writes.size()) << "the wide write reached the provider";
    EXPECT_EQ("score_limit", writes[0].first);
    EXPECT_EQ(-2147483647 - 1, writes[0].second);
}

// ===========================================================================
// is_unfenced / install_entity_bindings_into_og (bindings_entity.cpp:3727)
// ===========================================================================

// og.max/min/clamp/sign stay callable at a pack chunk's top level, and the
// draw-free og.combat.* formulas answer both there and in a campaign hook.
TEST_F(SemScriptCampaign, pure_helpers_and_combat_formulas_answer_outside_dispatch)
{
    register_script(R"LUA(
local k = og.clamp(5, 1, 3)
local mx = og.max(1, 2)
local r = og.combat.yell_radius(3)
og.register_campaign_hooks({
  picker_action = function(entry_id)
    og.log("top " .. k .. " " .. mx .. " " .. r)
    og.log("hook " .. og.combat.yell_radius(3))
    return nil
  end,
}))LUA");
    ASSERT_TRUE(vm_errors().empty()) << "no load error at the chunk's top level: " << dump();
    ASSERT_TRUE(run_action()) << dump();
    const auto& log = vm_log();
    ASSERT_EQ(2u, log.size()) << dump();
    const std::string yr = std::to_string(og::combat::yell_radius(3));
    EXPECT_EQ("top 3 2 " + yr, log[0])
        << "og.clamp, og.max and og.combat at load time";
    EXPECT_EQ("hook " + yr, log[1]) << "og.combat inside a campaign hook";
    EXPECT_TRUE(vm_errors().empty()) << dump();
}
