#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
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
        seqs[static_cast<std::size_t>(i)][0] = static_cast<signed char>(i % NUM_FACINGS);
        seqs[static_cast<std::size_t>(i)][1] = -1;
        seqs[static_cast<std::size_t>(i)][2] = -1;
        rows[static_cast<std::size_t>(i)] = seqs[static_cast<std::size_t>(i)].data();
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

namespace detail_modern_dynamics {
namespace {

class CountingRandom final : public IRandom
{
public:
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        ++calls;
        (void)max_exclusive;
        return 0;
    }

    std::uint32_t calls = 0;
};

struct SimInputFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    CountingRandom rng;
    ScopedGameplayContext gameplay;

    SimInputFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(SimInputFixture& fx, unsigned char team,
                   short x, short y, signed char user = -1,
                   short floor = 0)
{
    auto owned = std::make_unique<living>();
    static PixieData test_sprite(
        NUM_FACINGS, 16, 16,
        new unsigned char[NUM_FACINGS * 16 * 16]{});
    owned->set_data(test_sprite);
    owned->set_order_family(Order::Living, FAMILY_SOLDIER);
    bind_test_entity_sim_context(fx.level, owned.get());
    owned->set_sizex(16);
    owned->set_sizey(16);
    owned->set_stepsize(1.0f);
    owned->set_normal_stepsize(1.0f);
    owned->set_floor(floor);
    owned->setxy(x, y);
    owned->set_team_num(team);
    owned->set_real_team_num(255);
    owned->set_dead(0);
    owned->set_user(user);
    owned->set_act_type(user == -1 ? ACT_RANDOM : ACT_CONTROL);
    owned->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    walker* const result = owned.get();
    fx.level.world().oblist.push_back(std::move(owned));
    return result;
}

walker* add_nonliving(SimInputFixture& fx, Order order, unsigned char team,
                      short x, short y)
{
    auto owned = std::make_unique<walker>();
    owned->set_order_family(order, FAMILY_KNIFE);
    bind_test_entity_sim_context(fx.level, owned.get());
    owned->set_sizex(16);
    owned->set_sizey(16);
    owned->set_stepsize(1.0f);
    owned->set_normal_stepsize(1.0f);
    owned->setxy(x, y);
    owned->set_team_num(team);
    owned->set_real_team_num(255);
    owned->set_dead(0);
    walker* const result = owned.get();
    fx.level.world().oblist.push_back(std::move(owned));
    return result;
}

walker* add_control(SimInputFixture& fx)
{
    return add_living(fx, 0, 80, 80, 0);
}

SimInputResult process(SimInputFixture& fx, walker*& control,
                       SimInputDebounce& debounce, const InputState& input)
{
    static std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    return sim_process_player_input(input.players[0], control,
                                    fx.level.world(), 0, 0, debounce,
                                    special_names, &fx.events);
}

void process_right(SimInputFixture& fx, walker*& control,
                   SimInputDebounce& debounce,
                   bool press_fire = false,
                   bool press_special = false)
{
    InputState input;
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Fire)] = press_fire;
    input.players[0].pressed[static_cast<int>(InputAction::Special)] = press_special;
    process(fx, control, debounce, input);
}

SimInputResult process_yell(SimInputFixture& fx, walker*& control,
                            SimInputDebounce& debounce)
{
    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    return process(fx, control, debounce, input);
}

void reset_facing_left(walker* control)
{
    control->setxy(80, 80);
    control->set_curdir(FACE_LEFT);
    control->set_enddir(FACE_LEFT);
    control->set_lastx(-1.0f);
    control->set_lasty(0.0f);
}

