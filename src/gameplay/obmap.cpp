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
// OBMAP -- an object to handle locations of pixies on a hash table.
#include <cstdint>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/key_mask.h>
#include <openglad/core/util.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <openglad/gameplay/sim_emit.h>


// quiet: the probe asks the loud call's question and changes nothing (no
// draw, no treasure eaten, no door opened, no collide()); see query_list in
// obmap.h. Every caller but obmap::query_list(..., true) is loud.
short ob_pass_check(short x, short y, walker* ob, const std::list<walker*>& pile, const obmap* map, bool quiet = false);

//#define XRES GRID_SIZE
//#define YRES GRID_SIZE
inline constexpr int OBRES = 32; //GRID_SIZE

// Floor-keyed bucketing for multi-floor levels: each floor occupies a distinct
// numx band [floor*OB_FLOOR_STRIDE, floor*OB_FLOOR_STRIDE+199] so entities on
// different floors never share a collision pile. Floor 0 maps to [0,199],
// byte-identical to pre-Z. Clamped to keep the pos_to_walker short key in range.
inline constexpr int OB_FLOOR_STRIDE = 256;
static short floor_numx_offset(const walker* ob)
{
	// rule: Walkers on different floors never share a collision pile, so they never block, hit or
	//   eat each other; floors outside 0..120 collide as floor 0 / floor 120.
	// tests: ZAxis.collision_is_floor_separated
	// tests: SemAi2.collision_piles_clamp_the_floor_to_zero_through_one_hundred_twenty
	int f = ob ? static_cast<int>(ob->floor()) : 0;
	// rule: Part of the rule above that begins "Walkers on different floors never share a collision
	//   pile, ..."
	// tests: ZAxis.collision_is_floor_separated
	// tests: SemAi2.collision_piles_clamp_the_floor_to_zero_through_one_hundred_twenty
	if (f < 0) f = 0;
	// rule: Part of the rule above that begins "Walkers on different floors never share a collision
	//   pile, ..."
	// tests: ZAxis.collision_is_floor_separated
	// tests: SemAi2.collision_piles_clamp_the_floor_to_zero_through_one_hundred_twenty
	if (f > 120) f = 120;
	return static_cast<short>(f * OB_FLOOR_STRIDE);
}

// Injective for every (short, short) pair: 16 bits per coordinate. Hash-derived
// coordinates are always non-negative, but crafted keys (tests) round-trip
// safely through the uint16 casts too.
static inline std::uint32_t pack_cell(short numx, short numy)
{
	return (static_cast<std::uint32_t>(static_cast<std::uint16_t>(numx)) << 16) |
	       static_cast<std::uint32_t>(static_cast<std::uint16_t>(numy));
}

// The only external mutation of pos_to_walker in the codebase is a wholesale
// clear() at level teardown (game_world / level_runtime_data), after which the
// map stays empty until the next obmap call. So "map empty, index non-empty"
// is exactly (and only) the stale-after-external-clear state.
void obmap::sync_cell_index()
{
	// plumbing: drop the cell-pointer cache after level teardown cleared pos_to_walker externally
	if (!cell_index_.empty() && pos_to_walker.empty())
		cell_index_.clear();
}

// These are passed in as PIXEL coordinates now...
obmap::obmap()
{
	obmapres = OBRES;
	
	pos_to_walker.clear();
	walker_to_pos.clear();
}

obmap::~obmap()
{}

size_t obmap::size() const
{
    return walker_to_pos.size();
}

