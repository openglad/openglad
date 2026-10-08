/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// New Specials: possession (og::sim::possess, release_possession, the
// host's countdown, the seat redirect and the death hand-offs) and the
// REASSEMBLE ward.
//
// The possession state machine, and the test that pins each transition:
//
//   free -> linked      possess()                  possess_moves_the_seat_tag_...
//   refused             possess() refusals         undead_seat_and_linked_hosts_refuse
//   linked: each tick   the host tick follows      host_tick_counts_down_follows_...
//   linked -> free      countdown reaches 0        host_tick_counts_down_follows_...
//   linked (permanent)  ticks 0 never counts down  permanent_possession_never_counts_down
//   linked -> free      the host dies              host_death_releases_with_half_overkill
//   linked -> free      the rider dies             hidden_rider_death_releases_the_host
//   linked -> free      the rider leaves the world rider_gone_from_the_world_unlinks_the_host
//   linked -> free      Switch Character           seat_redirect_follows_host_then_returns
//   seat follows        each release, every path   seat_redirect_follows_host_then_returns,
//                                                  seat_follows_the_ghost_out_of_a_dying_host,
//                                                  seat_redirect_passes_an_earlier_hero_corpse
//   linked + charm      a thief charms the host    thief_charm_on_a_possessed_host_...
//   seat redirect tick  consumes that tick's input possess_redirect_tick_consumes_...
//   parked seat         redirect is broadcast      disconnected_seat_path_broadcasts_...
//   claim scans         refuse both linked walkers claim_scans_refuse_linked_walkers
//                       and a hidden unlinked one  claim_scans_refuse_a_hidden_unlinked_walker
//   linked: body hides  turned away (DIG IN)       a_possessed_body_never_hides
//   snapshot seed       see test_world_snapshot.cpp (possess_link_survives_...,
//                       snapshot_seeded_world_resumes_a_possession_and_keeps_a_ward)
//   level end           the rider is a hero like any other (no code: the host is
//                       dropped with the level, the rider's guy record is saved)

#include <gtest/gtest.h>

#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/possession.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr unsigned char kGhostTeam = 0;
constexpr unsigned char kOrcTeam = 1;

walker* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->setxy(x, y);
    w->stats()->set_max_hitpoints(100.0f);
    w->stats()->set_hitpoints(100.0f);
    return w;
}

void seat(walker& w, short player)
{
    w.set_user(static_cast<signed char>(player));
    w.set_act_type(ACT_CONTROL);
}

bool in_obmap(TestGameWorld& tw, walker* w)
{
    return tw.world().myobmap->walker_to_pos.count(w) != 0;
}

bool notified(const TestGameWorld& tw, const std::string& text)
{
    const auto& events = tw.events.events();
    return std::any_of(events.begin(), events.end(), [&text](const auto& e) {
        return e.kind == og::sim::EventKind::Notification && e.text == text;
    });
}

int count_family(const TestGameWorld& tw, Order order, int family)
{
    int n = 0;
    for (const auto& uptr : tw.world().oblist)
        if (uptr && !uptr->dead() && uptr->query_order() == order &&
            uptr->family() == family)
            ++n;
    return n;
}

walker* find_family(const TestGameWorld& tw, Order order, int family)
{
    for (const auto& uptr : tw.world().oblist)
        if (uptr && !uptr->dead() && uptr->query_order() == order &&
            uptr->family() == family)
            return uptr.get();
    return nullptr;
}

// The world, a ghost on team 0 and an orc on team 1 beside it.
struct PossessionScene {
    TestGameWorld tw;
    walker* ghost = nullptr;
    walker* orc = nullptr;

    PossessionScene()
    {
        tw.world().new_specials = 1;
        ghost = add_living(tw, FAMILY_GHOST, kGhostTeam, 64, 64);
        orc = add_living(tw, FAMILY_ORC, kOrcTeam, 96, 80);
    }
};

}  // namespace