void assign_direction_ani(walker* w)
{
    constexpr int kAniRows = NUM_FACINGS * (ANI_SLIME_SPLIT + 1);
    static std::array<std::array<signed char, 4>, kAniRows> seqs{};
    static std::array<signed char*, kAniRows> rows{};
    static bool initialized = false;
    if (!initialized)
    {
        for (int i = 0; i < kAniRows; ++i)
        {
            const signed char frame =
                static_cast<signed char>(i % NUM_FACINGS);
            seqs[static_cast<std::size_t>(i)] = {frame, frame, frame, -1};
            rows[static_cast<std::size_t>(i)] =
                seqs[static_cast<std::size_t>(i)].data();
        }
        initialized = true;
    }
    w->ani = rows.data();
}

void assert_forced_walk(const walker* target, int count,
                        int dx, int dy, std::size_t queue_size = 1)
{
    ASSERT_EQ(queue_size, target->stats()->commands.size());
    const command& pushed = target->stats()->commands.front();
    ASSERT_EQ(COMMAND_WALK, pushed.commandtype);
    ASSERT_EQ(count, pushed.commandcount);
    ASSERT_EQ(dx, pushed.com1);
    ASSERT_EQ(dy, pushed.com2);
    ASSERT_TRUE(pushed.forced);
}

} // namespace

TEST(SimInputUnit, modern_dynamics_moves_on_the_direction_change_tick)
{
    SimInputFixture fx;
    walker* control = add_control(fx);
    SimInputDebounce debounce{};

    reset_facing_left(control);
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Classic;
    process_right(fx, control, debounce);
    ASSERT_EQ(80, control->xpos());
    ASSERT_EQ(80, control->ypos());
    ASSERT_EQ(FACE_LEFT, control->curdir());
    ASSERT_EQ(FACE_RIGHT, control->enddir());
    ASSERT_EQ(1.0f, control->lastx());
    ASSERT_EQ(0.0f, control->lasty());

    reset_facing_left(control);
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
    process_right(fx, control, debounce);
    ASSERT_EQ(81, control->xpos());
    ASSERT_EQ(80, control->ypos());
    ASSERT_EQ(FACE_RIGHT, control->curdir());
    ASSERT_EQ(FACE_RIGHT, control->enddir());
    ASSERT_EQ(1.0f, control->lastx());
    ASSERT_EQ(0.0f, control->lasty());
}

TEST(SimInputUnit, dynamics_turn_cadence_is_exact_for_45_90_and_180_degrees)
{
    struct TurnCase
    {
        short start_dir;
        std::array<short, 4> classic_after_act;
        int turn_ticks;
    };
    constexpr std::array<TurnCase, 3> cases = {{
        {FACE_UP_RIGHT, {FACE_RIGHT, 0, 0, 0}, 1},
        {FACE_UP, {FACE_UP_RIGHT, FACE_RIGHT, 0, 0}, 2},
        {FACE_LEFT,
         {FACE_UP_LEFT, FACE_UP, FACE_UP_RIGHT, FACE_RIGHT}, 4},
    }};

    for (const TurnCase& turn_case : cases)
    {
        SCOPED_TRACE(turn_case.turn_ticks);
        {
            SimInputFixture fx;
            fx.level.world().dynamics_ruleset =
                og::sim::DynamicsRuleset::Classic;
            walker* control = add_control(fx);
            control->set_curdir(static_cast<signed char>(turn_case.start_dir));
            control->set_enddir(static_cast<char>(turn_case.start_dir));
            SimInputDebounce debounce{};

            for (int tick = 0; tick < turn_case.turn_ticks; ++tick)
            {
                process_right(fx, control, debounce);
                ASSERT_EQ(80, control->xpos());
                ASSERT_EQ(FACE_RIGHT, control->enddir());
                ASSERT_EQ(1, control->act());
                ASSERT_EQ(turn_case.classic_after_act[static_cast<std::size_t>(tick)],
                          control->curdir());
                ASSERT_EQ(80, control->xpos());
            }
            process_right(fx, control, debounce);
            ASSERT_EQ(81, control->xpos());
            ASSERT_EQ(FACE_RIGHT, control->curdir());
            ASSERT_EQ(FACE_RIGHT, control->enddir());
        }

        {
            SimInputFixture fx;
            fx.level.world().dynamics_ruleset =
                og::sim::DynamicsRuleset::Modern;
            walker* control = add_control(fx);
            control->set_curdir(static_cast<signed char>(turn_case.start_dir));
            control->set_enddir(static_cast<char>(turn_case.start_dir));
            SimInputDebounce debounce{};

            process_right(fx, control, debounce);
            ASSERT_EQ(81, control->xpos());
            ASSERT_EQ(FACE_RIGHT, control->curdir());
            ASSERT_EQ(FACE_RIGHT, control->enddir());
        }
    }
}

