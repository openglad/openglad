/* Respawn engine.
 *
 * The queue/timer/anchor machinery shared by the classic difficulty-submenu
 * respawn modes and the scripted-mode (TYPE_SCRIPTED) Lua respawn surface.
 * Extracted from the retired CTF engine (src/gameplay/ctf/ctf.cpp): the
 * flag/control-point/win machinery moved to campaign-pack Lua, the RNG-free
 * placement and revive paths stayed here. Nothing in this file draws
 * world.rng_, and entity lists iterate in list order, so the simulation
 * stays deterministic and replicable.
 */

#include <openglad/gameplay/respawn/respawn_state.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/placement.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>

namespace og::sim {

namespace {

walker* classic_fire_respawn(GameWorld& world, const RespawnEntry& entry);

[[nodiscard]] bool is_score_team(unsigned char team)
{
    return team < SCORE_TEAM_COUNT;
}

} // namespace

// setxy() routes obmap updates through obmap::move, which early-outs when the
// position is unchanged. A revived corpse (already removed from the obmap by
// walker::death) placed back on its own tile would silently stay unindexed,
// so re-register explicitly after placement.
void ensure_obmap_registration(GameWorld& world, walker* w)
{
    // dormant(): a delayed spawn holds the "never in the obmap" invariant
    // until its wake tick — no respawn/placement path may re-register one.
    // No current caller passes a dormant walker, so this is pure invariant
    // enforcement (byte-identical for every existing path).
    // rule: A walker brought back by a respawn (revived in place on its own tile, or a fresh AI
    //   replacement) is put back into the collision map so it blocks and is bumped like any live
    //   body; a dead, hidden, ignored or not-yet-woken (dormant) walker is never put into it.
    // tests: SemRespawn.a_revived_walker_rejoins_the_collision_map_and_an_absent_one_never_does
    if (world.myobmap == nullptr || w == nullptr || w->ignore() || w->dead() ||
        w->dormant() || w->hidden())
        return;
    if (world.myobmap->walker_to_pos.find(w) == world.myobmap->walker_to_pos.end())
        world.myobmap->add(w, w->xpos(), w->ypos());
}

namespace {

// Passability probe for spawn placement. query_passable routes through
// ob_pass_check, whose treasure overlap dispatches eat_me — a probe must
// never eat a drumstick or pick up a flag at a position the walker is not
// actually placed at. Grid check plus a manual blocking-entity scan instead.
[[nodiscard]] bool spawn_spot_blocked_by(const walker* other, const walker* w,
                                         short x, short y)
{
    // rule: A spawn or respawn spot is taken only by a live, visible body that occupies space: a
    //   living, a generator, a door, a tree, a boulder or a weapon family a pack declares as a
    //   placement blocker; treasure (food, flags, gems), other weapons, dead or hidden walkers and
    //   the walker itself never block, and the test is an exact box overlap.
    // tests: RespawnEngine.spawn_probe_rejects_doors_and_cross_bucket_blockers
    // tests: QuietProbe.body_spot_clear_asks_the_placement_rule_with_the_bodys_box
    // tests: SemRespawn.trees_boulders_and_declared_blockers_take_a_spot_and_dead_or_hidden_bodies_do_not
    if (other == nullptr || other == w || other->dead() || other->hidden())
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
    return x + w->sizex() > other->xpos() &&
           x < other->xpos() + other->sizex() &&
           y + w->sizey() > other->ypos() &&
           y < other->ypos() + other->sizey();
}

[[nodiscard]] bool spawn_spot_clear(GameWorld& world, walker* w, short x, short y)
{
    // rule: A spawn spot with no floor channel (floor < 0, the walker's own grid floor and floor-0
    //   buckets) is clear only when the ground under the walker's box is passable for that walker
    //   AND no blocking body overlaps it, including bodies registered only in a neighbouring 32px
    //   bucket the box straddles. Probing never eats treasure and never grows the obmap.
    // tests: RespawnEngine.spawn_probe_rejects_doors_and_cross_bucket_blockers
    // tests: RespawnEngine.exported_probes_hold_null_and_impassable_guards
    // tests: RespawnEngine.spawn_probes_make_no_empty_pile
    if (!world.query_grid_passable(x, y, w))
        return false;
    // plumbing: null obmap (no world map yet) answers clear; a bucket with no pile is skipped
    //   (find_list miss, #417 makes no node)
    if (world.myobmap == nullptr)
        return true;
    // Walkers register in every 32px obmap bucket their bbox spans; scan all
    // buckets the spawn bbox touches (a boolean check needs no dedup).
    constexpr int kBucket = 32;
    // plumbing: bucket sweep over the spawn box and pile iteration; the rule is above /
    //   above
    for (int bx = x; bx < x + w->sizex() + kBucket; bx += kBucket)
    {
        // plumbing: bucket sweep over the spawn box and pile iteration; the rule is above
        //   / above
        for (int by = y; by < y + w->sizey() + kBucket; by += kBucket)
        {
            const std::list<walker*>* pile = world.myobmap->find_list(
                static_cast<short>(bx), static_cast<short>(by));
            // plumbing: null obmap (no world map yet) answers clear; a bucket with no pile is
            //   skipped (find_list miss, #417 makes no node)
            if (pile == nullptr)
                continue;
            // plumbing: bucket sweep over the spawn box and pile iteration; the rule is
            //   above / above
            for (walker* other : *pile)
            {
                // rule: A spawn spot with no floor channel (floor < 0, the walker's own grid floor
                //   and floor-0 buckets) is clear only when the ground under the walker's box is
                //   passable for that walker AND no blocking body overlaps it, including bodies
                //   registered only in a neighbouring 32px bucket the box straddles.
                // tests: RespawnEngine.spawn_probe_rejects_doors_and_cross_bucket_blockers
                // tests: RespawnEngine.exported_probes_hold_null_and_impassable_guards
                // tests: RespawnEngine.spawn_probes_make_no_empty_pile
                if (spawn_spot_blocked_by(other, w, x, y))
                    return false;
            }
        }
    }
    return true;
}

// Revives a player corpse in place: same walker, same entity id, full HP,
// cleared targeting/commands. Placement is the caller's business (CTF paths
// rotate to the team's next anchor; classic paths move home when clear). If
// another live walker already holds the same player binding (the player
// switched bodies while dead), the revived walker rejoins the AI pool instead.
void revive_player_walker(GameWorld& world, walker* w, int team)
{
    // rule: A revived hero stands up alive at full hitpoints with its queued orders, foe and leader
    //   cleared and its controller binding restored (death_called reset so the death hooks can fire
    //   again next time).
    // tests: RespawnEngine.player_corpse_revives_in_place_with_control_preserved
    // tests: SemRespawn.a_revived_hero_is_its_own_controller_and_can_die_again
    w->set_dead(0);
    w->set_death_called(0);
    // plumbing: stats() null guard in revive_player_walker (the stats part of the revive is skipped
    //   for a walker without stats); the revive rule itself is row above
    if (w->stats() != nullptr)
    {
        // Tick cleanup nulls a corpse's controller binding; restore it
        // before clear_command(), which dereferences the controller.
        w->stats()->set_controller(w);
        w->stats()->set_hitpoints(w->stats()->max_hitpoints());
        w->stats()->clear_command();
    }
    w->set_foe(nullptr);
    w->set_leader(nullptr);
    // Respawn breaks charm: rejoin the recorded true team, fully uncharmed.
    // The real-team restore is explicit here since #317 (clear_command no
    // longer does it); the team < 4 branch below then applies the caller's
    // recorded team as before.
    // rule: Respawn breaks charm: a revived player rejoins its true team (the recorded real team,
    //   any byte including FFA/mutant band bytes 16-31), and when the caller names a score team 0-3
    //   that team wins; real_team is cleared to 255 and charm_left to 0.
    // tests: RespawnEngine.charmed_corpse_respawns_on_its_true_team
    // tests: RespawnEngine.charmed_band_byte_corpse_respawns_on_its_true_team
    // tests: RespawnEngine.win_latch_flush_breaks_charm_onto_true_team
    // tests: SemRespawn.an_uncharmed_hero_revives_on_its_own_team
    if (w->real_team_num() != 255)
        w->set_team_num(w->real_team_num());
    if (team >= 0 && team < 4)
        w->set_team_num(static_cast<unsigned char>(team));
    w->set_real_team_num(255);
    w->set_charm_left(0);

    // rule: If the player switched to another body while dead, the revived body does not steal the
    //   seat back: it joins the AI pool (user -1, ACT_RANDOM) and the player's current body keeps
    //   control.
    // tests: RespawnEngine.user_conflict_demotes_revived_walker_to_ai
    // tests: RespawnEngine.player_corpse_revives_in_place_with_control_preserved
    if (w->user() != -1)
    {
        // plumbing: oblist scan for a conflicting controller; rule is above
        for (const auto& uptr : world.oblist)
        {
            walker* other = uptr.get();
            // rule: If the player switched to another body while dead, the revived body does not
            //   steal the seat back.
            // tests: RespawnEngine.user_conflict_demotes_revived_walker_to_ai
            // tests: RespawnEngine.player_corpse_revives_in_place_with_control_preserved
            if (other != nullptr && other != w && !other->dead() &&
                other->query_order() == Order::Living &&
                other->user() == w->user() && other->act_type() == ACT_CONTROL)
            {
                w->set_user(-1);
                w->set_act_type(ACT_RANDOM);
                break;
            }
        }
    }
}

[[nodiscard]] bool already_scheduled(const RespawnState& respawn,
                                     std::uint32_t entity_id)
{
    // plumbing: queue scan; rule below
    for (const auto& entry : respawn.respawn_queue)
    {
        // rule: A corpse has at most one pending respawn: scheduling it again (Lua
        //   og.respawn_schedule twice, or the classic per-tick scan) is refused while its entry is
        //   pending, and 'pending' is reported per corpse.
        // tests: ModeBindings.respawn_schedule_pending_and_counts
        if (entry.walker_entity_id == entity_id)
            return true;
    }
    return false;
}

[[nodiscard]] bool same_guy_identity(const guy* lhs, const guy* rhs)
{
    return lhs != nullptr && rhs != nullptr && lhs->id == rhs->id &&
        lhs->owner_player_index == rhs->owner_player_index &&
        lhs->owner_save_slot == rhs->owner_save_slot;
}

// True when a live walker is already bound to the corpse's character
// (a cleric resurrect beat the respawner): the corpse must stay retired.
// Guy ids are only unique per owning player (each client numbers its roster
// from its own counter), so identity is the (id, owner-player, save-slot)
// triple. Owner tags are kNoOwner for local play, where bare ids suffice.
[[nodiscard]] bool live_duplicate_exists(const GameWorld& world,
                                         const walker* corpse)
{
    // rule: A hero whose character is already walking again (a cleric resurrect, a save merge,
    //   another door) is never revived or rescheduled; character identity is the guy id together
    //   with its owning player and save slot, so another player's same-numbered guy does not count.
    //   AI walkers (no guy) never have duplicates.
    // tests: RespawnEngine.live_duplicate_character_cancels_the_revive
    // tests: RespawnEngine.fire_time_live_duplicate_cancels_the_revive
    // tests: RespawnEngine.same_guy_id_under_a_different_owner_does_not_block_revive
    // tests: ClassicRespawn.live_duplicate_hero_corpse_is_never_rescheduled
    // tests: RespawnEngine.ai_respawn_spawns_fresh_walker_of_same_family_level_team
    if (corpse->myguy == nullptr)
        return false;
    // plumbing: oblist scan; rule above
    for (const auto& uptr : world.oblist)
    {
        walker* other = uptr.get();
        // rule: A hero whose character is already walking again (a cleric resurrect, a save merge,
        //   another door) is never revived or rescheduled.
        // tests: RespawnEngine.live_duplicate_character_cancels_the_revive
        // tests: RespawnEngine.fire_time_live_duplicate_cancels_the_revive
        // tests: RespawnEngine.same_guy_id_under_a_different_owner_does_not_block_revive
        // tests: ClassicRespawn.live_duplicate_hero_corpse_is_never_rescheduled
        // tests: RespawnEngine.ai_respawn_spawns_fresh_walker_of_same_family_level_team
        if (other != nullptr && other != corpse && !other->dead() &&
            same_guy_identity(other->myguy, corpse->myguy))
        {
            return true;
        }
    }
    return false;
}

enum class CorpseDropScrub
{
    LifeGemOnly,
    All,
};

[[nodiscard]] bool stain_matches_corpse_identity(const walker* stain,
                                                  const walker* corpse)
{
    // rule: A respawn only clears its own corpse's bloodstain: a player's stain pairs by its copied
    //   guy identity, and an AI corpse only ever clears a guy-less (AI) stain, so a bot's respawn
    //   never removes a nearby queued player's corpse and vice versa. (Same rule as below
    //   and below.)
    // tests: RespawnEngine.nearby_player_respawns_scrub_only_their_own_stain
    // tests: SemRespawn.a_respawn_fire_scrubs_only_a_stain_of_its_own_identity
    if (corpse->myguy == nullptr)
        return stain->myguy == nullptr;
    return same_guy_identity(stain->myguy, corpse->myguy);
}

// True when this stain is the still-pending corpse of ANY queued respawn.
// Every scheduled corpse — player and AI — stays in oblist until its entry
// fires, so entry.walker_entity_id resolves it through find_by_id and the
// stain pairs by the same identity/team/floor/exact-position rule for both
// kinds (guy triple for players, guy-less matching guy-less for AI).
[[nodiscard]] bool stain_belongs_to_pending_respawn(const GameWorld& world,
                                                    const walker* stain)
{
    // rule: A mode's positional corpse scrub (og.respawn_scrub_stains_at) never removes the stain
    //   of a fighter whose respawn is still pending, player or AI; only an exact corpse location
    //   with the same identity, team and floor is protected.
    // tests: RespawnEngine.positional_scrub_preserves_pending_ai_stains
    // tests: RespawnEngine.positional_scrub_preserves_only_pending_player_stains
    // tests: RespawnEngine.positional_scrub_matches_pending_stain_location
    for (const RespawnEntry& entry : world.respawn.respawn_queue)
    {
        const walker* corpse = world.find_by_id(entry.walker_entity_id);
        // rule: A mode's positional corpse scrub (og.respawn_scrub_stains_at) never removes the
        //   stain of a fighter whose respawn is still pending, player or AI.
        // tests: RespawnEngine.positional_scrub_preserves_pending_ai_stains
        // tests: RespawnEngine.positional_scrub_preserves_only_pending_player_stains
        // tests: RespawnEngine.positional_scrub_matches_pending_stain_location
        if (corpse != nullptr &&
            stain_matches_corpse_identity(stain, corpse) &&
            stain->team_num() == corpse->team_num() &&
            stain->floor() == corpse->floor() &&
            stain->xpos() == corpse->xpos() &&
            stain->ypos() == corpse->ypos())
        {
            return true;
        }
    }
    return false;
}

// True when this stain pairs with a dead body other than the firing corpse
// that is still in the world. At fire time such a neighbor is either a
// queued corpse mid-countdown (the dead sweep holds it for its entry) or a
// fighter that fell this very tick (mode_run_tick fires the timers before
// on_mode_tick can schedule it; classic mode 2 orders its scan the same
// way), and both keep their stain until their own fire. Pairing is the
// pending rule's: identity, team, floor, exact corner.
[[nodiscard]] bool stain_belongs_to_another_corpse(const GameWorld& world,
                                                   const walker* stain,
                                                   const walker* firing)
{
    // rule: When a respawn fires and clears its corpse's stain, a neighbouring dead fighter's stain
    //   is kept for that fighter's own countdown, whether the neighbour is already queued or fell
    //   this very tick (before the mode could schedule it).
    // tests: RespawnEngine.ai_fire_does_not_consume_an_adjacent_pending_ai_stain
    // tests: RespawnEngine.ai_fire_leaves_the_stain_of_a_neighbor_that_died_this_tick
    for (const auto& uptr : world.oblist)
    {
        const walker* other = uptr.get();
        // rule: When a respawn fires and clears its corpse's stain, a neighbouring dead fighter's
        //   stain is kept for that fighter's own countdown, whether the neighbour is already queued
        //   or fell this very tick (before the mode could schedule it).
        // tests: RespawnEngine.ai_fire_does_not_consume_an_adjacent_pending_ai_stain
        // tests: RespawnEngine.ai_fire_leaves_the_stain_of_a_neighbor_that_died_this_tick
        if (other != nullptr && other != firing && other->dead() &&
            other->query_order() == Order::Living &&
            stain_matches_corpse_identity(stain, other) &&
            stain->team_num() == other->team_num() &&
            stain->floor() == other->floor() &&
            stain->xpos() == other->xpos() &&
            stain->ypos() == other->ypos())
        {
            return true;
        }
    }
    return false;
}

// LIFE_GEM hearts are retired as soon as any corpse is scheduled: repeated
// respawns must not farm the time-limit tiebreaker (gems only ever drop for
// myguy corpses, so that arm is a no-op for AI). Every fighter's stain —
// player and AI alike — remains during the countdown for cleric raises and
// the visible corpse. The fire paths scrub both drops before reviving the
// player (or cancelling for a live duplicate) or fielding the AI
// replacement, so no stale stain survives the queue.
void scrub_corpse_drops_in(const GameWorld& world,
                           const GameWorld::EntityList& list,
                           const walker* corpse, CorpseDropScrub scrub)
{
    // rule: Scheduling a respawn immediately removes the corpse's LIFE_GEM (so respawn cycles
    //   cannot farm the time-limit tiebreaker) but leaves its bloodstain for the countdown (visible
    //   corpse, cleric raises); firing (or cancelling) removes both. Only drops on the corpse's own
    //   team and floor are touched.
    // tests: RespawnEngine.scheduling_scrubs_the_corpse_life_gem
    // tests: RespawnEngine.player_corpse_stain_survives_until_the_respawn_fires
    // tests: RespawnEngine.ai_corpse_stain_survives_until_the_respawn_fires
    // tests: RespawnEngine.respawn_fire_scrubs_only_the_corpses_floor
    // tests: SemRespawn.a_respawn_fire_leaves_another_teams_drop
    for (const auto& uptr : list)
    {
        walker* fx = uptr.get();
        // rule: Scheduling a respawn immediately removes the corpse's LIFE_GEM (so respawn cycles
        //   cannot farm the time-limit tiebreaker) but leaves its bloodstain for the countdown
        //   (visible corpse, cleric raises).
        // tests: RespawnEngine.scheduling_scrubs_the_corpse_life_gem
        // tests: RespawnEngine.player_corpse_stain_survives_until_the_respawn_fires
        // tests: RespawnEngine.ai_corpse_stain_survives_until_the_respawn_fires
        // tests: RespawnEngine.respawn_fire_scrubs_only_the_corpses_floor
        // tests: SemRespawn.a_respawn_fire_leaves_another_teams_drop
        if (fx == nullptr || fx->dead() ||
            fx->query_order() != Order::Treasure ||
            (fx->family() != FAMILY_LIFE_GEM &&
             (scrub != CorpseDropScrub::All ||
              fx->family() != FAMILY_STAIN)) ||
            fx->team_num() != corpse->team_num() ||
            fx->floor() != corpse->floor())
        {
            continue;
        }
        // Player stains carry a copied guy: pair them by the same identity
        // triple that guards duplicate revives. An AI corpse may only sweep
        // an AI stain, so a nearby player's queued corpse is never collateral.
        // rule: A respawn only clears its own corpse's bloodstain.
        // tests: RespawnEngine.nearby_player_respawns_scrub_only_their_own_stain
        // tests: SemRespawn.a_respawn_fire_scrubs_only_a_stain_of_its_own_identity
        if (fx->family() == FAMILY_STAIN &&
            !stain_matches_corpse_identity(fx, corpse))
        {
            continue;
        }
        // Guy-less stains all match each other, so identity alone cannot
        // shield a NEIGHBORING fighter's stain from this fire. Its body
        // can: a dead neighbor still in the world — queued mid-countdown,
        // or fallen this very tick with no entry yet (the death scan and
        // the mode's schedule_dead run after the timers) — keeps its stain
        // until its own fire. The firing corpse itself is excluded, so its
        // own stain still gets scrubbed.
        // rule: When a respawn fires and clears its corpse's stain, a neighbouring dead fighter's
        //   stain is kept for that fighter's own countdown, whether the neighbour is already queued
        //   or fell this very tick (before the mode could schedule it).
        // tests: RespawnEngine.ai_fire_does_not_consume_an_adjacent_pending_ai_stain
        // tests: RespawnEngine.ai_fire_leaves_the_stain_of_a_neighbor_that_died_this_tick
        if (fx->family() == FAMILY_STAIN &&
            stain_belongs_to_another_corpse(world, fx, corpse))
        {
            continue;
        }
        // Compare sprite centers: the LIFE_GEM is center_on'd the corpse
        // (corner deltas reach 3px per axis for 16px families), the stain
        // shares the corpse's corner.
        const int dx = std::abs((fx->xpos() + fx->sizex() / 2) -
                                (corpse->xpos() + corpse->sizex() / 2));
        const int dy = std::abs((fx->ypos() + fx->sizey() / 2) -
                                (corpse->ypos() + corpse->sizey() / 2));
        // rule: A corpse's drops are recognised by position: the stain shares the corpse's corner
        //   and the gem is centred on it, so anything whose centre is within 8px (Manhattan) of the
        //   corpse's centre is that corpse's drop; a matching drop further away is left alone.
        // tests: RespawnEngine.player_corpse_stain_survives_until_the_respawn_fires
        // tests: RespawnEngine.scheduling_scrubs_the_corpse_life_gem
        // tests: SemRespawn.a_drop_more_than_eight_pixels_from_the_corpse_is_not_its_drop
        if (dx + dy <= 8)
            fx->set_dead(1);
    }
}

void scrub_corpse_drops(GameWorld& world, const walker* corpse,
                        CorpseDropScrub scrub)
{
    scrub_corpse_drops_in(world, world.fxlist, corpse, scrub);
    scrub_corpse_drops_in(world, world.oblist, corpse, scrub);
}

void schedule_respawn(GameWorld& world, walker* corpse)
{
    RespawnState& respawn = world.respawn;
    // rule: The respawn queue is capped (the snapshot serializer's limit). A new corpse at the cap
    //   evicts the OLDEST queued bot, firing it immediately (its replacement is fielded now instead
    //   of later) so the roster never shrinks; if only players are queued the newcomer is refused
    //   (players are never evicted for anyone). If the evicted bot's fire was blocked and re-queued
    //   itself, that retry is dropped instead of the newcomer. In a scripted mode the eviction fire
    //   never dispatches on_respawn (see below: only the timer fire does).
    // tests: RespawnEngine.queue_cap_evicts_oldest_ai_entry_for_a_player
    // tests: RespawnEngine.queue_full_of_players_drops_incoming_ai_entry
    // tests: ClassicRespawn.full_queue_blocked_evict_keeps_the_incoming_corpse
    // tests: SemRespawn.a_full_queue_refuses_a_player_and_fields_an_evicted_bot_at_once
    if (respawn.respawn_queue.size() >= static_cast<std::size_t>(kRespawnMaxQueueEntries))
    {
        const auto victim = std::find_if(
            respawn.respawn_queue.begin(), respawn.respawn_queue.end(),
            [](const RespawnEntry& entry) { return entry.kind == 1; });
        // rule: The respawn queue is capped (the snapshot serializer's limit).
        // tests: RespawnEngine.queue_cap_evicts_oldest_ai_entry_for_a_player
        // tests: RespawnEngine.queue_full_of_players_drops_incoming_ai_entry
        // tests: ClassicRespawn.full_queue_blocked_evict_keeps_the_incoming_corpse
        // tests: SemRespawn.a_full_queue_refuses_a_player_and_fields_an_evicted_bot_at_once
        if (victim == respawn.respawn_queue.end())
            return;
        // Fire the evicted bot immediately instead of dropping it: the dead
        // sweep has been holding its corpse for this entry, so a plain erase
        // would hand the body to the end-of-tick sweep with its stain
        // stranded — and would shrink the roster forever. The fire disposes
        // of the held body and fields the replacement in one step.
        // Scripted modes take the RNG-free classic fire; NO on_respawn
        // dispatch here — scheduling can run inside a Lua binding call, and
        // a nested hook dispatch from an eviction is a re-entrancy the
        // engine does not owe (documented Phase-I behavior).
        const RespawnEntry evicted = *victim;
        respawn.respawn_queue.erase(victim);
        classic_fire_respawn(world, evicted);
        // A blocked classic AI fire re-enqueues itself: if the queue is full
        // again, drop that just-appended retry rather than the incoming
        // corpse. Either way one AI entry is permanently lost (a non-myguy
        // corpse with no pending entry is released to the end-of-tick sweep
        // and never re-scanned), but the retry's spot is currently occupied
        // while the incoming corpse's may be clear — and the cap must hold
        // for the snapshot serializer.
        // rule: The respawn queue is capped (the snapshot serializer's limit).
        // tests: RespawnEngine.queue_cap_evicts_oldest_ai_entry_for_a_player
        // tests: RespawnEngine.queue_full_of_players_drops_incoming_ai_entry
        // tests: ClassicRespawn.full_queue_blocked_evict_keeps_the_incoming_corpse
        // tests: SemRespawn.a_full_queue_refuses_a_player_and_fields_an_evicted_bot_at_once
        if (respawn.respawn_queue.size() >=
            static_cast<std::size_t>(kRespawnMaxQueueEntries))
        {
            respawn.respawn_queue.pop_back();
        }
    }

    RespawnEntry entry;
    // rule: A queued respawn remembers whether the dead was a hero (revived as the same walker) or
    //   a bot (replaced by a fresh walker), its TRUE team (charm is broken by death), its family
    //   and its level.
    // tests: RespawnEngine.player_corpse_revives_in_place_with_control_preserved
    // tests: RespawnEngine.ai_respawn_spawns_fresh_walker_of_same_family_level_team
    // tests: SemRespawn.a_charmed_corpse_is_queued_on_its_true_team
    entry.kind = (corpse->myguy != nullptr) ? 0 : 1;
    // A charmed corpse still wears the charmer's team_num; respawn breaks the
    // charm and the walker rejoins its true team.
    entry.team = (corpse->real_team_num() != 255) ? corpse->real_team_num()
                                                  : corpse->team_num();
    entry.family = static_cast<std::uint8_t>(corpse->family());
    const std::int32_t level =
        (corpse->stats() != nullptr) ? corpse->stats()->level() : 1;
    entry.level = static_cast<std::uint8_t>(std::clamp<std::int32_t>(level, 1, 255));
    entry.ticks_left = respawn.respawn_ticks;
    entry.walker_entity_id = corpse->entity_id();
    // Classic respawn location: the corpse's recorded spawn point when set,
    // else where it fell. CTF fire paths ignore these (anchor rotation stays
    // authoritative for CTF).
    // rule: A respawn goes back to the walker's recorded (level-authored or inherited) spawn point
    //   and floor when it has one, else to where it fell.
    // tests: ClassicRespawn.hero_scheduled_on_death_and_revives_at_spawn_point
    // tests: ClassicRespawn.entry_records_death_position_without_a_spawn_point
    // tests: SemRespawn.a_respawn_is_queued_on_the_floor_of_its_spawn_point_or_of_its_fall
    if (corpse->spawn_x() >= 0 && corpse->spawn_y() >= 0)
    {
        entry.x = corpse->spawn_x();
        entry.y = corpse->spawn_y();
        entry.floor = corpse->spawn_floor();
    }
    else
    {
        entry.x = corpse->xpos();
        entry.y = corpse->ypos();
        entry.floor = static_cast<std::uint8_t>(corpse->floor());
    }
    respawn.respawn_queue.push_back(entry);

    scrub_corpse_drops(world, corpse, CorpseDropScrub::LifeGemOnly);
}

} // namespace

// Per-tick countdown + fire (flat 1/tick decrement; the CTF control-point
// halving retired with the CTF engine — a mode wanting faster waits
// schedules with a shorter delay). Fire routing: scripted mode -> RNG-free
// classic placement + the on_respawn level hook (revive-in-place is the
// default when the mode registered none); classic modes -> classic
// placement. mode_run_tick calls this OUTSIDE any Lua dispatch, so the
// on_respawn dispatch never nests.
void respawn_run_timers(GameWorld& world)
{
    RespawnState& respawn = world.respawn;
    // plumbing: empty-queue early return in respawn_run_timers (no observable difference)
    if (respawn.respawn_queue.empty())
        return;

    const bool scripted = mode_scripted_active(world);
    // plumbing: queue walk with in-place erase; rules below
    for (std::size_t i = 0; i < respawn.respawn_queue.size();)
    {
        RespawnEntry& entry = respawn.respawn_queue[i];
        entry.ticks_left -= std::min<std::uint16_t>(1, entry.ticks_left);
        // rule: A respawn waits exactly its delay: the countdown drops by one per tick and the
        //   entry fires on the tick it reaches zero (a fresh corpse scheduled this tick waits its
        //   full delay).
        // tests: RespawnEngine.player_corpse_revives_in_place_with_control_preserved
        // tests: RespawnEngine.nearby_player_respawns_scrub_only_their_own_stain
        // tests: ClassicRespawn.hero_scheduled_on_death_and_revives_at_spawn_point
        if (entry.ticks_left > 0)
        {
            ++i;
            continue;
        }
        const RespawnEntry fired = entry;
        respawn.respawn_queue.erase(respawn.respawn_queue.begin() +
                                    static_cast<std::ptrdiff_t>(i));
        // rule: In a scripted mode a fired respawn calls the level's on_respawn hook with the
        //   revived walker (alive, at its recorded spot) so the mode can place it; a fire that
        //   revived nothing (blocked bot spot, duplicate hero, stale entry) calls no hook. Classic
        //   respawn modes never call Lua. This timer fire is the ONLY place on_respawn is
        //   dispatched: the cap-eviction fire in schedule_respawn (above)
        //   and every fire in respawn_flush_revive_all (below) call
        //   classic_fire_respawn directly, so a mode that repositions fighters in on_respawn never
        //   sees an evicted bot or a match-end flush.
        // tests: ModeTick.respawn_fires_in_place_and_dispatches_hook
        // tests: SemRespawn.on_respawn_runs_only_for_a_scripted_fire_that_revived_someone
        // tests: SemRespawn.a_full_queue_refuses_a_player_and_fields_an_evicted_bot_at_once
        // tests: SemRespawn.the_win_flush_fields_queued_bots_and_restores_true_teams_without_lua
        if (scripted)
        {
            // rule: In a scripted mode a fired respawn calls the level's on_respawn hook with the
            //   revived walker (alive, at its recorded spot) so the mode can place it.
            // tests: ModeTick.respawn_fires_in_place_and_dispatches_hook
            // tests: SemRespawn.on_respawn_runs_only_for_a_scripted_fire_that_revived_someone
            // tests: SemRespawn.a_full_queue_refuses_a_player_and_fields_an_evicted_bot_at_once
            // tests: SemRespawn.the_win_flush_fields_queued_bots_and_restores_true_teams_without_lua
            if (walker* revived = classic_fire_respawn(world, fired))
                og::script::hooks::level_respawn(&world, revived);
        }
        else
            classic_fire_respawn(world, fired);
    }
}

bool classic_respawn_active(const GameWorld& world)
{
    // Scripted (TYPE_SCRIPTED) worlds with an ACTIVE mode own their
    // respawns through the Lua surface; a scripted map whose mode failed
    // activation falls to classic rules, including these.
    // rule: The difficulty-submenu respawn modes (1 Heroes, 2 Everyone, 3 Team 1 Heroes) run only
    //   on worlds not owned by an active scripted mode; a scripted map whose mode failed activation
    //   falls back to them; mode 0 is off.
    // tests: ClassicRespawn.predicate_and_suppression_truth_table
    // tests: ClassicRespawn.scripted_active_worlds_ignore_respawn_mode
    // tests: SemRespawn.respawn_mode_off_leaves_a_dead_hero_dead
    return !mode_scripted_active(world) &&
           world.respawn_mode >= kRespawnModeHeroes &&
           world.respawn_mode <= kRespawnModeTeamOneHeroes;
}

bool respawn_retains_player_control(const GameWorld& world,
                                    const walker* control)
{
    // rule: A dead hero keeps its player's seat (the client is not handed another body) while a
    //   respawn will bring it back: in a scripted mode only while its entry is pending and the
    //   match is undecided; in classic respawn modes whenever the mode respawns that hero (mode 3
    //   only team 0) and its character is not walking elsewhere.
    // tests: RespawnEngine.retains_player_control_through_pending_scripted_entry
    // tests: ClassicRespawn.team_one_heroes_mode_filters_corpses_and_control_retention
    // tests: SemRespawn.only_a_dead_hero_whose_character_walks_nowhere_else_holds_its_seat
    if (control == nullptr || !control->dead() || control->myguy == nullptr ||
        control->query_order() != Order::Living)
    {
        return false;
    }

    // plumbing: the team is computed and discarded ((void)team); the seat check reads no team.
    const unsigned char team = control->real_team_num() != 255
        ? control->real_team_num()
        : control->team_num();
    (void)team;
    // Scripted arm: a mode-scheduled respawn (Lua owns eligibility via
    // og.respawn_schedule) keeps its seat while the entry is pending and the
    // match is undecided — the retired CTF arm generalized.
    // rule: A dead hero keeps its player's seat (the client is not handed another body) while a
    //   respawn will bring it back.
    // tests: RespawnEngine.retains_player_control_through_pending_scripted_entry
    // tests: ClassicRespawn.team_one_heroes_mode_filters_corpses_and_control_retention
    // tests: SemRespawn.only_a_dead_hero_whose_character_walks_nowhere_else_holds_its_seat
    if (mode_scripted_active(world))
    {
        return !world.mode.win_latched &&
               already_scheduled(world.respawn, control->entity_id()) &&
               !live_duplicate_exists(world, control);
    }
    // rule: A dead hero keeps its player's seat (the client is not handed another body) while a
    //   respawn will bring it back.
    // tests: RespawnEngine.retains_player_control_through_pending_scripted_entry
    // tests: ClassicRespawn.team_one_heroes_mode_filters_corpses_and_control_retention
    // tests: SemRespawn.only_a_dead_hero_whose_character_walks_nowhere_else_holds_its_seat
    if (!classic_respawn_active(world))
        return false;
    const bool mode_allows_hero =
        world.respawn_mode != kRespawnModeTeamOneHeroes || team == 0;
    return mode_allows_hero && !live_duplicate_exists(world, control);
}

bool respawn_suppress_team_wipe_endgame(const GameWorld& world,
                                        short wiped_team)
{
    // An undecided scripted match: wipes never end the level — the mode's
    // Lua owns the end, and respawns give the wiped team a way back in.
    // rule: A team being wiped out does not end the level while respawns can bring it back: always
    //   during an undecided scripted match; in classic modes 1 and 2 for every team; in mode 3 only
    //   for team 0 (another team's wipe stays terminal).
    // tests: ClassicRespawn.predicate_and_suppression_truth_table
    // tests: RespawnEngine.team_wipe_suppression_tracks_match_lifecycle
    // tests: SemRespawn.an_undecided_scripted_match_survives_a_team_wipe
    if (mode_scripted_active(world) && !world.mode.win_latched)
        return true;
    if (!classic_respawn_active(world))
        return false;
    // Modes 1 and 2 respawn heroes on every team. Mode 3 protects only the
    // player-facing Team 1 (internal team 0); another team's wipe must remain
    // terminal instead of being swallowed into an unwinnable empty seat.
    return world.respawn_mode != kRespawnModeTeamOneHeroes ||
           wiped_team < 0 || wiped_team == 0;
}

namespace {

// Floor-explicit spawn probe: classic respawn targets can sit on any floor,
// while spawn_spot_clear probes the walker's own grid floor and the floor-0
// obmap buckets (fine for CTF anchors, which carry no floor channel).
[[nodiscard]] bool classic_spot_clear(GameWorld& world, walker* w,
                                      short x, short y, int floor)
{
    // rule: A respawn target on an explicit floor is clear only when that floor's ground is
    //   passable for the walker and no blocking body on THAT floor overlaps the box (same blocker
    //   rule as above).
    // tests: ClassicRespawn.hero_revives_in_place_when_spawn_point_is_blocked
    // tests: ClassicRespawn.hero_revives_across_floors_at_its_spawn_floor
    // tests: RespawnEngine.exported_probes_hold_null_and_impassable_guards
    if (!world.query_grid_passable(x, y, w, floor))
        return false;
    // plumbing: null obmap answers clear; empty bucket skipped
    if (world.myobmap == nullptr)
        return true;
    // Same bucket sweep as spawn_spot_clear, keyed to the target floor.
    constexpr int kBucket = 32;
    // plumbing: floor-keyed bucket sweep; rule above
    for (int bx = x; bx < x + w->sizex() + kBucket; bx += kBucket)
    {
        // plumbing: floor-keyed bucket sweep; rule above
        for (int by = y; by < y + w->sizey() + kBucket; by += kBucket)
        {
            const std::list<walker*>* pile = world.myobmap->find_list(
                static_cast<short>(bx), static_cast<short>(by), floor);
            // plumbing: null obmap answers clear; empty bucket skipped
            if (pile == nullptr)
                continue;
            // plumbing: floor-keyed bucket sweep; rule above
            for (walker* other : *pile)
            {
                // rule: A respawn target on an explicit floor is clear only when that floor's
                //   ground is passable for the walker and no blocking body on THAT floor overlaps
                //   the box (same blocker rule as above).
                // tests: ClassicRespawn.hero_revives_in_place_when_spawn_point_is_blocked
                // tests: ClassicRespawn.hero_revives_across_floors_at_its_spawn_floor
                // tests: RespawnEngine.exported_probes_hold_null_and_impassable_guards
                if (spawn_spot_blocked_by(other, w, x, y))
                    return false;
            }
        }
    }
    return true;
}

// Classic player fire: revive the corpse in place (its position is already
// legal), then move it home when its own recorded spawn point is clear. No
// teleport() fallback — the classic engine never draws world.rng_.
// Returns the revived walker (nullptr when nothing revived) so the scripted
// fire arm can dispatch on_respawn.
walker* classic_fire_player_respawn(GameWorld& world,
                                    const RespawnEntry& entry)
{
    walker* w = world.find_by_id(entry.walker_entity_id);
    // rule: A hero's respawn fire is a no-op if the body is gone or already alive again (a cleric
    //   beat the timer: no second heal) but the entry is still consumed; before reviving it always
    //   clears the corpse's stain and gem, and if the character walks again elsewhere the revive is
    //   cancelled (the stain is still cleared).
    // tests: RespawnEngine.fired_entry_for_an_already_live_walker_is_a_no_op
    // tests: RespawnEngine.fire_time_live_duplicate_cancels_the_revive
    // tests: RespawnEngine.player_corpse_stain_survives_until_the_respawn_fires
    // tests: SemRespawn.a_fire_for_a_hero_already_alive_does_not_heal_it_again
    if (w == nullptr || !w->dead() || w->query_order() != Order::Living)
        return nullptr;
    scrub_corpse_drops(world, w, CorpseDropScrub::All);
    if (live_duplicate_exists(world, w))
        return nullptr;
    revive_player_walker(world, w, entry.team);
    // rule: A revived hero goes home to its spawn point (and spawn floor) when that spot is clear,
    //   otherwise it stands up where it fell; no random teleport.
    // tests: ClassicRespawn.hero_scheduled_on_death_and_revives_at_spawn_point
    // tests: ClassicRespawn.hero_revives_in_place_when_spawn_point_is_blocked
    // tests: ClassicRespawn.entry_records_death_position_without_a_spawn_point
    // tests: ClassicRespawn.hero_revives_across_floors_at_its_spawn_floor
    if (w->spawn_x() >= 0 && w->spawn_y() >= 0 &&
        classic_spot_clear(world, w, w->spawn_x(), w->spawn_y(),
                           static_cast<int>(w->spawn_floor())))
    {
        // set_floor BEFORE setxy: the obmap is floor-keyed.
        w->set_floor(static_cast<short>(w->spawn_floor()));
        w->setxy(w->spawn_x(), w->spawn_y());
    }
    ensure_obmap_registration(world, w);
    return w;
}

// Classic AI fire (mode 2): a fresh walker per the fire_ai_respawn recipe,
// placed at the entry's recorded coordinates. A blocked spot withdraws the
// probe walker and re-enqueues the entry on a short deterministic retry
// cadence instead of teleporting (no world.rng_ draw).
walker* classic_fire_ai_respawn(GameWorld& world, const RespawnEntry& entry)
{
    walker* w = world.add_ob(Order::Living, entry.family);
    // plumbing: add_ob failure (no loader data for the family) and null stats guards in
    //   classic_fire_ai_respawn
    if (w == nullptr)
        return nullptr;
    w->set_team_num(entry.team);
    w->set_real_team_num(255);
    if (w->stats() != nullptr)
        w->stats()->set_level(entry.level);
    w->set_difficulty(entry.level);
    // rule: A bot's respawn fields a FRESH walker of the same family, team (uncharmed) and level at
    //   its recorded spot when clear; the replacement inherits that spot as its own spawn point;
    //   the old body (if still dead) loses its stain and is retired; an entry naming a live walker
    //   never retires it.
    // tests: ClassicRespawn.mode2_respawns_fresh_ai_walker_at_authored_spot
    // tests: RespawnEngine.ai_corpse_stain_survives_until_the_respawn_fires
    // tests: RespawnEngine.fired_entry_for_a_live_walker_id_does_not_retire_it
    if (entry.x >= 0 && entry.y >= 0 &&
        classic_spot_clear(world, w, entry.x, entry.y,
                           static_cast<int>(entry.floor)))
    {
        w->set_floor(static_cast<short>(entry.floor));
        w->setxy(entry.x, entry.y);
        ensure_obmap_registration(world, w);
        // Keep the placement durable: the replacement inherits the spawn
        // point, so its own later deaths respawn at the authored spot.
        w->set_spawn_point(entry.x, entry.y, entry.floor);
        // The dude respawned: if the corpse it left is still there, delete
        // it — scrub its stain (a raised or eaten stain is already dead;
        // the scan then matches nothing) and retire the body the dead sweep
        // has been holding for this entry. The dead()/Living guards keep a
        // crafted or restored queue entry naming a live walker's id from
        // retiring that walker.
        walker* corpse = world.find_by_id(entry.walker_entity_id);
        // rule: A bot's respawn fields a FRESH walker of the same family, team (uncharmed) and
        //   level at its recorded spot when clear.
        // tests: ClassicRespawn.mode2_respawns_fresh_ai_walker_at_authored_spot
        // tests: RespawnEngine.ai_corpse_stain_survives_until_the_respawn_fires
        // tests: RespawnEngine.fired_entry_for_a_live_walker_id_does_not_retire_it
        if (corpse != nullptr && corpse->dead() &&
            corpse->query_order() == Order::Living)
        {
            scrub_corpse_drops(world, corpse, CorpseDropScrub::All);
            world.retire_corpse(corpse);
        }
        return w;
    }
    world.remove_ob(w);
    // rule: A bot whose respawn spot is occupied does not teleport: its probe body is withdrawn and
    //   the respawn retries on a short fixed cadence until the spot clears; if the queue is full
    //   the retry is lost.
    // tests: ClassicRespawn.mode2_blocked_ai_respawn_retries_until_clear
    // tests: SemRespawn.a_bot_entry_without_a_spot_fields_nothing_and_never_retries
    // unreachable: every caller (respawn_run_timers, the schedule_respawn eviction,
    //   respawn_flush_revive_all) removes the fired entry before classic_fire_ai_respawn runs, so
    //   the queue is below the cap at the retry; the negative-spot arm is asserted
    if (entry.x >= 0 && entry.y >= 0 &&
        world.respawn.respawn_queue.size() <
            static_cast<std::size_t>(kRespawnMaxQueueEntries))
    {
        RespawnEntry retry = entry;
        retry.ticks_left = kClassicBlockedRetryTicks;
        world.respawn.respawn_queue.push_back(retry);
    }
    return nullptr;
}

walker* classic_fire_respawn(GameWorld& world, const RespawnEntry& entry)
{
    // rule: Heroes are revived as the same walker (same entity id, keeps its guy and seat); bots
    //   are replaced by a fresh walker.
    // tests: RespawnEngine.player_corpse_revives_in_place_with_control_preserved
    // tests: RespawnEngine.ai_respawn_spawns_fresh_walker_of_same_family_level_team
    if (entry.kind == 0)
        return classic_fire_player_respawn(world, entry);
    return classic_fire_ai_respawn(world, entry);
}

// Classic scheduling eligibility: every dead hero (myguy) for mode 1, Team 1
// heroes only for mode 3, plus heroes and unowned AI livings WITH a recorded
// (level-authored) spawn point for mode 2. No team_active / score-team
// filters: classic levels field arbitrary teams. Generator spawns and summons
// stay the owner's business even after the owner dies — clear_stale_cross_refs
// nulls owner() the tick the owner dies, so the owner check alone would adopt
// orphans as permanent respawners; the spawn_x() gate keeps "endless battle"
// scoped to walkers with an authored placement (classic AI respawn copies
// inherit theirs).
[[nodiscard]] bool classic_respawn_corpse_eligible(GameWorld& world, walker* w)
{
    // rule: Which corpses the classic respawn modes bring back: mode 1 every dead hero; mode 3 only
    //   Team 1 (internal team 0) heroes, judged by true team; mode 2 heroes plus ownerless bots
    //   that have a level-authored (or inherited) spawn point. Summons, generator spawns and
    //   runtime spawns without a placement stay dead; a corpse is scheduled once, and never while
    //   its character walks again.
    // tests: ClassicRespawn.team_one_heroes_mode_filters_corpses_and_control_retention
    // tests: ClassicRespawn.mode1_does_not_respawn_unowned_ai_even_when_authored
    // tests: ClassicRespawn.generator_owned_and_summoned_walkers_never_respawn
    // tests: ClassicRespawn.runtime_spawns_without_authored_placement_never_respawn
    // tests: ClassicRespawn.live_duplicate_hero_corpse_is_never_rescheduled
    // tests: ClassicRespawn.hero_scheduled_on_death_and_revives_at_spawn_point
    if (w == nullptr || !w->dead() || w->query_order() != Order::Living)
        return false;
    const std::uint8_t team = w->real_team_num() != 255
        ? w->real_team_num()
        : w->team_num();
    const bool hero_eligible =
        w->myguy != nullptr &&
        (world.respawn_mode != kRespawnModeTeamOneHeroes || team == 0);
    const bool ai_eligible =
        world.respawn_mode == kRespawnModeEveryone &&
        w->myguy == nullptr && w->owner() == nullptr && w->spawn_x() >= 0;
    const bool eligible = hero_eligible || ai_eligible;
    if (!eligible)
        return false;
    if (already_scheduled(world.respawn, w->entity_id()))
        return false;
    if (live_duplicate_exists(world, w))
        return false;
    return true;
}

// Classic death scan: the same pre-sweep corpse scan as run_death_scan, with
// the classic eligibility above.
void classic_run_death_scan(GameWorld& world)
{
    // plumbing: oblist death scan; rule above
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        // rule: Which corpses the classic respawn modes bring back.
        // tests: ClassicRespawn.team_one_heroes_mode_filters_corpses_and_control_retention
        // tests: ClassicRespawn.mode1_does_not_respawn_unowned_ai_even_when_authored
        // tests: ClassicRespawn.generator_owned_and_summoned_walkers_never_respawn
        // tests: ClassicRespawn.runtime_spawns_without_authored_placement_never_respawn
        // tests: ClassicRespawn.live_duplicate_hero_corpse_is_never_rescheduled
        // tests: ClassicRespawn.hero_scheduled_on_death_and_revives_at_spawn_point
        if (!classic_respawn_corpse_eligible(world, w))
            continue;
        schedule_respawn(world, w);
    }
}

// The classic-effective team of a corpse / queue entry (schedule_respawn's
// charm-break rule), tested with the same strict team-color rule used by
// targeting, damage, and level completion.
[[nodiscard]] bool classic_team_hostile(const GameWorld& world,
                                        std::uint8_t team)
{
    return static_cast<short>(team) != world.my_team;
}

} // namespace