// free -> linked. The seat's tag moves from the ghost to the orc (never
// copied), the ghost is hidden at the orc's centre and out of the collision
// table, the orc fights for the ghost's team and its own team waits on the
// ghost. A bot ghost moves no tag.
TEST(KitPossession, possess_moves_the_seat_tag_hides_the_rider_and_flips_the_host)
{
    PossessionScene s;
    seat(*s.ghost, 0);
    ASSERT_TRUE(in_obmap(s.tw, s.ghost));

    const og::sim::PossessResult r =
        og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 100);
    ASSERT_TRUE(r.ok) << r.reason;

    EXPECT_EQ(s.orc->entity_id(), s.ghost->possess_link());
    EXPECT_EQ(s.ghost->entity_id(), s.orc->possess_link());
    EXPECT_EQ(100, s.orc->possess_ticks());
    EXPECT_EQ(0, s.ghost->possess_ticks());
    EXPECT_TRUE(s.ghost->hidden());
    EXPECT_FALSE(in_obmap(s.tw, s.ghost)) << "the rider leaves the obmap";
    EXPECT_TRUE(in_obmap(s.tw, s.orc));
    EXPECT_EQ(kGhostTeam, s.orc->team_num()) << "the host fights for the ghost";
    EXPECT_EQ(kOrcTeam, s.ghost->real_team_num())
        << "the host's own team waits on the rider";
    EXPECT_EQ(255, s.orc->real_team_num()) << "the host is not charmed";
    EXPECT_EQ(0, s.orc->charm_left());
    EXPECT_EQ(0, s.orc->user()) << "the seat's tag moved to the host";
    EXPECT_EQ(ACT_CONTROL, s.orc->act_type());
    EXPECT_EQ(-1, s.ghost->user()) << "and left the ghost: one tag, one body";
    EXPECT_NE(ACT_CONTROL, s.ghost->act_type());
    EXPECT_EQ(s.orc->xpos() + s.orc->sizex() / 2,
              s.ghost->xpos() + s.ghost->sizex() / 2);
    EXPECT_TRUE(notified(s.tw, "GHOST possesses ORC!"));

    // A bot ghost: nothing to move.
    PossessionScene bot;
    ASSERT_TRUE(og::sim::possess(bot.tw.world(), *bot.ghost, *bot.orc, 50).ok);
    EXPECT_EQ(-1, bot.orc->user());
    EXPECT_NE(ACT_CONTROL, bot.orc->act_type());
    EXPECT_EQ(kGhostTeam, bot.orc->team_num());
}

// Each refusal writes nothing.
TEST(KitPossession, undead_seat_and_linked_hosts_refuse)
{
    PossessionScene s;
    walker* skeleton = add_living(s.tw, FAMILY_SKELETON, kOrcTeam, 128, 64);
    walker* hero = add_living(s.tw, FAMILY_SOLDIER, kOrcTeam, 160, 64);
    walker* driven = add_living(s.tw, FAMILY_SOLDIER, kOrcTeam, 160, 96);
    walker* charmed = add_living(s.tw, FAMILY_ORC, kOrcTeam, 192, 64);
    walker* knife = s.tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(skeleton && hero && driven && charmed && knife);
    hero->set_user(1);
    driven->set_act_type(ACT_CONTROL);
    charmed->set_real_team_num(kGhostTeam);

    const auto refuse = [&s](walker& rider, walker& host, const char* why) {
        const unsigned char host_team = host.team_num();
        const og::sim::PossessResult r =
            og::sim::possess(s.tw.world(), rider, host, 100);
        EXPECT_FALSE(r.ok) << why;
        EXPECT_STREQ(why, r.reason);
        EXPECT_LE(std::string(r.reason).size(), 24u);
        EXPECT_EQ(0u, rider.possess_link()) << why;
        EXPECT_EQ(0u, host.possess_link()) << why;
        EXPECT_FALSE(rider.hidden()) << why;
        EXPECT_EQ(host_team, host.team_num()) << why;
    };
    refuse(*s.ghost, *skeleton, "UNDEAD RESIST");
    refuse(*s.ghost, *hero, "HERO RESISTS");
    refuse(*s.ghost, *driven, "HERO RESISTS");
    refuse(*s.ghost, *charmed, "HOST IS CHARMED");
    refuse(*s.ghost, *s.ghost, "CANNOT POSSESS");
    refuse(*s.ghost, *knife, "CANNOT POSSESS");
    refuse(*knife, *s.orc, "CANNOT POSSESS");

    // A charmed rider: its real_team_num is the charm's record of its true
    // team, which a possession would overwrite (and the release erase), so
    // the charm's expiry could never bring it home.
    {
        constexpr unsigned char kThiefTeam = 2;
        walker* charmed_ghost = add_living(s.tw, FAMILY_GHOST, kThiefTeam, 32, 96);
        ASSERT_NE(nullptr, charmed_ghost);
        charmed_ghost->set_real_team_num(kGhostTeam);
        charmed_ghost->set_charm_left(3);
        refuse(*charmed_ghost, *s.orc, "CANNOT POSSESS");
        EXPECT_EQ(kGhostTeam, charmed_ghost->real_team_num())
            << "the charm's record of the true team is untouched";
        EXPECT_EQ(kThiefTeam, charmed_ghost->team_num());
        for (int i = 0; i < 4 && charmed_ghost->charm_left() != 0; ++i)
            charmed_ghost->act();
        EXPECT_EQ(0, charmed_ghost->charm_left());
        EXPECT_EQ(kGhostTeam, charmed_ghost->team_num())
            << "the charm ran out: the ghost is home";
    }

    s.orc->set_hidden(true);
    refuse(*s.ghost, *s.orc, "NO HOST IN REACH");
    s.orc->set_hidden(false);
    s.orc->set_dead(1);
    refuse(*s.ghost, *s.orc, "NO HOST IN REACH");
    s.orc->set_dead(0);

    // A linked host, and a rider that already rides.
    walker* second_ghost = add_living(s.tw, FAMILY_GHOST, kGhostTeam, 32, 32);
    ASSERT_NE(nullptr, second_ghost);
    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 100).ok);
    {
        const og::sim::PossessResult r =
            og::sim::possess(s.tw.world(), *second_ghost, *s.orc, 100);
        EXPECT_FALSE(r.ok);
        EXPECT_STREQ("ALREADY POSSESSED", r.reason);
        EXPECT_EQ(0u, second_ghost->possess_link());
        EXPECT_EQ(s.ghost->entity_id(), s.orc->possess_link());
    }
    {
        walker* other_orc = add_living(s.tw, FAMILY_ORC, kOrcTeam, 224, 64);
        ASSERT_NE(nullptr, other_orc);
        const og::sim::PossessResult r =
            og::sim::possess(s.tw.world(), *s.ghost, *other_orc, 100);
        EXPECT_FALSE(r.ok);
        EXPECT_STREQ("CANNOT POSSESS", r.reason);
        EXPECT_EQ(0u, other_orc->possess_link());
        EXPECT_EQ(kOrcTeam, other_orc->team_num());
    }
}