TEST(SimInputUnit, modern_dynamics_resolves_facing_before_fire_and_special)
{
    SimInputFixture fx;
    walker* control = add_control(fx);
    assign_direction_ani(control);
    control->set_fire_frequency(5.0f);
    SimInputDebounce debounce{};

    reset_facing_left(control);
    control->set_ani_type(ANI_WALK);
    control->set_cycle(0);
    control->set_busy(0.0f);
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Classic;
    process_right(fx, control, debounce, /*press_fire=*/true);
    ASSERT_EQ(80, control->xpos());
    ASSERT_EQ(FACE_LEFT, control->curdir());
    ASSERT_EQ(FACE_RIGHT, control->enddir());
    ASSERT_EQ(ANI_ATTACK, control->ani_type());
    ASSERT_EQ(1, control->cycle());
    ASSERT_EQ(FACE_LEFT, control->frame());
    ASSERT_EQ(5.0f, control->busy());

    reset_facing_left(control);
    control->set_ani_type(ANI_WALK);
    control->set_cycle(0);
    control->set_busy(0.0f);
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
    process_right(fx, control, debounce, /*press_fire=*/true);
    ASSERT_EQ(81, control->xpos());
    ASSERT_EQ(FACE_RIGHT, control->curdir());
    ASSERT_EQ(FACE_RIGHT, control->enddir());
    ASSERT_EQ(ANI_ATTACK, control->ani_type());
    ASSERT_EQ(2, control->cycle());
    ASSERT_EQ(FACE_RIGHT, control->frame());
    ASSERT_EQ(5.0f, control->busy());

    // Soldier special 1 (charge) records lastx/lasty in COMMAND_RUSH. Pin
    // that Modern resolves the new direction before dispatching the special.
    control->stats()->clear_command();
    reset_facing_left(control);
    control->set_ani_type(ANI_WALK);
    control->set_cycle(0);
    control->set_busy(0.0f);
    control->set_current_special(1);
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Classic;
    process_right(fx, control, debounce, false, /*press_special=*/true);
    ASSERT_EQ(1u, control->stats()->commands.size());
    ASSERT_EQ(COMMAND_RUSH, control->stats()->commands.front().commandtype);
    ASSERT_EQ(3, control->stats()->commands.front().commandcount);
    ASSERT_EQ(-1, control->stats()->commands.front().com1);
    ASSERT_EQ(0, control->stats()->commands.front().com2);

    control->stats()->clear_command();
    reset_facing_left(control);
    control->set_ani_type(ANI_WALK);
    control->set_cycle(0);
    control->set_busy(0.0f);
    control->set_current_special(1);
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
    process_right(fx, control, debounce, false, /*press_special=*/true);
    ASSERT_EQ(1u, control->stats()->commands.size());
    ASSERT_EQ(COMMAND_RUSH, control->stats()->commands.front().commandtype);
    ASSERT_EQ(3, control->stats()->commands.front().commandcount);
    ASSERT_EQ(1, control->stats()->commands.front().com1);
    ASSERT_EQ(0, control->stats()->commands.front().com2);
}

