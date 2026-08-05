/* Unit coverage for the runaway-effect mechanisms
 * (docs/runaway-effects-design.md §3 and §4):
 *   - command::forced flag (set ONLY by force_command)
 *   - statistics::force_fright merge semantics (§3.2)
 *   - statistics::clear_command_for_control_switch selective clear (§4)
 *   - frozen_delay masked getter / raw accessor / thaw immunity (§3.3)
 *   - living::act immunity climb + AI drain shape (thaw to 0, never negative)
 *   - walker::special enemy_freeze bank clamp (§3.1)
 *   - sim_input_handler switch/claim + player thaw integration
 * All mechanisms are golden-invisible by construction: below every cap the
 * behavior asserted here is bit-identical to the legacy paths.
 */
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/core/irandom.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/core/constants.h>
#include <openglad/core/combat_math.h>
#include <memory>
#include <string>
#include <gtest/gtest.h>
#include "test_gameplay_context_scope.h"

namespace {

struct FrightFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    FrightFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_walker(FrightFixture& fx, char family, unsigned char team,
                   signed char user = -1)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(80, 80);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    w->set_user(user);
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

living* add_living_entity(FrightFixture& fx, char family, unsigned char team)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(96, 96);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

} // namespace

// --- §3.2: the forced flag -------------------------------------------------

TEST(StatsFright, forced_flag_set_only_by_force_command)
{
    FrightFixture fx;
    walker* w = add_walker(fx, FAMILY_SOLDIER, 0);

    w->stats()->force_command(COMMAND_WALK, 5, 1, 0);
    ASSERT_TRUE(w->stats()->commands.front().forced)
        << "force_command must mark its entry forced";

    w->stats()->add_command(COMMAND_WALK, 3, -1, 0);
    ASSERT_FALSE(w->stats()->commands.back().forced)
        << "add_command must leave forced false";
    ASSERT_TRUE(w->stats()->commands.front().forced)
        << "the earlier forced entry keeps its flag";
}

// --- §3.2: force_fright fall-through (legacy prepend, byte-identical) -------

TEST(StatsFright, force_fright_falls_through_to_legacy_prepend)
{
    FrightFixture fx;
    walker* w = add_walker(fx, FAMILY_SOLDIER, 0);
    statistics* s = w->stats();

    // Empty queue: exact legacy prepend.
    s->force_fright(25, -1, 0);
    ASSERT_EQ(1u, s->commands.size());
    ASSERT_EQ(COMMAND_WALK, s->commands.front().commandtype);
    ASSERT_EQ(25, s->commands.front().commandcount);
    ASSERT_EQ(-1, s->commands.front().com1);
    ASSERT_EQ(0, s->commands.front().com2);
    ASSERT_TRUE(s->commands.front().forced);

    // Degenerate iterations (the myguy rng(con) subtraction can go negative):
    // preserved verbatim on the fall-through path.
    s->commands.clear();
    s->force_fright(-3, 1, 1);
    ASSERT_EQ(1u, s->commands.size());
    ASSERT_EQ(-3, s->commands.front().commandcount);

    // Non-forced front (an AI-issued walk): prepend, do not merge.
    s->commands.clear();
    s->add_command(COMMAND_WALK, 9, 1, 0);
    s->force_fright(30, 0, 1);
    ASSERT_EQ(2u, s->commands.size());
    ASSERT_EQ(30, s->commands.front().commandcount);
    ASSERT_TRUE(s->commands.front().forced);
    ASSERT_EQ(9, s->commands.back().commandcount);
    ASSERT_FALSE(s->commands.back().forced);

    // Forced front that is NOT a walk (a forced FIRE): prepend, do not merge.
    s->commands.clear();
    s->force_command(COMMAND_FIRE, 4, 1, 0);
    s->force_fright(20, 1, 1);
    ASSERT_EQ(2u, s->commands.size());
    ASSERT_EQ(COMMAND_WALK, s->commands.front().commandtype);
    ASSERT_EQ(20, s->commands.front().commandcount);
}

// --- §3.2: force_fright merge (max-remaining, re-point direction) -----------

