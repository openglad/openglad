// Scripted-mode engine tests: the TYPE_SCRIPTED activation truth table, the
// win latch (one-shot Lua call, C++ re-assert every tick), the first-arming
// revive flush, the pre-damage gate arms, kill attribution, the
// on_act_override family hook, and the on_respawn engine->Lua handoff.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>

#include "../test_game_world_fixture.h"

#include <memory>
#include <string>
#include <vector>

namespace {

loader& mode_tick_test_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

struct ModeWorld : TestGameWorld
{
    explicit ModeWorld(int level_id = 42)
        : TestGameWorld(level_id)
    {
        init_all_registries();
        og::script::clear_pack_scripts();
        loader* game_loader = &mode_tick_test_loader();
        world().entity_factory =
            [game_loader](Order order, std::int32_t family) {
                return game_loader->create_walker_owned(order, family);
            };
        world().entity_configurator =
            [game_loader](walker& entity, Order order,
                          std::int32_t family) -> const PixieData* {
                game_loader->set_walker(&entity, order, family);
                return game_loader->graphics_for(entity.query_order(),
                                                 entity.family());
            };
        world().entity_derived_stats =
            [game_loader](walker* entity, Order order, std::int32_t family) {
                if (entity != nullptr)
                    game_loader->set_derived_stats(entity, order, family);
            };
        world().type = GameWorld::TYPE_SCRIPTED;
    }

    ~ModeWorld() { og::script::clear_pack_scripts(); }

    void register_script(const std::string& body)
    {
        og::script::register_pack_script({"test.mode", "mode.lua", body});
    }

    const std::vector<std::string>& vm_log()
    {
        return world().scripts().host().log();
    }

    bool logged(const std::string& needle)
    {
        for (const auto& line : vm_log())
        {
            if (line.find(needle) != std::string::npos)
                return true;
        }
        return false;
    }