short obmap::query_list(walker  *ob, short x, short y, bool quiet)
{
	short numx, startnumx, endnumx;
	short numy, startnumy, endnumy;
	// plumbing: null or dead prober: logged and treated as passable (dead walkers do not move);
	//   asserted by LivingCombat.obmap_guard_and_hash_and_move_branches
	if (!ob || ob->dead())
	{
		Log("Bad ob to query_list.\n");
		return 1;
	}
	const short fo = floor_numx_offset(ob);
	startnumx = static_cast<short>(hash(x) + fo);
	endnumx   = static_cast<short>(hash( static_cast<short>(x+ob->sizex()) ) + fo);
	startnumy = hash(y);
	endnumy   = hash( static_cast<short>(y+ob->sizey()) );

	sync_cell_index();

	// For each y grid row we are in...
	// plumbing: iterate every hash cell the prober's box covers; see obmap.cpp:134 and the
	//   ob_pass_check rows
	for (numx = startnumx; numx <= endnumx; numx++)
	{
		// plumbing: iterate every hash cell the prober's box covers; see obmap.cpp:134 and the
		//   ob_pass_check rows
		for (numy = startnumy; numy <= endnumy; numy++)
		{
			const std::list<walker*>* pile;
			const std::uint32_t key = pack_cell(numx, numy);
			auto cached = cell_index_.find(key);
			// plumbing: cell-index cache lookup; an absent pile is skipped without creating one
			if (cached != cell_index_.end())
			{
				pile = cached->second;
			}
			else
			{
				// Avoid default-constructing empty piles during queries.
				auto it = pos_to_walker.find(std::make_pair(numx, numy));
				// plumbing: cell-index cache lookup; an absent pile is skipped without creating one
				if (it == pos_to_walker.end())
					continue;
				pile = &it->second;
				cell_index_.emplace(key, &it->second);
			}
			// Snapshot the pile: collision handlers inside ob_pass_check
			// (e.g. walker::death → obmap::remove) can erase this
			// pos_to_walker entry, which would leave a dangling reference
			// and corrupt iterator traversal — the root cause of the
			// walker_to_pos.find() infinite-loop hang.
			auto pile_snapshot = *pile;
			// rule: A movement probe is blocked if its box meets a blocker in ANY covered 32px
			//   cell; cells are scanned column-major and the scan stops at the first blocking cell,
			//   so treasure in a later cell is not eaten (and later bodies get no collide()) on a
			//   blocked probe.
			// tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
			// tests: SemAi2.a_blocked_probe_stops_at_the_first_blocking_cell_and_eats_nothing_beyond_it
			if (!ob_pass_check(x, y, ob, pile_snapshot, this, quiet))
				return 0;
		}
	}
	return 1;
}


short obmap::remove(walker  *ob)  // This goes in walker's destructor
{
    // Find all of the instances of the object and remove them from the map
    // Some of this is redundant, but its worth avoiding segfaults, IMO :-)
    // plumbing: remove(nullptr) is a no-op
    if (!ob)
        return 0;

    sync_cell_index();

    // Fast path only: removal is driven by walker_to_pos bookkeeping.
    // Do not scan the entire pos_to_walker map in production; that can turn
    // legitimate gameplay/test loops into O(N^2) behavior.
    // Get the list of positions that the walker occupies
    auto e = walker_to_pos.find(ob);
    // plumbing: bookkeeping-mismatch fallback: bounded removal from the walker's current bbox cells
    //   (skipped at negative coords), returns whether anything was removed; asserted by
    //   LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
    if (e == walker_to_pos.end())
    {
        // Bookkeeping mismatch: attempt a bounded removal based on the
        // walker's current bounding box. This avoids global scans while still
        // preventing stale pointers in pos_to_walker (ASan/UAF).
        // plumbing: bookkeeping-mismatch fallback: bounded removal from the walker's current bbox
        //   cells (skipped at negative coords), returns whether anything was removed; asserted by
        //   LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
        if (ob->xpos() < 0 || ob->ypos() < 0)
            return 0;

        short numx, startnumx, endnumx;
        short numy, startnumy, endnumy;
        const short fo = floor_numx_offset(ob);
        startnumx = static_cast<short>(hash(ob->xpos()) + fo);
        endnumx = static_cast<short>(hash(static_cast<short>(ob->xpos() + ob->sizex())) + fo);
        startnumy = hash(ob->ypos());
        endnumy = hash(static_cast<short>(ob->ypos() + ob->sizey()));

        bool removed_any = false;
        // plumbing: bookkeeping-mismatch fallback: bounded removal from the walker's current bbox
        //   cells (skipped at negative coords), returns whether anything was removed; asserted by
        //   LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
        for (numx = startnumx; numx <= endnumx; numx++)
        {
            // plumbing: bookkeeping-mismatch fallback: bounded removal from the walker's current
            //   bbox cells (skipped at negative coords), returns whether anything was removed;
            //   asserted by LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
            for (numy = startnumy; numy <= endnumy; numy++)
            {
                auto g = pos_to_walker.find(std::make_pair(numx, numy));
                // plumbing: bookkeeping-mismatch fallback: bounded removal from the walker's
                //   current bbox cells (skipped at negative coords), returns whether anything was
                //   removed; asserted by
                //   LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
                if (g == pos_to_walker.end())
                    continue;
                const size_t before = g->second.size();
                g->second.remove(ob);
                // plumbing: bookkeeping-mismatch fallback: bounded removal from the walker's
                //   current bbox cells (skipped at negative coords), returns whether anything was
                //   removed; asserted by
                //   LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
                if (g->second.size() != before)
                    removed_any = true;
                // plumbing: bookkeeping-mismatch fallback: bounded removal from the walker's
                //   current bbox cells (skipped at negative coords), returns whether anything was
                //   removed; asserted by
                //   LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
                if (g->second.empty())
                {
                    cell_index_.erase(pack_cell(numx, numy));
                    pos_to_walker.erase(g);
                }
            }
        }
        // plumbing: bookkeeping-mismatch fallback: bounded removal from the walker's current bbox
        //   cells (skipped at negative coords), returns whether anything was removed; asserted by
        //   LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
        return removed_any ? 1 : 0;
    }

    // For each occupied cell, remove all occurrences (defensive against
    // accidental duplicates) and drop empty piles.
    // plumbing: normal removal: erase from each recorded cell, drop empty piles and their cache
    //   entry
    for (auto& pos : e->second)
    {
        auto g = pos_to_walker.find(pos);
        // plumbing: normal removal: erase from each recorded cell, drop empty piles and their cache
        //   entry
        if (g == pos_to_walker.end())
            continue;
        g->second.remove(ob);
        // plumbing: normal removal: erase from each recorded cell, drop empty piles and their cache
        //   entry
        if (g->second.empty())
        {
            cell_index_.erase(pack_cell(pos.first, pos.second));
            pos_to_walker.erase(g);
        }
    }

    // Erase the walker from the walker map too
    walker_to_pos.erase(e);
    return 1;
}