// linked, each tick: the rider follows the host (floor too) and the
// countdown runs; at 0 the possession ends, the ghost reappears where the
// host stands, the host goes home, the tag goes back. The countdown is wired
// into living::act: one act() is one tick off.
TEST(KitPossession, host_tick_counts_down_follows_and_releases_at_zero)
{
    PossessionScene s;
    seat(*s.ghost, 0);
    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 3).ok);
    auto* host = dynamic_cast<living*>(s.orc);
    ASSERT_NE(nullptr, host);

    s.orc->setxy(200, 160);
    s.orc->set_floor(1);
    og::sim::possession_host_tick(s.tw.world(), *host);
    EXPECT_EQ(2, s.orc->possess_ticks());
    EXPECT_EQ(s.orc->xpos() + s.orc->sizex() / 2,
              s.ghost->xpos() + s.ghost->sizex() / 2) << "the rider follows";
    EXPECT_EQ(1, s.ghost->floor()) << "floor too";
    EXPECT_FALSE(in_obmap(s.tw, s.ghost)) << "and stays out of the obmap";
    s.orc->set_floor(0);

    s.orc->act();
    EXPECT_EQ(1, s.orc->possess_ticks()) << "living::act runs the host tick";
    ASSERT_NE(0u, s.orc->possess_link());

    og::sim::possession_host_tick(s.tw.world(), *host);
    EXPECT_EQ(0u, s.orc->possess_link()) << "released at zero";
    EXPECT_EQ(0u, s.ghost->possess_link());
    EXPECT_EQ(0, s.orc->possess_ticks());
    EXPECT_FALSE(s.ghost->hidden());
    EXPECT_TRUE(in_obmap(s.tw, s.ghost)) << "back in the obmap";
    EXPECT_EQ(kOrcTeam, s.orc->team_num()) << "the host goes home";
    EXPECT_EQ(255, s.ghost->real_team_num());
    EXPECT_EQ(255, s.orc->real_team_num());
    EXPECT_EQ(0, s.ghost->user()) << "the tag is back on the ghost";
    EXPECT_EQ(ACT_CONTROL, s.ghost->act_type());
    EXPECT_EQ(-1, s.orc->user());
    EXPECT_NE(ACT_CONTROL, s.orc->act_type());
    EXPECT_EQ(s.orc->xpos() + s.orc->sizex() / 2,
              s.ghost->xpos() + s.ghost->sizex() / 2)
        << "the ghost reappears where the host stands";
    EXPECT_EQ(1, count_family(s.tw, Order::FX, FAMILY_GHOST_SCARE))
        << "the exit shows a scare burst";

    // The burst plays: it is on the scare row, and a world tick advances it
    // instead of killing it (an effect left on the walk row dies the first
    // time it acts, and nothing would ever be drawn).
    walker* burst = find_family(s.tw, Order::FX, FAMILY_GHOST_SCARE);
    ASSERT_NE(nullptr, burst);
    EXPECT_EQ(ANI_SCARE, burst->ani_type()) << "the burst is on the scare row";
    EXPECT_EQ(0, burst->cycle());
    s.tw.world().tick();
    EXPECT_FALSE(burst->dead()) << "the burst survives its first act";
    EXPECT_EQ(1, count_family(s.tw, Order::FX, FAMILY_GHOST_SCARE));
    EXPECT_EQ(ANI_SCARE, burst->ani_type()) << "and is still on the scare row";
    EXPECT_GT(burst->cycle(), 0) << "one frame of the burst has played";

    // A tick on a free walker does nothing at all.
    og::sim::possession_host_tick(s.tw.world(), *host);
    EXPECT_EQ(0u, s.orc->possess_link());
    EXPECT_EQ(nullptr, og::sim::release_possession(s.tw.world(), *s.orc))
        << "releasing an unlinked walker is a no-op";
}

