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
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/placement.h>
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
	// rule: A blink lands only where no solid body stands: a live visible living, a generator, or
	//   solid scenery (door, tree, boulder, any weapon that declares it blocks placement) blocks
	//   the landing; treasure, FX, corpses, hidden walkers and the caster itself never do.
	// tests: WalkerSpecialsUnit.teleport_blocked_marker_denied_without_probe_eat
	// tests: WalkerSpecialsUnit.teleport_probe_keeps_no_collide_and_height_gate
	// tests: Blink.a_blink_lands_only_where_no_solid_body_stands
	if (other == nullptr || other == self || other->dead() || other->hidden())
		return false;
	const Order order = other->query_order();
	const bool blocking =
		order == Order::Living || order == Order::Generator ||
		(order == Order::Weapon &&
		 (other->family() == FAMILY_DOOR || other->family() == FAMILY_TREE ||
		  other->family() == FAMILY_BOULDER ||
		  og::sim::declares_blocks_placement(*other)));
	if (!blocking)
		return false;
	// Cylinder z-overlap gate, mirroring ob_pass_check: height-disjoint pairs
	// (e.g. a flyer hovering above the landing spot) never collide.
	// sizez()==0 is the full-height sentinel.
	// rule: Bodies separated in height never block a landing: a hoverer above the landing spot does
	//   not stop a grounded blink; a height of 0 means full height and always overlaps.
	// tests: WalkerSpecialsUnit.teleport_probe_keeps_no_collide_and_height_gate
	// tests: Blink.a_height_overlapping_body_blocks_a_blink
	if (self->sizez() > 0 && other->sizez() > 0)
	{
		const float sz2 = self_z + static_cast<float>(self->sizez());
		const float oz1 = other->worldz();
		const float oz2 = oz1 + static_cast<float>(other->sizez());
		// rule: Bodies separated in height never block a landing: a hoverer above the landing spot
		//   does not stop a grounded blink; a height of 0 means full height and always overlaps.
		// tests: WalkerSpecialsUnit.teleport_probe_keeps_no_collide_and_height_gate
		// tests: Blink.a_height_overlapping_body_blocks_a_blink
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
	// rule: A blink lands only on ground the caster could stand on without transient flight: a
	//   flight potion or stuck-rule flight never blesses trees, boulders, water or lava as a
	//   landing, while permanent flyers/forestwalkers/ethereals keep their landing rights; the
	//   probe restores flight_left.
	// tests: WalkerSpecialsUnit.teleport_never_lands_on_obstacles_despite_flight
	// tests: WalkerSpecialsUnit.teleport_marker_over_boulder_denied_despite_flight
	// tests: WalkerSpecialsUnit.teleport_blocked_marker_denied_without_probe_eat
	// tests: Blink.a_permanent_flyer_lands_over_water_but_transient_flight_does_not
	if (!grid_ok)
		return false;
	// plumbing: myobmap == nullptr: no collision table, grid probe alone decides (headless/test
	//   worlds)
	if (world.myobmap == nullptr)
		return true;
	// ob_pass_check lets BIT_NO_COLLIDE walkers through every overlap; keep
	// that: they may blink onto occupied ground exactly as before.
	// rule: A walker that collides with nothing (BIT_NO_COLLIDE) may blink onto occupied ground, as
	//   it may walk through bodies.
	// tests: WalkerSpecialsUnit.teleport_probe_keeps_no_collide_and_height_gate
	// tests: WalkerSpecialsUnit.teleport_blocked_marker_denied_without_probe_eat
	if (self->stats() != nullptr &&
	    self->stats()->query_bit_flags(BIT_NO_COLLIDE))
		return true;
	// rule: A landing on the caster's own floor is probed at the caster's current height; a landing
	//   on another floor is probed at ground height of that floor.
	// tests: Blink.a_landing_is_probed_at_the_casters_height_on_its_floor_and_at_ground_elsewhere
	const float self_z =
		(target_floor == self->floor()) ? self->worldz() : 0.0f;
	// Walkers register in every 32px obmap bucket their bbox spans; scan all
	// buckets the landing bbox touches (a boolean check needs no dedup).
	constexpr std::int32_t kBucket = 32;
	// plumbing: obmap bucket scan over every 32 px bucket the landing box spans; per-candidate test
	//   is the rule above; pile==nullptr is an empty-bucket skip
	for (std::int32_t bx = x; bx < x + self->sizex() + kBucket; bx += kBucket)
	{
		// plumbing: obmap bucket scan over every 32 px bucket the landing box spans; per-candidate
		//   test is the rule above; pile==nullptr is an
		//   empty-bucket skip
		for (std::int32_t by = y; by < y + self->sizey() + kBucket; by += kBucket)
		{
			const std::list<walker*>* pile = world.myobmap->find_list(static_cast<short>(bx), static_cast<short>(by), target_floor);
			// plumbing: obmap bucket scan over every 32 px bucket the landing box spans;
			//   per-candidate test is the rule above; pile==nullptr
			//   is an empty-bucket skip
			if (pile == nullptr) continue;
			// plumbing: obmap bucket scan over every 32 px bucket the landing box spans;
			//   per-candidate test is the rule above; pile==nullptr
			//   is an empty-bucket skip
			for (const walker* other : *pile)
			{
				// plumbing: obmap bucket scan over every 32 px bucket the landing box spans;
				//   per-candidate test is the rule above; pile==nullptr
				//   is an empty-bucket skip
				if (teleport_spot_blocked_by(other, self, x, y, self_z))
					return false;
			}
		}
	}
	return true;
}

