/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <algorithm>
#include <cstdint>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/sim_emit.h>

#include <list>

namespace
{

// Eat-free teleport destination probe (bug A6/A7 fix; a DELIBERATE semantic
// change, parity-audited). The legacy probe was GameWorld::query_passable,
// which had two defects:
//  (a) its grid half accepts trees/boulders/water/lava under the TRANSIENT
//      flight bypass (flight potion / stuck-rule flight_left), so a blink
//      cast mid-flight parked the caster permanently inside impassable
//      scenery once the flight ticks ran out — probe under ground rules
//      instead (permanent BIT_FLYING/BIT_FORESTWALK/BIT_ETHEREAL still
//      apply, so genuine flyers keep their landing rights);
//  (b) its obmap half routes through ob_pass_check, whose overlap scan EATS
//      any treasure at the probed spot and fires collide() handlers — a
//      REJECTED destination must not consume potions/gold the caster never
//      touched.
// Blocking-entity shape matches the CTF spawn probe (spawn_spot_clear,
// src/gameplay/ctf/ctf.cpp): livings, generators, and solid weapon-order
// scenery (doors/trees/boulders) block; treasures and FX do not. The A2-A4
// stair-landing fix is expected to promote a shared floor-aware helper onto
// GameWorld; fold this local copy into it once that exists.
bool teleport_spot_blocked_by(const walker* other, const walker* self,
                              std::int32_t x, std::int32_t y, float self_z)
{
	if (other == nullptr || other == self || other->dead())
		return false;
	const Order order = other->query_order();
	const bool blocking =
		order == Order::Living || order == Order::Generator ||
		(order == Order::Weapon &&
		 (other->family() == FAMILY_DOOR || other->family() == FAMILY_TREE ||
		  other->family() == FAMILY_BOULDER));
	if (!blocking)
		return false;
	// Cylinder z-overlap gate, mirroring ob_pass_check: height-disjoint pairs
	// (e.g. a flyer hovering above the landing spot) never collide.
	// sizez()==0 is the full-height sentinel.
	if (self->sizez() > 0 && other->sizez() > 0)
	{
		const float sz2 = self_z + static_cast<float>(self->sizez());
		const float oz1 = other->worldz();
		const float oz2 = oz1 + static_cast<float>(other->sizez());
		if (sz2 <= oz1 || oz2 <= self_z)
			return false;
	}
	return x + self->sizex() > other->xpos() &&
	       x < other->xpos() + other->sizex() &&
	       y + self->sizey() > other->ypos() &&
	       y < other->ypos() + other->sizey();
}

bool teleport_landing_clear(GameWorld& world, walker* self,
                            std::int32_t x, std::int32_t y, int target_floor)
{
	// Ground-rules grid probe: mask the transient flight ticks for the
	// duration of the query so the flight bypass cannot bless the landing.
	const short saved_flight = self->flight_left();
	self->set_flight_left(0);
	const bool grid_ok = world.query_grid_passable(
		static_cast<float>(x), static_cast<float>(y), self, target_floor);
	self->set_flight_left(saved_flight);
	if (!grid_ok)
		return false;
	if (world.myobmap == nullptr)
		return true;
	// ob_pass_check lets BIT_NO_COLLIDE walkers through every overlap; keep
	// that: they may blink onto occupied ground exactly as before.
	if (self->stats() != nullptr &&
	    self->stats()->query_bit_flags(BIT_NO_COLLIDE))
		return true;
	const float self_z =
		(target_floor == self->floor()) ? self->worldz() : 0.0f;
	// Walkers register in every 32px obmap bucket their bbox spans; scan all
	// buckets the landing bbox touches (a boolean check needs no dedup).
	constexpr std::int32_t kBucket = 32;
	for (std::int32_t bx = x; bx < x + self->sizex() + kBucket; bx += kBucket)
	{
		for (std::int32_t by = y; by < y + self->sizey() + kBucket; by += kBucket)
		{
			const std::list<walker*>& pile = world.myobmap->obmap_get_list(
				static_cast<short>(bx), static_cast<short>(by), target_floor);
			for (const walker* other : pile)
			{
				if (teleport_spot_blocked_by(other, self, x, y, self_z))
					return false;
			}
		}
	}
	return true;
}

} // namespace