TEST(KitPossession, permanent_possession_never_counts_down)
{
    PossessionScene s;
    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 0).ok);
    auto* host = dynamic_cast<living*>(s.orc);
    ASSERT_NE(nullptr, host);
    for (int i = 0; i < 500; ++i)
        og::sim::possession_host_tick(s.tw.world(), *host);
    EXPECT_EQ(s.ghost->entity_id(), s.orc->possess_link());
    EXPECT_EQ(0, s.orc->possess_ticks());
    EXPECT_TRUE(s.ghost->hidden());
    EXPECT_EQ(kGhostTeam, s.orc->team_num());
}

// linked -> free on the host's death, from walker::death itself: the ghost
// takes half the overkill, reappears, and the host dies on its own team.
// When the overkill is more than the ghost has, the ghost dies too.
TEST(KitPossession, host_death_releases_with_half_overkill)
{
    PossessionScene s;
    seat(*s.ghost, 0);
    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 100).ok);
    s.ghost->stats()->set_hitpoints(40.0f);
    s.orc->stats()->set_hitpoints(-10.0f);
    s.orc->set_dead(1);
    s.orc->death();

    EXPECT_EQ(1, s.orc->death_called()) << "the host still dies";
    EXPECT_EQ(kOrcTeam, s.orc->team_num()) << "on its own team";
    EXPECT_EQ(-1, s.orc->user());
    EXPECT_EQ(0u, s.orc->possess_link());
    EXPECT_EQ(0u, s.ghost->possess_link());
    EXPECT_FALSE(s.ghost->hidden());
    EXPECT_FALSE(s.ghost->dead());
    EXPECT_FLOAT_EQ(35.0f, s.ghost->stats()->hitpoints())
        << "half of the 10 overkill";
    EXPECT_EQ(0, s.ghost->user()) << "the seat's tag is back on the ghost";
    EXPECT_TRUE(notified(s.tw, "GHOST is cast out"));

    PossessionScene fatal;
    ASSERT_TRUE(og::sim::possess(fatal.tw.world(), *fatal.ghost, *fatal.orc, 100).ok);
    fatal.ghost->stats()->set_hitpoints(4.0f);
    fatal.orc->stats()->set_hitpoints(-20.0f);
    fatal.orc->set_dead(1);
    fatal.orc->death();
    EXPECT_TRUE(fatal.ghost->dead()) << "10 damage kills a 4 hp ghost";
    EXPECT_EQ(1, fatal.ghost->death_called());
    EXPECT_FALSE(fatal.ghost->hidden());

    // A host that dies with no overkill (hp still positive) costs nothing.
    PossessionScene clean;
    ASSERT_TRUE(og::sim::possess(clean.tw.world(), *clean.ghost, *clean.orc, 100).ok);
    clean.ghost->stats()->set_hitpoints(40.0f);
    clean.orc->set_dead(1);
    clean.orc->death();
    EXPECT_FLOAT_EQ(40.0f, clean.ghost->stats()->hitpoints());
}

// linked -> free on the rider's death: the host goes free and home.
TEST(KitPossession, hidden_rider_death_releases_the_host)
{
    PossessionScene s;
    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 100).ok);
    s.ghost->set_dead(1);
    s.ghost->death();

    EXPECT_EQ(1, s.ghost->death_called());
    EXPECT_FALSE(s.ghost->hidden());
    EXPECT_FALSE(in_obmap(s.tw, s.ghost)) << "a corpse is not in the obmap";
    EXPECT_EQ(0u, s.ghost->possess_link());
    EXPECT_EQ(0u, s.orc->possess_link());
    EXPECT_EQ(0, s.orc->possess_ticks());
    EXPECT_FALSE(s.orc->dead());
    EXPECT_EQ(kOrcTeam, s.orc->team_num());
}

// linked -> free when the rider has left the world without dying through
// walker::death (removed outright): the host drops the link on its next tick
// and keeps the team it has; nothing dangles.
TEST(KitPossession, rider_gone_from_the_world_unlinks_the_host)
{
    PossessionScene s;
    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 100).ok);
    s.orc->set_possess_link(0xfffffff0u);  // an id no walker carries
    auto* host = dynamic_cast<living*>(s.orc);
    ASSERT_NE(nullptr, host);
    og::sim::possession_host_tick(s.tw.world(), *host);
    EXPECT_EQ(0u, s.orc->possess_link());
    EXPECT_EQ(0, s.orc->possess_ticks());
    EXPECT_EQ(kGhostTeam, s.orc->team_num());
}