short obmap::add(walker  *ob, short x, short y)  // This goes in walker's constructor
{
	short numx, startnumx, endnumx;
	short numy, startnumy, endnumy;

	// plumbing: add(nullptr) refused
	if (!ob)
		return 0;

	// rule: A walker placed at negative coordinates is not registered in the collision map: nothing
	//   can bump into it, and it blocks nothing, until it is moved back onto the map.
	// tests: SemAi2.a_walker_at_negative_coordinates_is_not_in_the_collision_map
	if (x < 0 || y < 0)
		return 0;

	sync_cell_index();

	// Tighten invariants: a walker may only be present once. If callers try to
	// add it again, remove via O(1) bookkeeping first.
	// plumbing: re-adding a registered walker removes its old registration first (one registration
	//   per walker)
	if (walker_to_pos.find(ob) != walker_to_pos.end())
		(void)remove(ob);

	const short fo = floor_numx_offset(ob);
	startnumx = static_cast<short>(hash(x) + fo);
	endnumx   = static_cast<short>(hash( static_cast<short>(x + ob->sizex()) ) + fo);
	startnumy = hash(y);
	endnumy   = hash( static_cast<short>(y + ob->sizey()) );

    // Figure out all of the positions that are occupied
	std::list<std::pair<short,short> > pos;
	// plumbing: register the walker in every covered cell
	for (numx = startnumx; numx <= endnumx; numx++)
	{
		// plumbing: register the walker in every covered cell
		for (numy = startnumy; numy <= endnumy; numy++)
		{
		    // Store this position
		    pos.push_back(std::make_pair(numx, numy));

			// Put the walker here too (no duplicates).
			const std::uint32_t key = pack_cell(numx, numy);
			std::list<walker*>* pile;
			auto cached = cell_index_.find(key);
			// plumbing: cached pile lookup / create pile; no duplicate entries in a pile
			if (cached != cell_index_.end())
			{
				pile = cached->second;
			}
			else
			{
				pile = &pos_to_walker[std::make_pair(numx, numy)];
				cell_index_.emplace(key, pile);
			}
			// plumbing: cached pile lookup / create pile; no duplicate entries in a pile
			if (std::find(pile->begin(), pile->end(), ob) == pile->end())
				pile->push_back(ob);
				}
			}
		
	// Now record where he is
	walker_to_pos[ob] = std::move(pos);
			
	return 1;
}


short obmap::move(walker* ob, short x, short y)  // This goes in walker's setxy
{
    // Do we really need to move?
	// plumbing: move to the same position is a no-op
	if(x == ob->xpos() && y == ob->ypos())
        return 1;

	remove(ob);
	add(ob, x, y);
	return 1;
}