bool walker::special()
{
	// Per-placed-NPC scenario flag: a specials-disabled walker never executes
	// its special, whatever the caller (player input, AI act logic, hit
	// responses, archmage scripting). Bail before the trace and before any
	// cost is charged so a disabled special simply does not fire.
	if (specials_disabled())
		return false;

	TRACE("walker", "special: family=%d current_special=%d", family(), current_special());

	// Are we somehow dead already?
	if (dead())
	{
		Log("Dead guy doing special!\n");
		return 0;
	}

	// Do we have a stats object? If not, freak out and exit :)
	if (!stats_)
	{
		Log("Special with no stats\n");
		return 0;
	}

		int special_index = static_cast<int>(current_special());
		if (special_index < 0 || special_index >= NUM_SPECIALS)
		{
			set_current_special(1);
			special_index = 1;
		}

	// Do we have enough for our special ability?
	if (stats_->magicpoints() < stats_->special_cost(special_index))
		return 0;

	if (query_order() != Order::Living)
		return 0;

	// Dispatch via scripted hook or family descriptor callback
	auto* fd = get_family_descriptor(family());
	bool did_special = false;
	if (auto hook_result = og::script::hooks::do_special(fd, this))
	{
		did_special = *hook_result;
		if (did_special)
			stats_->set_magicpoints(	stats_->magicpoints() - stats_->special_cost(special_index));
	}
	// Bound the world's accumulated time-stop bank after each special. The
	// per-cast formula remains in packs/core/families/living-03-mage.lua;
	// chain-casting at the cap spends the usual MP without extending the
	// freeze.
	if (current_game != nullptr && current_game->world != nullptr &&
	    current_game->world->enemy_freeze > og::combat::kEnemyFreezeBankCap)
	{
		TRACE("walker", "enemy_freeze bank clamped %d -> %d",
		      current_game->world->enemy_freeze, og::combat::kEnemyFreezeBankCap);
		current_game->world->enemy_freeze = og::combat::kEnemyFreezeBankCap;
	}
	return did_special;
}

bool walker::teleport()
{
	std::int32_t newx = 0, newy = 0;
	std::int32_t distance = 0;

	// Stamp the self-teleport marker the moment the blink begins, BEFORE
	// any destination probing: consumers (the CTF flag rules) must be able
	// to tell the blink apart from real movement within this same tick.
	// Server-only transient, stale at every tick boundary; nothing outside
	// CTF reads it. (The probe itself is eat-free nowadays — see
	// teleport_landing_clear — but the blink/movement distinction remains.)
	if (current_game != nullptr && current_game->world != nullptr)
		set_last_self_teleport_tick(current_game->world->tick_count_);

	// First check to see if we have a marker to go to
	// NOTE: it must be a bit away from us ..
	for(auto& uptr : current_game->world->oblist)
	{
	    walker* ob = uptr.get();
		if (ob &&
		        ob->query_order() == Order::FX &&
		        ob->family() == FAMILY_MARKER &&
		        ob->owner() == this &&
		        !ob->dead()
		   )
		{
			// Found our marker! We land centered on it (same math as
			// center_on), on the MARKER's floor, provided the landing spot
			// is genuinely clear: ground-rules grid + eat-free occupancy
			// probe (bug A6), floor-aware for stacked levels (bug A7).
			distance = distance_to_ob(ob);
			const std::int32_t landx = ob->xpos() + ob->sizex() / 2 - sizex() / 2;
			const std::int32_t landy = ob->ypos() + ob->sizey() / 2 - sizey() / 2;
			const short marker_floor = ob->floor();
			if ((distance > 64) &&
			    teleport_landing_clear(*current_game->world, this,
			                           landx, landy, marker_floor))
			{
				// Floor first, then position: change_floor re-buckets the
				// obmap at the walker's CURRENT coordinates (no-op when
				// already on the marker's floor), then center_on/setxy
				// moves it within that floor.
				change_floor(marker_floor);
				fall_stories_ = 0; // teleport ends any fall cascade uncharged
				TRACE("zaxis", "teleport reset fall accumulator");
				center_on(ob);
				ob->set_lifetime(ob->lifetime() - 1);
				if (ob->lifetime() < 1)
				{
					ob->set_dead(1);
					ob->death();
				}
				return 1;
			} // end of successful transport
			else  // blocked somehow?
			{
				if (user() != -1 && (distance > 64) ) // only tell players
					og::sim::emit_notification(current_game->sim_events, "Marker is Blocked!");
			}
			}
			} // end of checking for marker (we failed)

	// No marker: pick a random passable grid cell. Historically this was an
	// unbounded loop, which can hang tests (and gameplay) if level/grid state
	// isn't initialized or nothing is passable.
	if (!current_game || !current_game->world || !current_game->world->grid.valid() ||
	    current_game->world->grid.w <= 0 || current_game->world->grid.h <= 0 ||
	    current_game->world->pixmaxx <= 0 || current_game->world->pixmaxy <= 0)
		return 0;

	GameWorld& world = *current_game->world;
	const int floors = world.floor_count();
	short target_floor = floor();
	std::int32_t keep_going = 200; // maxtries
	do
	{
		newx = static_cast<std::int32_t>(world.rng_.next(static_cast<std::uint32_t>(world.grid.w))) * GRID_SIZE;
		newy = static_cast<std::int32_t>(world.rng_.next(static_cast<std::uint32_t>(world.grid.h))) * GRID_SIZE;
		// Stacked-floor levels (bug A7): the blink considers EVERY floor —
		// one uniform floor draw per attempt; floors with no clear cell at
		// the drawn spot simply reject and retry. The extra draw is GATED on
		// floor_count() > 1 so single-floor levels keep a byte-identical
		// RNG stream (same gating pattern as apply_z_motion).
		if (floors > 1)
			target_floor = static_cast<short>(world.rng_.next(static_cast<std::uint32_t>(floors)));
		keep_going--;
	} while (keep_going > 0 &&
	         !teleport_landing_clear(world, this, newx, newy, target_floor));

	if (keep_going > 0)
	{
		// Floor before position (see the marker path above).
		change_floor(target_floor);
		fall_stories_ = 0; // teleport ends any fall cascade uncharged
		TRACE("zaxis", "teleport reset fall accumulator");
		setxy(newx, newy);
		return 1;
	}
	return 0;
}