void classic_respawn_run_tick(GameWorld& world)
{
    // rule: The classic respawn delay is the lobby's requested value when it lies in the allowed
    //   range, else the default; on the tick the level ends every pending hero is revived at once
    //   (so the roster never saves a mid-respawn corpse) and pending bot entries are dropped.
    // tests: ClassicRespawn.requested_delay_resolution_clamps_out_of_range_values
    // tests: ClassicRespawn.end_of_level_revives_pending_heroes_and_clears_queue
    // tests: ClassicRespawn.end_driven_completion_flushes_and_drops_pending_ai
    // tests: ClassicRespawn.timeout_end_shape_flushes_pending_heroes
    // tests: ClassicRespawn.scripted_active_worlds_ignore_respawn_mode
    if (!classic_respawn_active(world))
        return;

    RespawnState& respawn = world.respawn;
    // Lazy config resolution. Idempotent, so re-running it every active tick
    // is safe: nothing else writes respawn_ticks on a classic level, and a
    // snapshot restore rewrites this and the requested knob coherently.
    const short requested = world.ctf_requested_respawn_ticks;
    // rule: The classic respawn delay is the lobby's requested value when it lies in the allowed
    //   range, else the default.
    // tests: ClassicRespawn.requested_delay_resolution_clamps_out_of_range_values
    // tests: ClassicRespawn.end_of_level_revives_pending_heroes_and_clears_queue
    // tests: ClassicRespawn.end_driven_completion_flushes_and_drops_pending_ai
    // tests: ClassicRespawn.timeout_end_shape_flushes_pending_heroes
    // tests: ClassicRespawn.scripted_active_worlds_ignore_respawn_mode
    respawn.respawn_ticks =
        (requested >= kClassicMinRespawnTicks &&
         requested <= kClassicMaxRespawnTicks)
            ? static_cast<std::uint16_t>(requested)
            : kRespawnDefaultTicks;

    // End of level (decided this same tick): revive every pending player
    // corpse immediately so roster persistence never sees a mid-respawn hero
    // — mirrors run_win_check's revive-all. Pending AI entries are dropped
    // with the cleared queue (the level is over).
    // rule: The classic respawn delay is the lobby's requested value when it lies in the allowed
    //   range, else the default.
    // tests: ClassicRespawn.requested_delay_resolution_clamps_out_of_range_values
    // tests: ClassicRespawn.end_of_level_revives_pending_heroes_and_clears_queue
    // tests: ClassicRespawn.end_driven_completion_flushes_and_drops_pending_ai
    // tests: ClassicRespawn.timeout_end_shape_flushes_pending_heroes
    // tests: ClassicRespawn.scripted_active_worlds_ignore_respawn_mode
    if (world.game_ended)
    {
        classic_respawn_flush_pending(world);
        return;
    }

    // Timers first, then the death scan, matching ctf_run_tick's phase order:
    // a fresh corpse always waits its full delay.
    respawn_run_timers(world);
    classic_run_death_scan(world);
}

