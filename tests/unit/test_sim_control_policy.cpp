// Pure control-rights policy (og::sim, sim_control_policy.cpp —
// company-basecamp design §4.4): the policy-off ≡ legacy equivalence battery
// over crafted classic/allied/CTF/respawn worlds (incl. the [NET-R1] allied
// claimed-teammate baseline), the owner-locked allow/deny matrix
// (same-machine multi-seat), the [NET-F3] unowned-troop rule, the
// Follow-vs-EndGame reacquire verdicts, and the encoding/derivation helpers.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/save_data.h>

#include "test_gameplay_context_scope.h"

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace {

using og::sim::SimReacquire;
using og::sim::control_claim_allowed;
using og::sim::derive_control_policy;
using og::sim::encode_player_machine;
using og::sim::kControlPolicyLegacy;
using og::sim::kControlPolicyOwnerLocked;
using og::sim::kPlayerMachineDeployedBit;
using og::sim::kPlayerMachineNone;
using og::sim::kPlayerMachineSlots;
using og::sim::player_machine_deployed;
using og::sim::player_machine_id;
using og::sim::seat_is_follow_only;
using og::sim::set_control_policy;
using og::sim::sim_find_next_control_owned;
using og::sim::sim_reacquire_control;

using MachineMap = std::array<std::uint8_t, kPlayerMachineSlots>;

MachineMap empty_machine_map()
{
    MachineMap machines;
    machines.fill(kPlayerMachineNone);
    return machines;
}

// The canonical owner-locked lobby of these tests:
//   players 0 and 1 = machine 0 (multi-seat host), deployed;
//   player 2        = machine 2 (joiner), deployed;
//   player 3        = machine 3 (spectator joiner), 0-deploy.
MachineMap canonical_machine_map()
{
    MachineMap machines = empty_machine_map();
    machines[0] = encode_player_machine(0, true);
    machines[1] = encode_player_machine(0, true);
    machines[2] = encode_player_machine(2, true);
    machines[3] = encode_player_machine(3, false);
    return machines;
}

struct ControlPolicyFixture
{
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    ControlPolicyFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }

    GameWorld& world() { return level.world(); }

    struct WalkerSpec
    {
        unsigned char team = 0;
        signed char user = -1;
        bool hero = false; // attach a myguy
        std::uint8_t owner = guy::kNoOwner;
        bool dead = false;
        bool dormant = false;
    };

    walker* add(const WalkerSpec& spec)
    {
        auto w = std::make_unique<walker>();
        w->set_order_family(Order::Living, FAMILY_SOLDIER);
        bind_test_entity_sim_context(level, w.get());
        w->setxy(80, 80);
        w->set_sizex(16);
        w->set_sizey(16);
        w->set_stepsize(1.0f);
        w->set_team_num(spec.team);
        w->set_real_team_num(255);
        w->set_dead(spec.dead ? 1 : 0);
        w->set_user(spec.user);
        if (spec.hero)
        {
            w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
            w->myguy->owner_player_index = spec.owner;
        }
        if (spec.dormant)
            w->set_dormant(true);
        walker* out = w.get();
        world().oblist.push_back(std::move(w));
        return out;
    }

    void clear_walkers() { world().oblist.clear(); }
};

// Assert every next-control decision (scan result + reacquire verdict) of the
// NEW module matches the LEGACY selector for the current world, across a
// spread of teams and seats. Policy off must be byte-identical; a fully
// permissive owner-locked map must reduce to the same decisions.
void expect_decisions_match_legacy(ControlPolicyFixture& fx,
                                   const char* shape_label)
{
    for (short team = 0; team <= 2; ++team)
    {
        walker* const legacy = sim_find_next_control(fx.world(), team);
        for (short player = 0; player <= 3; ++player)
        {
            walker* const owned =
                sim_find_next_control_owned(fx.world(), team, player);
            EXPECT_EQ(legacy, owned)
                << shape_label << ": scan diverged for team " << team
                << " player " << player;

            walker* reacquired = nullptr;
            const SimReacquire verdict =
                sim_reacquire_control(fx.world(), team, player, reacquired);
            if (legacy != nullptr)
            {
                EXPECT_EQ(SimReacquire::Claimed, verdict)
                    << shape_label << ": team " << team << " player " << player;
                EXPECT_EQ(legacy, reacquired)
                    << shape_label << ": team " << team << " player " << player;
            }
            else
            {
                // Never Follow when the policy is permissive: EndGame is
                // exactly today's "no control found" request (the server's
                // suppressions stay downstream).
                EXPECT_EQ(SimReacquire::EndGame, verdict)
                    << shape_label << ": team " << team << " player " << player;
                EXPECT_EQ(nullptr, reacquired) << shape_label;
            }
        }
    }
}