TEST(StatsFright, force_fright_merges_max_remaining_and_repoints)
{
    FrightFixture fx;
    walker* w = add_walker(fx, FAMILY_SOLDIER, 0);
    statistics* s = w->stats();

    // A prior forced walk (scare or knockback) sits at the front.
    s->force_command(COMMAND_WALK, 10, 1, 0);

    // Fresh scare with a larger value: count refreshes to max, direction
    // re-points, and nothing is prepended (total fright never sums).
    s->force_fright(25, -1, 1);
    ASSERT_EQ(1u, s->commands.size());
    ASSERT_EQ(25, s->commands.front().commandcount);
    ASSERT_EQ(-1, s->commands.front().com1);
    ASSERT_EQ(1, s->commands.front().com2);

    // Fresh scare with a SMALLER value: count keeps the larger remaining
    // (max semantics), but the direction still tracks the latest cast.
    s->force_fright(5, 1, -1);
    ASSERT_EQ(1u, s->commands.size());
    ASSERT_EQ(25, s->commands.front().commandcount);
    ASSERT_EQ(1, s->commands.front().com1);
    ASSERT_EQ(-1, s->commands.front().com2);

    // Merge clamps deltas exactly like force_command's COMMAND_WALK branch.
    s->force_fright(2, 5, -7);
    ASSERT_EQ(1, s->commands.front().com1);
    ASSERT_EQ(-1, s->commands.front().com2);
    s->force_fright(2, 0, 0);
    ASSERT_EQ(1, s->commands.front().com1);
    ASSERT_EQ(1, s->commands.front().com2);
    ASSERT_EQ(25, s->commands.front().commandcount);
}

// --- §4: selective clear ----------------------------------------------------

TEST(StatsFright, selective_clear_preserves_forced_run_keeps_hygiene_and_charm)
{
    FrightFixture fx;
    walker* w = add_walker(fx, FAMILY_SOLDIER, 0);
    walker* other = add_walker(fx, FAMILY_SOLDIER, 0);
    statistics* s = w->stats();

    // Leading run of two forced walks + an unforced AI walk queued behind.
    s->force_command(COMMAND_WALK, 8, 1, 0);
    s->force_command(COMMAND_WALK, 12, -1, 0);
    s->add_command(COMMAND_WALK, 30, 0, 1);

    // Switch hygiene inputs: leader set, weapon shifted, and a charm applied
    // (real_team_num != 255 means "charmed away from real team 2").
    w->set_leader(other);
    w->set_default_weapon(FAMILY_KNIFE);
    w->set_current_weapon(FAMILY_ARROW);
    w->set_real_team_num(2);
    w->set_team_num(0);

    s->clear_command_for_control_switch();

    // 1. The forced run survives (count + direction), the AI walk is erased.
    ASSERT_EQ(2u, s->commands.size());
    ASSERT_EQ(12, s->commands.front().commandcount);
    ASSERT_EQ(-1, s->commands.front().com1);
    ASSERT_TRUE(s->commands.front().forced);
    ASSERT_EQ(8, s->commands.back().commandcount);
    ASSERT_TRUE(s->commands.back().forced);

    // 2. Legacy switch hygiene kept: weapon reset + leader clear.
    ASSERT_EQ(static_cast<unsigned short>(FAMILY_KNIFE), w->current_weapon());
    ASSERT_TRUE(w->leader() == nullptr);

    // 3. Charm survives: real_team_num is NOT restored.
    ASSERT_EQ(0, static_cast<int>(w->team_num()));
    ASSERT_EQ(2, static_cast<int>(w->real_team_num()));

    // Contrast: the legacy full clear (level load path) still un-charms.
    s->clear_command();
    ASSERT_TRUE(s->commands.empty());
    ASSERT_EQ(2, static_cast<int>(w->team_num()));
    ASSERT_EQ(255, static_cast<int>(w->real_team_num()));
}

TEST(StatsFright, selective_clear_without_forced_front_wipes_queue)
{
    FrightFixture fx;
    walker* w = add_walker(fx, FAMILY_SOLDIER, 0);
    statistics* s = w->stats();

    s->add_command(COMMAND_WALK, 30, 1, 0);
    s->add_command(COMMAND_FOLLOW, 100, 0, 0);
    s->clear_command_for_control_switch();
    ASSERT_TRUE(s->commands.empty())
        << "no leading forced entries: extensionally identical to clear_command";

    // Empty-queue call is a safe no-op on the queue (scenario-start claim).
    s->clear_command_for_control_switch();
    ASSERT_TRUE(s->commands.empty());
}