void classic_respawn_flush_pending(GameWorld& world)
{
    // rule: At level end (including the synchronous exit-accept path) the death scan runs first so
    //   a hero who died on the winning tick is caught, then every pending hero is revived and every
    //   pending bot entry is dropped; running it twice changes nothing.
    // tests: ClassicRespawn.flush_pending_covers_the_synchronous_exit_accept_shape
    // tests: ClassicRespawn.same_tick_mutual_kill_still_revives_the_hero
    // tests: ClassicRespawn.end_driven_completion_flushes_and_drops_pending_ai
    if (!classic_respawn_active(world))
        return;
    RespawnState& respawn = world.respawn;
    // Scan first, matching ctf_run_tick's death-scan-before-win-check order:
    // a hero dying on the very tick the level ends (a mutual kill with the
    // last foe, or a death the synchronous exit-accept path hasn't scanned
    // yet) gets its entry here and revives below. Timer values don't matter.
    classic_run_death_scan(world);
    // Swap the queue out, then fire the player (kind-0) entries from the
    // local copy: erase-before-fire, so a firing player's own stain is never
    // protected as "pending" by the scrub. Kind-0 fires never mutate the
    // queue, so it stays empty; pending AI entries drop with the swap (the
    // level is over).
    std::vector<RespawnEntry> pending;
    pending.swap(respawn.respawn_queue);
    for (const RespawnEntry& entry : pending)
    {
        // rule: At level end (including the synchronous exit-accept path) the death scan runs first
        //   so a hero who died on the winning tick is caught, then every pending hero is revived
        //   and every pending bot entry is dropped.
        // tests: ClassicRespawn.flush_pending_covers_the_synchronous_exit_accept_shape
        // tests: ClassicRespawn.same_tick_mutual_kill_still_revives_the_hero
        // tests: ClassicRespawn.end_driven_completion_flushes_and_drops_pending_ai
        if (entry.kind == 0)
            classic_fire_player_respawn(world, entry);
    }
}