    int log_count(const std::string& needle)
    {
        int count = 0;
        for (const auto& line : vm_log())
        {
            if (line.find(needle) != std::string::npos)
                count++;
        }
        return count;
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

    walker* spawn_hero(int family, int team, int x, int y, int guy_id)
    {
        walker* w = spawn_living(family, team, x, y);
        if (w == nullptr)
            return nullptr;
        w->set_owned_myguy(std::make_unique<guy>(family));
        w->myguy->id = guy_id;
        return w;
    }

    void tick(int count = 1)
    {
        for (int i = 0; i < count; ++i)
            world().tick();
    }
};

constexpr const char* kMinimalModeScript =
    "og.register_level_hooks(42, {\n"
    "  on_mode_init = function(level)\n"
    "    og.log('mode_init', level)\n"
    "  end,\n"
    "  on_mode_tick = function(level, tick)\n"
    "    og.log('mode_tick', tick)\n"
    "  end,\n"
    "})\n";

}  // namespace

// ---------------------------------------------------------------------------
// Activation truth table
// ---------------------------------------------------------------------------

TEST(ModeTick, scripted_bit_plus_hooks_activates_once)
{
    ModeWorld fx;
    fx.register_script(kMinimalModeScript);
    fx.spawn_living(FAMILY_ORC, 1, 320, 320);  // keep classic completion cold
    fx.tick(2);
    EXPECT_TRUE(fx.world().mode.active);
    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_EQ(1, fx.log_count("mode_init\t42"));
    EXPECT_TRUE(fx.logged("mode_tick\t1"));
    EXPECT_TRUE(fx.logged("mode_tick\t2"));
    EXPECT_FALSE(fx.world().game_ended);
}

TEST(ModeTick, scripted_bit_without_hooks_falls_classic_next_tick)
{
    ModeWorld fx;
    // No pack scripts at all; the world is empty, so classic completion
    // would end it immediately — but the scripted branch owns the failing
    // first tick (the CTF activation discipline).
    fx.tick(1);
    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_FALSE(fx.world().game_ended) << "the failing init owns its tick";
    fx.tick(1);
    EXPECT_TRUE(fx.world().game_ended) << "classic rules from the next tick";
    EXPECT_EQ(0, fx.world().ending);
    EXPECT_EQ(43, fx.world().next_level);
}

TEST(ModeTick, hooks_without_scripted_bit_stay_classic)
{
    ModeWorld fx;
    fx.world().type = 0;
    fx.register_script(kMinimalModeScript);
    fx.tick(1);
    EXPECT_FALSE(fx.world().mode.init_attempted);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_FALSE(fx.logged("mode_init"));
    EXPECT_TRUE(fx.world().game_ended) << "empty classic world completes";
}

TEST(ModeTick, erroring_mode_init_falls_classic_next_tick)
{
    ModeWorld fx;
    fx.register_script(
        "og.register_level_hooks(42, {\n"
        "  on_mode_init = function(level)\n"
        "    error('boom')\n"
        "  end,\n"
        "})\n");
    fx.tick(1);
    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_FALSE(fx.world().game_ended);
    fx.tick(1);
    EXPECT_TRUE(fx.world().game_ended);
}

TEST(ModeTick, run_tick_refuses_failed_init_world_when_called_directly)
{
    ModeWorld fx;
    fx.tick(1);  // no hooks: init attempted and failed, mode inactive
    ASSERT_TRUE(fx.world().mode.init_attempted);
    ASSERT_FALSE(fx.world().mode.active);
    // GameWorld::tick's caller-side guard filters failed-init worlds out,
    // so the engine function's own inactive guard must hold for direct
    // callers too: no re-init, no phases, no win shape.
    og::sim::mode_run_tick(fx.world());
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_FALSE(fx.world().game_ended);
}

// ---------------------------------------------------------------------------
// Win latch
// ---------------------------------------------------------------------------

TEST(ModeTick, declare_winner_is_one_shot_and_reasserted_every_tick)
{
    ModeWorld fx;
    fx.register_script(
        "og.register_level_hooks(42, {\n"
        "  on_mode_init = function(level)\n"
        "    og.set_mode_name('TDM')\n"
        "  end,\n"
        "  on_mode_tick = function(level, tick)\n"
        "    og.log('mode_tick', tick)\n"
        "    if tick == 3 then\n"
        "      og.declare_winner(0)\n"
        "    end\n"
        "  end,\n"
        "})\n");
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 160, 160, 7);
    ASSERT_NE(nullptr, hero);
    fx.spawn_living(FAMILY_ORC, 1, 480, 480);
    fx.tick(2);
    EXPECT_FALSE(fx.world().game_ended);
    fx.tick(1);
    // The ONE Lua call decided the match this tick.
    EXPECT_TRUE(fx.world().game_ended);
    EXPECT_EQ(0, fx.world().ending);
    EXPECT_EQ(43, fx.world().next_level) << "live myguy on team 0 advances";
    EXPECT_TRUE(fx.world().mode.winner_is_player);
    EXPECT_STREQ("TDM", fx.world().mode.name.data());
    // A session layer may step the world again: the latch re-asserts the
    // full win shape (tick entry zeroes it) and runs no more mode Lua.
    fx.tick(2);
    EXPECT_TRUE(fx.world().game_ended);
    EXPECT_EQ(0, fx.world().ending);
    EXPECT_EQ(43, fx.world().next_level);
    EXPECT_EQ(3, fx.log_count("mode_tick\t"))
        << "a decided match dispatches no further on_mode_tick";
}

TEST(ModeTick, end_level_loss_shape_reasserts)
{
    ModeWorld fx;
    fx.register_script(
        "og.register_level_hooks(42, {\n"
        "  on_mode_init = function(level)\n"
        "  end,\n"
        "  on_mode_tick = function(level, tick)\n"
        "    if tick == 1 then\n"
        "      og.end_level(1, -1)\n"
        "    end\n"
        "  end,\n"
        "})\n");
    fx.spawn_living(FAMILY_ORC, 1, 480, 480);
    fx.tick(1);
    EXPECT_TRUE(fx.world().game_ended);
    EXPECT_EQ(1, fx.world().ending);
    EXPECT_EQ(-1, fx.world().next_level);
    fx.tick(1);
    EXPECT_TRUE(fx.world().game_ended);
    EXPECT_EQ(1, fx.world().ending);
    EXPECT_EQ(-1, fx.world().next_level);
}

