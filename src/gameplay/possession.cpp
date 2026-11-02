/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// New Specials possession and the REASSEMBLE ward (see possession.h).
//
// The states, and what moves a walker between them:
//
//   free      possess_link 0 on both walkers.
//   linked    the rider (a ghost) is HIDDEN and links to the host; the host
//             links back and carries the countdown in possess_ticks (0 =
//             permanent). The host fights on the rider's team; its own team
//             waits on the rider's real_team_num. When a seat drove the
//             rider, the seat's tag (walker::user) now sits on the host.
//             The host is never hidden: walker::set_hidden refuses to hide
//             a walker whose partner is hidden, so a possessed skeleton
//             cannot DIG IN.
//
//   free -> linked     possess() (the POSSESS cast).
//   linked -> free     release_possession(), reached from: the countdown
//                      reaching 0 (possession_host_tick), Switch Character
//                      on the host (sim_input_handler.cpp), the host dying
//                      (kit_on_death: the rider takes half the overkill),
//                      the rider dying (kit_on_death), or the rider
//                      vanishing from the world (possession_host_tick).
//
// A level that ends with a possession running drops the host like any other
// walker and saves the rider's guy record like any other hero's. A snapshot
// carries all three fields, so a snapshot-seeded world resumes the
// possession exactly where it was.
//
// Nothing here draws a random number; every branch is on a field that only
// a New Specials kit writes, so a world without the kits never enters one.
#include <openglad/gameplay/possession.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <format>
#include <string>
#include <string_view>