// A thief charms the possessed host (the writes living-11-thief.lua's charm
// makes): the host's TRUE team must survive the release and come back when
// the charm runs out (living::act's own charm expiry).
TEST(KitPossession, thief_charm_on_a_possessed_host_does_not_corrupt_release)
{
    PossessionScene s;
    constexpr unsigned char kThiefTeam = 2;
    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 100).ok);
    ASSERT_EQ(kGhostTeam, s.orc->team_num());

    // The charm, as the thief casts it.
    ASSERT_EQ(255, s.orc->real_team_num()) << "the charm's own gate passes";
    s.orc->set_real_team_num(s.orc->team_num());
    s.orc->set_team_num(kThiefTeam);
    s.orc->set_charm_left(3);
    EXPECT_EQ(kGhostTeam, s.orc->real_team_num());
    EXPECT_EQ(kThiefTeam, s.orc->team_num());

    ASSERT_NE(nullptr, og::sim::release_possession(s.tw.world(), *s.orc));
    EXPECT_EQ(kThiefTeam, s.orc->team_num()) << "the charm still holds";
    EXPECT_EQ(kOrcTeam, s.orc->real_team_num())
        << "and the host's true team waits for its end";

    for (int i = 0; i < 4; ++i) {
        s.orc->act();
        if (s.orc->charm_left() == 0)
            break;
    }
    EXPECT_EQ(kOrcTeam, s.orc->team_num()) << "the charm ran out: home";
    EXPECT_EQ(255, s.orc->real_team_num());
}

// The seat follows its hero into the body and back out, by every release:
// Switch Character, the countdown, and (below) the host's death. Driven
// through sim_process_player_input, the function every client's seat runs.
TEST(KitPossession, seat_redirect_follows_host_then_returns)
{
    PossessionScene s;
    walker* ally = add_living(s.tw, FAMILY_SOLDIER, kGhostTeam, 32, 160);
    ASSERT_NE(nullptr, ally);
    seat(*s.ghost, 0);
    walker* control = s.ghost;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    const PlayerInput& pi = input.players[0];
    const auto run = [&]() {
        return sim_process_player_input(pi, control, s.tw.world(), 0,
                                        kGhostTeam, debounce, &s.tw.events);
    };

    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 100).ok);
    SimInputResult r = run();
    EXPECT_EQ(s.orc, control) << "the seat drives the body";
    EXPECT_EQ(s.orc, r.new_control);
    EXPECT_TRUE(r.control_hp_changed);

    r = run();
    EXPECT_EQ(s.orc, control) << "and keeps driving it";

    // Switch Character leaves the body; the ghost is the seat's again.
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    r = run();
    EXPECT_EQ(s.ghost, control) << "Switch Character leaves the body";
    EXPECT_EQ(0u, s.orc->possess_link());
    EXPECT_FALSE(s.ghost->hidden());
    EXPECT_EQ(0, s.ghost->user());
    EXPECT_EQ(kOrcTeam, s.orc->team_num());
    // The next press cycles heroes as usual.
    input.clear();
    run();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    run();
    EXPECT_EQ(ally, control) << "the next press switches heroes";
    input.clear();

    // The countdown ends it: the seat follows the tag back to the ghost.
    walker* orc2 = add_living(s.tw, FAMILY_ORC, kOrcTeam, 128, 128);
    ASSERT_NE(nullptr, orc2);
    seat(*s.ghost, 0);
    ally->set_user(-1);
    ally->restore_act_type();
    control = s.ghost;
    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *orc2, 1).ok);
    run();
    ASSERT_EQ(orc2, control);
    og::sim::possession_host_tick(s.tw.world(), *dynamic_cast<living*>(orc2));
    ASSERT_EQ(0u, orc2->possess_link());
    r = run();
    EXPECT_EQ(s.ghost, control) << "the timer hands the seat back";
    EXPECT_EQ(s.ghost, r.new_control);
    EXPECT_EQ(-1, orc2->user()) << "the seat did not claim the freed body";
}

// The host dies under the seat: the seat follows the ghost out. (When the
// overkill kills the ghost too, the redirect passes the dead ghost, control
// stays on the tagless dead host for that tick, and the usual death path
// takes over from there.)
TEST(KitPossession, seat_follows_the_ghost_out_of_a_dying_host)
{
    PossessionScene s;
    seat(*s.ghost, 0);
    walker* control = s.ghost;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    const auto run = [&]() {
        return sim_process_player_input(input.players[0], control,
                                        s.tw.world(), 0, kGhostTeam, debounce,
                                        &s.tw.events);
    };
    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 100).ok);
    run();
    ASSERT_EQ(s.orc, control);
    s.orc->stats()->set_hitpoints(-4.0f);
    s.orc->set_dead(1);
    s.orc->death();
    run();
    EXPECT_EQ(s.ghost, control) << "out of the corpse, back to the ghost";
    EXPECT_FALSE(s.ghost->dead());

    // With the setting off no possession exists; a body that wears no tag
    // is claimed as it always was (the redirect never scans).
    TestGameWorld off;
    walker* lone = add_living(off, FAMILY_SOLDIER, kGhostTeam, 64, 64);
    walker* tagged = add_living(off, FAMILY_SOLDIER, kGhostTeam, 96, 64);
    ASSERT_TRUE(lone && tagged);
    seat(*tagged, 0);
    walker* off_control = lone;
    sim_process_player_input(input.players[0], off_control, off.world(), 0,
                             kGhostTeam, debounce, &off.events);
    EXPECT_EQ(lone, off_control) << "classic: the supplied body is claimed";
    EXPECT_EQ(0, lone->user());
}

