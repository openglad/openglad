#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/input_state.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/legacy/base.h>
#include <memory>
#include <string>
#include <gtest/gtest.h>
#include <openglad/gameplay/living.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <array>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include "test_gameplay_context_scope.h"

// --- From test_sim_input_coverage_push.cpp ---
namespace detail_sim_input_coverage_push {
namespace {

struct SimInputFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    SimInputFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(SimInputFixture& fx, unsigned char team, signed char user = -1)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
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

} // namespace

TEST(SimInputUnit, sim_input_find_next_control_priorities)
{
    SimInputFixture fx;
    walker* team_npc = add_living(fx, 0, -1);
    walker* player_like = add_living(fx, 0, -1);
    ASSERT_TRUE(team_npc != nullptr);
    ASSERT_TRUE(player_like != nullptr);

    walker* found = sim_find_next_control(fx.level.world(), 0);
    ASSERT_TRUE(found == team_npc);

    team_npc->set_user(0);
    found = sim_find_next_control(fx.level.world(), 0);
    ASSERT_TRUE(found == player_like);
}

TEST(SimInputUnit, sim_input_endgame_and_control_assignment_paths)
{
    SimInputFixture fx;
    walker* control = nullptr;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};

    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.endgame_requested);
    ASSERT_TRUE(result.endgame_type == 1);

    walker* w = add_living(fx, 0, -1);
    w->stats()->set_hitpoints(37.0f);
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(!result.endgame_requested);
    ASSERT_TRUE(control == w);
    ASSERT_TRUE(result.control_hp_changed);
    ASSERT_TRUE(result.control_hp == 37.0f);
}

TEST(SimInputUnit, sim_input_switch_special_yell_and_mismatch_paths)
{
    SimInputFixture fx;
    walker* control = add_living(fx, 0, 0);
    walker* ally = add_living(fx, 0, -1);
    ASSERT_TRUE(control != nullptr);
    ASSERT_TRUE(ally != nullptr);
    control->set_act_type(ACT_CONTROL);
    ally->set_act_type(ACT_RANDOM);

    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    special_names[FAMILY_SOLDIER][2] = "NONE";

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchSpecial)] = true;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(debounce.changedspec == 1);
    ASSERT_TRUE(control->current_special() == 1);
    ASSERT_TRUE(result.new_control == control);

    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    control->set_action(0);
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.notify_text == "SUMMONING DEFENSE!");
    ASSERT_TRUE(ally->action() == ACTION_FOLLOW);

    control->set_action(ACTION_FOLLOW);
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.notify_text == "RELEASING MEN!");
    ASSERT_TRUE(ally->action() == 0);

    control->set_user(1);
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.new_control == control);

    control->set_user(0);
    control->stats()->set_frozen_delay(1);
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control->stats()->frozen_delay() == 0);
}
} // namespace detail_sim_input_coverage_push

// --- From test_sim_input_r11.cpp ---
namespace detail_sim_input_r11 {
namespace {

struct SimInputFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    SimInputFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(SimInputFixture& fx, unsigned char team, signed char user = -1)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
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

void assign_basic_ani(walker* w)
{
    constexpr int kAniRows = NUM_FACINGS * (ANI_SLIME_SPLIT + 1);
    static std::array<std::array<signed char, 3>, kAniRows> seqs{};
    static std::array<signed char*, kAniRows> rows{};
    for (int i = 0; i < kAniRows; ++i)
    {
        seqs[i][0] = static_cast<signed char>(i % NUM_FACINGS);
        seqs[i][1] = -1;
        seqs[i][2] = -1;
        rows[i] = seqs[i].data();
    }
    w->ani = rows.data();
}

} // namespace

TEST(SimInputUnit, sim_input_r11_switch_char_error_and_wrap_paths)
{
    SimInputFixture fx;
    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    InputState input;
    input.clear();

    walker orphan;
    orphan.set_user(0);
    orphan.set_order_family(Order::Living, FAMILY_SOLDIER);
    walker* control = &orphan;

    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.control_hp_changed);
    ASSERT_TRUE(control == &orphan);

    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    debounce.changedchar = 0;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.control_hp_changed);
    ASSERT_TRUE(control == &orphan);

    // Forward wrap and reverse wrap over eligible entries.
    walker* a = add_living(fx, 0, -1);
    walker* b = add_living(fx, 0, -1);
    walker* c = add_living(fx, 0, -1);
    a->set_user(0);
    control = a;

    input.clear();
    debounce.changedchar = 0;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control == b || control == c);

    input.clear();
    debounce.changedchar = 0;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    control = a;
    a->set_user(0);
    b->set_user(-1);
    c->set_user(-1);
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control == c || control == b);
}