TEST(SimInputUnit, modern_dynamics_refreshes_blocked_walk_facing_frame)
{
    SimInputFixture fx;
    walker* control = add_control(fx);
    fx.level.world().grid.data[static_cast<std::size_t>(
        5 * fx.level.world().grid.w + 6)] = PIX_WALL2;
    assign_direction_ani(control);
    reset_facing_left(control);
    control->set_ani_type(ANI_WALK);
    control->set_cycle(1);
    control->set_frame(FACE_LEFT);
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
    SimInputDebounce debounce{};

    process_right(fx, control, debounce);

    ASSERT_EQ(80, control->xpos());
    ASSERT_EQ(80, control->ypos());
    ASSERT_EQ(FACE_RIGHT, control->curdir());
    ASSERT_EQ(FACE_RIGHT, control->enddir());
    ASSERT_EQ(FACE_RIGHT, control->frame());
    ASSERT_EQ(1, control->cycle());
    ASSERT_EQ(ANI_WALK, control->ani_type());
}

TEST(SimInputUnit, modern_dynamics_respects_frozen_and_command_gates)
{
    SimInputFixture fx;
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
    walker* control = add_control(fx);
    SimInputDebounce debounce{};

    reset_facing_left(control);
    control->stats()->set_frozen_delay(2);
    process_right(fx, control, debounce);
    ASSERT_EQ(80, control->xpos());
    ASSERT_EQ(FACE_LEFT, control->curdir());
    ASSERT_EQ(FACE_LEFT, control->enddir());
    ASSERT_EQ(1, control->stats()->frozen_delay());

    control->stats()->set_frozen_delay(0);
    control->stats()->add_command(COMMAND_SEARCH, 5, 0, 0);
    reset_facing_left(control);
    process_right(fx, control, debounce);
    ASSERT_EQ(80, control->xpos());
    ASSERT_EQ(FACE_LEFT, control->curdir());
    ASSERT_EQ(FACE_LEFT, control->enddir());
    ASSERT_EQ(1u, control->stats()->commands.size());
    ASSERT_EQ(COMMAND_SEARCH,
              control->stats()->commands.front().commandtype);
    ASSERT_EQ(5, control->stats()->commands.front().commandcount);
}

TEST(SimInputUnit, modern_dynamics_zero_step_actor_aims_before_firing)
{
    SimInputFixture fx;
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
    walker* control = add_control(fx);
    control->set_order_family(Order::Living, FAMILY_TOWER1);
    control->set_stepsize(0.0f);
    control->set_normal_stepsize(0.0f);
    control->set_curdir(FACE_LEFT);
    control->set_enddir(FACE_LEFT);
    control->set_lastx(0.0f);
    control->set_lasty(0.0f);
    control->set_fire_frequency(5.0f);
    control->set_ani_type(ANI_WALK);
    control->set_cycle(0);
    assign_direction_ani(control);
    SimInputDebounce debounce{};

    process_right(fx, control, debounce, /*press_fire=*/true);

    ASSERT_EQ(80, control->xpos());
    ASSERT_EQ(80, control->ypos());
    ASSERT_EQ(FACE_RIGHT, control->curdir());
    ASSERT_EQ(FACE_RIGHT, control->enddir());
    ASSERT_EQ(1.0f, control->lastx());
    ASSERT_EQ(0.0f, control->lasty());
    ASSERT_EQ(ANI_ATTACK, control->ani_type());
    ASSERT_EQ(1, control->cycle());
    ASSERT_EQ(FACE_RIGHT, control->frame());
    ASSERT_EQ(5.0f, control->busy());
}