TEST(ModeTick, latch_arming_revives_player_corpses_before_winner_check)
{
    ModeWorld fx;
    fx.register_script(
        "og.register_level_hooks(42, {\n"
        "  on_mode_init = function(level)\n"
        "  end,\n"
        "  on_mode_tick = function(level, tick)\n"
        "    if tick == 2 then\n"
        "      og.declare_winner(0)\n"
        "    end\n"
        "  end,\n"
        "})\n");
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 160, 160, 7);
    ASSERT_NE(nullptr, hero);
    fx.spawn_living(FAMILY_ORC, 1, 480, 480);
    fx.tick(1);
    // Kill the only team-0 hero between ticks; the corpse persists (myguy).
    hero->set_dead(1);
    fx.tick(1);
    // D2: the first-arming flush revived the corpse BEFORE winner_is_player
    // was computed — a mid-respawn roster never reads as a bot win.
    EXPECT_FALSE(hero->dead());
    EXPECT_TRUE(fx.world().mode.winner_is_player);
    EXPECT_EQ(43, fx.world().next_level);
    ASSERT_NE(nullptr, hero->stats());
    EXPECT_EQ(hero->stats()->max_hitpoints(), hero->stats()->hitpoints());
}

// ---------------------------------------------------------------------------
// Damage gate
// ---------------------------------------------------------------------------

namespace {

// Combat rig: soldier (team 0) attacks orc (team 1) once, directly through
// walker::attack — the dispatch site the gate guards.
struct GateRig
{
    ModeWorld fx;
    walker* attacker = nullptr;
    walker* target = nullptr;

    explicit GateRig(const std::string& script = std::string())
    {
        if (!script.empty())
            fx.register_script(script);
        attacker = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
        target = fx.spawn_living(FAMILY_ORC, 1, 176, 160);
        EXPECT_NE(nullptr, attacker);
        EXPECT_NE(nullptr, target);
        if (target != nullptr && target->stats() != nullptr)
            target->stats()->set_hitpoints(100);
    }

    float hp() const { return target->stats()->hitpoints(); }
};

}  // namespace