short obmap::hash(short y)
{
	//  For now, this assumes no one is smaller than size 8
	//  Also note that the hash table never loops.
	// return (y/8);
	short num = static_cast<short>(y/OBRES) ;
	// rule: The collision grid has 199 real 32px cells per axis; any coordinate beyond cell 198 (x
	//   >= 6368) or below -31 lands in the shared overflow cell 199, so far off-map things collide
	//   with each other there; pixels -1..-31 truncate to cell 0.
	// tests: LivingCombat.obmap_guard_and_hash_and_move_branches
	// tests: LivingCombat.obmap_round11_stale_query_and_helper_accessors_paths
	if (num > 198)
		num = 199;
	// rule: Part of the rule above that begins "The collision grid has 199 real 32px cells per ..."
	// tests: LivingCombat.obmap_guard_and_hash_and_move_branches
	// tests: LivingCombat.obmap_round11_stale_query_and_helper_accessors_paths
	if (num < 0)
		num = 199;
	return num;
}

short obmap::unhash(short y)
{
    return y*OBRES;
}

// The pile key of (x, y) on `floor`, the floor clamped as floor_numx_offset
// clamps a walker's: one rule for obmap_get_list and find_list.
static std::pair<short, short> floor_cell(short x, short y, int floor)
{
	// rule: Pile lookups by (x,y,floor) clamp the floor exactly as a walker's own registration does
	//   (same rule as obmap.cpp:49), so searches on an out-of-range floor see floor 0/120's
	//   walkers.
	// tests: ZAxis.find_near_foe_probes_the_searchers_floor
	// tests: SemAi2.pile_lookups_clamp_the_floor_like_registration_does
	if (floor < 0) floor = 0;
	// rule: Part of the rule above that begins "Pile lookups by (x,y,floor) clamp the floor exactly
	//   as ..."
	// tests: ZAxis.find_near_foe_probes_the_searchers_floor
	// tests: SemAi2.pile_lookups_clamp_the_floor_like_registration_does
	if (floor > 120) floor = 120;
	const short fo = static_cast<short>(floor * OB_FLOOR_STRIDE);
	return {static_cast<short>(obmap::hash(x) + fo), obmap::hash(y)};
}

std::list<walker*>& obmap::obmap_get_list(short x, short y, int floor)
{
	const auto [numx, numy] = floor_cell(x, y, floor);
	sync_cell_index();
	const std::uint32_t key = pack_cell(numx, numy);
	auto cached = cell_index_.find(key);
	// plumbing: obmap_get_list cache hit
	if (cached != cell_index_.end())
		return *cached->second;
	// A miss still default-constructs the empty pile node (the historical
	// operator[] semantics) for the one caller that writes into the list
	// (GameWorld::find_near_foe); read-only sweeps use find_list, so the
	// debug overlay (obmap_debug_draw.cpp) now draws fewer empty boxes.
	std::list<walker*>& pile = pos_to_walker[std::make_pair(numx, numy)];
	cell_index_.emplace(key, &pile);
	return pile;
}

const std::list<walker*>* obmap::find_list(short x, short y, int floor)
{
	const auto [numx, numy] = floor_cell(x, y, floor);
	sync_cell_index();
	auto cached = cell_index_.find(pack_cell(numx, numy));
	// plumbing: find_list cache hit / miss returns nullptr without creating a pile
	if (cached != cell_index_.end())
		return cached->second;
	// A miss makes no node in either map.
	auto it = pos_to_walker.find(std::make_pair(numx, numy));
	// plumbing: find_list cache hit / miss returns nullptr without creating a pile
	if (it == pos_to_walker.end())
		return nullptr;
	return &it->second;
}