TEST(SimInputUnit, modern_yell_breakaway_pushes_two_contacts_and_replaces_rally)
{
    SimInputFixture fx;
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
    walker* control = add_control(fx);
    walker* const left = add_living(fx, 1, 63, 80);
    walker* const right = add_living(fx, 1, 97, 80);
    walker* const ally = add_living(fx, 0, 160, 80);
    left->set_curdir(FACE_UP);
    left->set_enddir(FACE_UP);
    right->set_curdir(FACE_UP);
    right->set_enddir(FACE_UP);
    SimInputDebounce debounce{};
    const std::uint32_t rng_calls_before = fx.rng.calls;

    const SimInputResult result = process_yell(fx, control, debounce);

    ASSERT_EQ(rng_calls_before, fx.rng.calls);
    ASSERT_EQ(SOUND_YO, result.play_sound);
    ASSERT_EQ("Yo!", result.notify_text);
    ASSERT_EQ(control, result.notify_source);
    ASSERT_EQ(30, control->yo_delay());
    ASSERT_EQ(og::sim::kBreakawayRecoveryTicks, control->busy());
    ASSERT_EQ(63, left->xpos());
    ASSERT_EQ(97, right->xpos());
    ASSERT_EQ(FACE_LEFT, left->curdir());
    ASSERT_EQ(FACE_LEFT, left->enddir());
    ASSERT_EQ(-1.0f, left->lastx());
    ASSERT_EQ(0.0f, left->lasty());
    ASSERT_EQ(FACE_RIGHT, right->curdir());
    ASSERT_EQ(FACE_RIGHT, right->enddir());
    ASSERT_EQ(1.0f, right->lastx());
    ASSERT_EQ(0.0f, right->lasty());
    assert_forced_walk(left, og::sim::kBreakawayWalkTicks, -1, 0);
    assert_forced_walk(right, og::sim::kBreakawayWalkTicks, 1, 0);
    ASSERT_EQ(nullptr, ally->leader());
    ASSERT_TRUE(ally->stats()->commands.empty());
    ASSERT_EQ(2u, fx.events.size());
    ASSERT_EQ(og::sim::EventKind::PlaySound, fx.events.events()[0].kind);
    ASSERT_EQ(static_cast<std::uint32_t>(SOUND_YO),
              fx.events.events()[0].a);
    ASSERT_EQ(og::sim::EventKind::Notification,
              fx.events.events()[1].kind);
    ASSERT_EQ("Yo!", fx.events.events()[1].text);

    // A repeated press inside the shared Yell cooldown cannot stack walks.
    fx.events.clear();
    const SimInputResult repeat = process_yell(fx, control, debounce);
    ASSERT_EQ(-1, repeat.play_sound);
    ASSERT_TRUE(repeat.notify_text.empty());
    ASSERT_EQ(29, control->yo_delay());
    ASSERT_EQ(0u, fx.events.size());
    assert_forced_walk(left, og::sim::kBreakawayWalkTicks, -1, 0);
    assert_forced_walk(right, og::sim::kBreakawayWalkTicks, 1, 0);

    for (int tick = 0; tick < og::sim::kBreakawayWalkTicks; ++tick)
    {
        ASSERT_EQ(1, left->act());
        ASSERT_EQ(1, right->act());
    }
    ASSERT_EQ(59, left->xpos());
    ASSERT_EQ(101, right->xpos());
    ASSERT_TRUE(left->stats()->commands.empty());
    ASSERT_TRUE(right->stats()->commands.empty());
}