// --- §3.3: masked getter, raw accessor, immunity write + climb --------------

TEST(StatsFright, frozen_delay_masked_getter_and_raw)
{
    FrightFixture fx;
    walker* w = add_walker(fx, FAMILY_SOLDIER, 0);
    statistics* s = w->stats();

    s->set_frozen_delay(7);
    ASSERT_EQ(7, s->frozen_delay());
    ASSERT_EQ(7, s->frozen_delay_raw());

    s->set_frozen_delay(-5);
    ASSERT_EQ(0, s->frozen_delay()) << "masked getter never returns negatives";
    ASSERT_EQ(-5, s->frozen_delay_raw());

    // Climb: raw +1/tick toward 0; the mask holds throughout.
    s->set_frozen_delay(static_cast<short>(-og::combat::kFreezeThawImmunityTicks));
    for (int i = 0; i < og::combat::kFreezeThawImmunityTicks; ++i)
    {
        ASSERT_EQ(0, s->frozen_delay());
        s->tick_freeze_immunity();
    }
    ASSERT_EQ(0, s->frozen_delay_raw());
}

TEST(StatsFright, player_thaw_tick_writes_immunity_on_last_tick)
{
    FrightFixture fx;
    walker* w = add_walker(fx, FAMILY_SOLDIER, 0);
    statistics* s = w->stats();

    // Ordinary drain step: 5 -> 4 (identical to the AI drains).
    s->set_frozen_delay(5);
    s->player_thaw_tick();
    ASSERT_EQ(4, s->frozen_delay_raw());

    // The 1 -> 0 transition writes the immunity phase instead of 0.
    s->set_frozen_delay(1);
    s->player_thaw_tick();
    ASSERT_EQ(-og::combat::kFreezeThawImmunityTicks, s->frozen_delay_raw());
    ASSERT_EQ(0, s->frozen_delay());

    // Defensive no-op when not frozen (call sites guard, but stay safe).
    s->set_frozen_delay(0);
    s->player_thaw_tick();
    ASSERT_EQ(0, s->frozen_delay_raw());
}

TEST(StatsFright, living_act_climbs_immunity_and_ai_drain_thaws_to_zero)
{
    FrightFixture fx;
    living* self = add_living_entity(fx, FAMILY_SOLDIER, 0);
    self->set_act_type(ACT_CONTROL);

    // Immunity climb: raw recovers +1 per act() and the living still acts
    // (the masked getter keeps the frozen early-return off).
    self->stats()->set_frozen_delay(-3);
    ASSERT_TRUE(self->act());
    ASSERT_EQ(-2, self->stats()->frozen_delay_raw());
    ASSERT_TRUE(self->act());
    ASSERT_TRUE(self->act());
    ASSERT_EQ(0, self->stats()->frozen_delay_raw());
    ASSERT_TRUE(self->act());
    ASSERT_EQ(0, self->stats()->frozen_delay_raw()) << "climb stops at 0";

    // AI drain shape audit (§3.3): the living::act drain thaws to exactly 0,
    // never into the negative phase — only the player-side drain writes it.
    self->stats()->set_frozen_delay(2);
    ASSERT_TRUE(self->act());
    ASSERT_EQ(1, self->stats()->frozen_delay_raw());
    ASSERT_TRUE(self->act());
    ASSERT_EQ(0, self->stats()->frozen_delay_raw());
}

// --- §3.1: enemy_freeze bank clamp in walker::special ------------------------