TEST(SimInputUnit, sim_input_r11_switch_special_yell_and_action_default)
{
    SimInputFixture fx;
    walker* control = add_living(fx, 0, 0);
    walker* ally = add_living(fx, 0, -1);
    ASSERT_TRUE(control != nullptr && ally != nullptr);
    control->set_act_type(ACT_CONTROL);
    ally->set_act_type(ACT_RANDOM);

    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    special_names[FAMILY_SOLDIER][1] = "A";
    special_names[FAMILY_SOLDIER][2] = "NONE";

    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchSpecial)] = true;
    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control->current_special() == 1); // wrap due to NONE

    input.clear();
    debounce.changedspec = 0;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.play_sound == SOUND_YO);
    ASSERT_TRUE(control->yo_delay() == 30);
    ASSERT_TRUE(ally->leader() == control);

    // Shift+Yell default case in switch(control->action())
    input.clear();
    control->set_action(99);
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control->action() == 0);
}

TEST(SimInputUnit, sim_input_r11_animate_movement_and_bit_animate_paths)
{
    SimInputFixture fx;
    walker* control = add_living(fx, 0, 0);
    ASSERT_TRUE(control != nullptr);
    control->set_act_type(ACT_CONTROL);
    assign_basic_ani(control);

    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    InputState input;

    // ani_type != ANI_WALK branch
    control->set_ani_type(ANI_ATTACK);
    control->set_cycle(0);
    input.clear();
    SimInputResult result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(result.new_control == control);

    // movement branch (walkstep)
    control->set_ani_type(ANI_WALK);
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    const float x_before = control->xpos();
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control->xpos() >= x_before);

    // BIT_ANIMATE idle animation branch + frame reset path
    control->stats()->set_bit_flags(BIT_ANIMATE, 1);
    control->set_cycle(0);
    input.clear();
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(control->cycle() == 0);

    // held fire path
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Fire)] = true;
    result = sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_EQ(control, result.new_control);
}

// A1 regression: the SwitchChar cycle must never hand control to a dormant
// (delayed-spawn) or dead ally. Dormant walkers are invisible, out of the
// obmap, and excluded from snapshots — landing on one strands the player on
// a ghost and blanks the HUD (bug A10 fallout).
TEST(SimInputUnit, sim_input_switch_char_skips_dormant_and_dead_allies)
{
    SimInputFixture fx;
    SimInputDebounce debounce{};
    std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    InputState input;

    walker* a = add_living(fx, 0, 0);       // current control
    walker* b = add_living(fx, 0, -1);      // dormant delayed-entry ally
    walker* c = add_living(fx, 0, -1);      // awake ally
    a->set_act_type(ACT_CONTROL);
    b->set_dormant(true);

    // Forward cycle from A must skip dormant B and land on C.
    walker* control = a;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_EQ(control, c) << "switch cycle landed on a dormant/dead ally";

    // Keep cycling: with B dormant the rotation is A <-> C, never B.
    for (int i = 0; i < 4; ++i)
    {
        input.clear();
        debounce.changedchar = 0;
        input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
        sim_process_player_input(
            input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
        ASSERT_NE(control, b) << "switch cycle landed on dormant ally at step " << i;
        ASSERT_FALSE(control->dormant());
    }

    // Dead allies are skipped explicitly too (not just via is_friendly).
    c->set_dead(1);
    control = a;
    a->set_user(0);
    input.clear();
    debounce.changedchar = 0;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_EQ(control, a) << "with only dormant/dead allies, control must fall back";

    // Reverse (Shift) cycle honors the same filter.
    c->set_dead(0);
    a->set_user(0);
    control = a;
    input.clear();
    debounce.changedchar = 0;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    sim_process_player_input(
        input.players[0], control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_EQ(control, c);
}
} // namespace detail_sim_input_r11

// --- §4.4 enforcement wiring: the owner-locked policy-on matrix through
// sim_process_player_input (site 1 = the SwitchChar cycle conjunction,
// site 2 = the sim_reacquire_apply death/entry hook). The policy-off twin of
// every case is asserted in the same test: with control_policy == 0 the two
// sites must behave exactly like the legacy shared pool. ---
namespace detail_sim_control_enforcement {
namespace {

struct SimInputFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    SimInputFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }

    GameWorld& world() { return level.world(); }
};

// A living walker; `hero` attaches a myguy carrying `owner` as its
// owner_player_index (guy::kNoOwner = orphaned hero; hero=false = scenario
// troop with no myguy at all).
walker* add_char(SimInputFixture& fx, unsigned char team, signed char user,
                 bool hero, std::uint8_t owner = guy::kNoOwner)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(80, 80);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    w->set_user(user);
    if (hero)
    {
        w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        w->myguy->owner_player_index = owner;
    }
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

using MachineMap = std::array<std::uint8_t, og::sim::kPlayerMachineSlots>;

MachineMap machine_map(
    std::initializer_list<std::pair<int, std::uint8_t>> entries)
{
    MachineMap machines;
    machines.fill(og::sim::kPlayerMachineNone);
    for (const auto& [player, entry] : entries)
        machines[static_cast<std::size_t>(player)] = entry;
    return machines;
}

SimInputResult process(SimInputFixture& fx, const InputState& input,
                       walker*& control, short player_num,
                       SimInputDebounce& debounce)
{
    static std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    return sim_process_player_input(input.players[player_num], control,
                                    fx.world(), player_num, 0, debounce,
                                    special_names, &fx.events);
}

} // namespace