bool walker::teleport_ranged(std::int32_t range)
{
	std::int32_t newx = 0, newy = 0;
	std::int32_t keep_going = 200; // maxtries

	// Self-teleport marker, stamped before the destination probes below —
	// see walker::teleport for the rationale.
	if (current_game != nullptr && current_game->world != nullptr)
		set_last_self_teleport_tick(current_game->world->tick_count_);

	newx = static_cast<std::int32_t>(current_game->world->rng_.next(static_cast<std::uint32_t>(2 * range))) - range + xpos();
	newy = static_cast<std::int32_t>(current_game->world->rng_.next(static_cast<std::uint32_t>(2 * range))) - range + ypos();

	// Same-floor by design: this is a short-range tactical escape hop (the
	// range is 2D), not the mage's full blink — cross-floor travel stays the
	// business of walker::teleport. The destination probe is the shared
	// ground-rules + eat-free one (bug A6).
	while(!teleport_landing_clear(*current_game->world, this, newx, newy, floor()) && keep_going)
	{
		newx = static_cast<std::int32_t>(current_game->world->rng_.next(static_cast<std::uint32_t>(2 * range))) - range + xpos();
		newy = static_cast<std::int32_t>(current_game->world->rng_.next(static_cast<std::uint32_t>(2 * range))) - range + ypos();
		keep_going--;
	}
	if (keep_going)
	{
		setxy(newx, newy);
		return 1;
	}
	return 0; // failed to find safe spot
}

// Turns undead; ie, skeleton or ghost, within range
// Returns the number of dead destroyed
std::int32_t walker::turn_undead(std::int32_t range, [[maybe_unused]] std::int32_t power)
{
	std::int32_t killed = 0;
	std::int32_t targets = 0;

	std::list<walker*> deadlist = current_game->world->find_foes_in_range(current_game->world->oblist, range,
	                                       &targets, this);
	if (!targets)
		return -1;

    for(auto* w : deadlist)
	{
		const auto* target_fd = w ? get_family_descriptor(w->family()) : nullptr;
		if (w && target_fd && target_fd->is_undead)
		{
			if (current_game->world->rng_.next(static_cast<std::uint32_t>(range*40)) > current_game->world->rng_.next(static_cast<std::uint32_t>(w->stats()->level()*10)) )
			{
				GameWorld* const world = current_game->world;
				const bool scripted_mode =
				    (world->type & GameWorld::TYPE_SCRIPTED) &&
				    !(world->mode.init_attempted && !world->mode.active);
				if (scripted_mode)
				{
					// The classic pre-kill below never enters walker::attack's
					// hit path (dead-target early-out), so the level damage
					// gate, the kill-attribution stamp, and the death hooks
					// would all miss this kill class. Scripted matches route
					// the instant kill through all three, honoring the full
					// gate contract: a cancel means the victim is not a legal
					// target this phase, and a replacement BELOW the offered
					// lethal amount is ordinary damage — hp write, positive-
					// amount attribution stamp, retaliation — never a kill.
					// This arm runs no hp <= 0 check of its own, so unlike
					// walker::attack even a 0 replacement cannot finish off a
					// target that is already at 0 hp.
					const short lethal = static_cast<short>(
					    std::clamp(w->stats()->hitpoints(), 1.0f, 32000.0f));
					const short gated =
					    og::script::hooks::level_damage_gate(w, this, lethal);
					if (gated < 0)
						continue;
					walker* headguy = this;
					while (headguy->owner() && headguy->owner() != headguy)
						headguy = headguy->owner();
					if (gated < lethal)
					{
						if (gated > 0 &&
						    headguy->query_order() == Order::Living)
							w->note_attacker(headguy, world->tick_count_);
						do_combat_damage(this, w, gated);
						w->stats()->hit_response(this);
						continue;
					}
					w->note_attacker(headguy, world->tick_count_);
					w->set_dead(1);
					w->stats()->set_hitpoints(0);
					w->death();
					killed++;
					continue;
				}
				w->set_dead(1);
				w->stats()->set_hitpoints(0);
				//w->death();
				attack(w); // to generate bloodspot, etc.
				killed++;
			}
		}
	}
	
	return killed;
}

// *******************************************
//
//    MONSTER intellIGENCE ROUTINES
//
// *******************************************

// Basically, we check a direction for foes.
// If we find one, we init_fire.  If not,
// we do nothing. init_fire will take care of
// turning us if we need it.