bool classic_respawn_pending_hostile_foe(GameWorld& world)
{
    // rule: Under classic respawn the level is not won by extermination while a hostile fighter is
    //   coming back: a queued enemy bot (mode 2 only), a queued enemy hero, or an enemy corpse that
    //   died this very tick and will be queued. Hostility is the strict team-colour rule (team !=
    //   my_team), unchanged by seat/allied mode. Corpses that will never respawn do not hold the
    //   win.
    // tests: ClassicRespawn.pending_hostile_foe_truth_table
    // tests: ClassicRespawn.mode2_pending_hostile_ai_blocks_extermination_win
    // tests: ClassicRespawn.runtime_spawns_without_authored_placement_never_respawn
    if (!classic_respawn_active(world))
        return false;
    // Entries scheduled on earlier ticks. Kind-1 (AI) entries only exist in
    // mode 2; kind-0 covers hostile heroes (other players' teams) too.
    for (const RespawnEntry& entry : world.respawn.respawn_queue)
    {
        // rule: Under classic respawn the level is not won by extermination while a hostile fighter
        //   is coming back.
        // tests: ClassicRespawn.pending_hostile_foe_truth_table
        // tests: ClassicRespawn.mode2_pending_hostile_ai_blocks_extermination_win
        // tests: ClassicRespawn.runtime_spawns_without_authored_placement_never_respawn
        if (entry.kind == 1 &&
            world.respawn_mode == kRespawnModeEveryone &&
            classic_team_hostile(world, entry.team))
            return true;
        // rule: Under classic respawn the level is not won by extermination while a hostile fighter
        //   is coming back.
        // tests: ClassicRespawn.pending_hostile_foe_truth_table
        // tests: ClassicRespawn.mode2_pending_hostile_ai_blocks_extermination_win
        // tests: ClassicRespawn.runtime_spawns_without_authored_placement_never_respawn
        if (entry.kind == 0 && classic_team_hostile(world, entry.team))
            return true;
    }
    // Corpses that died this very tick: the death scan runs AFTER the
    // completion decision, so the last foe's corpse has no entry yet.
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        // rule: Under classic respawn the level is not won by extermination while a hostile fighter
        //   is coming back.
        // tests: ClassicRespawn.pending_hostile_foe_truth_table
        // tests: ClassicRespawn.mode2_pending_hostile_ai_blocks_extermination_win
        // tests: ClassicRespawn.runtime_spawns_without_authored_placement_never_respawn
        if (!classic_respawn_corpse_eligible(world, w))
            continue;
        const std::uint8_t team = (w->real_team_num() != 255)
                                      ? w->real_team_num()
                                      : w->team_num();
        // rule: Under classic respawn the level is not won by extermination while a hostile fighter
        //   is coming back.
        // tests: ClassicRespawn.pending_hostile_foe_truth_table
        // tests: ClassicRespawn.mode2_pending_hostile_ai_blocks_extermination_win
        // tests: ClassicRespawn.runtime_spawns_without_authored_placement_never_respawn
        if (classic_team_hostile(world, team))
            return true;
    }
    return false;
}