TEST(ModeTick, damage_gate_classic_path_untouched_without_hook)
{
    GateRig rig;
    const float before = rig.hp();
    rig.attacker->attack(rig.target);
    EXPECT_LT(rig.hp(), before) << "no registered gate: damage lands";
    // Nothing dispatched, no script errors.
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST(ModeTick, damage_gate_nil_keeps_authored_amount)
{
    // Two rigs — one gated with a nil return, one bare — must land the
    // exact same damage.
    GateRig bare;
    const float bare_before = bare.hp();
    bare.attacker->attack(bare.target);
    const float authored_drop = bare_before - bare.hp();
    ASSERT_GT(authored_drop, 0.0f);

    GateRig gated(
        "og.register_level_hooks(42, {\n"
        "  on_damage = function(target, attacker, amount)\n"
        "    og.log('gate', amount, og.entity_id(target),\n"
        "           og.entity_id(attacker))\n"
        "    return nil\n"
        "  end,\n"
        "})\n");
    const float before = gated.hp();
    gated.attacker->attack(gated.target);
    EXPECT_EQ(authored_drop, before - gated.hp());
    EXPECT_TRUE(gated.fx.logged("gate\t"));
}

TEST(ModeTick, damage_gate_number_replaces_amount)
{
    GateRig rig(
        "og.register_level_hooks(42, {\n"
        "  on_damage = function(target, attacker, amount)\n"
        "    return 5\n"
        "  end,\n"
        "})\n");
    const float before = rig.hp();
    rig.attacker->attack(rig.target);
    EXPECT_EQ(5.0f, before - rig.hp());
}

TEST(ModeTick, damage_gate_negative_replacement_clamps_to_zero)
{
    GateRig rig(
        "og.register_level_hooks(42, {\n"
        "  on_damage = function(target, attacker, amount)\n"
        "    return -3\n"
        "  end,\n"
        "})\n");
    const float before = rig.hp();
    rig.attacker->attack(rig.target);
    EXPECT_EQ(before, rig.hp());
    // A zero-amount hit never stamps attribution.
    EXPECT_EQ(0u, rig.target->last_attacker_id());
}

TEST(ModeTick, damage_gate_false_cancels_the_hit)
{
    GateRig rig(
        "og.register_level_hooks(42, {\n"
        "  on_damage = function(target, attacker, amount)\n"
        "    return false\n"
        "  end,\n"
        "})\n");
    const float before = rig.hp();
    rig.attacker->attack(rig.target);
    EXPECT_EQ(before, rig.hp());
    EXPECT_EQ(0u, rig.target->last_attacker_id())
        << "a cancelled hit must not stamp attribution";
}

// #179: `return 0` is a replacement amount, not a cancel. The hit still
// lands for zero damage, and walker::attack's own hp <= 0 check runs after
// it — so a zero-damage hit finishes off a target already sitting at 0 hp.
// This is the generator explode-instead-of-flip chain from #174.
TEST(ModeTick, damage_gate_zero_still_kills_a_zero_hp_target)
{
    GateRig rig(
        "og.register_level_hooks(42, {\n"
        "  on_damage = function(target, attacker, amount)\n"
        "    return 0\n"
        "  end,\n"
        "})\n");
    rig.target->stats()->set_hitpoints(0.0f);
    ASSERT_FALSE(rig.target->dead());
    rig.attacker->attack(rig.target);
    EXPECT_TRUE(rig.target->dead())
        << "a 0 return applies zero damage but does not skip the death check";
}

// The paired negative: false is the only cancel, and it returns before the
// death check the test above trips.
TEST(ModeTick, damage_gate_false_spares_a_zero_hp_target)
{
    GateRig rig(
        "og.register_level_hooks(42, {\n"
        "  on_damage = function(target, attacker, amount)\n"
        "    return false\n"
        "  end,\n"
        "})\n");
    rig.target->stats()->set_hitpoints(0.0f);
    rig.attacker->attack(rig.target);
    EXPECT_FALSE(rig.target->dead())
        << "false is the only return that skips the whole hit path";
    EXPECT_EQ(0.0f, rig.target->stats()->hitpoints());
}

// `true` is not a cancel either — it is an explicit "keep the amount".
TEST(ModeTick, damage_gate_true_keeps_authored_amount)
{
    GateRig rig(
        "og.register_level_hooks(42, {\n"
        "  on_damage = function(target, attacker, amount)\n"
        "    return true\n"
        "  end,\n"
        "})\n");
    const float before = rig.hp();
    rig.attacker->attack(rig.target);
    EXPECT_LT(rig.hp(), before);
}

// A non-number, non-boolean return keeps the amount rather than cancelling.
TEST(ModeTick, damage_gate_non_number_return_keeps_amount)
{
    GateRig rig(
        "og.register_level_hooks(42, {\n"
        "  on_damage = function(target, attacker, amount)\n"
        "    return {}\n"
        "  end,\n"
        "})\n");
    const float before = rig.hp();
    rig.attacker->attack(rig.target);
    EXPECT_LT(rig.hp(), before);
}

// NaN compares false against everything, so the clamp's `v < 0` and
// `v > 32767` both missed it and it reached static_cast<short>(NaN) --
// undefined behavior. It lands on 0 now.
//
// This pins the CONTRACT, not the old bug: UB has no defined outcome to
// assert against, and on x86-64 the pre-fix cast happened to produce 0 too,
// so this test passes either way. Catching it mechanically needs
// -fsanitize=float-cast-overflow, which GCC's -fsanitize=undefined does NOT
// include; turning that on is its own piece of work because it also fires on
// a pre-existing overflow in compute_xp_from_attack.
TEST(ModeTick, damage_gate_nan_replacement_is_treated_as_zero)
{
    GateRig rig(
        "og.register_level_hooks(42, {\n"
        "  on_damage = function(target, attacker, amount)\n"
        "    return 0/0\n"
        "  end,\n"
        "})\n");
    const float before = rig.hp();
    rig.attacker->attack(rig.target);
    EXPECT_EQ(before, rig.hp())
        << "a NaN replacement applies zero damage rather than invoking UB";
    EXPECT_FALSE(rig.target->dead());
}

TEST(ModeTick, damage_gate_error_keeps_authored_amount)
{
    GateRig rig(
        "og.register_level_hooks(42, {\n"
        "  on_damage = function(target, attacker, amount)\n"
        "    error('gate boom')\n"
        "  end,\n"
        "})\n");
    const float before = rig.hp();
    rig.attacker->attack(rig.target);
    EXPECT_LT(rig.hp(), before) << "R9: an erroring gate keeps the amount";
}

// ---------------------------------------------------------------------------
// Kill attribution
// ---------------------------------------------------------------------------

TEST(ModeTick, weapon_kill_attributes_to_owner_chain_root)
{
    ModeWorld fx;
    fx.register_script(
        "og.register_level_hooks(42, {\n"
        "  on_entity_death = function(ent, killer, killer_team)\n"
        "    og.log('death', og.entity_id(ent),\n"
        "           killer and og.entity_id(killer) or -1, killer_team)\n"
        "  end,\n"
        "})\n");
    walker* soldier = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    walker* victim = fx.spawn_living(FAMILY_ORC, 1, 320, 160);
    ASSERT_NE(nullptr, soldier);
    ASSERT_NE(nullptr, victim);
    walker* knife = fx.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->set_team_num(0);
    knife->set_owner(soldier);
    knife->setxy(304, 160);
    ASSERT_NE(nullptr, victim->stats());
    victim->stats()->set_hitpoints(1);
    knife->attack(victim);
    EXPECT_TRUE(victim->dead());
    const std::string expected =
        "death\t" + std::to_string(victim->entity_id()) + "\t" +
        std::to_string(soldier->entity_id()) + "\t0";
    EXPECT_TRUE(fx.logged(expected))
        << "killer must resolve to the owner-chain ROOT, with its team";
}

TEST(ModeTick, environment_death_passes_nil_killer)
{
    ModeWorld fx;
    fx.register_script(
        "og.register_level_hooks(42, {\n"
        "  on_entity_death = function(ent, killer, killer_team)\n"
        "    og.log('death', killer == nil and 1 or 0, killer_team)\n"
        "  end,\n"
        "})\n");
    walker* victim = fx.spawn_living(FAMILY_ORC, 1, 320, 160);
    ASSERT_NE(nullptr, victim);
    victim->set_dead(1);
    victim->death();
    EXPECT_TRUE(fx.logged("death\t1\t-1"));
}

TEST(ModeTick, stale_attacker_stamp_reads_as_environment)
{
    ModeWorld fx;
    fx.register_script(
        "og.register_level_hooks(42, {\n"
        "  on_entity_death = function(ent, killer, killer_team)\n"
        "    og.log('death', killer == nil and 1 or 0, killer_team)\n"
        "  end,\n"
        "})\n");
    walker* soldier = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    walker* victim = fx.spawn_living(FAMILY_ORC, 1, 176, 160);
    ASSERT_NE(nullptr, soldier);
    ASSERT_NE(nullptr, victim);
    ASSERT_NE(nullptr, victim->stats());
    victim->stats()->set_hitpoints(100);
    soldier->attack(victim);
    EXPECT_NE(0u, victim->last_attacker_id()) << "the hit stamped";
    // 49 ticks later (past kKillAttributionTicks) the stamp is stale.
    fx.world().tick_count_ += og::sim::kKillAttributionTicks + 1;
    victim->set_dead(1);
    victim->death();
    EXPECT_TRUE(fx.logged("death\t1\t-1"));
}

TEST(ModeTick, per_entity_death_hook_receives_killer_args)
{
    ModeWorld fx;
    fx.register_script(
        "og.register_level_hooks(42, {\n"
        "  on_entity_spawn = function(ent)\n"
        "    og.set_entity_hooks(ent, { on_death = function(e, killer, team)\n"
        "      og.log('custom', killer and og.entity_id(killer) or -1, team)\n"
        "    end })\n"
        "  end,\n"
        "})\n");
    walker* soldier = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, soldier);
    // Prime the VM so on_entity_spawn is live for the victim's add_ob.
    walker* victim = fx.spawn_living(FAMILY_ORC, 1, 176, 160);
    ASSERT_NE(nullptr, victim);
    ASSERT_NE(nullptr, victim->stats());
    victim->stats()->set_hitpoints(1);
    soldier->attack(victim);
    EXPECT_TRUE(victim->dead());
    const std::string expected =
        "custom\t" + std::to_string(soldier->entity_id()) + "\t0";
    EXPECT_TRUE(fx.logged(expected));
}