// A hero that died earlier in the level keeps its seat tag and stays in the
// world (it has a guy record). After a release the seat must follow its tag
// back to the living ghost, not to that corpse, which sits earlier in the
// object list: a seat on the corpse would leave the ghost tagged and driven
// by no one, since the claim scans only take untagged bodies.
TEST(KitPossession, seat_redirect_passes_an_earlier_hero_corpse)
{
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* corpse = add_living(tw, FAMILY_SOLDIER, kGhostTeam, 32, 32);
    ASSERT_NE(nullptr, corpse);
    corpse->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    seat(*corpse, 0);
    corpse->stats()->set_hitpoints(-1.0f);
    corpse->set_dead(1);
    corpse->death();
    ASSERT_TRUE(corpse->dead());
    ASSERT_EQ(0, corpse->user()) << "a hero corpse keeps its tag";

    walker* ghost = add_living(tw, FAMILY_GHOST, kGhostTeam, 64, 64);
    walker* orc = add_living(tw, FAMILY_ORC, kOrcTeam, 96, 80);
    ASSERT_TRUE(ghost && orc);
    seat(*ghost, 0);
    walker* control = ghost;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    const auto run = [&]() {
        return sim_process_player_input(input.players[0], control, tw.world(),
                                        0, kGhostTeam, debounce, &tw.events);
    };

    ASSERT_TRUE(og::sim::possess(tw.world(), *ghost, *orc, 1).ok);
    run();
    ASSERT_EQ(orc, control);
    og::sim::possession_host_tick(tw.world(), *dynamic_cast<living*>(orc));
    ASSERT_EQ(0u, orc->possess_link());
    ASSERT_EQ(0, ghost->user()) << "the tag is back on the ghost";

    const SimInputResult r = run();
    EXPECT_EQ(ghost, control) << "the seat follows its tag back to the ghost"
                              << (control == corpse ? ", not the corpse" : "");
    EXPECT_EQ(ghost, r.new_control);
    EXPECT_FALSE(control->dead());
}

// The tick the seat moves into the body is spent on the move: a Special
// still held from the cast that possessed casts nothing on the host. The
// tick after, the held key is the host's (the contrast that proves the
// assertion can fail).
TEST(KitPossession, possess_redirect_tick_consumes_the_rest_of_the_input)
{
    PossessionScene s;
    s.orc->stats()->set_level(4);
    s.orc->stats()->set_max_magicpoints(200.0f);
    s.orc->stats()->set_magicpoints(200.0f);
    s.orc->set_current_special(1);
    seat(*s.ghost, 0);
    walker* control = s.ghost;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    input.players[0].held[static_cast<int>(InputAction::Special)] = true;
    input.players[0].held[static_cast<int>(InputAction::Fire)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::Fire)] = true;
    const auto run = [&]() {
        return sim_process_player_input(input.players[0], control,
                                        s.tw.world(), 0, kGhostTeam, debounce,
                                        &s.tw.events);
    };

    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 100).ok);
    run();
    ASSERT_EQ(s.orc, control);
    EXPECT_FLOAT_EQ(200.0f, s.orc->stats()->magicpoints())
        << "the held Special cast nothing on the host";
    EXPECT_EQ(ANI_WALK, s.orc->ani_type()) << "and Fire started nothing";

    input.players[0].pressed[static_cast<int>(InputAction::Fire)] = false;
    run();
    EXPECT_LT(s.orc->stats()->magicpoints(), 200.0f)
        << "the next tick the held key casts the host's special";
}

// Neither side of a possession is a free body for any seat's claim or
// switch scan, nor is the hidden rider a camera target.
TEST(KitPossession, claim_scans_refuse_linked_walkers)
{
    PossessionScene s;
    // A bot ghost on team 0 takes an orc; the orc now fights for team 0.
    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 100).ok);
    ASSERT_EQ(kGhostTeam, s.orc->team_num());

    EXPECT_EQ(nullptr, sim_find_next_control(s.tw.world(), kGhostTeam))
        << "neither the rider nor the host is a free body";
    EXPECT_EQ(nullptr, og::sim::sim_find_next_control_owned(
                           s.tw.world(), kGhostTeam, 0))
        << "the owner-aware scan agrees";

    walker* hero = add_living(s.tw, FAMILY_SOLDIER, kGhostTeam, 32, 32);
    ASSERT_NE(nullptr, hero);
    seat(*hero, 0);
    EXPECT_EQ(nullptr,
              sim_switch_control(s.tw.world(), *hero, hero, kGhostTeam, 0, false))
        << "switching never lands on either side of a possession";
    EXPECT_FALSE(og::sim::follow_target_visible(s.ghost))
        << "a hidden rider is no camera target";
    EXPECT_TRUE(og::sim::follow_target_visible(s.orc));

    // Free again: both are claimable.
    og::sim::release_possession(s.tw.world(), *s.orc);
    hero->set_user(0);
    EXPECT_EQ(s.ghost,
              sim_switch_control(s.tw.world(), *hero, hero, kGhostTeam, 0, false));
}