// Site 2, the Follow verdict: a dead seat whose only candidate is a foreign
// machine's hero goes NULL with no endgame request — the walker is never
// stamped (the follow camera's engagement signal). Policy off: today's
// shared-pool claim of the very same walker.
TEST(SimInputUnit, sim_control_owner_locked_death_reacquire_follows_not_claims)
{
    SimInputFixture fx;
    walker* foreign = add_char(fx, 0, -1, true, /*owner=*/2);
    og::sim::set_control_policy(
        fx.world(), og::sim::kControlPolicyOwnerLocked,
        machine_map({{0, og::sim::encode_player_machine(0, true)},
                     {2, og::sim::encode_player_machine(2, true)}}));

    walker* control = nullptr;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    SimInputResult result = process(fx, input, control, 0, debounce);
    ASSERT_FALSE(result.endgame_requested)
        << "an ownership denial must NOT request the endgame";
    ASSERT_EQ(nullptr, control) << "the seat must go null (Follow)";
    ASSERT_EQ(nullptr, result.new_control);
    ASSERT_FALSE(result.control_hp_changed);
    ASSERT_EQ(-1, foreign->user()) << "the refused walker is never stamped";

    // Policy off, same world: today's shared pool claims the foreign hero.
    og::sim::set_control_policy(fx.world(), og::sim::kControlPolicyLegacy,
                                machine_map({}));
    result = process(fx, input, control, 0, debounce);
    ASSERT_FALSE(result.endgame_requested);
    ASSERT_EQ(foreign, control);
    ASSERT_EQ(0, foreign->user());
    ASSERT_TRUE(result.control_hp_changed);
}

// Site 2, Claimed: same-machine seats stay free among their machine's
// characters — the reacquire claims a seatmate's hero exactly like today.
TEST(SimInputUnit, sim_control_owner_locked_death_reacquire_claims_same_machine)
{
    SimInputFixture fx;
    walker* seatmate_hero = add_char(fx, 0, -1, true, /*owner=*/1);
    og::sim::set_control_policy(
        fx.world(), og::sim::kControlPolicyOwnerLocked,
        machine_map({{0, og::sim::encode_player_machine(0, true)},
                     {1, og::sim::encode_player_machine(0, true)}}));

    walker* control = nullptr;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    const SimInputResult result = process(fx, input, control, 0, debounce);
    ASSERT_FALSE(result.endgame_requested);
    ASSERT_EQ(seatmate_hero, control);
    ASSERT_EQ(0, seatmate_hero->user());
    ASSERT_EQ(ACT_CONTROL, seatmate_hero->act_type());
    ASSERT_TRUE(result.control_hp_changed);
}

// Site 2, EndGame: a full wipe keeps today's endgame result under
// owner-locked (the level ending stays reachable, [NET-F3]).
TEST(SimInputUnit, sim_control_owner_locked_death_reacquire_endgame_on_wipe)
{
    SimInputFixture fx;
    walker* corpse = add_char(fx, 0, -1, true, /*owner=*/0);
    corpse->set_dead(1);
    og::sim::set_control_policy(
        fx.world(), og::sim::kControlPolicyOwnerLocked,
        machine_map({{0, og::sim::encode_player_machine(0, true)}}));

    walker* control = nullptr;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    const SimInputResult result = process(fx, input, control, 0, debounce);
    ASSERT_TRUE(result.endgame_requested);
    ASSERT_EQ(1, result.endgame_type);
    ASSERT_EQ(nullptr, control);
}