TEST(SimInputUnit, modern_yell_needs_two_contacts_and_classic_keeps_rally)
{
    {
        SimInputFixture fx;
        fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
        walker* control = add_control(fx);
        walker* const only_foe = add_living(fx, 1, 63, 80);
        walker* const ally = add_living(fx, 0, 160, 80);
        only_foe->set_curdir(FACE_UP);
        only_foe->set_enddir(FACE_UP);
        SimInputDebounce debounce{};

        process_yell(fx, control, debounce);

        ASSERT_EQ(FACE_UP, only_foe->curdir());
        ASSERT_EQ(FACE_UP, only_foe->enddir());
        ASSERT_TRUE(only_foe->stats()->commands.empty());
        ASSERT_EQ(control, ally->leader());
        ASSERT_EQ(1u, ally->stats()->commands.size());
        ASSERT_EQ(COMMAND_FOLLOW,
                  ally->stats()->commands.front().commandtype);
        ASSERT_EQ(100, ally->stats()->commands.front().commandcount);
        ASSERT_EQ(0.0f, control->busy());
    }

    {
        SimInputFixture fx;
        fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Classic;
        walker* control = add_control(fx);
        walker* const left = add_living(fx, 1, 63, 80);
        walker* const right = add_living(fx, 1, 97, 80);
        walker* const ally = add_living(fx, 0, 160, 80);
        left->set_curdir(FACE_UP);
        left->set_enddir(FACE_UP);
        right->set_curdir(FACE_DOWN);
        right->set_enddir(FACE_DOWN);
        SimInputDebounce debounce{};

        process_yell(fx, control, debounce);

        ASSERT_EQ(FACE_UP, left->curdir());
        ASSERT_EQ(FACE_UP, left->enddir());
        ASSERT_EQ(FACE_DOWN, right->curdir());
        ASSERT_EQ(FACE_DOWN, right->enddir());
        ASSERT_TRUE(left->stats()->commands.empty());
        ASSERT_TRUE(right->stats()->commands.empty());
        ASSERT_EQ(control, ally->leader());
        ASSERT_EQ(COMMAND_FOLLOW,
                  ally->stats()->commands.front().commandtype);
        ASSERT_EQ(30, control->yo_delay());
        ASSERT_EQ(0.0f, control->busy());
    }
}

TEST(SimInputUnit, modern_yell_forced_walk_expires_against_an_obstacle)
{
    SimInputFixture fx;
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
    walker* control = add_control(fx);
    walker* const left = add_living(fx, 1, 63, 80);
    walker* const right = add_living(fx, 1, 97, 80);
    walker* const blocker =
        add_nonliving(fx, Order::Generator, 1, 48, 80);
    ASSERT_NE(nullptr, blocker);
    assign_direction_ani(left);
    left->set_ani_type(ANI_WALK);
    left->set_cycle(1);
    left->set_frame(FACE_UP);
    SimInputDebounce debounce{};

    process_yell(fx, control, debounce);
    ASSERT_EQ(FACE_LEFT, left->frame());
    ASSERT_EQ(1, left->cycle());
    ASSERT_EQ(ANI_WALK, left->ani_type());
    for (int tick = 0; tick < og::sim::kBreakawayWalkTicks; ++tick)
    {
        ASSERT_EQ(1, left->act());
        ASSERT_EQ(1, right->act());
    }

    ASSERT_EQ(63, left->xpos());
    ASSERT_EQ(FACE_LEFT, left->curdir());
    ASSERT_EQ(ANI_WALK, left->ani_type());
    ASSERT_EQ(101, right->xpos());
    ASSERT_EQ(FACE_RIGHT, right->curdir());
    ASSERT_TRUE(left->stats()->commands.empty());
    ASSERT_TRUE(right->stats()->commands.empty());
}