// ===========================================================================
// Mode-agnostic respawn entry points (respawn/respawn_state.h). Serve the
// scripted-mode (TYPE_SCRIPTED) engine and the og.respawn_* bindings; the
// CTF and classic engines above keep their own routes through the same
// shared helpers. Everything here is RNG-free.
// ===========================================================================

void respawn_scan_anchors(GameWorld& world)
{
    // Team start markers, dead ones included (the level bootstrap kills the
    // markers it consumes for player placement). Idempotent: the arrays are
    // rebuilt from scratch.
    RespawnState& respawn = world.respawn;
    // plumbing: anchor array reset and oblist scan; rule below
    for (int t = 0; t < 4; ++t)
        respawn.anchor_count[t] = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        // rule: A scripted mode's respawn anchors per team are the level's team start markers
        //   (consumed/dead ones included), in list order, teams 0-3 only, up to the per-team cap;
        //   markers of other teams and extras beyond the cap are ignored.
        // tests: ModeTick.stage_init_activates_without_a_tick
        // tests: ModeBindings.respawn_anchors_read_back_and_validate
        // tests: SemRespawn.respawn_anchors_take_score_team_markers_up_to_the_cap
        if (w == nullptr || w->query_order() != Order::Special ||
            w->family() != FAMILY_RESERVED_TEAM)
        {
            continue;
        }
        const unsigned char team = w->team_num();
        if (!is_score_team(team) ||
            respawn.anchor_count[team] >= kRespawnMaxAnchorsPerTeam)
            continue;
        respawn.anchor_x[team][respawn.anchor_count[team]] = w->xpos();
        respawn.anchor_y[team][respawn.anchor_count[team]] = w->ypos();
        respawn.anchor_count[team]++;
    }
}