// A hidden walker with no link at all (a dug-in skeleton) is no free body
// for any claim pass either: the hidden test stands on its own, not on the
// link test. Each pass is reached in turn: with and without a guy record
// (the "player character" and "anyone on our team" passes), under the
// shared pool and under owner-locked seats, and the scripted owner-tag pass.
// Every row is checked visible first, so each "nullptr" can fail.
TEST(KitPossession, claim_scans_refuse_a_hidden_unlinked_walker)
{
    struct Row {
        const char* pass;
        bool with_guy;
        bool owner_locked;
        bool scripted;
    };
    const Row rows[] = {
        {"shared pool, player character", true, false, false},
        {"shared pool, anyone on the team", false, false, false},
        {"owner-locked, player character", true, true, false},
        {"owner-locked, anyone on the team", false, true, false},
        {"scripted owner tag", true, false, true},
    };
    for (const Row& row : rows)
    {
        PossessionScene s;
        GameWorld& world = s.tw.world();
        if (row.owner_locked)
        {
            std::array<std::uint8_t, og::sim::kPlayerMachineSlots> machines;
            machines.fill(og::sim::encode_player_machine(0, true));
            og::sim::set_control_policy(
                world, og::sim::kControlPolicyOwnerLocked, machines);
        }
        if (row.scripted)
            world.type = static_cast<char>(world.type | GameWorld::TYPE_SCRIPTED);
        if (row.with_guy)
        {
            auto record = std::make_unique<guy>(FAMILY_GHOST);
            record->owner_player_index = 0;
            s.ghost->set_owned_myguy(std::move(record));
        }
        walker* hero = add_living(s.tw, FAMILY_SOLDIER, kGhostTeam, 32, 32);
        ASSERT_NE(nullptr, hero);
        seat(*hero, 0);

        ASSERT_EQ(s.ghost, og::sim::sim_find_next_control_owned(world, kGhostTeam, 0))
            << row.pass << ": visible, the ghost is claimable";
        s.ghost->set_hidden(true);
        ASSERT_EQ(0u, s.ghost->possess_link());
        EXPECT_EQ(nullptr, og::sim::sim_find_next_control_owned(world, kGhostTeam, 0))
            << row.pass << ": hidden, it is not";
        if (!row.owner_locked && !row.scripted)
        {
            EXPECT_EQ(nullptr, sim_find_next_control(world, kGhostTeam))
                << row.pass << ": the shared-pool scan agrees";
            EXPECT_EQ(nullptr,
                      sim_switch_control(world, *hero, hero, kGhostTeam, 0, false))
                << row.pass << ": nor is it switched to";
            hero->set_user(0);
            s.ghost->set_hidden(false);
            EXPECT_EQ(s.ghost,
                      sim_switch_control(world, *hero, hero, kGhostTeam, 0, false))
                << row.pass << ": revealed, it is switched to";
        }
    }
}

// A possessed body never hides: hiding is how a possession tells its rider
// from its host, and a hidden body would stop acting. A possessed skeleton's
// DIG IN is the case in play. The hide is turned away, the body stays in the
// collision table, its countdown keeps running, and Switch Character still
// leaves it. Free again, the same walker hides as usual.
TEST(KitPossession, a_possessed_body_never_hides)
{
    PossessionScene s;
    seat(*s.ghost, 0);
    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 50).ok);
    ASSERT_TRUE(s.ghost->hidden()) << "the rider itself hid";

    s.orc->set_hidden(true);
    EXPECT_FALSE(s.orc->hidden()) << "the body's hide is turned away";
    EXPECT_TRUE(in_obmap(s.tw, s.orc)) << "and it stays in the obmap";

    s.orc->act();
    EXPECT_EQ(49, s.orc->possess_ticks()) << "the body still acts and counts";

    walker* control = s.ghost;
    SimInputDebounce debounce{};
    InputState input;
    input.clear();
    const auto run = [&]() {
        return sim_process_player_input(input.players[0], control,
                                        s.tw.world(), 0, kGhostTeam, debounce,
                                        &s.tw.events);
    };
    run();
    ASSERT_EQ(s.orc, control) << "the seat drives the body";
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    run();
    EXPECT_EQ(s.ghost, control) << "Switch Character leaves the body";
    EXPECT_EQ(0u, s.orc->possess_link());
    EXPECT_EQ(0u, s.ghost->possess_link());
    EXPECT_FALSE(s.ghost->hidden()) << "the ghost is back";
    EXPECT_TRUE(in_obmap(s.tw, s.ghost));
    EXPECT_EQ(kOrcTeam, s.orc->team_num()) << "the body goes home";
    EXPECT_EQ(0, s.ghost->user()) << "and the seat's tag is the ghost's";

    s.orc->set_hidden(true);
    EXPECT_TRUE(s.orc->hidden()) << "a free walker hides as usual";
    EXPECT_FALSE(in_obmap(s.tw, s.orc));
}