// Populate a deterministic pseudo-random world: a mix of heroes (owned and
// orphaned), troops, claimed walkers, corpses, and dormant spawns.
void populate_random_world(ControlPolicyFixture& fx, std::mt19937& gen)
{
    std::uniform_int_distribution<int> count_dist(0, 6);
    std::uniform_int_distribution<int> team_dist(0, 2);
    std::uniform_int_distribution<int> user_dist(-1, 1);
    std::uniform_int_distribution<int> owner_dist(0, 4);
    std::bernoulli_distribution flag(0.5);
    std::bernoulli_distribution rare(0.25);

    const int count = count_dist(gen);
    for (int i = 0; i < count; ++i)
    {
        ControlPolicyFixture::WalkerSpec spec;
        spec.team = static_cast<unsigned char>(team_dist(gen));
        spec.user = static_cast<signed char>(user_dist(gen));
        spec.hero = flag(gen);
        if (spec.hero)
        {
            const int owner = owner_dist(gen);
            spec.owner = owner == 4 ? guy::kNoOwner
                                    : static_cast<std::uint8_t>(owner);
        }
        spec.dead = rare(gen);
        spec.dormant = rare(gen);
        fx.add(spec);
    }
}

// ---------------------------------------------------------------------------
// Policy-off equivalence: classic / allied / CTF / respawn shapes.

TEST(SimControlPolicy, policy_off_matches_legacy_across_the_four_shapes)
{
    ControlPolicyFixture fx;
    ASSERT_EQ(kControlPolicyLegacy, fx.world().control_policy)
        << "worlds must default to the legacy policy";

    // Classic: own-team hero, claimed hero, troop, corpse, dormant spawn,
    // and an off-team hero for the pass-3 fallback.
    fx.add({.team = 0, .user = -1, .hero = true, .owner = 0});
    fx.add({.team = 0, .user = 1, .hero = true, .owner = 1});
    fx.add({.team = 0, .user = -1, .hero = false});
    fx.add({.team = 0, .user = -1, .hero = true, .owner = 0, .dead = true});
    fx.add({.team = 0, .user = -1, .hero = true, .owner = 2, .dormant = true});
    fx.add({.team = 1, .user = -1, .hero = true, .owner = 2});
    expect_decisions_match_legacy(fx, "classic");

    // Allied fold: everyone on team 0, one claimed teammate ([NET-R1] shape
    // with the teammate still claimable-by-nobody), plus a troop.
    fx.clear_walkers();
    fx.world().allied_mode = 1;
    fx.add({.team = 0, .user = -1, .hero = true, .owner = 0, .dead = true});
    fx.add({.team = 0, .user = 1, .hero = true, .owner = 1});
    fx.add({.team = 0, .user = -1, .hero = false});
    expect_decisions_match_legacy(fx, "allied");

    // CTF: the scans are mode-agnostic; the CTF own-hero preference lives in
    // bind_player (§4.4 site 3), not here.
    fx.clear_walkers();
    fx.world().allied_mode = 0;
    fx.world().type = static_cast<char>(fx.world().type | GameWorld::TYPE_CTF);
    fx.add({.team = 0, .user = -1, .hero = true, .owner = 0});
    fx.add({.team = 1, .user = -1, .hero = true, .owner = 2});
    fx.add({.team = 1, .user = 2, .hero = true, .owner = 2, .dead = true});
    expect_decisions_match_legacy(fx, "ctf");

    // Respawn queue: dead heroes stay in the oblist awaiting revival; both
    // scans must skip them identically (the respawn endgame suppression is a
    // downstream server concern).
    fx.clear_walkers();
    fx.world().type = static_cast<char>(fx.world().type & ~GameWorld::TYPE_CTF);
    fx.world().respawn_mode = 2;
    fx.add({.team = 0, .user = -1, .hero = true, .owner = 0, .dead = true});
    fx.add({.team = 0, .user = -1, .hero = true, .owner = 1, .dead = true});
    fx.add({.team = 0, .user = -1, .hero = false});
    expect_decisions_match_legacy(fx, "respawn");
}