void respawn_flush_revive_all(GameWorld& world)
{
    // First: fire every queued entry through the RNG-free classic paths. A
    // blocked AI fire re-enqueues a retry; swapping the queue out first
    // keeps this loop bounded, and the trailing clear drops those retries
    // (the match is decided — nothing will run their timers meaningfully).
    RespawnState& respawn = world.respawn;
    std::vector<RespawnEntry> pending;
    pending.swap(respawn.respawn_queue);
    // rule: When a scripted match is decided every queued respawn fires at once (heroes revived,
    //   bots REPLACED with fresh walkers) and every other dead hero is revived in place on its true
    //   team, so the roster merge never sees a corpse; a hero whose character walks again is not
    //   revived but its stain is cleared. The flush never dispatches on_respawn, and any
    //   blocked-bot retry it pushes is dropped by the trailing clear.
    // tests: RespawnEngine.win_latch_flush_revives_all_player_corpses
    // tests: RespawnEngine.win_latch_flush_revives_an_unqueued_player_and_scrubs_stain
    // tests: RespawnEngine.win_latch_flush_scrubs_an_unqueued_duplicate_corpse
    // tests: SemRespawn.the_win_flush_fields_queued_bots_and_restores_true_teams_without_lua
    // unreachable: FFA and Mutant schedule every band hero at its death (the cap evicts bots
    //   first), so the flush never meets an unqueued band hero (triaged unreachable); every other
    //   missing arm is asserted
    for (const RespawnEntry& entry : pending)
        classic_fire_respawn(world, entry);
    respawn.respawn_queue.clear();

    // Then: revive still-unqueued eligible player corpses in place (a hero
    // dying the very tick the latch arms has no entry yet) — the CTF
    // win-check revive-all semantics with classic in-place placement. A
    // corpse that died charmed revives on its recorded true team.
    // rule: When a scripted match is decided every queued respawn fires at once (heroes revived,
    //   bots REPLACED with fresh walkers) and every other dead hero is revived in place on its true
    //   team, so the roster merge never sees a corpse.
    // tests: RespawnEngine.win_latch_flush_revives_all_player_corpses
    // tests: RespawnEngine.win_latch_flush_revives_an_unqueued_player_and_scrubs_stain
    // tests: RespawnEngine.win_latch_flush_scrubs_an_unqueued_duplicate_corpse
    // tests: SemRespawn.the_win_flush_fields_queued_bots_and_restores_true_teams_without_lua
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        // rule: When a scripted match is decided every queued respawn fires at once (heroes
        //   revived, bots REPLACED with fresh walkers) and every other dead hero is revived in
        //   place on its true team, so the roster merge never sees a corpse.
        // tests: RespawnEngine.win_latch_flush_revives_all_player_corpses
        // tests: RespawnEngine.win_latch_flush_revives_an_unqueued_player_and_scrubs_stain
        // tests: RespawnEngine.win_latch_flush_scrubs_an_unqueued_duplicate_corpse
        // tests: SemRespawn.the_win_flush_fields_queued_bots_and_restores_true_teams_without_lua
        if (w == nullptr || !w->dead() ||
            w->query_order() != Order::Living || w->myguy == nullptr)
            continue;
        scrub_corpse_drops(world, w, CorpseDropScrub::All);
        // rule: When a scripted match is decided every queued respawn fires at once (heroes
        //   revived, bots REPLACED with fresh walkers) and every other dead hero is revived in
        //   place on its true team, so the roster merge never sees a corpse.
        // tests: RespawnEngine.win_latch_flush_revives_all_player_corpses
        // tests: RespawnEngine.win_latch_flush_revives_an_unqueued_player_and_scrubs_stain
        // tests: RespawnEngine.win_latch_flush_scrubs_an_unqueued_duplicate_corpse
        // tests: SemRespawn.the_win_flush_fields_queued_bots_and_restores_true_teams_without_lua
        if (live_duplicate_exists(world, w))
            continue;
        const unsigned char raw_team = (w->real_team_num() != 255)
            ? w->real_team_num()
            : w->team_num();
        // rule: When a scripted match is decided every queued respawn fires at once (heroes
        //   revived, bots REPLACED with fresh walkers) and every other dead hero is revived in
        //   place on its true team, so the roster merge never sees a corpse.
        // tests: RespawnEngine.win_latch_flush_revives_all_player_corpses
        // tests: RespawnEngine.win_latch_flush_revives_an_unqueued_player_and_scrubs_stain
        // tests: RespawnEngine.win_latch_flush_scrubs_an_unqueued_duplicate_corpse
        // tests: SemRespawn.the_win_flush_fields_queued_bots_and_restores_true_teams_without_lua
        const int team = is_score_team(raw_team) ? raw_team : 0;
        revive_player_walker(world, w, team);
        ensure_obmap_registration(world, w);
    }
}