// Site 2, the [NET-F3] troop rule: an unowned scenario troop is claimable by
// a deployed machine's seat and follow-only for a 0-deploy machine.
TEST(SimInputUnit, sim_control_owner_locked_troop_claim_deployed_vs_zero_deploy)
{
    SimInputFixture fx;
    walker* troop = add_char(fx, 0, -1, /*hero=*/false);
    og::sim::set_control_policy(
        fx.world(), og::sim::kControlPolicyOwnerLocked,
        machine_map({{0, og::sim::encode_player_machine(0, true)},
                     {3, og::sim::encode_player_machine(3, false)}}));

    // The 0-deploy machine's seat follows (no claim, no endgame request).
    walker* control = nullptr;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    SimInputResult result = process(fx, input, control, 3, debounce);
    ASSERT_FALSE(result.endgame_requested);
    ASSERT_EQ(nullptr, control);
    ASSERT_EQ(-1, troop->user());

    // The deployed machine's seat claims the troop and can finish the level.
    control = nullptr;
    result = process(fx, input, control, 0, debounce);
    ASSERT_FALSE(result.endgame_requested);
    ASSERT_EQ(troop, control);
    ASSERT_EQ(0, troop->user());
}

// Site 1: the SwitchChar cycle skips a foreign machine's hero and lands on
// the same-machine one; the policy-off cycle takes the foreign hero first
// (today's deliberate shared-pool stealing). Reverse honors the same filter.
TEST(SimInputUnit, sim_control_owner_locked_switch_char_skips_foreign_hero)
{
    SimInputFixture fx;
    walker* own = add_char(fx, 0, 0, true, /*owner=*/0);
    walker* foreign = add_char(fx, 0, -1, true, /*owner=*/2);
    walker* mate = add_char(fx, 0, -1, true, /*owner=*/1);
    own->set_act_type(ACT_CONTROL);

    SimInputDebounce debounce{};
    InputState input;

    // Policy off first: the legacy cycle lands on the foreign hero (it is
    // the next eligible walker in oblist order).
    walker* control = own;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    process(fx, input, control, 0, debounce);
    ASSERT_EQ(foreign, control) << "legacy shared pool takes the foreign hero";

    // Restore the pre-switch state (the cycle stamps the new user only on
    // the NEXT tick, so only own's released tag needs resetting).
    control = own;
    own->set_user(0);

    // Owner-locked: the same press skips the foreign hero onto the
    // same-machine one, and the foreign hero is never stamped.
    og::sim::set_control_policy(
        fx.world(), og::sim::kControlPolicyOwnerLocked,
        machine_map({{0, og::sim::encode_player_machine(0, true)},
                     {1, og::sim::encode_player_machine(0, true)},
                     {2, og::sim::encode_player_machine(2, true)}}));
    debounce.changedchar = 0;
    process(fx, input, control, 0, debounce);
    ASSERT_EQ(mate, control) << "owner-locked cycle must skip the foreign hero";
    ASSERT_EQ(-1, foreign->user());

    // Reverse (Shift) cycling honors the same conjunction: from own the
    // reverse order visits mate first anyway, never foreign.
    control = own;
    own->set_user(0);
    mate->set_user(-1);
    input.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    debounce.changedchar = 0;
    process(fx, input, control, 0, debounce);
    ASSERT_EQ(mate, control);
    ASSERT_EQ(-1, foreign->user());
}

// Site 1, fallback: when every candidate is foreign the cycle finds nothing
// and control falls back to the seat's own walker (no foreign stamp).
TEST(SimInputUnit, sim_control_owner_locked_switch_char_falls_back_to_own)
{
    SimInputFixture fx;
    walker* own = add_char(fx, 0, 0, true, /*owner=*/0);
    walker* foreign = add_char(fx, 0, -1, true, /*owner=*/2);
    own->set_act_type(ACT_CONTROL);
    og::sim::set_control_policy(
        fx.world(), og::sim::kControlPolicyOwnerLocked,
        machine_map({{0, og::sim::encode_player_machine(0, true)},
                     {2, og::sim::encode_player_machine(2, true)}}));

    walker* control = own;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    const SimInputResult result = process(fx, input, control, 0, debounce);
    ASSERT_EQ(own, control)
        << "with only foreign candidates the cycle must fall back";
    ASSERT_EQ(-1, foreign->user());
    ASSERT_TRUE(result.control_hp_changed);
}
} // namespace detail_sim_control_enforcement
