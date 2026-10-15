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
    const std::string_view fallback =
        (fd != nullptr && fd->name != nullptr) ? fd->name : "SOMEONE";
    return std::string(entity_display_name(&w, fallback));
}

void notify(const std::string& message)
{
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
    if (fx == nullptr)
        return;
    fx->set_ani_type(ANI_SCARE);
    fx->set_cycle(0);
    fx->set_team_num(where.team_num());
    fx->set_floor(where.floor());
    fx->center_on(&where);
}

}  // namespace

// The caller's world must be the current gameplay context: the rider's hide
// and its move (walker::set_hidden, walker::setxy) act on the collision
// table of the world that is current, as every dormant-walker verb does.
PossessResult possess(GameWorld& world, walker& rider, walker& host,
                      std::int16_t ticks)
{
    if (&rider == &host || !is_living(rider) || !is_living(host))
        return {false, "CANNOT POSSESS"};
    if (rider.dead() || rider.hidden() || rider.dormant() ||
        rider.possess_link() != 0)
        return {false, "CANNOT POSSESS"};
    // A charmed rider's real_team_num holds its true team for the charm's
    // expiry (living::act); the possession would overwrite that record.
    if (rider.real_team_num() != kNoTeam)
        return {false, "CANNOT POSSESS"};
    if (host.dead() || host.hidden() || host.dormant())
        return {false, "NO HOST IN REACH"};
    if (host.possess_link() != 0)
        return {false, "ALREADY POSSESSED"};
    if (is_undead(host))
        return {false, "UNDEAD RESIST"};
    // A body a seat drives is a hero; heroes are never taken.
    if (host.user() != -1 || host.act_type() == ACT_CONTROL)
        return {false, "HERO RESISTS"};
    // A charmed body's own team already waits on its real_team_num.
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
    if (either_side.possess_link() == 0)
        return nullptr;

    // The rider is the hidden side; the partner is looked up by id, so a
    // partner that has left the world reads as null. Only the rider can be
    // the hidden side: walker::set_hidden refuses to hide a walker whose
    // partner is hidden, so a host never hides while its rider rides.
    walker* const partner = world.find_by_id(either_side.possess_link());
    const bool from_rider = either_side.hidden();
    walker* const rider = from_rider ? &either_side : partner;
    walker* const host = from_rider ? partner : &either_side;

    if (host != nullptr)
    {
        const unsigned char original =
            rider != nullptr ? rider->real_team_num() : kNoTeam;
        if (original != kNoTeam)
        {
            // Charmed while possessed: the thief's charm recorded the
            // rider's team as the host's real team. Put the true team there
            // and leave the charmer's team on, so the charm's own expiry
            // (living::act) brings the host home.
            if (host->real_team_num() != kNoTeam)
                host->set_real_team_num(original);
            else
                host->set_team_num(original);
        }
        host->set_possess_link(0);
        host->set_possess_ticks(0);
    }

    if (rider == nullptr)
        return nullptr;

    if (host != nullptr)
    {
        // Still hidden here, so the move leaves the collision table alone;
        // the reveal below registers the rider at the host's spot.
        rider->center_on(host);
        rider->set_floor(host->floor());
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

    if (host != nullptr && host->dead())
        notify(std::format("{} is cast out", name_of(*rider)));

    if (overkill > 0 && !rider->dead())
    {
        rider->stats()->set_hitpoints(rider->stats()->hitpoints() -
                                      static_cast<float>(overkill) / 2.0f);
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
    if (host.possess_link() == 0)
        return;
    walker* const rider = world.find_by_id(host.possess_link());
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

    if (host.possess_ticks() > 0)
    {
        host.set_possess_ticks(
            static_cast<std::int16_t>(host.possess_ticks() - 1));
        if (host.possess_ticks() == 0)
            release_possession(world, host);
    }
}

bool kit_on_death(GameWorld& world, walker& self)
{
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

    if (self.possess_link() == 0)
        return false;
    if (self.hidden())
    {
        // A rider dying while it rides (nothing here can hurt a hidden
        // walker, but a script can still kill one): the host goes free and
        // the rider dies where the host stands.
        release_possession(world, self);
        return false;
    }
    const float hp = self.stats() != nullptr ? self.stats()->hitpoints() : 0.0f;
    const int overkill = hp < 0.0f ? static_cast<int>(-hp) : 0;
    release_possession(world, self, overkill);
    return false;
}

void possession_seat_redirect(GameWorld& world, walker*& control,
                              short player_num)
{
    if (control == nullptr)
        return;

    // The seat's hero rides a body: the seat drives the body.
    if (control->hidden() && control->possess_link() != 0)
    {
        walker* const host = world.find_by_id(control->possess_link());
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
    if (world.new_specials == 0 || control->user() != -1 ||
        control->query_order() != Order::Living)
        return;
    for (auto& uptr : world.oblist)
    {
        walker* const w = uptr.get();
        if (w != nullptr && w != control && !w->dead() &&
            w->query_order() == Order::Living && w->user() == player_num)
        {
            control = w;
            return;
        }
    }
}

}  // namespace og::sim