bool respawn_schedule_corpse(GameWorld& world, walker* corpse,
                             int ticks_override)
{
    // rule: og.respawn_schedule (the scripted modes' respawn request) accepts only a dead living
    //   body not already pending and whose character is not walking again; it reports failure when
    //   the queue is full of heroes; an explicit delay replaces the default.
    // tests: ModeBindings.respawn_schedule_pending_and_counts
    // tests: RespawnEngine.live_duplicate_character_cancels_the_revive
    // tests: RespawnEngine.player_corpse_revives_in_place_with_control_preserved
    // tests: SemRespawn.a_respawn_request_fails_on_a_queue_full_of_heroes_and_clamps_its_delay
    if (corpse == nullptr || !corpse->dead() ||
        corpse->query_order() != Order::Living)
        return false;
    if (already_scheduled(world.respawn, corpse->entity_id()))
        return false;
    if (live_duplicate_exists(world, corpse))
        return false;
    schedule_respawn(world, corpse);
    if (!already_scheduled(world.respawn, corpse->entity_id()))
    {
        // A full queue of player entries refuses new work (schedule_respawn
        // evicts bots only).
        return false;
    }
    if (ticks_override > 0)
    {
        world.respawn.respawn_queue.back().ticks_left =
            static_cast<std::uint16_t>(
                std::min(ticks_override, 65535));
    }
    return true;
}

bool respawn_pending_for(const GameWorld& world, const walker* w)
{
    // plumbing: null walker guard in respawn_pending_for (asserted by
    //   exported_probes_hold_null_and_impassable_guards)
    if (w == nullptr)
        return false;
    return already_scheduled(world.respawn, w->entity_id());
}

int respawn_pending_count(const GameWorld& world, int team)
{
    int count = 0;
    // rule: og.respawn_pending_count(team) counts the queued respawns of that true team (heroes and
    //   bots).
    // tests: ModeBindings.respawn_schedule_pending_and_counts
    for (const RespawnEntry& entry : world.respawn.respawn_queue)
    {
        // rule: og.respawn_pending_count(team) counts the queued respawns of that true team (heroes
        //   and bots).
        // tests: ModeBindings.respawn_schedule_pending_and_counts
        if (static_cast<int>(entry.team) == team)
            count++;
    }
    return count;
}