// What TURN UNDEAD can turn: an undead family, not warded. A warded
// (BIT_INVINCIBLE / invulnerable) undead is skipped before the resistance
// rolls: turning is damage, and both classic arms of the kill loop kill
// without ever entering walker::attack's gate. The kill loop, the Gameplay
// Fixes refusal and the cleric's bot gate all ask this one question.
bool turnable(const walker* w)
{
	// rule: TURN UNDEAD can only turn an undead family (skeleton, ghost, any pack family declaring
	//   is_undead) that is not warded: an undead carrying BIT_INVINCIBLE or an invulnerability
	//   potion is never turned, counted, or used to justify the cast. The kill loop, the Gameplay
	//   Fixes refusal and the cleric bot gate share this one test.
	// tests: GameplayFixesEngine.undead_foes_in_range_counts_what_turn_undead_could_turn
	// tests: WalkerSpecialsUnit.walker_specials_r11_turn_undead_paths
	// tests: WalkerSpecials.turn_undead_destroys_undead_foes_and_spares_the_living
	// tests: GameplayFixesClericBot.raises_with_nothing_to_turn_in_reach
	const auto* target_fd = w ? get_family_descriptor(w->family()) : nullptr;
	const bool warded =
	    w != nullptr && (w->stats()->query_bit_flags(BIT_INVINCIBLE) ||
	                     w->invulnerable_left() != 0);
	return w && target_fd && target_fd->is_undead && !warded;
}

} // namespace