namespace og::sim {

namespace {

// The rider's team waits here while it rides; 255 means "nothing kept".
constexpr unsigned char kNoTeam = 255;

std::string name_of(const walker& w)
{
    const FamilyDescriptor* fd = get_family_descriptor(w.family());
    // plumbing: display-name fallback 'SOMEONE' for notification text
    const std::string_view fallback =
        (fd != nullptr && fd->name != nullptr) ? fd->name : "SOMEONE";
    return std::string(entity_display_name(&w, fallback));
}

void notify(const std::string& message)
{
    // plumbing: no gameplay context: notification dropped
    if (current_game != nullptr)
        emit_notification(current_game->sim_events, message);
}

bool is_living(const walker& w)
{
    return w.query_order() == Order::Living && w.stats() != nullptr;
}

bool is_undead(const walker& w)
{
    const FamilyDescriptor* fd = get_family_descriptor(w.family());
    return fd != nullptr && fd->is_undead;
}

// A ghost-scare burst with no owner: the classic scare frights foes only on
// behalf of a live owner, so an ownerless one is a picture and nothing else.
// The loader stamps every effect ANI_WALK, and effect::act kills an effect
// on that row the first time it acts; the scare row (ANI_SCARE, the ghost's
// own cast sets it) is what plays the expanding burst.
void scare_burst_at(GameWorld& world, walker& where)
{
    walker* fx = world.add_ob(Order::FX, FAMILY_GHOST_SCARE);
    // plumbing: add_ob refused (world full): no exit burst
    if (fx == nullptr)
        return;
    fx->set_ani_type(ANI_SCARE);
    fx->set_cycle(0);
    fx->set_team_num(where.team_num());
    fx->set_floor(where.floor());
    fx->center_on(&where);
}

}  // namespace

bool seat_driven(const walker& w) noexcept
{
    return w.user() != -1 || w.act_type() == ACT_CONTROL;
}

// The caller's world must be the current gameplay context: the rider's hide
// and its move (walker::set_hidden, walker::setxy) act on the collision
// table of the world that is current, as every dormant-walker verb does.
PossessResult possess(GameWorld& world, walker& rider, walker& host,
                      std::int16_t ticks)
{
    // rule: POSSESS needs a free rider: a visible, awake, living walker that is not already riding,
    //   not charmed, and not the host itself; otherwise 'CANNOT POSSESS' and nothing changes.
    // tests: KitPossession.undead_seat_and_linked_hosts_refuse
    // tests: KitPossession.possess_moves_the_seat_tag_hides_the_rider_and_flips_the_host
    // tests: SemPossession.a_dead_hidden_or_dormant_rider_cannot_possess
    if (&rider == &host || !is_living(rider) || !is_living(host))
        return {false, "CANNOT POSSESS"};
    // rule: Part of the rule above that begins "POSSESS needs a free rider: a visible, awake,
    //   living ..."
    // tests: KitPossession.undead_seat_and_linked_hosts_refuse
    // tests: KitPossession.possess_moves_the_seat_tag_hides_the_rider_and_flips_the_host
    // tests: SemPossession.a_dead_hidden_or_dormant_rider_cannot_possess
    if (rider.dead() || rider.hidden() || rider.dormant() ||
        rider.possess_link() != 0)
        return {false, "CANNOT POSSESS"};
    // A charmed rider's real_team_num holds its true team for the charm's
    // expiry (living::act); the possession would overwrite that record.
    // rule: Part of the rule above that begins "POSSESS needs a free rider: a visible, awake,
    //   living ..."
    // tests: KitPossession.undead_seat_and_linked_hosts_refuse
    // tests: KitPossession.possess_moves_the_seat_tag_hides_the_rider_and_flips_the_host
    // tests: SemPossession.a_dead_hidden_or_dormant_rider_cannot_possess
    if (rider.real_team_num() != kNoTeam)
        return {false, "CANNOT POSSESS"};
    // rule: A dead, hidden or dormant body cannot be possessed ('NO HOST IN REACH').
    // tests: KitPossession.undead_seat_and_linked_hosts_refuse
    // tests: SemPossession.a_dormant_body_is_no_host_in_reach
    if (host.dead() || host.hidden() || host.dormant())
        return {false, "NO HOST IN REACH"};
    // rule: A body already possessed cannot be taken by a second ghost ('ALREADY POSSESSED').
    // tests: KitPossession.undead_seat_and_linked_hosts_refuse
    // tests: KitPossession.two_ghosts_reach_for_one_host_in_one_tick
    if (host.possess_link() != 0)
        return {false, "ALREADY POSSESSED"};
    // rule: Undead bodies resist possession ('UNDEAD RESIST').
    // tests: KitPossession.undead_seat_and_linked_hosts_refuse
    if (is_undead(host))
        return {false, "UNDEAD RESIST"};
    // A body a seat drives is a hero; heroes are never taken.
    // rule: A body a seat drives is a hero and is never possessed ('HERO RESISTS').
    // tests: KitPossession.undead_seat_and_linked_hosts_refuse
    if (seat_driven(host))
        return {false, "HERO RESISTS"};
    // A charmed body's own team already waits on its real_team_num.
    // rule: A charmed body cannot be possessed ('HOST IS CHARMED'): its true team already waits on
    //   its real_team_num.
    // tests: KitPossession.undead_seat_and_linked_hosts_refuse
    if (host.real_team_num() != kNoTeam)
        return {false, "HOST IS CHARMED"};

    rider.set_possess_link(host.entity_id());
    host.set_possess_link(rider.entity_id());
    host.set_possess_ticks(ticks);

    // A plain conversion: the host's own team waits on the rider, the host
    // fights for the rider's side. charm_left is not touched.
    rider.set_real_team_num(host.team_num());
    host.set_team_num(rider.team_num());
    host.set_leader(nullptr);

    rider.set_hidden(true);
    rider.center_on(&host);
    rider.set_floor(host.floor());

    // The seat's tag MOVES to the host, never copies: two walkers wearing
    // one tag would race every scan that looks a seat's body up by its tag.
    // rule: When a player's ghost possesses, the player's seat tag MOVES to the host (host
    //   ACT_CONTROL, rider untagged); a bot ghost moves no tag. The host fights for the rider's
    //   team, the rider hides at its centre.
    // tests: KitPossession.possess_moves_the_seat_tag_hides_the_rider_and_flips_the_host
    if (rider.user() != -1)
    {
        host.set_user(rider.user());
        host.set_act_type(ACT_CONTROL);
        host.stats()->clear_command_for_control_switch();
        rider.set_user(-1);
        rider.restore_act_type();
    }

    notify(std::format("{} possesses {}!", name_of(rider), name_of(host)));
    (void)world;  // the hide and the move read the current context (above)
    return {true, ""};
}

walker* release_possession(GameWorld& world, walker& either_side, int overkill)
{
    // rule: Releasing a walker that is not possessed or riding does nothing.
    // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
    // tests: SemPossession.releasing_an_unlinked_walker_changes_nothing
    if (either_side.possess_link() == 0)
        return nullptr;

    // The rider is the hidden side; the partner is looked up by id, so a
    // partner that has left the world reads as null. Only the rider can be
    // the hidden side: walker::set_hidden refuses to hide a walker whose
    // partner is hidden, so a host never hides while its rider rides.
    walker* const partner = world.find_by_id(either_side.possess_link());
    const bool from_rider = either_side.hidden();
    // rule: When a possession ends the host goes home to its own team; if the host was charmed
    //   while possessed, its true team is written into the charm's record and the charm runs on
    //   (its expiry brings the host home). The hidden side is always the rider.
    // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
    // tests: KitPossession.thief_charm_on_a_possessed_host_does_not_corrupt_release
    // tests: KitPossession.charmed_host_released_by_the_countdown
    // tests: KitPossession.hidden_rider_death_releases_the_host
    walker* const rider = from_rider ? &either_side : partner;
    // rule: Part of the rule above that begins "When a possession ends the host goes home to ..."
    // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
    // tests: KitPossession.thief_charm_on_a_possessed_host_does_not_corrupt_release
    // tests: KitPossession.charmed_host_released_by_the_countdown
    // tests: KitPossession.hidden_rider_death_releases_the_host
    walker* const host = from_rider ? partner : &either_side;

    // rule: Part of the rule above that begins "When a possession ends the host goes home to ..."
    // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
    // tests: KitPossession.thief_charm_on_a_possessed_host_does_not_corrupt_release
    // tests: KitPossession.charmed_host_released_by_the_countdown
    // tests: KitPossession.hidden_rider_death_releases_the_host
    if (host != nullptr)
    {
        // rule: Part of the rule above that begins "When a possession ends the host goes home to
        //   ..."
        // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
        // tests: KitPossession.thief_charm_on_a_possessed_host_does_not_corrupt_release
        // tests: KitPossession.charmed_host_released_by_the_countdown
        // tests: KitPossession.hidden_rider_death_releases_the_host
        const unsigned char original =
            rider != nullptr ? rider->real_team_num() : kNoTeam;
        // rule: Part of the rule above that begins "When a possession ends the host goes home to
        //   ..."
        // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
        // tests: KitPossession.thief_charm_on_a_possessed_host_does_not_corrupt_release
        // tests: KitPossession.charmed_host_released_by_the_countdown
        // tests: KitPossession.hidden_rider_death_releases_the_host
        if (original != kNoTeam)
        {
            // Charmed while possessed: the thief's charm recorded the
            // rider's team as the host's real team. Put the true team there
            // and leave the charmer's team on, so the charm's own expiry
            // (living::act) brings the host home.
            // rule: Part of the rule above that begins "When a possession ends the host goes home
            //   to ..."
            // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
            // tests: KitPossession.thief_charm_on_a_possessed_host_does_not_corrupt_release
            // tests: KitPossession.charmed_host_released_by_the_countdown
            // tests: KitPossession.hidden_rider_death_releases_the_host
            if (host->real_team_num() != kNoTeam)
                host->set_real_team_num(original);
            else
                host->set_team_num(original);
        }
        host->set_possess_link(0);
        host->set_possess_ticks(0);
    }

    // rule: If the rider has left the world, releasing the host only unlinks it (no reveal, no
    //   burst).
    // tests: SemPossession.a_release_with_the_rider_gone_only_unlinks_the_host
    if (rider == nullptr)
        return nullptr;

    // rule: On release the ghost reappears at the host's centre and floor, and a seat driving the
    //   host goes back to the ghost.
    // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
    // tests: SemPossession.a_released_rider_takes_the_hosts_floor_and_a_bot_moves_no_tag
    if (host != nullptr)
    {
        // Still hidden here, so the move leaves the collision table alone;
        // the reveal below registers the rider at the host's spot.
        rider->center_on(host);
        rider->set_floor(host->floor());
        // rule: Part of the rule above that begins "On release the ghost reappears at the host's
        //   centre ..."
        // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
        // tests: SemPossession.a_released_rider_takes_the_hosts_floor_and_a_bot_moves_no_tag
        if (host->user() != -1)
        {
            rider->set_user(host->user());
            rider->set_act_type(ACT_CONTROL);
            host->set_user(-1);
            host->restore_act_type();
        }
    }
    rider->set_real_team_num(kNoTeam);
    rider->set_possess_link(0);
    rider->set_hidden(false);
    scare_burst_at(world, *rider);

    // rule: A ghost thrown out of a dying body announces '<name> is cast out'.
    // tests: KitPossession.host_death_releases_with_half_overkill
    // tests: SemPossession.only_a_dying_host_casts_its_rider_out_aloud
    if (host != nullptr && host->dead())
        notify(std::format("{} is cast out", name_of(*rider)));

    // rule: A ghost cast out of a killed body takes half the killing blow's overkill, and dies if
    //   that empties it.
    // tests: KitPossession.host_death_releases_with_half_overkill
    if (overkill > 0 && !rider->dead())
    {
        rider->stats()->set_hitpoints(rider->stats()->hitpoints() -
                                      static_cast<float>(overkill) / 2.0f);
        // rule: Part of the rule above that begins "A ghost cast out of a killed body takes ..."
        // tests: KitPossession.host_death_releases_with_half_overkill
        if (rider->stats()->hitpoints() <= 0.0f)
        {
            rider->set_dead(1);
            rider->death();
        }
    }
    return rider;
}

void possession_host_tick(GameWorld& world, living& host)
{
    // rule: If the rider is gone or dead without its death releasing the host, the host just drops
    //   the link and keeps fighting on the team it has (the seat keeps driving it).
    // tests: KitPossession.rider_gone_from_the_world_unlinks_the_host
    // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
    // tests: SemPossession.a_host_whose_rider_is_dead_just_drops_the_link
    if (host.possess_link() == 0)
        return;
    walker* const rider = world.find_by_id(host.possess_link());
    // rule: Part of the rule above that begins "If the rider is gone or dead without its ..."
    // tests: KitPossession.rider_gone_from_the_world_unlinks_the_host
    // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
    // tests: SemPossession.a_host_whose_rider_is_dead_just_drops_the_link
    if (rider == nullptr || rider->dead())
    {
        // The rider left the world, or was marked dead without
        // walker::death running (which would have released this host).
        // release_possession is not called: it would reveal the rider, and
        // a revealed corpse that walker::death never processed would be
        // filed into the collision table with nothing to take it out again;
        // it would also play the exit burst over a corpse and hand the
        // seat's tag to a dead body. So drop the link and keep the team the
        // host has (the seat, if any, keeps driving it).
        host.set_possess_link(0);
        host.set_possess_ticks(0);
        return;
    }

    // Hidden walkers never touch the collision table, so following is free.
    rider->center_on(&host);
    rider->set_floor(host.floor());

    // rule: A timed possession counts down one per host act and releases at zero; a possession of 0
    //   ticks is permanent. Meanwhile the hidden rider follows the host's position and floor.
    // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
    // tests: KitPossession.permanent_possession_never_counts_down
    if (host.possess_ticks() > 0)
    {
        host.set_possess_ticks(
            static_cast<std::int16_t>(host.possess_ticks() - 1));
        // rule: Part of the rule above that begins "A timed possession counts down one per host act
        //   ..."
        // tests: KitPossession.host_tick_counts_down_follows_and_releases_at_zero
        // tests: KitPossession.permanent_possession_never_counts_down
        if (host.possess_ticks() == 0)
            release_possession(world, host);
    }
}

bool kit_on_death(GameWorld& world, walker& self)
{
    // rule: A skeleton under REASSEMBLE's ward does not die: the ward is used up, it stands back up
    //   at a quarter of max hp playing the teleport-in animation, its orders cleared, and the blow
    //   counts no kill; it announces '<name> reassembles!'.
    // tests: KitSkeleton.warded_skeleton_stands_back_up_at_a_quarter
    // tests: KitSkeleton.warded_death_tells_nobody_and_counts_no_kill
    // tests: KitSkeleton.warded_victim_is_not_a_kill, KitSkeleton.unwarded_death_is_final
    // tests: SemPossession.a_warded_skeleton_stands_up_on_the_teleport_in_row
    if ((self.kit_state() & KIT_WARD) != 0 && self.stats() != nullptr)
    {
        // REASSEMBLE: the death is cancelled before death() marks it, takes
        // the walker out of the collision table or drops a life gem, so
        // nothing has to be undone. attack() then counts no kill for that blow.
        self.set_kit_state(static_cast<std::uint8_t>(self.kit_state() & ~KIT_WARD));
        self.set_dead(0);
        self.stats()->set_hitpoints(self.stats()->max_hitpoints() / 4.0f);
        self.stats()->clear_command();
        self.set_ani_type(ANI_TELE_IN);
        self.set_cycle(0);
        notify(std::format("{} reassembles!", name_of(self)));
        return true;
    }

    // rule: Death ends a possession: a rider dying while it rides frees the host (no overkill
    //   passed); a host dying frees the rider, passing the overkill (negative hp) to it.
    // tests: KitPossession.hidden_rider_death_releases_the_host
    // tests: KitPossession.host_death_releases_with_half_overkill
    // tests: SemPossession.an_unlinked_death_touches_no_possession
    if (self.possess_link() == 0)
        return false;
    // rule: Part of the rule above that begins "Death ends a possession: a rider dying while it
    //   ..."
    // tests: KitPossession.hidden_rider_death_releases_the_host
    // tests: KitPossession.host_death_releases_with_half_overkill
    // tests: SemPossession.an_unlinked_death_touches_no_possession
    if (self.hidden())
    {
        // A rider dying while it rides (nothing here can hurt a hidden
        // walker, but a script can still kill one): the host goes free and
        // the rider dies where the host stands.
        release_possession(world, self);
        return false;
    }
    // rule: Part of the rule above that begins "Death ends a possession: a rider dying while it
    //   ..."
    // tests: KitPossession.hidden_rider_death_releases_the_host
    // tests: KitPossession.host_death_releases_with_half_overkill
    // tests: SemPossession.an_unlinked_death_touches_no_possession
    const float hp = self.stats() != nullptr ? self.stats()->hitpoints() : 0.0f;
    // rule: Part of the rule above that begins "Death ends a possession: a rider dying while it
    //   ..."
    // tests: KitPossession.hidden_rider_death_releases_the_host
    // tests: KitPossession.host_death_releases_with_half_overkill
    // tests: SemPossession.an_unlinked_death_touches_no_possession
    const int overkill = hp < 0.0f ? static_cast<int>(-hp) : 0;
    release_possession(world, self, overkill);
    return false;
}

void possession_seat_redirect(GameWorld& world, walker*& control,
                              short player_num)
{
    // plumbing: null seat walker
    if (control == nullptr)
        return;

    // The seat's hero rides a body: the seat drives the body.
    // rule: A seat whose hero rides a body drives that body, provided the body is alive and wears
    //   the seat's tag.
    // tests: KitPossession.seat_redirect_follows_host_then_returns
    // tests: SemPossession.the_seat_drives_a_ridden_body_only_if_alive_and_tagged_for_it
    if (control->hidden() && control->possess_link() != 0)
    {
        walker* const host = world.find_by_id(control->possess_link());
        // rule: Part of the rule above that begins "A seat whose hero rides a body drives that ..."
        // tests: KitPossession.seat_redirect_follows_host_then_returns
        // tests: SemPossession.the_seat_drives_a_ridden_body_only_if_alive_and_tagged_for_it
        if (host != nullptr && !host->dead() && host->user() == player_num)
            control = host;
        return;
    }

    // The body was let go (timer, Switch Character, its death): release
    // moved the seat's tag back to the hero, so the seat follows the tag.
    // Only a body that wears no tag at all is a candidate, and only in a
    // world with the setting on, so no classic path reaches the scan. A hero
    // that died earlier keeps its tag and stays in the world, so the scan
    // takes only a living body.
    // rule: With New Specials on, after a release moved the seat's tag back to the hero, a seat
    //   still pointing at the untagged body follows its tag to the living hero (skipping an earlier
    //   dead hero that keeps a stale tag); classic worlds never run this scan.
    // tests: KitPossession.seat_follows_the_ghost_out_of_a_dying_host
    // tests: KitPossession.seat_redirect_passes_an_earlier_hero_corpse
    // tests: SemPossession.a_seat_on_a_still_tagged_body_keeps_it
    if (world.new_specials == 0 || control->user() != -1 ||
        control->query_order() != Order::Living)
        return;
    // rule: Part of the rule above that begins "With New Specials on, after a release moved the
    //   ..."
    // tests: KitPossession.seat_follows_the_ghost_out_of_a_dying_host
    // tests: KitPossession.seat_redirect_passes_an_earlier_hero_corpse
    // tests: SemPossession.a_seat_on_a_still_tagged_body_keeps_it
    for (auto& uptr : world.oblist)
    {
        walker* const w = uptr.get();
        // rule: Part of the rule above that begins "With New Specials on, after a release moved the
        //   ..."
        // tests: KitPossession.seat_follows_the_ghost_out_of_a_dying_host
        // tests: KitPossession.seat_redirect_passes_an_earlier_hero_corpse
        // tests: SemPossession.a_seat_on_a_still_tagged_body_keeps_it
        if (w != nullptr && w != control && !w->dead() &&
            w->query_order() == Order::Living && w->user() == player_num)
        {
            control = w;
            return;
        }
    }
}

}  // namespace og::sim