TEST(StatsFright, walker_special_clamps_enemy_freeze_bank)
{
    FrightFixture fx;
    walker* mage = add_walker(fx, FAMILY_MAGE, 0);
    mage->stats()->set_level(30);
    mage->stats()->set_magicpoints(10000.0f);
    mage->stats()->set_max_magicpoints(10000.0f);
    mage->set_current_special(3); // FREEZE TIME (player/team-0 branch)

    // Single L30 cast from empty: += 20 + 11*30 = 350, clamped to the bank
    // cap post-cast (before: unbounded accumulation).
    ASSERT_TRUE(mage->special());
    ASSERT_EQ(og::combat::kEnemyFreezeBankCap, fx.level.world().enemy_freeze);

    // Chain-casting at the cap never exceeds it.
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(mage->special());
        ASSERT_LE(fx.level.world().enemy_freeze, og::combat::kEnemyFreezeBankCap);
    }
    ASSERT_EQ(og::combat::kEnemyFreezeBankCap, fx.level.world().enemy_freeze);

    // Below the cap the clamp is the identity map (golden identity: every
    // single golden cast is <= 300 pending).
    fx.level.world().enemy_freeze = 152; // the L12 enemy_freeze_mage golden value
    walker* cleric = add_walker(fx, FAMILY_CLERIC, 0);
    cleric->stats()->set_magicpoints(1.0f); // heal (cost 50 in-family) fails
    cleric->set_current_special(1);
    (void)cleric->special();
    ASSERT_EQ(152, fx.level.world().enemy_freeze)
        << "non-freeze specials never move a below-cap bank";
}

// --- §4 + §3.3 integration through sim_process_player_input -----------------

TEST(StatsFright, switch_and_claim_preserve_fright_and_hygiene)
{
    FrightFixture fx;
    walker* a = add_walker(fx, FAMILY_SOLDIER, 0, 0);
    walker* b = add_walker(fx, FAMILY_SOLDIER, 0, -1);
    a->set_act_type(ACT_CONTROL);

    // b carries a scare (forced walk) + an AI walk + switch-hygiene state.
    b->stats()->force_fright(50, 1, 0);
    b->stats()->add_command(COMMAND_WALK, 7, 0, 1);
    b->set_leader(a);
    b->set_default_weapon(FAMILY_KNIFE);
    b->set_current_weapon(FAMILY_ARROW);

    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    InputState input;
    input.clear();

    // Press SwitchChar: control cycles a -> b (b is user -1 at this point,
    // so the handler returns before the oldcontrol clear).
    walker* control = a;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control == b);
    ASSERT_TRUE(result.new_control == b);

    // Next tick claims b (user -1 -> 0) and runs the SELECTIVE clear.
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control == b);
    ASSERT_EQ(0, static_cast<int>(b->user()));

    // The scare survived the switch (before this fix, two SwitchChar presses
    // laundered any scare); the AI walk behind it was erased; weapon reset
    // and leader clear still happened.
    ASSERT_EQ(1u, b->stats()->commands.size());
    ASSERT_TRUE(b->stats()->commands.front().forced);
    ASSERT_EQ(COMMAND_WALK, b->stats()->commands.front().commandtype);
    ASSERT_EQ(50, b->stats()->commands.front().commandcount);
    ASSERT_EQ(static_cast<unsigned short>(FAMILY_KNIFE), b->current_weapon());
    ASSERT_TRUE(b->leader() == nullptr);
}

TEST(StatsFright, claim_path_no_longer_uncharms)
{
    FrightFixture fx;
    walker* c = add_walker(fx, FAMILY_SOLDIER, 0, -1);
    c->set_real_team_num(1); // charmed onto team 0 (real team 1)

    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    InputState input;
    input.clear();

    walker* control = c;
    (void)sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);

    ASSERT_EQ(0, static_cast<int>(c->team_num()));
    ASSERT_EQ(1, static_cast<int>(c->real_team_num()))
        << "the claim-path clear must not restore real_team_num (charm survives)";
}

TEST(StatsFright, sim_player_thaw_immunity_window)
{
    FrightFixture fx;
    walker* a = add_walker(fx, FAMILY_SOLDIER, 0, 0);
    a->set_act_type(ACT_CONTROL);
    a->stats()->set_frozen_delay(2);

    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    InputState input;
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;

    walker* control = a;

    // Tick 1: frozen (2 -> 1); all input swallowed.
    (void)sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_EQ(1, a->stats()->frozen_delay_raw());
    ASSERT_EQ(0, static_cast<int>(a->shifter_down()));

    // Tick 2: the 1 -> 0 transition writes the immunity phase.
    (void)sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_EQ(-og::combat::kFreezeThawImmunityTicks, a->stats()->frozen_delay_raw());

    // Tick 3: masked getter reads 0 — the player acts during immunity
    // (before: this tick was the thaw tick and a re-freeze could land first).
    (void)sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_EQ(1, static_cast<int>(a->shifter_down()))
        << "input reaches the movement branch during the immunity window";
}