TEST(SimInputUnit, modern_yell_filters_non_contacts_and_preserves_queue_tail)
{
    SimInputFixture fx;
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
    walker* control = add_control(fx);
    walker* const left = add_living(fx, 1, 63, 80);
    walker* const right = add_living(fx, 1, 97, 80);
    walker* const friendly = add_living(fx, 0, 80, 63);
    walker* const other_floor = add_living(fx, 1, 80, 97, -1, 1);
    walker* const dead = add_living(fx, 1, 80, 63);
    walker* const dormant = add_living(fx, 1, 80, 97);
    walker* const weapon = add_nonliving(fx, Order::Weapon, 1, 63, 80);
    walker* const gap_two = add_living(fx, 1, 98, 80);
    walker* const above = add_living(fx, 1, 80, 63);
    walker* const stationary = add_living(fx, 1, 63, 80);
    walker* const no_collide = add_living(fx, 1, 97, 80);
    dead->set_dead(1);
    dormant->set_dormant(true);
    above->set_sizez(8);
    above->set_worldz(16.0f);
    stationary->set_order_family(Order::Living, FAMILY_TOWER1);
    no_collide->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
    control->set_sizez(8);
    control->set_worldz(0.0f);
    left->stats()->add_command(COMMAND_SEARCH, 7, 0, 0);
    SimInputDebounce debounce{};

    process_yell(fx, control, debounce);

    assert_forced_walk(left, og::sim::kBreakawayWalkTicks, -1, 0, 2);
    ASSERT_EQ(COMMAND_SEARCH, left->stats()->commands.back().commandtype);
    ASSERT_EQ(7, left->stats()->commands.back().commandcount);
    assert_forced_walk(right, og::sim::kBreakawayWalkTicks, 1, 0);
    ASSERT_TRUE(friendly->stats()->commands.empty());
    ASSERT_TRUE(other_floor->stats()->commands.empty());
    ASSERT_TRUE(dead->stats()->commands.empty());
    ASSERT_TRUE(dormant->stats()->commands.empty());
    ASSERT_TRUE(weapon->stats()->commands.empty());
    ASSERT_TRUE(gap_two->stats()->commands.empty());
    ASSERT_TRUE(above->stats()->commands.empty());
    ASSERT_TRUE(stationary->stats()->commands.empty());
    ASSERT_TRUE(no_collide->stats()->commands.empty());
}

TEST(SimInputUnit, modern_yell_requires_actionable_control)
{
    for (int blocked_case = 0; blocked_case < 4; ++blocked_case)
    {
        SCOPED_TRACE(blocked_case);
        SimInputFixture fx;
        fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
        walker* control = add_control(fx);
        walker* const left = add_living(fx, 1, 63, 80);
        walker* const right = add_living(fx, 1, 97, 80);
        switch (blocked_case)
        {
            case 0:
                control->stats()->set_frozen_delay(2);
                break;
            case 1:
                control->set_ani_type(ANI_ATTACK);
                break;
            case 2:
                control->stats()->add_command(COMMAND_SEARCH, 5, 0, 0);
                break;
            case 3:
                control->set_busy(2.0f);
                break;
            default:
                FAIL() << "unexpected actionable-gate case";
        }
        SimInputDebounce debounce{};

        process_yell(fx, control, debounce);

        ASSERT_TRUE(left->stats()->commands.empty());
        ASSERT_TRUE(right->stats()->commands.empty());
        ASSERT_EQ(30, control->yo_delay());
        if (blocked_case == 0)
        {
            ASSERT_EQ(1, control->stats()->frozen_delay());
        }
        if (blocked_case == 2)
        {
            ASSERT_EQ(1u, control->stats()->commands.size());
            ASSERT_EQ(COMMAND_SEARCH,
                      control->stats()->commands.front().commandtype);
            ASSERT_EQ(5, control->stats()->commands.front().commandcount);
        }
        if (blocked_case == 3)
        {
            ASSERT_EQ(2.0f, control->busy());
        }
    }
}

TEST(SimInputUnit, modern_yell_coincident_fallback_is_stable)
{
    SimInputFixture fx;
    fx.level.world().dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
    walker* control = add_control(fx);
    walker* const first = add_living(fx, 1, 80, 80);
    walker* const second = add_living(fx, 1, 80, 80);
    SimInputDebounce debounce{};

    process_yell(fx, control, debounce);

    // Control is oblist ordinal 0, so pre-id contacts use ordinals 1 and 2.
    assert_forced_walk(first, og::sim::kBreakawayWalkTicks, 1, -1);
    assert_forced_walk(second, og::sim::kBreakawayWalkTicks, 1, 0);
    ASSERT_EQ(FACE_UP_RIGHT, first->curdir());
    ASSERT_EQ(FACE_RIGHT, second->curdir());
}

} // namespace detail_modern_dynamics