// ---------------------------------------------------------------------------
// on_act_override
// ---------------------------------------------------------------------------

TEST(ModeTick, act_override_true_owns_the_tick)
{
    ModeWorld fx;
    fx.register_script(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  on_act_override = function(self)\n"
        "    og.log('override', og.entity_id(self))\n"
        "    return true\n"
        "  end,\n"
        "})\n");
    walker* w = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, w);
    w->set_ani_type(ANI_WALK);
    ASSERT_NE(nullptr, w->stats());
    w->stats()->set_frozen_delay(3);
    fx.tick(1);
    EXPECT_TRUE(fx.logged("override\t"));
    EXPECT_EQ(3, w->stats()->frozen_delay())
        << "a handled act never reaches the frozen-delay decrement";
}

TEST(ModeTick, act_override_false_falls_through_to_default_act)
{
    ModeWorld fx;
    fx.register_script(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  on_act_override = function(self)\n"
        "    og.log('override', og.entity_id(self))\n"
        "    return false\n"
        "  end,\n"
        "})\n");
    walker* w = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, w);
    w->set_ani_type(ANI_WALK);
    ASSERT_NE(nullptr, w->stats());
    w->stats()->set_frozen_delay(3);
    fx.tick(1);
    EXPECT_TRUE(fx.logged("override\t"));
    EXPECT_EQ(2, w->stats()->frozen_delay())
        << "an unhandled act runs the default living act";
}