namespace {

// A transport that records what the server sends.
class RecordingTransport final : public og::sim::ITransport
{
public:
    void send(og::sim::PeerId peer_id, const std::uint8_t* data,
              std::size_t len) override
    {
        sent_.push_back({peer_id, {data, data + len}});
    }
    std::vector<og::sim::ReceivedMessage> poll() override
    {
        std::vector<og::sim::ReceivedMessage> result = std::move(inbound_);
        inbound_.clear();
        return result;
    }
    void accept_connections() override {}
    void disconnect(og::sim::PeerId peer_id) override
    {
        std::erase(connected_, peer_id);
    }
    std::vector<og::sim::PeerId> connected_peers() const override
    {
        return connected_;
    }
    void set_connected(std::vector<og::sim::PeerId> peers)
    {
        connected_ = std::move(peers);
    }
    void queue(og::sim::PeerId peer_id, std::vector<std::uint8_t> bytes)
    {
        inbound_.push_back({peer_id, std::move(bytes)});
    }
    void clear_sent() { sent_.clear(); }

    std::vector<og::sim::ControlChangeMessage> control_changes(
        og::sim::PeerId peer_id) const
    {
        std::vector<og::sim::ControlChangeMessage> out;
        for (const auto& sent : sent_) {
            if (sent.peer_id != peer_id)
                continue;
            og::sim::TransportEnvelope envelope;
            if (!og::sim::decode_transport_envelope(sent.data, envelope) ||
                envelope.message_type != og::sim::kControlChangeMessageType)
                continue;
            if (auto change =
                    og::sim::deserialize_control_change_message(sent.data))
                out.push_back(*change);
        }
        return out;
    }

private:
    std::vector<og::sim::PeerId> connected_;
    std::vector<og::sim::ReceivedMessage> inbound_;
    std::vector<og::sim::ReceivedMessage> sent_;
};

}  // namespace

// A seat parked by a dropped connection still plays itself out of its last
// input. When its ghost rides a body the parked seat moves into the body,
// and every connected mirror hears the ControlChange.
TEST(KitPossession, disconnected_seat_path_broadcasts_control_change)
{
    og::test::ScopedPackStoreState pack_store;
    PossessionScene s;
    RecordingTransport transport;
    og::sim::GameServer server(s.tw.world(), s.tw.events, transport);
    transport.set_connected({91u, 92u});
    server.poll_incoming_messages();
    server.connect_client(91u);
    server.connect_client(92u);

    walker* other = add_living(s.tw, FAMILY_SOLDIER, kGhostTeam, 32, 32);
    ASSERT_NE(nullptr, other);
    seat(*s.ghost, 0);
    seat(*other, 1);
    server.bind_player(91u, 0u, kGhostTeam, s.ghost);
    server.bind_player(92u, 1u, kGhostTeam, other);
    server.step();
    for (const og::sim::PeerId peer : {91u, 92u})
        transport.queue(peer, og::sim::serialize_client_ready_message(
                                  {.last_applied_tick = 0u}));
    server.step();

    // Seat 0 drops and is parked with its ghost.
    transport.set_connected({92u});
    server.poll_incoming_messages();
    ASSERT_EQ(1u, server.disconnected_players().size());
    ASSERT_EQ(s.ghost, server.disconnected_players().front().control);

    ASSERT_TRUE(og::sim::possess(s.tw.world(), *s.ghost, *s.orc, 100).ok);
    transport.clear_sent();
    const auto input_bytes =
        og::sim::serialize_input(s.tw.world().tick_count_ + 1u, InputState{});
    transport.queue(92u, std::vector<std::uint8_t>(input_bytes.begin(),
                                                   input_bytes.end()));
    server.step();

    ASSERT_EQ(s.orc, server.disconnected_players().front().control)
        << "the parked seat moved into the body";
    const auto changes = transport.control_changes(92u);
    const bool heard = std::any_of(
        changes.begin(), changes.end(),
        [&s](const og::sim::ControlChangeMessage& m) {
            return m.player_index == 0u && m.entity_id == s.orc->entity_id();
        });
    EXPECT_TRUE(heard) << "the connected mirror hears the parked seat move";
}