bool walker::special(SpecialFailure* why, std::string* reason)
{
	auto fail = [why, reason](SpecialFailure failure, const std::string& message) {
		// plumbing: SpecialFailure/reason out-param writes (null-checked); the reasons themselves
		//   are the rule rows below
		if (why)
			*why = failure;
		if (reason)
			*reason = message;
		return false;
	};
	// plumbing: SpecialFailure/reason out-param writes (null-checked); the reasons themselves are
	//   the rule rows below
	if (why)
		*why = SpecialFailure::None;
	// plumbing: SpecialFailure/reason out-param writes (null-checked); the reasons themselves are
	//   the rule rows below
	if (reason)
		reason->clear();

	// Per-placed-NPC scenario flag: a specials-disabled walker never executes
	// its special, whatever the caller (player input, AI act logic, hit
	// responses, archmage scripting). Bail before the trace and before any
	// cost is charged so a disabled special simply does not fire.
	// rule: A placed NPC whose scenario flag disables specials never casts, from any caller (input,
	//   AI, hit response, script), and pays nothing.
	// tests: WalkerSpecialsUnit.special_reports_why_it_refused
	// tests: SimInputUnit.sim_input_specials_disabled_and_frozen_cues
	// tests: WalkerSpecialsUnit.out_of_range_special_is_cast_as_special_one
	if (specials_disabled())
		return fail(SpecialFailure::Disabled, "SPECIALS DISABLED");

	TRACE("walker", "special: family=%d current_special=%d", family(), current_special());

	// Are we somehow dead already?
	// rule: A dead walker cannot cast.
	// tests: WalkerSpecialsUnit.walker_specials_r11_special_and_teleport_paths
	// tests: WalkerSpecialsUnit.special_reports_why_it_refused
	// tests: WalkerSpecialsUnit.out_of_range_special_is_cast_as_special_one
	if (dead())
	{
		Log("Dead guy doing special!\n");
		return fail(SpecialFailure::Dead, "CANNOT CAST WHILE DEAD");
	}

	// Do we have a stats object? If not, freak out and exit :)
	// plumbing: walker without stats: SpecialFailure::NoStats ('SPECIAL UNAVAILABLE'); no real
	//   walker reaches it (tests/integration/test_walker_specials.cpp no_stats_guard)
	if (!stats_)
	{
		Log("Special with no stats\n");
		return fail(SpecialFailure::NoStats, "SPECIAL UNAVAILABLE");
	}

		int special_index = static_cast<int>(current_special());
		// rule: An out-of-range selected special (from a snapshot or the Lua setter) is cast as
		//   special 1 and pays special 1's price; current_special is rewritten to 1.
		// tests: WalkerSpecialsUnit.out_of_range_special_is_cast_as_special_one
		if (special_index < 0 || special_index >= NUM_SPECIALS)
		{
			set_current_special(1);
			special_index = 1;
		}

	// Do we have enough for our special ability? One price gates and is
	// charged: the specials table as this session sees it (New Specials),
	// read once here, before the cast can change the walker's kit state.
	const unsigned short cast_price =
		og::sim::cast_cost(*this, special_index, shifter_down() != 0);
	// A slot that is not in play (undeclared, or hidden by New Specials)
	// says so whatever the pool.
	// rule: A slot that is not in play (undeclared by the family, or hidden by the New Specials
	//   setting) refuses as 'NO SPECIAL AVAILABLE' whatever the mana pool, before the MP test.
	// tests: WalkerSpecials.family_with_no_special_spends_no_magic
	// tests: KitSkeleton.dig_in_off_twin_is_not_cast
	// tests: WalkerSpecialsUnit.walker_specials_r11_special_and_teleport_paths
	// tests: WalkerSpecials.no_magic
	if (cast_price >= kSpecialCostDisabled)
		return fail(SpecialFailure::Unavailable, "NO SPECIAL AVAILABLE");
	// rule: A caster with less mana than the slot's price refuses with 'NOT ENOUGH MP' and pays
	//   nothing; the refusal is traced as 'refused ... reason=NOT ENOUGH MP'.
	// tests: WalkerSpecials.no_magic, WalkerSpecialsUnit.special_reports_why_it_refused
	// tests: WalkerSpecialsUnit.out_of_range_special_is_cast_as_special_one
	if (stats_->magicpoints() < cast_price)
		TRACE("special", "refused id=%u family=%d slot=%d shift=%d team=%d reason=NOT ENOUGH MP",
		      static_cast<unsigned>(entity_id()), family(), special_index, shifter_down(), team_num());
	if (stats_->magicpoints() < cast_price)
		return fail(SpecialFailure::NoMP, "NOT ENOUGH MP");

	// rule: Only livings have specials: a weapon/treasure/FX with a priced slot and mana still
	//   refuses.
	// tests: WalkerSpecialsUnit.special_reports_why_it_refused
	// tests: WalkerSpecialsUnit.out_of_range_special_is_cast_as_special_one
	if (query_order() != Order::Living)
		return fail(SpecialFailure::NotLiving, "NO SPECIAL AVAILABLE");

	// Dispatch via scripted hook or family descriptor callback
	auto* fd = get_family_descriptor(family());
	bool did_special = false;
	std::string decline_reason;
	// rule: A cast is charged its price exactly once and only when the family script reports
	//   success; a declined cast (or a family with no special hook) costs nothing.
	// tests: WalkerSpecialsUnit.out_of_range_special_is_cast_as_special_one
	// tests: WalkerSpecials.success_returns_true_and_spends_mp
	// tests: WalkerSpecials.turn_undead_without_undead_is_refused_with_gameplay_fixes_on
	// tests: GameplayFixesClericRaise.refusals_spend_nothing_on_both_values
	// tests: SemWalker4.a_family_with_no_special_hook_is_refused_free_with_the_default_reason
	if (auto hook_result = og::script::hooks::do_special(fd, this, &decline_reason))
	{
		did_special = hook_result->succeeded();
		decline_reason = hook_result->reason();
		// rule: A cast is charged its price exactly once and only when the family script reports
		//   success; a declined cast (or a family with no special hook) costs nothing.
		// tests: WalkerSpecialsUnit.out_of_range_special_is_cast_as_special_one
		// tests: WalkerSpecials.success_returns_true_and_spends_mp
		// tests: WalkerSpecials.turn_undead_without_undead_is_refused_with_gameplay_fixes_on
		// tests: GameplayFixesClericRaise.refusals_spend_nothing_on_both_values
		// tests: SemWalker4.a_family_with_no_special_hook_is_refused_free_with_the_default_reason
		if (did_special)
			stats_->set_magicpoints(	stats_->magicpoints() - cast_price);
	}
	// Bound the world's accumulated time-stop bank after each special. The
	// per-cast formula remains in packs/core/families/living-03-mage.lua;
	// chain-casting at the cap spends the usual MP without extending the
	// freeze.
	// rule: The world's FREEZE TIME bank never exceeds its cap (300 ticks): chain-casting at the
	//   cap still spends mana but cannot extend the freeze.
	// tests: StatsFright.walker_special_clamps_enemy_freeze_bank
	// tests: SillinessBattery.mage_freeze_bank_capped_under_chain_casting
	// tests: SillinessBattery.mage_freeze_single_cast_spectacle_preserved
	if (current_game != nullptr && current_game->world != nullptr &&
	    current_game->world->enemy_freeze > og::combat::kEnemyFreezeBankCap)
	{
		TRACE("walker", "enemy_freeze bank clamped %d -> %d",
		      current_game->world->enemy_freeze, og::combat::kEnemyFreezeBankCap);
		current_game->world->enemy_freeze = og::combat::kEnemyFreezeBankCap;
	}
	// rule: Every refused cast is traced once with its reason (the level-play census reads it) and
	//   returned as ScriptDeclined; a refusal with no reason text reads 'NO SPECIAL AVAILABLE'. A
	//   charged cast traces 'cast id=... price=' instead.
	// tests: WalkerSpecials.turn_undead_without_undead_is_refused_with_gameplay_fixes_on
	// tests: SimInputHandler.belt_cast_sets_slot_and_alt_and_casts
	// tests: TryxianLevelPlay.bots_fire_varying_specials_at_somebody
	// tests: SemWalker4.a_family_with_no_special_hook_is_refused_free_with_the_default_reason
	if (!did_special)
		TRACE("special", "refused id=%u family=%d slot=%d shift=%d team=%d reason=%s",
		      static_cast<unsigned>(entity_id()), family(), special_index, shifter_down(), team_num(),
		      decline_reason.empty() ? "NO SPECIAL AVAILABLE" : decline_reason.c_str());
	if (!did_special)
		return fail(SpecialFailure::ScriptDeclined,
		            decline_reason.empty() ? "NO SPECIAL AVAILABLE" : decline_reason);
	// A charged cast: the census the level-play test reads.
	TRACE("special", "cast id=%u family=%d slot=%d shift=%d team=%d x=%d y=%d floor=%d price=%u",
	      static_cast<unsigned>(entity_id()), family(), special_index, shifter_down(), team_num(),
	      static_cast<int>(xpos()), static_cast<int>(ypos()), static_cast<int>(floor()),
	      static_cast<unsigned>(cast_price));
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
	// rule: A self-teleport marks the caster with the current tick before any probing, so same-tick
	//   rules (CTF flag drop) can tell a blink from walking. Same rule as below.
	// tests: Blink.a_blink_stamps_the_self_teleport_tick_even_when_it_fails
	if (current_game != nullptr && current_game->world != nullptr)
		set_last_self_teleport_tick(current_game->world->tick_count_);

	// First check to see if we have a marker to go to
	// NOTE: it must be a bit away from us ..
	// rule: A blink first seeks the caster's OWN live teleport marker; markers owned by others, and
	//   spent (dead) markers, are ignored.
	// tests: WalkerSpecials.teleport_marker_lands_centered_and_spends_a_use
	// tests: WalkerSpecialsUnit.walker_specials_r11_special_and_teleport_paths
	// tests: Blink.a_blink_ignores_another_casters_marker
	for(auto& uptr : current_game->world->oblist)
	{
	    walker* ob = uptr.get();
		// rule: A blink first seeks the caster's OWN live teleport marker; markers owned by others,
		//   and spent (dead) markers, are ignored.
		// tests: WalkerSpecials.teleport_marker_lands_centered_and_spends_a_use
		// tests: WalkerSpecialsUnit.walker_specials_r11_special_and_teleport_paths
		// tests: Blink.a_blink_ignores_another_casters_marker
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
			// rule: A marker more than 64 px away whose spot is clear takes the caster there:
			//   centred on the marker, onto the marker's floor, ending any fall cascade; a marker
			//   within 64 px or with a blocked spot is skipped and the random blink runs instead.
			// tests: WalkerSpecials.teleport_marker_lands_centered_and_spends_a_use
			// tests: WalkerSpecialsUnit.walker_specials_r11_special_and_teleport_paths
			// tests: WalkerSpecials.teleport_ignores_a_marker_within_64_px
			// tests: WalkerSpecialsUnit.teleport_blocked_marker_denied_without_probe_eat
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
				// rule: Each marker trip spends one use; the marker dies when its uses run out.
				// tests: WalkerSpecials.teleport_marker_lands_centered_and_spends_a_use
				// tests: WalkerSpecialsUnit.walker_specials_r11_special_and_teleport_paths
				// tests: Blink.a_marker_trip_spends_one_use_and_the_last_use_kills_it
				if (ob->lifetime() < 1)
				{
					ob->set_dead(1);
					ob->death();
				}
				return 1;
			} // end of successful transport
			else  // blocked somehow?
			{
				// rule: Only a player-driven caster is told 'Marker is Blocked!', and only for a
				//   far marker; AI casters and near markers say nothing.
				// tests: WalkerSpecialsUnit.blocked_marker_notice_goes_only_to_the_casting_player
				// tests: Blink.a_near_blocked_marker_tells_the_player_nothing
				if (user() != -1 && (distance > 64) ) // only tell players
					og::sim::emit_notification(current_game->sim_events, "Marker is Blocked!",
					                           0, static_cast<std::int32_t>(user()));
			}
			}
			} // end of checking for marker (we failed)

	// No marker: pick a random passable grid cell. Historically this was an
	// unbounded loop, which can hang tests (and gameplay) if level/grid state
	// isn't initialized or nothing is passable.
	// plumbing: no world/grid/pixel extents: random blink refused (return 0) instead of looping
	if (!current_game || !current_game->world || !current_game->world->grid.valid() ||
	    current_game->world->grid.w <= 0 || current_game->world->grid.h <= 0 ||
	    current_game->world->pixmaxx <= 0 || current_game->world->pixmaxy <= 0)
		return 0;

	GameWorld& world = *current_game->world;
	const int floors = world.floor_count();
	short target_floor = floor();
	std::int32_t keep_going = 200; // maxtries
	// rule: With no usable marker the blink draws random grid cells (x then y, one each per
	//   attempt) until one is clear, up to 200 attempts; it lands on the cell's pixel origin, or
	//   fails and leaves the caster where it was.
	// tests: WalkerSpecialsUnit.walker_specials_r11_special_and_teleport_paths
	// tests: WalkerSpecialsUnit.teleport_single_floor_rng_stream_is_legacy_shaped
	// tests: WalkerSpecialsUnit.teleport_blocked_marker_denied_without_probe_eat
	do
	{
		newx = static_cast<std::int32_t>(world.rng_.next(static_cast<std::uint32_t>(world.grid.w))) * GRID_SIZE;
		newy = static_cast<std::int32_t>(world.rng_.next(static_cast<std::uint32_t>(world.grid.h))) * GRID_SIZE;
		// Stacked-floor levels (bug A7): the blink considers EVERY floor —
		// one uniform floor draw per attempt; floors with no clear cell at
		// the drawn spot simply reject and retry. The extra draw is GATED on
		// floor_count() > 1 so single-floor levels keep a byte-identical
		// RNG stream (same gating pattern as apply_z_motion).
		// rule: On stacked-floor levels the random blink also draws a floor uniformly per attempt;
		//   single-floor levels draw no floor, keeping the classic RNG stream.
		// tests: WalkerSpecialsUnit.teleport_reaches_all_floors_on_multifloor_levels
		// tests: WalkerSpecialsUnit.teleport_single_floor_rng_stream_is_legacy_shaped
		if (floors > 1)
			target_floor = static_cast<short>(world.rng_.next(static_cast<std::uint32_t>(floors)));
		keep_going--;
	} while (keep_going > 0 &&
	         !teleport_landing_clear(world, this, newx, newy, target_floor));

	// rule: With no usable marker the blink draws random grid cells (x then y, one each per
	//   attempt) until one is clear, up to 200 attempts.
	// tests: WalkerSpecialsUnit.walker_specials_r11_special_and_teleport_paths
	// tests: WalkerSpecialsUnit.teleport_single_floor_rng_stream_is_legacy_shaped
	// tests: WalkerSpecialsUnit.teleport_blocked_marker_denied_without_probe_eat
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
	// rule: Same rule as above: the short escape hop also stamps the
	//   self-teleport tick before probing.
	// tests: Blink.a_ranged_hop_stamps_the_self_teleport_tick_even_when_it_fails
	if (current_game != nullptr && current_game->world != nullptr)
		set_last_self_teleport_tick(current_game->world->tick_count_);

	newx = static_cast<std::int32_t>(current_game->world->rng_.next(static_cast<std::uint32_t>(2 * range))) - range + xpos();
	newy = static_cast<std::int32_t>(current_game->world->rng_.next(static_cast<std::uint32_t>(2 * range))) - range + ypos();

	// Same-floor by design: this is a short-range tactical escape hop (the
	// range is 2D), not the mage's full blink — cross-floor travel stays the
	// business of walker::teleport. The destination probe is the shared
	// ground-rules + eat-free one (bug A6).
	// rule: The escape hop lands within +/-range px of the caster on its own floor at the first
	//   clear random spot, trying the first draw plus up to 200 more; with none it fails and stays
	//   put.
	// tests: WalkerSpecialsUnit.teleport_ranged_stays_on_floor_and_off_obstacles
	// tests: WalkerSpecialsUnit.walker_specials_r11_special_and_teleport_paths
	// tests: WalkerSpecials.unknown_family_and_teleport_ranged_fail_loop
	// tests: WalkerSpecials.guard_paths_and_teleport_failures
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
// (-1: no foe in range at all; -2, with the Gameplay Fixes setting on: foes
// in range, none of them undead)
std::int32_t walker::turn_undead(std::int32_t range, std::int32_t power)
{
	std::int32_t killed = 0;
	std::int32_t targets = 0;

	std::list<walker*> deadlist = current_game->world->find_foes_in_range(current_game->world->oblist, range,
	                                       &targets, this);
	// rule: TURN UNDEAD with no foe at all in reach answers -1 ('nobody'), distinct from 0 ('nobody
	//   turned'); on both settings.
	// tests: WalkerSpecialsUnit.walker_specials_r11_turn_undead_paths
	// tests: GameplayFixesEngine.turn_undead_refuses_a_ring_without_undead_when_on
	if (!targets)
		return -1;

	const bool fixes = og::sim::session_gameplay_fixes() != 0;
	// rule: Gameplay Fixes: a ring with foes but nothing turnable is refused before any roll (-2),
	//   so the cast spends nothing. Classic: the same ring rolls nothing but answers 0 and the cast
	//   is paid.
	// tests: GameplayFixesEngine.turn_undead_refuses_a_ring_without_undead_when_on
	// tests: GameplayFixesEngine.turn_undead_roll_bound_is_classic_on_both_values
	// tests: WalkerSpecialsUnit.walker_specials_r11_turn_undead_paths
	// tests: parity:cleric_turn_undead_reach_fixes_scen99
	if (fixes)
	{
		// Gameplay Fixes: a ring with foes but nothing to turn is refused
		// before any roll, so the cast spends nothing (-2; -1 stays "nobody").
		bool any_undead = false;
		// rule: Gameplay Fixes: a ring with foes but nothing turnable is refused before any roll
		//   (-2), so the cast spends nothing.
		// tests: GameplayFixesEngine.turn_undead_refuses_a_ring_without_undead_when_on
		// tests: GameplayFixesEngine.turn_undead_roll_bound_is_classic_on_both_values
		// tests: WalkerSpecialsUnit.walker_specials_r11_turn_undead_paths
		// tests: parity:cleric_turn_undead_reach_fixes_scen99
		for (auto* w : deadlist)
			// rule: Gameplay Fixes: a ring with foes but nothing turnable is refused before any
			//   roll (-2), so the cast spends nothing.
			// tests: GameplayFixesEngine.turn_undead_refuses_a_ring_without_undead_when_on
			// tests: GameplayFixesEngine.turn_undead_roll_bound_is_classic_on_both_values
			// tests: WalkerSpecialsUnit.walker_specials_r11_turn_undead_paths
			// tests: parity:cleric_turn_undead_reach_fixes_scen99
			if (turnable(w)) { any_undead = true; break; }
		// rule: Gameplay Fixes: a ring with foes but nothing turnable is refused before any roll
		//   (-2), so the cast spends nothing.
		// tests: GameplayFixesEngine.turn_undead_refuses_a_ring_without_undead_when_on
		// tests: GameplayFixesEngine.turn_undead_roll_bound_is_classic_on_both_values
		// tests: WalkerSpecialsUnit.walker_specials_r11_turn_undead_paths
		// tests: parity:cleric_turn_undead_reach_fixes_scen99
		if (!any_undead)
			return -2;
	}
	// The classic roll strength is 40 per px of the classic reach, 4 px per
	// level: 160 per level. With the fixes on the reach may be wider than
	// 4 px per level, so the roll keeps the classic strength by the level
	// (power), and a wider ring never changes who resists.
	// rule: The turning roll's strength is the classic 160 per caster level: classic computes it
	//   from the classic reach (range*40 with range=4*level); with Gameplay Fixes the reach may be
	//   wider but the strength stays power*160, so a wider ring never changes who resists.
	// tests: GameplayFixesEngine.turn_undead_roll_bound_is_classic_on_both_values
	const auto roll_bound = fixes ? static_cast<std::uint32_t>(power * 160)
	                              : static_cast<std::uint32_t>(range * 40);

    // plumbing: iteration over the foes in reach; per-foe rules below,
    //   below
    for(auto* w : deadlist)
	{
		// rule: Inside the ring only turnable foes are rolled for: living foes and warded undead
		//   are never turned and draw no roll. Same predicate as above.
		// tests: GameplayFixesEngine.turn_undead_roll_bound_is_classic_on_both_values
		// tests: GameplayFixesEngine.turn_undead_refuses_a_ring_without_undead_when_on
		// tests: WalkerSpecialsUnit.walker_specials_r11_turn_undead_paths
		// deferred: #474 the old game turns a warded (invulnerable/invincible) undead with Gameplay
		//   Fixes off; the current game skips it, so the classic arm is not pinned
		if (turnable(w))
		{
			// rule: Each turnable undead resists unless the caster's roll beats the victim's level
			//   roll (bound vs level*10, caster drawn first); a won roll destroys it instantly and
			//   counts it.
			// tests: WalkerSpecialsUnit.walker_specials_r11_turn_undead_paths
			// tests: WalkerSpecials.turn_undead_destroys_undead_foes_and_spares_the_living
			// tests: parity:cleric_turn_undead_scen99
			if (current_game->world->rng_.next(roll_bound) > current_game->world->rng_.next(static_cast<std::uint32_t>(w->stats()->level()*10)) )
			{
				GameWorld* const world = current_game->world;
				const bool scripted_mode =
				    (world->type & GameWorld::TYPE_SCRIPTED) &&
				    !(world->mode.init_attempted && !world->mode.active);
				// rule: In a scripted match a turned undead is a real kill: it goes through the
				//   level damage gate, stamps the kill on the caster's chain head and runs death()
				//   (mode hooks score it). In a classic world (or a scripted level whose mode
				//   failed to start) it is the classic pre-kill: dead, hp 0, a bloodspot via
				//   attack(), no attribution stamp, no death() call (the engine reaps it).
				// tests: ModesTdm.turn_undead_kill_scores_the_frag_and_schedules_the_corpse
				// tests: ModesTdm.classic_worlds_keep_the_legacy_turn_undead_prekill
				// tests: ModesOnslaught.turn_undead_replacement_at_lethal_still_destroys
				// tests: TurnUndead.a_scripted_level_whose_mode_failed_turns_undead_the_classic_way
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
					// rule: In a scripted match the level's damage gate decides a turn: a cancel
					//   spares the victim; a replacement below the victim's hp is ordinary damage
					//   (hp drops by it, a positive amount stamps the attacker's chain head, the
					//   victim retaliates) and never a kill; a replacement at or above hp destroys
					//   and credits the chain head (a charmed caster heads its own chain with fixes
					//   on).
					// tests: ModesOnslaught.turn_undead_gate_cancel_spares_the_victim
					// tests: ModesOnslaught.turn_undead_partial_replacement_damages_not_kills
					// tests: ModesOnslaught.turn_undead_zero_replacement_leaves_victim_untouched
					// tests: ModesOnslaught.turn_undead_replacement_at_lethal_still_destroys
					// tests: ModesOnslaught.turn_undead_partial_hit_credits_the_owner_chain_root
					// tests: ModesOnslaught.turn_undead_partial_hit_by_a_charmed_caster_credits_the_caster
					if (gated < 0)
						continue;
					walker* const credit = credit_head();  // #411: the topmost living
					if (gated < lethal)
					{
						// rule: In a scripted match the level's damage gate decides a turn.
						// tests: ModesOnslaught.turn_undead_gate_cancel_spares_the_victim
						// tests: ModesOnslaught.turn_undead_partial_replacement_damages_not_kills
						// tests: ModesOnslaught.turn_undead_zero_replacement_leaves_victim_untouched
						// tests: ModesOnslaught.turn_undead_replacement_at_lethal_still_destroys
						// tests: ModesOnslaught.turn_undead_partial_hit_credits_the_owner_chain_root
						// tests: ModesOnslaught.turn_undead_partial_hit_by_a_charmed_caster_credits_the_caster
						if (gated > 0 &&
						    credit != nullptr)
							w->note_attacker(credit, world->tick_count_);
						do_combat_damage(this, w, gated);
						w->stats()->hit_response(this);
						continue;
					}
					w->note_attacker(credit, world->tick_count_);
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

std::int32_t walker::undead_foes_in_range(std::int32_t range)
{
	std::int32_t targets = 0;
	const std::list<walker*> foes = current_game->world->find_foes_in_range(
	    current_game->world->oblist, range, &targets, this);
	std::int32_t undead = 0;
	// rule: The cleric's question 'how many foes here could TURN UNDEAD turn' counts only turnable
	//   foes in reach and draws nothing (bot gate, Lua walker:undead_foes_in_range).
	// tests: GameplayFixesEngine.undead_foes_in_range_counts_what_turn_undead_could_turn
	for (auto* w : foes)
		// rule: The cleric's question 'how many foes here could TURN UNDEAD turn' counts only
		//   turnable foes in reach and draws nothing (bot gate, Lua walker:undead_foes_in_range).
		// tests: GameplayFixesEngine.undead_foes_in_range_counts_what_turn_undead_could_turn
		if (turnable(w))
			++undead;
	return undead;
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

// Gameplay Fixes AD (#412): with the setting on a charmed walker heads its own
// chain for the charm's length. charm_left is the charm's clock: CHARM and
// MIND CONTROL set it with the colour, living::act counts it down and puts the
// team back when it runs out. (real_team_num alone is not the test: the
// parity runners, mapgen, several level hooks and the possession rider write
// it for a tick or a possession with no charm behind it.) Its owner, a tent, a
// bones pile or the caster that summoned it, stays on the old side, and the
// alliance follows the walker's own colour until the charm ends. Off, the
// climbs pass through it as always. Appended at the end of the file so the
// parity canary pin above stays where it is.
bool walker::charmed_head() const
{
	// rule: Gameplay Fixes AD (#412): while a charm runs, a charmed walker heads its own owner
	//   chain (credit, friendliness and alliance follow its own colour); with the setting off the
	//   owner climbs pass through it to its tent/summoner as in the classic game.
	// tests: GameplayFixesCharm.a_charmed_tent_troop_fights_for_its_charmer
	// tests: GameplayFixesCharm.an_uncharmed_tent_troop_answers_for_its_tent_on_both_values
	// tests: parity:thief_charm_tent_troop_fixes_scen99
	return og::sim::session_gameplay_fixes() != 0 && charm_left() > 0;
}