/***********************************************
**  All pass checking from here down.
***********************************************/
short ob_pass_check(short x, short y, walker* ob, const std::list<walker*>& pile, const obmap* map, bool quiet)
{
	short oxsize, oysize;
	short x2, y2, xsize2, ysize2;
	Order targetorder;//, targetteam, targetfamily;
	Order myorder;

	// plumbing: null prober passes
	if (!ob)
		return 1;

	oxsize = ob->sizex();
	oysize = ob->sizey();

	myorder = ob->query_order();

	// Check each object to see if sizes collide.
	// plumbing: iterate a snapshot of the pile; rules at obmap.cpp:371..525
	for (auto it = pile.begin(); it != pile.end(); )
	{
		// Advance the iterator up-front: collision handlers may remove elements
		// from the obmap piles (including the current element), and std::list
		// iterator invalidation would otherwise cause a UAF when the loop
		// increments.
		walker* w = *it;
		++it;

	    // rule: A walker is never blocked by itself, and a dead walker (corpse, opened door, spent
	    //   weapon) never blocks, is never eaten and gets no collision.
	    // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
	    // tests: SemAi2.a_dead_walker_never_blocks_a_probe
	    if (w != ob)
        {
            // If bookkeeping is consistent, any live walker in a pile must also
            // be present in walker_to_pos. If not, treat it as stale and skip
            // without dereferencing (ASan UAF hardening).
            // plumbing: stale pile entry (not in walker_to_pos) skipped without dereference;
            //   asserted by LivingCombat.obmap_round11_stale_query_and_helper_accessors_paths
            if (map != nullptr && map->walker_to_pos.find(w) == map->walker_to_pos.end())
                continue;

            // rule: Part of the rule above that begins "A walker is never blocked by itself, and a
            //   ..."
            // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
            // tests: SemAi2.a_dead_walker_never_blocks_a_probe
            if (w->dead())
                continue;

            targetorder = w->query_order();
            
            // Let our own team's weapons pass over us
            // rule: A weapon and anything friendly to it (same team on the owner chain: its
            //   thrower, allies, allied weapons) pass through each other: you never block or are
            //   blocked by your own side's projectiles.
            // tests: GameplayFixesBots.elf_rocks_an_ally_on_the_pad_is_not_in_the_way
            // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
            if((targetorder == Order::Weapon || myorder == Order::Weapon) && ob->is_friendly(w))
                continue;
            // Allow weapons to sometimes 'miss' opposing team's weapons
            // (a quiet probe never rolls: an enemy weapon in flight passes it)
            else if(targetorder == Order::Weapon && myorder == Order::Weapon && (quiet || current_game->world->rng_.next(10) > 3))  // rule: Two hostile projectiles
                //   meeting usually fly through each other: 6 in 10 times (rng.next(10) > 3) they
                //   pass, 4 in 10 they collide; a quiet probe never rolls and always passes.
                // tests: QuietProbe.an_enemy_weapon_in_flight_passes_a_quiet_probe_without_a_draw
                // tests: SemAi2.hostile_projectiles_pass_each_other_on_a_roll_of_four_to_nine
                continue;
            // Weapons never hit treasure
            else if(targetorder == Order::Treasure && myorder == Order::Weapon)  // rule: Projectiles never hit or pick
                //   up treasure: an arrow flies over gold, food and exit pads.
                // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                // tests: SemAi2.a_projectile_passes_over_treasure_and_eats_none
                continue;
            else  // rule: Boxes interact only when they overlap after each is shrunk by 1px on
                //   every side (edge contact is not a collision); a loud probe that shares a cell
                //   with a body without overlapping it forgets the mover's remembered collision
                //   partner (collide_ob cleared); a quiet probe keeps it.
                // tests: QuietProbe.a_quiet_probe_keeps_the_collide_ob_it_found
                // tests: LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
                // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
            {
                // Cylinder z-overlap: skip pairs whose height ranges don't
                // overlap. sizez()==0 is the full-height sentinel (legacy
                // entities), so single-floor collision is unchanged.
                // rule: Height matters only when both have a height: two things whose vertical
                //   spans (worldz..worldz+sizez) do not overlap pass each other (a high projectile
                //   flies over a short walker); either one with sizez 0 is full-height (classic
                //   single-floor collision).
                // tests: ZAxis.cylinder_zoverlap_lets_high_projectile_pass
                // tests: SemAi3.a_thing_of_height_zero_is_full_height_and_a_high_knife_still_meets_it
                if (ob->sizez() > 0 && w->sizez() > 0)
                {
                    const float oz1 = ob->worldz();
                    const float oz2 = oz1 + static_cast<float>(ob->sizez());
                    const float wz1 = w->worldz();
                    const float wz2 = wz1 + static_cast<float>(w->sizez());
                    // rule: Part of the rule above that begins "Height matters only when both have
                    //   a height: two ..."
                    // tests: ZAxis.cylinder_zoverlap_lets_high_projectile_pass
                    // tests: SemAi3.a_thing_of_height_zero_is_full_height_and_a_high_knife_still_meets_it
                    if (oz2 <= wz1 || wz2 <= oz1)
                        continue;
                }
                x2 = w->xpos();
                y2 = w->ypos();
                xsize2 = w->sizex();
                ysize2 = w->sizey();
                // rule: Part of the rule above that begins "Boxes interact only when they overlap
                //   after each is ..."
                // tests: QuietProbe.a_quiet_probe_keeps_the_collide_ob_it_found
                // tests: LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
                // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                if(collide(x,y,oxsize,oysize,x2,y2,xsize2,ysize2))
                {
                    // rule: Stepping onto a treasure picks it up (eat_me) and the treasure never
                    //   blocks; a quiet probe picks nothing up.
                    // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                    if ( targetorder == Order::Treasure )
                    {
                        // A quiet probe eats nothing and latches no exit: a
                        // treasure never blocks, so it is simply passed.
                        // rule: Part of the rule above that begins "Stepping onto a treasure picks
                        //   it up (eat_me) and ..."
                        // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                        if (quiet)
                            continue;
                        // #160: exit pads fire once per CONTACT, not once per
                        // skip_exit cooldown. Every movement probe lands here,
                        // so held-direction walking on the pad used to re-run
                        // the exit prompt / "Foes remain!" toast each time the
                        // 10-tick cooldown expired (and in MP every re-prompt
                        // froze the sim for everyone). The prober latches the
                        // pad's rect on its first eaten contact and skips
                        // exit-pad eats until it has fully left the rect
                        // (cleared each tick in living::act via
                        // update_exit_latch()). Non-exit treasures are
                        // unchanged; skip_exit stays as a secondary bound
                        // (it also throttles teleporters/door toasts and
                        // feeds game_server's prompt-routing sniff).
                        // rule: An exit pad fires once per contact: the first probe that touches it
                        //   eats it and latches the walker to the pad's rect; further probes skip
                        //   it until the walker has fully left the rect (cleared in living::act),
                        //   so held walking on the pad prompts once.
                        // tests: SimWorldHeadless.exit_prompts_once_per_contact_not_per_cooldown
                        // tests: SimWorldHeadless.exit_reprompts_after_stepping_fully_off_and_back
                        // tests: SimWorldHeadless.exit_latch_holds_while_standing_still
                        // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                        if (w->family() == FAMILY_EXIT)
                        {
                            // rule: Part of the rule above that begins "An exit pad fires once per
                            //   contact: the first ..."
                            // tests: SimWorldHeadless.exit_prompts_once_per_contact_not_per_cooldown
                            // tests: SimWorldHeadless.exit_reprompts_after_stepping_fully_off_and_back
                            // tests: SimWorldHeadless.exit_latch_holds_while_standing_still
                            // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                            if (ob->exit_latched())
                                continue;
                            ob->latch_exit_contact(w);
                        }
                        w->eat_me(ob);
                    }
                    else if ( (targetorder == Order::Weapon)
                              && (w->family() == FAMILY_DOOR) )  // rule: Walking into a door whose key
                                  //   you carry (key bit = 2^door level) opens it: the door dies;
                                  //   you are still stopped for that step unless you are
                                  //   no-collide.
                                  // tests: LivingCombat.obmap_query_list_door_unlock_and_lock_branches
                                  // tests: QuietProbe.a_door_is_neither_opened_nor_announced_by_a_quiet_probe
                    {
                        // A quiet probe gives the loud answer below without
                        // opening the door, posting the notice or colliding:
                        // a door it holds the key to blocks this round unless
                        // the prober is BIT_NO_COLLIDE; a locked one blocks.
                        // rule: A quiet probe answers a door like the loud one without touching it:
                        //   a door you hold the key to blocks this round unless you are no-collide;
                        //   a locked door blocks.
                        // tests: QuietProbe.a_door_is_neither_opened_nor_announced_by_a_quiet_probe
                        // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                        if (quiet)
                        {
                            const bool keyed = (ob->keys() & static_cast<std::uint32_t>(key_level_mask(w->stats()->level()))) != 0;
                            // rule: Part of the rule above that begins "A quiet probe answers a
                            //   door like the loud ..."
                            // tests: QuietProbe.a_door_is_neither_opened_nor_announced_by_a_quiet_probe
                            // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                            return (keyed && ob->stats()->query_bit_flags(BIT_NO_COLLIDE)) ? 1 : 0;
                        }
                        // Can we unlock this door?
                        // rule: Part of the rule above that begins "Walking into a door whose key
                        //   you carry (key ..."
                        // tests: LivingCombat.obmap_query_list_door_unlock_and_lock_branches
                        // tests: QuietProbe.a_door_is_neither_opened_nor_announced_by_a_quiet_probe
                        if (ob->keys() & static_cast<std::uint32_t>(key_level_mask(w->stats()->level())))
                        {
                            // Open the door ..
                            w->set_dead(1);
                            w->death();
                            ob->collide(w);
                            // rule: Part of the rule above that begins "Walking into a door whose
                            //   key you carry (key ..."
                            // tests: LivingCombat.obmap_query_list_door_unlock_and_lock_branches
                            // tests: QuietProbe.a_door_is_neither_opened_nor_announced_by_a_quiet_probe
                            if (ob->stats()->query_bit_flags(BIT_NO_COLLIDE))
                                return 1;
                            return 0; // block for this round
                        } // end of unlocked door
                        else  // rule: A locked door blocks; a player-controlled walker bumping it
                            //   is told 'Key N needed!' at most once per 10-tick cooldown; NPCs are
                            //   never told.
                            // tests: QuietProbe.a_door_is_neither_opened_nor_announced_by_a_quiet_probe
                            // tests: LivingCombat.obmap_query_list_door_unlock_and_lock_branches
                            // tests: SemAi3.a_locked_door_tells_a_seat_once_per_cooldown_and_never_tells_an_npc
                        {
                            // Do we notify?
                            // rule: Part of the rule above that begins "A locked door blocks; a
                            //   player-controlled walker bumping it ..."
                            // tests: QuietProbe.a_door_is_neither_opened_nor_announced_by_a_quiet_probe
                            // tests: LivingCombat.obmap_query_list_door_unlock_and_lock_branches
                            // tests: SemAi3.a_locked_door_tells_a_seat_once_per_cooldown_and_never_tells_an_npc
                            if (!(ob->skip_exit()) && (ob->user() != -1))
                            {
                                std::string message = std::format("Key {} needed!",
                                        w->stats()->level());
                                og::sim::emit_notification(
                                        current_game->sim_events, message, 0,
                                        static_cast<std::int32_t>(ob->user()));
                                ob->set_skip_exit(10);
                            } // end of failed open door notification
                            ob->collide(w);
                            return 0; // failed to open door
                        }
                    } // end of door case
                    else  // rule: Any other overlapping thing (a living, a generator, a
                        //   tree/obstacle weapon, an enemy weapon that did not miss) blocks the
                        //   step; a loud probe records the collision (collide(): melee swing at an
                        //   enemy, collide_ob set); a no-collide walker (ghost etc.) records it but
                        //   passes.
                        // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                        // tests: SemAi3.a_body_blocks_a_normal_mover_and_a_no_collide_mover_records_it_and_passes
                    {
                        // Interpenetration escape rule (2026-07-07, part of
                        // the guard-standoff wedge fix — see
                        // docs/GAMEPLAY_FIXES_FROM_CLASSIC.md). A transient
                        // pass-through can leave two livings overlapped;
                        // classically that state was ABSORBING: every step
                        // the trapped walker tried still collided with the
                        // body around it, so the pair stood merged forever —
                        // an unkillable shield (weapons died on whichever of
                        // the two they met first). If we ALREADY overlap this
                        // living where we stand, let any strictly separating
                        // move pass (it may still be blocked by someone
                        // else); only non-separating moves collide. Purely
                        // integer arithmetic, no RNG, and inert unless the
                        // boxes already interpenetrate.
                        // rule: Two livings already overlapping can always step apart: if the mover
                        //   already overlaps this living, a step that strictly increases the centre
                        //   distance passes it (others may still block); a step that does not
                        //   separate collides as usual.
                        // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                        // tests: SemAi3.two_overlapping_livings_may_step_apart_but_not_closer
                        if (myorder == Order::Living &&
                            targetorder == Order::Living &&
                            collide(ob->xpos(), ob->ypos(), oxsize, oysize,
                                    x2, y2, xsize2, ysize2))
                        {
                            const std::int32_t tx = 2 * x2 + xsize2;
                            const std::int32_t ty = 2 * y2 + ysize2;
                            const std::int32_t nx = 2 * x + oxsize - tx;
                            const std::int32_t ny = 2 * y + oysize - ty;
                            const std::int32_t oxc =
                                2 * ob->xpos() + oxsize - tx;
                            const std::int32_t oyc =
                                2 * ob->ypos() + oysize - ty;
                            const std::int64_t d_new =
                                static_cast<std::int64_t>(nx) * nx +
                                static_cast<std::int64_t>(ny) * ny;
                            const std::int64_t d_old =
                                static_cast<std::int64_t>(oxc) * oxc +
                                static_cast<std::int64_t>(oyc) * oyc;
                            // rule: Part of the rule above that begins "Two livings already
                            //   overlapping can always step apart: if ..."
                            // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                            // tests: SemAi3.two_overlapping_livings_may_step_apart_but_not_closer
                            if (d_new > d_old)
                                continue; // separating: not blocked by w
                        }
                        // rule: Part of the rule above that begins "Any other overlapping thing (a
                        //   living, a generator, a ..."
                        // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                        // tests: SemAi3.a_body_blocks_a_normal_mover_and_a_no_collide_mover_records_it_and_passes
                        if (!quiet)
                            ob->collide(w);
                        // rule: Part of the rule above that begins "Any other overlapping thing (a
                        //   living, a generator, a ..."
                        // tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
                        // tests: SemAi3.a_body_blocks_a_normal_mover_and_a_no_collide_mover_records_it_and_passes
                        if (ob->stats()->query_bit_flags(BIT_NO_COLLIDE))
                            return 1;
                        return 0;
                    }
                }
                else if (!quiet)
                    // if (ob->collide_ob()) //let's just assume its safe
                    ob->set_collide_ob(nullptr);
            }
        }
	}
	
	return 1;
}