// ---------------------------------------------------------------------------
// on_respawn
// ---------------------------------------------------------------------------

TEST(ModeTick, respawn_fires_in_place_and_dispatches_hook)
{
    ModeWorld fx;
    fx.register_script(
        "og.register_level_hooks(42, {\n"
        "  on_mode_init = function(level)\n"
        "  end,\n"
        "  on_mode_tick = function(level, tick)\n"
        "    if tick == 1 then\n"
        "      for _, w in ipairs(og.oblist()) do\n"
        "        if w:dead() ~= 0 then\n"
        "          og.log('sched', og.respawn_schedule(w, 3) and 1 or 0)\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "  end,\n"
        "  on_respawn = function(ent)\n"
        "    og.log('respawn', ent:xpos(), ent:ypos(), ent:dead())\n"
        "  end,\n"
        "})\n");
    fx.spawn_living(FAMILY_ORC, 1, 480, 480);  // keeps the level alive
    walker* corpse = fx.spawn_living(FAMILY_SOLDIER, 0, 224, 224);
    ASSERT_NE(nullptr, corpse);
    corpse->set_dead(1);
    fx.tick(1);
    EXPECT_TRUE(fx.logged("sched\t1"));
    // Countdown 3 -> fires on the third following tick.
    fx.tick(3);
    // The engine default: a fresh walker revived IN PLACE at the recorded
    // death spot, alive when the hook observes it (the hook may reposition).
    EXPECT_TRUE(fx.logged("respawn\t224\t224\t0"));
    int live_soldiers = 0;
    for (const auto& uptr : fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living &&
            w->family() == FAMILY_SOLDIER)
            live_soldiers++;
    }
    EXPECT_EQ(1, live_soldiers);
    EXPECT_TRUE(fx.world().respawn.respawn_queue.empty());
}