bool respawn_spot_clear(GameWorld& world, walker* w, short x, short y,
                        int floor)
{
    // rule: The exported spawn probe (og.spawn_spot_clear) asks the walker's own floor when no
    //   floor is given, and the named floor otherwise; a null walker is never clear.
    // tests: RespawnEngine.exported_probes_hold_null_and_impassable_guards
    // tests: RespawnEngine.spawn_probes_make_no_empty_pile
    // tests: RespawnEngine.spawn_probe_rejects_doors_and_cross_bucket_blockers
    if (w == nullptr)
        return false;
    if (floor < 0)
        return spawn_spot_clear(world, w, x, y);
    return classic_spot_clear(world, w, x, y, floor);
}

namespace {

// The detached body both probes below ask with: a walker of (order,
// family) at (x, y) on `floor`, never in a list or the obmap. False for a
// family the loader cannot make.
[[nodiscard]] bool make_body_probe(GameWorld& world, walker& probe, Order order,
                                   std::int32_t family, short x, short y, int floor)
{
    const PixieData* data = world.configure_existing_entity(probe, order, family);
    // The loader stands a family 0 in for an id with no slot and a soldier in
    // for a living with no descriptor: any stand-in is "cannot make". (Since
    // #443 every id 0..255 is its own family byte.)
    // rule: Asking whether a body of a family fits somewhere answers no for a family the loader
    //   cannot make: an empty slot, an id outside the family byte, or a living with no family
    //   (never a soldier stand-in); the configured body must be the order and family asked for.
    // tests: QuietProbe.body_spot_clear_asks_the_placement_rule_with_the_bodys_box
    // tests: RespawnEngine.body_ground_clear_asks_the_grid_with_the_bodys_own_flags
    // tests: SemRespawn.a_body_probe_answers_no_for_a_family_slot_the_loader_cannot_make
    // tests: SemRespawn.a_body_probe_answers_no_for_an_id_past_the_family_byte_or_a_living_with_no_family
    if (data == nullptr || probe.query_order() != order ||
        static_cast<std::int32_t>(probe.family()) != family)
        return false;
    probe.set_data(*data);
    probe.set_floor(static_cast<short>(floor));
    probe.set_xpos(x);      // SimEntity's plain setters: never setxy, which
    probe.set_ypos(y);      // would register the probe in the obmap
    return true;
}

// The ground rule: the grid with the body's own box and flags, as
// spawn_spot_clear and classic_spot_clear ask it (the probe's floor is
// `floor`, so the two arms read the same grid).
[[nodiscard]] bool probe_ground_clear(GameWorld& world, walker& probe,
                                      short x, short y, int floor)
{
    return world.query_grid_passable(x, y, &probe, floor);
}

} // namespace

bool body_ground_clear(GameWorld& world, Order order, std::int32_t family,
                       short x, short y, int floor)
{
    walker probe(walker::WeaponProfileProbeTag{});
    // rule: Asking whether a body of a family fits somewhere answers no for a family the loader
    //   cannot make: an empty slot, an id outside the family byte, or a living with no family
    //   (never a soldier stand-in); the configured body must be the order and family asked for.
    // tests: QuietProbe.body_spot_clear_asks_the_placement_rule_with_the_bodys_box
    // tests: RespawnEngine.body_ground_clear_asks_the_grid_with_the_bodys_own_flags
    // tests: SemRespawn.a_body_probe_answers_no_for_a_family_slot_the_loader_cannot_make
    // tests: SemRespawn.a_body_probe_answers_no_for_an_id_past_the_family_byte_or_a_living_with_no_family
    if (!make_body_probe(world, probe, order, family, x, y, floor))
        return false;
    return probe_ground_clear(world, probe, x, y, floor);
}

// Probed with a detached walker (walker.h, WeaponProfileProbeTag: never in a
// list or the obmap, no entity id, no draw), through the eat-free placement
// rule every spawn uses (spawn_spot_blocked_by: the grid with the body's own
// flags, then livings, generators, doors, trees, boulders and placement
// blockers on exact boxes), and never onto a delayed spawn that has not
// entered the world yet. The ground is body_ground_clear's question, asked
// first; respawn_spot_clear asks the same grid again before its bodies.
bool body_spot_clear(GameWorld& world, Order order, std::int32_t family,
                     short x, short y, int floor)
{
    walker probe(walker::WeaponProfileProbeTag{});
    // rule: Asking whether a body of a family fits somewhere answers no for a family the loader
    //   cannot make: an empty slot, an id outside the family byte, or a living with no family
    //   (never a soldier stand-in); the configured body must be the order and family asked for.
    // tests: QuietProbe.body_spot_clear_asks_the_placement_rule_with_the_bodys_box
    // tests: RespawnEngine.body_ground_clear_asks_the_grid_with_the_bodys_own_flags
    // tests: SemRespawn.a_body_probe_answers_no_for_a_family_slot_the_loader_cannot_make
    // tests: SemRespawn.a_body_probe_answers_no_for_an_id_past_the_family_byte_or_a_living_with_no_family
    if (!make_body_probe(world, probe, order, family, x, y, floor))
        return false;
    // rule: A body of a family fits at a spot only when the ground takes that family's box and
    //   movement flags (a flyer fits over water, a walker does not), no blocking body overlaps it,
    //   and no delayed spawn that has not yet entered the world is waiting there; nothing is drawn
    //   and nothing is filed.
    // tests: RespawnEngine.body_ground_clear_asks_the_grid_with_the_bodys_own_flags
    // tests: QuietProbe.body_spot_clear_asks_the_placement_rule_with_the_bodys_box
    if (!probe_ground_clear(world, probe, x, y, floor))
        return false;
    // Reads the grid a second time: the same answer, and no draw.
    if (!respawn_spot_clear(world, &probe, x, y, floor))
        return false;
    return !world.dormant_occupies_spot(&probe);
}

bool body_spot_near(GameWorld& world, Order order, std::int32_t family,
                    short x, short y, int floor, int radius,
                    short& out_x, short& out_y)
{
    // rule: Placing a body near a spot takes the spot itself when it fits, else the nearest pixel
    //   ring position that fits, trying left, right, up, down, then the four diagonals at each
    //   distance; if nothing fits within the radius it answers no and leaves the outputs alone.
    // tests: QuietProbe.body_spot_near_tries_the_corner_then_the_ring_in_order
    if (body_spot_clear(world, order, family, x, y, floor))
    {
        out_x = x;
        out_y = y;
        return true;
    }
    static constexpr int kRing[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (int r = 1; r <= radius; ++r)
    {
        // rule: Placing a body near a spot takes the spot itself when it fits, else the nearest
        //   pixel ring position that fits, trying left, right, up, down, then the four diagonals at
        //   each distance.
        // tests: QuietProbe.body_spot_near_tries_the_corner_then_the_ring_in_order
        for (const auto& step : kRing)
        {
            const short sx = static_cast<short>(x + step[0] * r);
            const short sy = static_cast<short>(y + step[1] * r);
            // rule: Placing a body near a spot takes the spot itself when it fits, else the nearest
            //   pixel ring position that fits, trying left, right, up, down, then the four
            //   diagonals at each distance.
            // tests: QuietProbe.body_spot_near_tries_the_corner_then_the_ring_in_order
            if (body_spot_clear(world, order, family, sx, sy, floor))
            {
                out_x = sx;
                out_y = sy;
                return true;
            }
        }
    }
    return false;
}

void respawn_scrub_stains_at(GameWorld& world, short x, short y, int floor)
{
    // The corpse-relative rule of scrub_corpse_drops_in, keyed by position:
    // (x, y) is the corpse's top-left, treated as a 16px family footprint
    // (larger corpses' drops still land within the 8px Manhattan window in
    // practice — the stain shares the corner, the gem is center_on'd). No
    // team filter: the calling hook context decides which corpse to scrub.
    const auto scrub_list = [&](const GameWorld::EntityList& list) {
        // plumbing: fxlist/oblist walk in respawn_scrub_stains_at; rules below
        for (const auto& uptr : list)
        {
            walker* fx = uptr.get();
            // rule: A mode can clear a corpse's drops by position (og.respawn_scrub_stains_at):
            //   every live bloodstain and life gem whose centre is within 8px of a 16px body at
            //   that corner, on that floor (any floor when none is given), of any team, is removed,
            //   except a stain that belongs to a still-pending respawn.
            // tests: RespawnEngine.positional_scrub_preserves_pending_ai_stains
            // tests: RespawnEngine.positional_scrub_preserves_only_pending_player_stains
            // tests: RespawnEngine.positional_scrub_matches_pending_stain_location
            // tests: SemRespawn.a_positional_scrub_takes_gems_and_every_floor_when_none_is_named
            if (fx == nullptr || fx->dead() ||
                fx->query_order() != Order::Treasure ||
                (fx->family() != FAMILY_STAIN &&
                 fx->family() != FAMILY_LIFE_GEM))
            {
                continue;
            }
            if (floor >= 0 && static_cast<int>(fx->floor()) != floor)
                continue;
            const int dx = std::abs((fx->xpos() + fx->sizex() / 2) - (x + 8));
            const int dy = std::abs((fx->ypos() + fx->sizey() / 2) - (y + 8));
            if (dx + dy > 8)
                continue;
            // A mode may scrub a permanent corpse beside a queued fighter.
            // The positional API has no corpse identity, so preserve any
            // stain whose identity and exact corpse location resolve to a
            // pending respawn entry — player or AI.
            if (fx->family() == FAMILY_STAIN &&
                stain_belongs_to_pending_respawn(world, fx))
            {
                continue;
            }
            fx->set_dead(1);
        }
    };
    scrub_list(world.fxlist);
    scrub_list(world.oblist);
}

} // namespace og::sim