TEST(SimControlPolicy, policy_off_matches_legacy_over_random_worlds)
{
    ControlPolicyFixture fx;
    std::mt19937 gen(20260720u);
    for (int round = 0; round < 150; ++round)
    {
        fx.clear_walkers();
        populate_random_world(fx, gen);
        expect_decisions_match_legacy(fx, "random policy-off");
    }
}

// The reimplemented owner-locked 3-pass scan must reduce to the legacy scan
// when the machine map permits everything (all owners on one deployed
// machine): this pins the pass structure — including pass 3's deliberate
// lack of a dormant() check — against sim_find_next_control itself.
TEST(SimControlPolicy, owner_locked_permissive_map_matches_legacy_over_random_worlds)
{
    ControlPolicyFixture fx;
    MachineMap machines;
    machines.fill(encode_player_machine(0, true));
    set_control_policy(fx.world(), kControlPolicyOwnerLocked, machines);

    std::mt19937 gen(4711u);
    for (int round = 0; round < 150; ++round)
    {
        fx.clear_walkers();
        populate_random_world(fx, gen);
        for (short team = 0; team <= 2; ++team)
        {
            walker* const legacy = sim_find_next_control(fx.world(), team);
            for (short player = 0; player <= 3; ++player)
            {
                EXPECT_EQ(legacy,
                          sim_find_next_control_owned(fx.world(), team, player))
                    << "permissive owner-locked scan diverged, team " << team
                    << " player " << player;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// [NET-R1] baseline: the allied claimed-teammates-alive shape. Policy off
// must produce today's EndGame request (which the server's bound-team
// suppression turns into seat-null + ControlChange 0 + no world ending —
// pinned end-to-end by CtfNetwork.allied_claimed_teammate_alive_...).

TEST(SimControlPolicy, net_r1_allied_claimed_teammate_shape_yields_endgame_verdict)
{
    ControlPolicyFixture fx;
    fx.world().allied_mode = 1;
    fx.add({.team = 0, .user = -1, .hero = true, .owner = 0, .dead = true});
    walker* const teammate =
        fx.add({.team = 0, .user = 1, .hero = true, .owner = 1});
    ASSERT_FALSE(teammate->dead());

    // Old path: no sim_find_next_control pass may claim a user() != -1
    // walker, so the legacy scan comes up empty and requests the endgame.
    ASSERT_EQ(nullptr, sim_find_next_control(fx.world(), 0));

    // New path, policy off: the EndGame verdict, NOT Follow — byte-identical
    // to today once the downstream suppression swallows it.
    walker* reacquired = nullptr;
    EXPECT_EQ(SimReacquire::EndGame,
              sim_reacquire_control(fx.world(), 0, 0, reacquired));
    EXPECT_EQ(nullptr, reacquired);

    // Owner-locked, same shape: the legacy probe cannot see the claimed
    // teammate either, so the verdict stays EndGame (justified by [NET-R1]:
    // the existing suppression tracks the requester's own bound team — there
    // is no spurious-wipe bug to fix, and Follow is reserved for pure
    // ownership denials).
    set_control_policy(fx.world(), kControlPolicyOwnerLocked,
                       canonical_machine_map());
    reacquired = nullptr;
    EXPECT_EQ(SimReacquire::EndGame,
              sim_reacquire_control(fx.world(), 0, 0, reacquired));
    EXPECT_EQ(nullptr, reacquired);

    // Terminal equivalence: the teammate dies too — still EndGame in both
    // policies (the full wipe must end the level).
    teammate->set_dead(1);
    reacquired = nullptr;
    EXPECT_EQ(SimReacquire::EndGame,
              sim_reacquire_control(fx.world(), 0, 0, reacquired));
    set_control_policy(fx.world(), kControlPolicyLegacy, empty_machine_map());
    reacquired = nullptr;
    EXPECT_EQ(SimReacquire::EndGame,
              sim_reacquire_control(fx.world(), 0, 0, reacquired));
}

// ---------------------------------------------------------------------------
// Owner-locked allow/deny matrix.

TEST(SimControlPolicy, owner_locked_allow_deny_matrix_with_same_machine_seats)
{
    ControlPolicyFixture fx;
    walker* const host_hero =
        fx.add({.team = 0, .user = -1, .hero = true, .owner = 0});
    walker* const joiner_hero =
        fx.add({.team = 0, .user = -1, .hero = true, .owner = 2});
    walker* const troop = fx.add({.team = 0, .user = -1, .hero = false});

    // Policy off: everything is claimable by everyone (v7 shared pool).
    for (short player = 0; player <= 3; ++player)
    {
        EXPECT_TRUE(control_claim_allowed(fx.world(), host_hero, player));
        EXPECT_TRUE(control_claim_allowed(fx.world(), joiner_hero, player));
        EXPECT_TRUE(control_claim_allowed(fx.world(), troop, player));
    }

    set_control_policy(fx.world(), kControlPolicyOwnerLocked,
                       canonical_machine_map());

    // Same-machine seats are free among their machine's characters: seat 1
    // (the host's second local player) may claim seat 0's hero.
    EXPECT_TRUE(control_claim_allowed(fx.world(), host_hero, 0));
    EXPECT_TRUE(control_claim_allowed(fx.world(), host_hero, 1));
    // Cross-machine claims are denied both ways.
    EXPECT_FALSE(control_claim_allowed(fx.world(), host_hero, 2));
    EXPECT_FALSE(control_claim_allowed(fx.world(), joiner_hero, 0));
    EXPECT_FALSE(control_claim_allowed(fx.world(), joiner_hero, 1));
    EXPECT_TRUE(control_claim_allowed(fx.world(), joiner_hero, 2));
    // The 0-deploy machine owns nothing and may claim no owned hero.
    EXPECT_FALSE(control_claim_allowed(fx.world(), host_hero, 3));
    EXPECT_FALSE(control_claim_allowed(fx.world(), joiner_hero, 3));
    // Unbound / out-of-range seats claim nothing under owner-locked.
    EXPECT_FALSE(control_claim_allowed(fx.world(), host_hero, 5));
    EXPECT_FALSE(control_claim_allowed(fx.world(), troop, -1));
    EXPECT_FALSE(control_claim_allowed(fx.world(), troop, 99));
    // Null walker is never claimable.
    EXPECT_FALSE(control_claim_allowed(fx.world(), nullptr, 0));
}

TEST(SimControlPolicy, owner_locked_unowned_troop_rule_net_f3)
{
    ControlPolicyFixture fx;
    walker* const troop = fx.add({.team = 0, .user = -1, .hero = false});
    // An orphaned hero (owner tag never stamped) and one whose tagged owner
    // has no machine entry this level both fall back to the troop rule, so
    // they can never become permanently unclaimable.
    walker* const orphan =
        fx.add({.team = 0, .user = -1, .hero = true, .owner = guy::kNoOwner});
    walker* const unbound_owner =
        fx.add({.team = 0, .user = -1, .hero = true, .owner = 9});

    set_control_policy(fx.world(), kControlPolicyOwnerLocked,
                       canonical_machine_map());

    for (walker* w : {troop, orphan, unbound_owner})
    {
        // Deployed machines (both host seats + the joiner) may claim.
        EXPECT_TRUE(control_claim_allowed(fx.world(), w, 0));
        EXPECT_TRUE(control_claim_allowed(fx.world(), w, 1));
        EXPECT_TRUE(control_claim_allowed(fx.world(), w, 2));
        // The 0-deploy machine stays follow-only.
        EXPECT_FALSE(control_claim_allowed(fx.world(), w, 3));
    }
}

// ---------------------------------------------------------------------------
// Reacquire verdicts: the [NET-F3] softlock killer and Follow-vs-EndGame.

TEST(SimControlPolicy, owner_locked_reacquire_claims_troop_for_deployed_machines)
{
    ControlPolicyFixture fx;
    // All owned heroes are dead; an allied scenario troop fights on.
    fx.add({.team = 0, .user = -1, .hero = true, .owner = 0, .dead = true});
    fx.add({.team = 0, .user = -1, .hero = true, .owner = 2, .dead = true});
    walker* const troop = fx.add({.team = 0, .user = -1, .hero = false});

    set_control_policy(fx.world(), kControlPolicyOwnerLocked,
                       canonical_machine_map());

    // A deployed machine claims the troop and can finish the level.
    walker* reacquired = nullptr;
    EXPECT_EQ(SimReacquire::Claimed,
              sim_reacquire_control(fx.world(), 0, 0, reacquired));
    EXPECT_EQ(troop, reacquired);
    EXPECT_EQ(troop, sim_find_next_control_owned(fx.world(), 0, 2));

    // The 0-deploy machine keeps following (never claims, never requests the
    // endgame while the troop lives).
    reacquired = nullptr;
    EXPECT_EQ(SimReacquire::Follow,
              sim_reacquire_control(fx.world(), 0, 3, reacquired));
    EXPECT_EQ(nullptr, reacquired);

    // Once the troop falls, EndGame becomes reachable for every seat — the
    // softlock [NET-F3] guards against cannot occur.
    troop->set_dead(1);
    for (short player : {short{0}, short{2}, short{3}})
    {
        reacquired = nullptr;
        EXPECT_EQ(SimReacquire::EndGame,
                  sim_reacquire_control(fx.world(), 0, player, reacquired))
            << "player " << player;
        EXPECT_EQ(nullptr, reacquired);
    }
}

TEST(SimControlPolicy, owner_locked_follow_when_only_foreign_heroes_remain)
{
    ControlPolicyFixture fx;
    // The host machine's heroes are dead; the joiner's hero lives UNCLAIMED
    // (its player is between claims). Legacy would hand the host the
    // joiner's hero from the shared pool; owner-locked must spectate instead.
    fx.add({.team = 0, .user = -1, .hero = true, .owner = 0, .dead = true});
    fx.add({.team = 0, .user = -1, .hero = true, .owner = 1, .dead = true});
    walker* const foreign =
        fx.add({.team = 0, .user = -1, .hero = true, .owner = 2});

    set_control_policy(fx.world(), kControlPolicyOwnerLocked,
                       canonical_machine_map());

    ASSERT_EQ(foreign, sim_find_next_control(fx.world(), 0))
        << "the legacy pool WOULD claim the foreign hero";

    walker* reacquired = nullptr;
    EXPECT_EQ(SimReacquire::Follow,
              sim_reacquire_control(fx.world(), 0, 0, reacquired));
    EXPECT_EQ(nullptr, reacquired);
    EXPECT_EQ(SimReacquire::Follow,
              sim_reacquire_control(fx.world(), 0, 1, reacquired));

    // The owner itself still claims its hero.
    EXPECT_EQ(SimReacquire::Claimed,
              sim_reacquire_control(fx.world(), 0, 2, reacquired));
    EXPECT_EQ(foreign, reacquired);

    // Full wipe: everyone converges on EndGame.
    foreign->set_dead(1);
    for (short player = 0; player <= 3; ++player)
    {
        reacquired = nullptr;
        EXPECT_EQ(SimReacquire::EndGame,
                  sim_reacquire_control(fx.world(), 0, player, reacquired))
            << "player " << player;
    }
}

// ---------------------------------------------------------------------------
// SwitchChar cycle composition (§4.4 site 1 preview): the legacy cycle
// filter conjoined with control_claim_allowed keeps same-machine cycling and
// skips foreign heroes; with the policy off the conjunction is the legacy
// filter exactly.

TEST(SimControlPolicy, cycle_filter_conjunction_skips_foreign_heroes)
{
    ControlPolicyFixture fx;
    walker* const current =
        fx.add({.team = 0, .user = 0, .hero = true, .owner = 0});
    walker* const foreign =
        fx.add({.team = 0, .user = -1, .hero = true, .owner = 2});
    walker* const same_machine =
        fx.add({.team = 0, .user = -1, .hero = true, .owner = 1});

    const short my_team = 0;
    walker* const oldcontrol = current;
    const auto legacy_filter = [oldcontrol, my_team](const walker* w) {
        return !w->dead() && !w->dormant() &&
               w->query_order() == Order::Living &&
               w->is_friendly(oldcontrol) && w->team_num() == my_team &&
               w->real_team_num() == 255 && w->user() == -1;
    };

    // Policy off: the conjunction changes nothing — the legacy pick (the
    // next matching walker in oblist order) survives.
    const auto conjoined_for = [&](short player) {
        return [&, player](const walker* w) {
            return legacy_filter(w) &&
                   control_claim_allowed(fx.world(), w, player);
        };
    };
    EXPECT_EQ(foreign, sim_cycle_next_character(fx.world().oblist, current,
                                                false, legacy_filter));
    EXPECT_EQ(foreign, sim_cycle_next_character(fx.world().oblist, current,
                                                false, conjoined_for(0)));

    // Owner-locked: the same-machine seat cycles onto its machine's other
    // hero, skipping the foreign one the legacy filter would have taken.
    set_control_policy(fx.world(), kControlPolicyOwnerLocked,
                       canonical_machine_map());
    EXPECT_EQ(same_machine, sim_cycle_next_character(fx.world().oblist, current,
                                                     false, conjoined_for(0)));
    // Reverse cycling honors the same denial.
    EXPECT_EQ(same_machine, sim_cycle_next_character(fx.world().oblist, current,
                                                     true, conjoined_for(0)));
}

// ---------------------------------------------------------------------------
// The §4.4 site-2 hook: sim_reacquire_apply maps the reacquire verdict onto
// the caller's SimInputResult (Claimed ⇒ false + control set + result
// untouched; Follow ⇒ true + null control + result untouched; EndGame ⇒
// true + today's endgame fields).

TEST(SimControlPolicy, reacquire_apply_maps_verdicts_onto_sim_input_result)
{
    ControlPolicyFixture fx;

    // Claimed: the hook is transparent — the caller runs today's claim tail.
    walker* const own_hero =
        fx.add({.team = 0, .user = -1, .hero = true, .owner = 0});
    walker* control = nullptr;
    SimInputResult result;
    EXPECT_FALSE(og::sim::sim_reacquire_apply(fx.world(), 0, 0, control, result));
    EXPECT_EQ(own_hero, control);
    EXPECT_FALSE(result.endgame_requested);
    EXPECT_EQ(0, result.endgame_type);
    EXPECT_FALSE(result.control_hp_changed);

    // Follow (owner-locked pure ownership denial): return-now with the
    // result untouched — the null seat is the only observable.
    fx.clear_walkers();
    walker* const foreign =
        fx.add({.team = 0, .user = -1, .hero = true, .owner = 2});
    set_control_policy(fx.world(), kControlPolicyOwnerLocked,
                       canonical_machine_map());
    ASSERT_EQ(foreign, sim_find_next_control(fx.world(), 0))
        << "the legacy pool WOULD claim the foreign hero";
    control = nullptr;
    result = {};
    EXPECT_TRUE(og::sim::sim_reacquire_apply(fx.world(), 0, 0, control, result));
    EXPECT_EQ(nullptr, control);
    EXPECT_FALSE(result.endgame_requested);
    EXPECT_EQ(0, result.endgame_type);
    EXPECT_FALSE(result.control_hp_changed);

    // EndGame: exactly today's "no control found" result fields, in both
    // policies (full wipe).
    foreign->set_dead(1);
    control = nullptr;
    result = {};
    EXPECT_TRUE(og::sim::sim_reacquire_apply(fx.world(), 0, 0, control, result));
    EXPECT_EQ(nullptr, control);
    EXPECT_TRUE(result.endgame_requested);
    EXPECT_EQ(1, result.endgame_type);
    set_control_policy(fx.world(), kControlPolicyLegacy, empty_machine_map());
    control = nullptr;
    result = {};
    EXPECT_TRUE(og::sim::sim_reacquire_apply(fx.world(), 0, 0, control, result));
    EXPECT_TRUE(result.endgame_requested);
    EXPECT_EQ(1, result.endgame_type);
}

// ---------------------------------------------------------------------------
// Helpers: derivation, encoding, classification, stamping.

TEST(SimControlPolicy, derive_encode_and_classify_helpers)
{
    // Owner-locked = networked && !cross_control, from the game-start config.
    EXPECT_EQ(kControlPolicyLegacy, derive_control_policy(false, false));
    EXPECT_EQ(kControlPolicyLegacy, derive_control_policy(false, true));
    EXPECT_EQ(kControlPolicyLegacy, derive_control_policy(true, true));
    EXPECT_EQ(kControlPolicyOwnerLocked, derive_control_policy(true, false));

    // Encoding roundtrip (§4.1: low 7 bits machine id, bit7 deployed).
    EXPECT_EQ(0x82, encode_player_machine(2, true));
    EXPECT_EQ(0x03, encode_player_machine(3, false));
    EXPECT_EQ(2, player_machine_id(0x82));
    EXPECT_EQ(3, player_machine_id(0x03));
    EXPECT_TRUE(player_machine_deployed(0x82));
    EXPECT_FALSE(player_machine_deployed(0x03));
    // 0xff means "no player", never "deployed machine 127".
    EXPECT_FALSE(player_machine_deployed(kPlayerMachineNone));
    // A machine id above the mask never bleeds into the deployed bit.
    EXPECT_EQ(0x7f, encode_player_machine(0xff, false));

    ControlPolicyFixture fx;
    // Legacy policy: no seat is follow-only, bound or not.
    EXPECT_FALSE(seat_is_follow_only(fx.world(), 0));
    EXPECT_FALSE(seat_is_follow_only(fx.world(), 15));

    const MachineMap machines = canonical_machine_map();
    set_control_policy(fx.world(), kControlPolicyOwnerLocked, machines);
    EXPECT_EQ(kControlPolicyOwnerLocked, fx.world().control_policy);
    EXPECT_EQ(machines[2], fx.world().player_machine[2]);

    // Deployed machines' seats are not follow-only; the 0-deploy machine,
    // unbound seats, and out-of-range indices are.
    EXPECT_FALSE(seat_is_follow_only(fx.world(), 0));
    EXPECT_FALSE(seat_is_follow_only(fx.world(), 1));
    EXPECT_FALSE(seat_is_follow_only(fx.world(), 2));
    EXPECT_TRUE(seat_is_follow_only(fx.world(), 3));
    EXPECT_TRUE(seat_is_follow_only(fx.world(), 7));
    EXPECT_TRUE(seat_is_follow_only(fx.world(), -1));
    EXPECT_TRUE(seat_is_follow_only(fx.world(), 99));
}

// §4.4 install helpers: the machine map derives from the lobby bindings
// (machine id = the peer's lowest bound global player index) and the
// deploy-filtered roster's owner tags (bit7 ORs across a machine's seats);
// install_control_policy stamps the derived policy + map in one call.
TEST(SimControlPolicy, install_helpers_build_machine_map_from_bindings_and_roster)
{
    // Roster: owners 1 (twice), 3, one unowned (kNoOwner), one null slot,
    // one out-of-range tag — only players 1 and 3 count as deployed.
    std::vector<std::unique_ptr<guy>> roster;
    roster.push_back(std::make_unique<guy>(FAMILY_SOLDIER));
    roster.back()->owner_player_index = 1;
    roster.push_back(std::make_unique<guy>(FAMILY_ELF));
    roster.back()->owner_player_index = 1;
    roster.push_back(std::make_unique<guy>(FAMILY_MAGE));
    roster.back()->owner_player_index = 3;
    roster.push_back(std::make_unique<guy>(FAMILY_ARCHER));
    EXPECT_EQ(guy::kNoOwner, roster.back()->owner_player_index);
    roster.push_back(nullptr);
    roster.push_back(std::make_unique<guy>(FAMILY_THIEF));
    roster.back()->owner_player_index = 200;

    const std::array<bool, kPlayerMachineSlots> deployed =
        og::sim::deployed_players_from_roster(roster);
    for (std::size_t index = 0; index < deployed.size(); ++index)
        EXPECT_EQ(index == 1 || index == 3, deployed[index]) << index;

    // Bindings: peer 10 seats players 0+1 (multi-seat machine), peer 20
    // seats player 3, peer 30 seats player 5 (0-deploy), plus one unbound
    // (0xff) binding that must be ignored.
    std::vector<og::sim::LobbyPlayerBinding> bindings;
    bindings.push_back(
        og::sim::LobbyPlayerBinding{.peer_id = 10u, .player_index = 1u, .team = 0});
    bindings.push_back(
        og::sim::LobbyPlayerBinding{.peer_id = 10u, .player_index = 0u, .team = 0});
    bindings.push_back(
        og::sim::LobbyPlayerBinding{.peer_id = 20u, .player_index = 3u, .team = 1});
    bindings.push_back(
        og::sim::LobbyPlayerBinding{.peer_id = 30u, .player_index = 5u, .team = 2});
    bindings.push_back(
        og::sim::LobbyPlayerBinding{.peer_id = 40u, .player_index = 0xffu, .team = 0});

    const MachineMap machines =
        og::sim::build_player_machine_map(bindings, deployed);
    // Peer 10: machine id 0 (lowest of {0,1}), deployed via player 1's tag —
    // BOTH its seats carry the bit ("this player's machine deployed").
    EXPECT_EQ(encode_player_machine(0, true), machines[0]);
    EXPECT_EQ(encode_player_machine(0, true), machines[1]);
    // Peer 20: single-seat machine 3, deployed.
    EXPECT_EQ(encode_player_machine(3, true), machines[3]);
    // Peer 30: single-seat machine 5, 0-deploy.
    EXPECT_EQ(encode_player_machine(5, false), machines[5]);
    // Everyone else (incl. the ignored unbound binding): no player.
    EXPECT_EQ(kPlayerMachineNone, machines[2]);
    EXPECT_EQ(kPlayerMachineNone, machines[4]);
    for (std::size_t index = 6; index < machines.size(); ++index)
        EXPECT_EQ(kPlayerMachineNone, machines[index]) << index;

    // install_control_policy = derive + build + stamp in one call.
    ControlPolicyFixture fx;
    og::sim::install_control_policy(fx.world(), /*networked=*/true,
                                    /*cross_control=*/false, bindings, roster);
    EXPECT_EQ(kControlPolicyOwnerLocked, fx.world().control_policy);
    EXPECT_EQ(machines, fx.world().player_machine);

    // Cross-control ON keeps the legacy policy; the map is still stamped
    // (legacy claims ignore it).
    og::sim::install_control_policy(fx.world(), /*networked=*/true,
                                    /*cross_control=*/true, bindings, roster);
    EXPECT_EQ(kControlPolicyLegacy, fx.world().control_policy);
    EXPECT_EQ(machines, fx.world().player_machine);

    // Non-networked derivation stays legacy (the production local installs
    // never even call this).
    og::sim::install_control_policy(fx.world(), /*networked=*/false,
                                    /*cross_control=*/false, bindings, roster);
    EXPECT_EQ(kControlPolicyLegacy, fx.world().control_policy);
}

} // namespace