short collide(short x,  short y,  short xsize,  short ysize,
              short x2, short y2, short xsize2, short ysize2)
{
	short xright, x2right;
	short ydown,  y2down;

	//return 0; // debug

	// Shrink values:
	x += 1;
	x2 += 1;
	y += 1;
	y2 += 1;
	xsize -= 2;
	xsize2 -= 2;
	ysize -= 2;
	ysize2 -= 2;

	x2right = static_cast<short>(x2+xsize2);
	// rule: Overlap test: each box shrunk by one pixel on every side; separated on any axis means
	//   no collision.
	// tests: LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
	// tests: GameplayFixesWalk.step_meets_answers_what_collide_answers_on_the_three_boxes
	if (x > x2right)
		return 0;

	xright = static_cast<short>(x+xsize);
	// rule: Part of the rule above that begins "Overlap test: each box shrunk by one pixel on ..."
	// tests: LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
	// tests: GameplayFixesWalk.step_meets_answers_what_collide_answers_on_the_three_boxes
	if (xright < x2)
		return 0;

	y2down = static_cast<short>(y2+ysize2);
	// rule: Part of the rule above that begins "Overlap test: each box shrunk by one pixel on ..."
	// tests: LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
	// tests: GameplayFixesWalk.step_meets_answers_what_collide_answers_on_the_three_boxes
	if (y > y2down)
		return 0;

	ydown = static_cast<short>(y+ysize);
	// rule: Part of the rule above that begins "Overlap test: each box shrunk by one pixel on ..."
	// tests: LivingCombat.obmap_remove_stale_and_collide_axis_reject_paths
	// tests: GameplayFixesWalk.step_meets_answers_what_collide_answers_on_the_three_boxes
	if (ydown < y2)
		return 0;

	return 1;
}

bool og::sim::step_meets(const walker& mover, float dx, float dy, const walker& body)
{
	const float offs[3][2] = { {dx, dy}, {ceilf(dx), ceilf(dy)}, {floorf(dx), floorf(dy)} };
	// rule: A fractional step 'meets' a body when the box at the exact offset, the rounded-up
	//   offset or the rounded-down offset overlaps it (used by the Gameplay Fixes walk rules to
	//   decide what a step actually touched).
	// tests: GameplayFixesWalk.step_meets_answers_what_collide_answers_on_the_three_boxes
	for (const auto& o : offs)
		// rule: Part of the rule above that begins "A fractional step 'meets' a body when the box
		//   ..."
		// tests: GameplayFixesWalk.step_meets_answers_what_collide_answers_on_the_three_boxes
		if (collide(static_cast<short>(mover.xpos() + o[0]), static_cast<short>(mover.ypos() + o[1]),
		            mover.sizex(), mover.sizey(), body.xpos(), body.ypos(), body.sizex(), body.sizey()))
			return true;
	return false;
}
