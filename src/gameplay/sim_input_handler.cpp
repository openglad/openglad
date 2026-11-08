/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Sim-layer input processing: translates InputState into entity commands.
// Extracted from viewscreen::process_input() (G11) so entity-driving logic
// lives in the sim layer, not the render layer.

#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/core/constants.h>
#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/possession.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/sound_ids.h>
#include <openglad/core/test_trace.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

namespace {

// What a failed cast says, or nullptr when it says nothing: a corpse, a
// statless walker or a non-Living body are engine states the player cannot
// answer, so they stay silent rather than blaming the key.
const char* special_failure_message(walker::SpecialFailure why,
                                    const std::string& reason)
{
    // rule: A failed player special voices its reason only when the player can act on it (NOT
    //   ENOUGH MP, unavailable, disabled, the script's refusal); engine states (dead, statless,
    //   non-living, no failure) stay silent.
    // tests: SimInputUnit.sim_input_failed_special_cue_is_seat_addressed_and_throttled
    // tests: SimInputUnit.sim_input_specials_disabled_and_frozen_cues
    // tests: SimInputUnit.intelligence_refusals_use_one_private_cue
    // tests: SemSimInput.a_special_that_fails_on_a_non_living_body_stays_silent
    // unreachable: Dead (the entry check below returns or replaces a dead control), NoStats
    //   (every walker constructor builds stats_), None (walker::special never fails with None); the
    //   NotLiving sub-case is asserted
    switch (why)
    {
        case walker::SpecialFailure::NoMP:
        case walker::SpecialFailure::Unavailable:
        case walker::SpecialFailure::Disabled:
        case walker::SpecialFailure::ScriptDeclined:
            return reason.c_str();
        case walker::SpecialFailure::None:
        case walker::SpecialFailure::Dead:
        case walker::SpecialFailure::NoStats:
        case walker::SpecialFailure::NotLiving:
            break;
    }
    return nullptr;
}

// A seat-addressed cue with its clang, dropped while the throttle is warm.
// EVERY cue goes through here, press-debounced ones included: the debounce
// stops a held key, the throttle stops a mashed one from filling the feed
// with copies of a single line.
void emit_throttled_cue(SimInputDebounce& debounce, short player_num,
                        og::sim::SimEventLog* sim_events, const char* message)
{
    // rule: A seat hears at most one failure cue per throttle window (kSimCueThrottleTicks); a
    //   mashed key does not flood the feed.
    // tests: SimInputUnit.sim_input_failed_special_cue_is_seat_addressed_and_throttled
    // tests: SimInputUnit.sim_input_cue_throttle_drains_while_the_seat_is_dead
    // tests: SemSimInput.a_special_that_fails_on_a_non_living_body_stays_silent
    if (message == nullptr || debounce.cue_delay > 0)
        return;
    debounce.cue_delay = kSimCueThrottleTicks;
    og::sim::emit_sound(sim_events, SOUND_CLANG,
                        static_cast<std::int32_t>(player_num));
    og::sim::emit_notification(sim_events, message, 0,
                               static_cast<std::int32_t>(player_num));
}

// Cast a player special, and voice the reason when nothing happened.
//
// press_edge is false on the held-Special repeat. A script that declines
// because the family is still on its own cooldown is answering a key the
// player never pressed again, so ScriptDeclined stays silent there — the
// running special IS the answer.
bool player_cast_special(walker* control, SimInputDebounce& debounce,
                         short player_num, og::sim::SimEventLog* sim_events,
                         bool press_edge)
{
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
    // rule: A successful cast says nothing; a script refusal on the HELD repeat (the family still
    //   on its own cooldown) stays silent, the running special being the answer; any other failure
    //   is voiced.
    // tests: SimInputUnit.sim_input_successful_cast_is_never_called_a_failure
    // tests: SimInputUnit.sim_input_declined_cast_explains_blocked_path_on_a_press
    if (control->special(&why, &reason))
        return true;
    if (why == walker::SpecialFailure::ScriptDeclined && !press_edge)
        return false;
    emit_throttled_cue(debounce, player_num, sim_events,
                       special_failure_message(why, reason));
    return false;
}

} // namespace

bool sim_arm_belt_entry(const GameWorld& level, walker& control,
                        std::uint8_t belt_cast, bool press_edge,
                        SimInputDebounce& debounce, short player_num,
                        og::sim::SimEventLog* sim_events)
{
    const FamilyDescriptor* const descriptor = get_family_descriptor(
        static_cast<int>(static_cast<unsigned char>(control.family())));
    og::sim::belt::Entry entry;
    int opens_at = 0;
    const og::sim::belt::Refusal refusal = og::sim::belt::validate_cast(
        descriptor, control.stats()->level(), level.new_specials, belt_cast,
        &entry, &opens_at);
    // rule: New Ability Select belt: a valid belt key sets the slot and the shift from the entry; a
    //   slot the hero's level has not opened refuses with 'LEVEL N NEEDED', an empty or malformed
    //   key with 'NO ABILITY THERE', voiced only on the press (not the hold).
    // tests: SimInputHandler.belt_cast_sets_slot_and_alt_and_casts
    // tests: SimInputHandler.locked_slot_is_refused_with_a_cue_and_no_cast
    // tests: SimInputHandler.cast_nothing_voices_no_ability_there
    // tests: SimInputHandler.malformed_byte_is_refused_like_cast_nothing
    switch (refusal)
    {
        case og::sim::belt::Refusal::None:
            control.set_current_special(static_cast<char>(entry.slot));
            // rule: New Ability Select belt: a valid belt key sets the slot and the shift from the
            //   entry.
            // tests: SimInputHandler.belt_cast_sets_slot_and_alt_and_casts
            // tests: SimInputHandler.locked_slot_is_refused_with_a_cue_and_no_cast
            // tests: SimInputHandler.cast_nothing_voices_no_ability_there
            // tests: SimInputHandler.malformed_byte_is_refused_like_cast_nothing
            control.set_shifter_down(entry.alt ? 1 : 0);
            return true;
        case og::sim::belt::Refusal::Locked:
            // rule: New Ability Select belt: a valid belt key sets the slot and the shift from the
            //   entry.
            // tests: SimInputHandler.belt_cast_sets_slot_and_alt_and_casts
            // tests: SimInputHandler.locked_slot_is_refused_with_a_cue_and_no_cast
            // tests: SimInputHandler.cast_nothing_voices_no_ability_there
            // tests: SimInputHandler.malformed_byte_is_refused_like_cast_nothing
            if (press_edge)
            {
                const std::string cue = std::format("LEVEL {} NEEDED", opens_at);
                emit_throttled_cue(debounce, player_num, sim_events, cue.c_str());
            }
            return false;
        case og::sim::belt::Refusal::NothingThere:
            break;
    }
    if (press_edge)
        emit_throttled_cue(debounce, player_num, sim_events, "NO ABILITY THERE");
    return false;
}

walker* sim_find_next_control(GameWorld& level, short my_team)
{
    TRACE("sim_input", "find_next_control for team %d", my_team);

    // First look for a player character, not already controlled
    // plumbing: oblist scans of sim_find_next_control; the rules are below and
    //   below
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        // rule: A seat that needs a body takes first a player character (a hero with a company
        //   record) of its own team that is alive, awake, visible, unpossessed and not held by
        //   another seat (and, with Gameplay Fixes on, not a walker our side charmed).
        // tests: SimInputHandler.sim_find_next_control_player_first
        // tests: GameplayFixesSeats.a_seat_skips_a_walker_our_side_charmed
        if (w && !w->dead() && !w->dormant() && !w->hidden() &&
            w->possess_link() == 0 &&
            w->query_order() == Order::Living &&
            w->user() == -1 &&
            w->myguy &&
            w->team_num() == my_team &&
            og::sim::seat_may_claim(level, *w))
        {
            TRACE("sim_input", "found player character '%s'", w->stats()->name.c_str());
            return w;
        }
    }

    // Second, look for anyone on our team
    // plumbing: oblist scans of sim_find_next_control; the rules are above and
    //   below
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        // rule: With no player character left, a seat takes any eligible member of its own team
        //   (with Gameplay Fixes on, not a walker our side charmed: #354, #450); it never takes a
        //   walker of another team (the 2002 'ANYONE' pass is gone).
        // tests: SimInputHandler.sim_find_next_control_team_fallback, SimInputHandler.sim_find_next_control_none, SimInputHandler.sim_find_next_control_never_claims_another_team
        // tests: GameplayFixesSeats.a_seat_skips_a_walker_our_side_charmed, GameplayFixesSeats.a_dead_heros_seat_takes_a_charmed_foe_only_with_the_setting_off, GameplayFixesSeats.a_dead_heros_seat_takes_a_real_teammate_on_both_settings
        if (w && !w->dead() && !w->dormant() && !w->hidden() &&
            w->possess_link() == 0 &&
            w->query_order() == Order::Living &&
            w->user() == -1 &&
            w->team_num() == my_team &&
            og::sim::seat_may_claim(level, *w))
        {
            TRACE("sim_input", "found team member '%s'", w->stats()->name.c_str());
            return w;
        }
    }

    // The 2002 third pass announced itself with the wonderfully blunt
    // "Now try for ANYONE who's left alive." Keep the line as history, not
    // behavior: a seat may never fall through to another team. A foreign
    // company hero is an enemy, not an emergency replacement body.
    TRACE("sim_input", "found no one");
    return nullptr;
}

walker* sim_cycle_next_character(
    const std::list<std::unique_ptr<walker>>& oblist,
    walker* current,
    bool reverse,
    const std::function<bool(const walker*)>& pred)
{
    auto is_current = [current](const std::unique_ptr<walker>& p) { return p.get() == current; };

    // rule: Switch Character cycles through eligible bodies in object-list order after the current
    //   one, wrapping to the start; Shift cycles backward. A current body not in the list yields
    //   nothing.
    // tests: SimInputHandler.sim_input_switch_char_wraparound_forward_and_reverse
    // tests: SimInputHandler.sim_input_switch_char_forward_and_reverse_paths
    // tests: SemSimInput.switch_character_from_a_body_outside_the_list_finds_nobody
    if (!reverse)
    {
        // Get where we are in the list
        auto mine = std::find_if(oblist.begin(), oblist.end(), is_current);
        // rule: Switch Character cycles through eligible bodies in object-list order after the
        //   current one, wrapping to the start; Shift cycles backward.
        // tests: SimInputHandler.sim_input_switch_char_wraparound_forward_and_reverse
        // tests: SimInputHandler.sim_input_switch_char_forward_and_reverse_paths
        // tests: SemSimInput.switch_character_from_a_body_outside_the_list_finds_nobody
        if (mine == oblist.end())
            return nullptr;
        // Search from after current to end
        // plumbing: the four list scans of sim_cycle_next_character; rule at
        //   above
        for (auto e = std::next(mine); e != oblist.end(); ++e)
        {
            walker* w = e->get();
            // rule: Switch Character cycles through eligible bodies in object-list order after the
            //   current one, wrapping to the start; Shift cycles backward.
            // tests: SimInputHandler.sim_input_switch_char_wraparound_forward_and_reverse
            // tests: SimInputHandler.sim_input_switch_char_forward_and_reverse_paths
            // tests: SemSimInput.switch_character_from_a_body_outside_the_list_finds_nobody
            if (w && pred(w))
                return w;
        }
        // Wrap: search from begin to current
        for (auto e = oblist.begin(); e != mine; ++e)
        {
            walker* w = e->get();
            // rule: Switch Character cycles through eligible bodies in object-list order after the
            //   current one, wrapping to the start; Shift cycles backward.
            // tests: SimInputHandler.sim_input_switch_char_wraparound_forward_and_reverse
            // tests: SimInputHandler.sim_input_switch_char_forward_and_reverse_paths
            // tests: SemSimInput.switch_character_from_a_body_outside_the_list_finds_nobody
            if (w && pred(w))
                return w;
        }
    }
    else
    {
        auto mine = std::find_if(oblist.rbegin(), oblist.rend(), is_current);
        // rule: Switch Character cycles through eligible bodies in object-list order after the
        //   current one, wrapping to the start; Shift cycles backward.
        // tests: SimInputHandler.sim_input_switch_char_wraparound_forward_and_reverse
        // tests: SimInputHandler.sim_input_switch_char_forward_and_reverse_paths
        // tests: SemSimInput.switch_character_from_a_body_outside_the_list_finds_nobody
        if (mine == oblist.rend())
            return nullptr;
        // plumbing: the four list scans of sim_cycle_next_character; rule at
        //   above
        for (auto e = std::next(mine); e != oblist.rend(); ++e)
        {
            walker* w = e->get();
            // rule: Switch Character cycles through eligible bodies in object-list order after the
            //   current one, wrapping to the start; Shift cycles backward.
            // tests: SimInputHandler.sim_input_switch_char_wraparound_forward_and_reverse
            // tests: SimInputHandler.sim_input_switch_char_forward_and_reverse_paths
            // tests: SemSimInput.switch_character_from_a_body_outside_the_list_finds_nobody
            if (w && pred(w))
                return w;
        }
        // plumbing: the four list scans of sim_cycle_next_character; rule at
        //   above
        for (auto e = oblist.rbegin(); e != mine; ++e)
        {
            walker* w = e->get();
            // rule: Switch Character cycles through eligible bodies in object-list order after the
            //   current one, wrapping to the start; Shift cycles backward.
            // tests: SimInputHandler.sim_input_switch_char_wraparound_forward_and_reverse
            // tests: SimInputHandler.sim_input_switch_char_forward_and_reverse_paths
            // tests: SemSimInput.switch_character_from_a_body_outside_the_list_finds_nobody
            if (w && pred(w))
                return w;
        }
    }
    return nullptr;
}

void sim_advance_current_special(const GameWorld& level, walker& control)
{
    control.set_current_special(control.current_special() + 1);

    const int special_index = static_cast<int>(control.current_special());
    const FamilyDescriptor* const descriptor = get_family_descriptor(
        static_cast<int>(static_cast<unsigned char>(control.family())));
    // rule: Switch Special treats a slot outside the family's specials, or a family with no
    //   descriptor, as unnamed, so the cycle wraps there.
    // tests: SimInputHandler.sim_input_switch_special_wraps_and_debounces
    const char* const special_name =
        descriptor == nullptr || special_index < 0 ||
                special_index >= NUM_SPECIALS
            ? nullptr
            : og::sim::special_name(descriptor, special_index,
                                    level.new_specials);
    const bool special_missing =
        special_name == nullptr ||
        std::string_view(special_name) == "NONE";

    // rule: Switch Special steps to the next special and wraps to slot 1 past the last slot, at an
    //   unnamed (NONE) slot, or at a slot the hero's level has not opened.
    // tests: SimInputHandler.sim_input_switch_special_wraps_and_debounces
    // tests: SimInputHandler.sim_input_switch_special_valid_advance_and_shift_yell_default_branch
    // tests: ViewInputPaths.view_input_special_switch_cycles_the_current_special
    // tests: SimInputUnit.sim_input_r11_switch_special_yell_and_action_default
    // tests: CoverageMisc.final_r16_sim_input_switch_special_and_yell
    // tests: SpecialsView.cycle_skips_hidden_new_kit_slots
    // tests: KitCaptain.fresh_captain_has_howl_and_eat_corpse_from_level_1
    // tests: KitFaerie.four_slots_open_at_levels_1_4_7_10_and_none_when_off
    // tests: SemSimInput.switch_special_past_the_last_slot_wraps_to_slot_one
    if (special_index < 0 || special_index > (NUM_SPECIALS - 1)
        || special_missing
        || !og::sim::slot_open(descriptor, special_index, control.stats()->level())) // open by default when (control.current_special() - 1) * 3 + 1 <= level
        control.set_current_special(1);
}

walker* sim_switch_control(GameWorld& level, walker& control, walker* anchor,
                           short my_team, short player_num, bool reverse)
{
    // Unset our control
    // rule: Switching away from a hero hands it back to its AI (its own act type restored,
    //   unseated).
    // tests: SimInputUnit.sim_input_switch_char_on_the_claim_tick_cycles_from_the_claimed_hero
    // tests: SemSimInput.switching_away_from_another_seats_body_leaves_it_seated
    if (control.user() == player_num)
    {
        control.restore_act_type();
        control.set_user(-1);
    }

    auto filter = [&level, anchor, my_team, player_num](const walker* w) {
        // Never hand control to a dead or dormant (delayed-spawn) ally:
        // dormant walkers are invisible, out of the obmap, skipped by the
        // act phase, and excluded from snapshots, so selecting one strands
        // the player on a ghost and blanks the HUD (bugs A1/A10).
        // Nor to a hidden one (dug in, or a ghost riding a body), nor to
        // either side of a possession: a possessed body belongs to the seat
        // whose ghost rides it, and a bot's possession is the bot's.
        // rule: Switch Character never hands a seat a dead, dormant, hidden, possessed, charmed
        //   (real team set), foreign-team or other-seat body, nor one the ownership policy refuses.
        // tests: SimInputHandler.sim_input_switch_char_skips_ineligible_candidates_then_selects_valid
        // tests: SimInputUnit.sim_input_switch_char_skips_dormant_and_dead_allies
        // tests: SimInputUnit.sim_control_owner_locked_switch_char_skips_foreign_hero
        // tests: KitPossession.claim_scans_refuse_linked_walkers
        // tests: KitPossession.claim_scans_refuse_a_hidden_unlinked_walker
        return !w->dead() && !w->dormant() && !w->hidden() &&
               w->possess_link() == 0 &&
               w->query_order() == Order::Living &&
               w->is_friendly(anchor) && w->team_num() == my_team &&
               w->real_team_num() == 255 && w->user() == -1 &&
               og::sim::control_claim_allowed(level, w, player_num); // §4.4 site 1
    };
    return sim_cycle_next_character(level.oblist, anchor, reverse, filter);
}

bool sim_claim_control(GameWorld& level, walker& control, short player_num)
{
    // rule: A seat may only take a body the ownership policy allows (owner-locked networked games
    //   keep each machine on its own heroes); a claim takes it under the seat's keys and clears its
    //   commands except a forced fright and charm.
    // tests: SimInputUnit.sim_control_owner_locked_refused_supplied_control_is_not_claimed
    // tests: SimInputUnit.sim_control_owner_locked_allowed_supplied_control_is_claimed
    // tests: StatsFright.switch_and_claim_preserve_fright_and_hygiene
    if (!og::sim::control_claim_allowed(level, &control, player_num))
        return false;
    control.set_act_type(ACT_CONTROL);
    control.set_user(static_cast<signed char>(player_num));
    control.stats()->clear_command_for_control_switch(); // forced fright + charm survive (runaway-specials §4)
    return true;
}

SimInputResult sim_process_player_input(
    const PlayerInput& pi,
    walker*& control,
    GameWorld& level,
    short player_num,
    short my_team,
    SimInputDebounce& debounce,
    og::sim::SimEventLog* sim_events)
{
    SimInputResult result;
    walker* oldcontrol = control;

    // The failure-cue throttle (#222) ticks here, above every early return
    // below: a seat whose hero is dead or mid-respawn still leaves the
    // function, and a throttle that only drained on healthy frames would stay
    // warm for the whole death and mute the first cue after the revive.
    // rule: The failure-cue throttle cools one tick per input tick even while the seat's hero is
    //   dead or respawning.
    // tests: SimInputUnit.sim_input_cue_throttle_drains_while_the_seat_is_dead
    // tests: SemSimInput.a_cold_cue_throttle_stays_cold
    if (debounce.cue_delay > 0)
        debounce.cue_delay--;

    // --- Possession (New Specials) ---
    // A seat whose hero rides a possessed body drives the body, and comes
    // back to the hero when the body is let go. The tick the seat moves is
    // spent on the move: a Special still held from the cast that possessed
    // casts nothing on the host this tick.
    {
        walker* const before_redirect = control;
        og::sim::possession_seat_redirect(level, control, player_num);
        // rule: New Specials possession: a seat whose ghost rides a body is moved to drive the body
        //   (and back to the ghost on release); the tick the seat moves is spent on the move, so a
        //   Special still held from the possessing cast casts nothing on the host.
        // tests: KitPossession.seat_redirect_follows_host_then_returns
        // tests: KitPossession.possess_redirect_tick_consumes_the_rest_of_the_input
        // tests: KitPossession.seat_follows_the_ghost_out_of_a_dying_host
        if (control != before_redirect)
        {
            result.new_control = control;
            // rule: New Specials possession: a seat whose ghost rides a body is moved to drive the
            //   body (and back to the ghost on release).
            // tests: KitPossession.seat_redirect_follows_host_then_returns
            // tests: KitPossession.possess_redirect_tick_consumes_the_rest_of_the_input
            // tests: KitPossession.seat_follows_the_ghost_out_of_a_dying_host
            if (control != nullptr && control->stats() != nullptr)
            {
                result.control_hp_changed = true;
                result.control_hp = control->stats()->hitpoints();
            }
            return result;
        }
    }

    // --- Control setup (§4.4 per-tick claim) ---
    // rule: A supplied unseated control is claimed this tick; if the ownership policy refuses it
    //   the seat is treated as having no control (#333).
    // tests: SimInputUnit.sim_control_owner_locked_refused_supplied_control_is_not_claimed
    // tests: SimInputUnit.sim_control_owner_locked_allowed_supplied_control_is_claimed
    if (control && control->user() == -1)
    {
        // rule: A supplied unseated control is claimed this tick; if the ownership policy refuses
        //   it the seat is treated as having no control (#333).
        // tests: SimInputUnit.sim_control_owner_locked_refused_supplied_control_is_not_claimed
        // tests: SimInputUnit.sim_control_owner_locked_allowed_supplied_control_is_claimed
        if (!sim_claim_control(level, *control, player_num))
        {
            // A supplied control the policy refuses is treated as no control
            // at all: the seat takes the same §4.4 site-2 verdict (Follow /
            // Claimed / EndGame) an auto-selected null seat takes below.
            // Issue #333.
            control = nullptr;
        }
    }
    // rule: A seat looks for a new body only when it has none or its body is dead: it keeps a live
    //   body whatever team that body now wears, so a charm running out takes the seat with it (#450).
    // tests: SimInputHandler.respawning_dead_hero_does_not_switch_to_teammate, SimInputHandler.sim_input_dead_control_reassigns_to_next_alive, GameplayFixesSeats.a_seat_keeps_its_walker_when_the_charm_runs_out
    if (!control || control->dead())
    {
        // Respawning players keep their exact entity assignment. The normal
        // death path below auto-claims another teammate, which makes the
        // player miss the countdown and then leaves the revived hero as AI.
        // This predicate also covers the just-died, pre-death-scan tick.
        // rule: A seat whose hero is dead but due to respawn keeps that exact hero (it does not
        //   jump to a teammate and miss the countdown).
        // tests: SimInputHandler.respawning_dead_hero_does_not_switch_to_teammate
        if (control != nullptr &&
            og::sim::respawn_retains_player_control(level, control))
        {
            result.new_control = control;
            return result;
        }

        // §4.4 site 2 (death auto-switch / entry claim): Follow ⇒ null seat,
        // NO endgame request (server broadcasts ControlChange entity 0);
        // EndGame ⇒ today's result fields; Claimed ⇒ today's claim tail.
        // rule: With no live control the seat takes the policy's verdict: follow (spectate, no
        //   end-game), end the game (company wiped), or claim the next eligible body, which is
        //   seated at once.
        // tests: SimInputHandler.sim_input_endgame_when_no_control
        // tests: SimInputHandler.sim_input_dead_control_reassigns_to_next_alive
        // tests: SimInputUnit.sim_control_owner_locked_death_reacquire_follows_not_claims
        // tests: SimInputUnit.sim_control_owner_locked_death_reacquire_endgame_on_wipe
        // tests: SimInputUnit.sim_control_owner_locked_death_reacquire_claims_same_machine, GameplayFixesSeats.a_dead_heros_seat_takes_a_charmed_foe_only_with_the_setting_off
        if (og::sim::sim_reacquire_apply(level, my_team, player_num, control, result))
            return result;
        if (control->user() == -1)
            control->set_user(static_cast<signed char>(player_num));
        control->set_act_type(ACT_CONTROL);
        result.control_hp_changed = true;
        result.control_hp = control->stats()->hitpoints();
    }

    // --- Bonus rounds ---
    // rule: A seated hero with banked bonus rounds spends one per input tick on repeating its last
    //   step (if it was moving).
    // tests: SimInputHandler.sim_input_bonus_rounds_walks_when_last_vector_nonzero
    // tests: SimInputHandler.sim_input_bonus_round_repeat_fire_and_walkstep_all_land_in_one_tick
    // tests: SimInputHandler.sim_input_held_direction_walksteps_and_advances_one_stepsize
    if (control && control->bonus_rounds())
    {
        control->set_bonus_rounds(control->bonus_rounds() - 1);
        // rule: A seated hero with banked bonus rounds spends one per input tick on repeating its
        //   last step (if it was moving).
        // tests: SimInputHandler.sim_input_bonus_rounds_walks_when_last_vector_nonzero
        // tests: SimInputHandler.sim_input_bonus_round_repeat_fire_and_walkstep_all_land_in_one_tick
        // tests: SimInputHandler.sim_input_held_direction_walksteps_and_advances_one_stepsize
        if (control->lastx() != 0.0f || control->lasty() != 0.0f)
            control->walk();
    }

    // --- Switch character ---
    // rule: Switch Character fires once per press (debounced until the key is released) and not
    //   while the Cheat key is held (Cheat+Switch changes team instead).
    // tests: SimInputHandler.sim_input_cheat_gates_and_command_queue_skip_movement_block
    // tests: SimInputHandler.sim_input_switch_char_forward_and_reverse_paths
    // tests: ViewInputPaths.view_input_switch_control_forward_and_reverse
    // tests: SemSimInput.switch_character_held_down_switches_once
    if (!pi.was_pressed(InputAction::SwitchChar))
        debounce.changedchar = 0;
    else if (!debounce.changedchar && !pi.is_held(InputAction::Cheat))
    {
        // KEY_SHIFTER will go backward
        bool reverse = pi.is_held(InputAction::Shift);
        debounce.changedchar = 1;

        // New Specials: in a possessed body, Switch Character leaves the
        // body (the ghost reappears where it stands); the next press cycles
        // heroes as usual.
        // rule: New Specials: Switch Character inside a possessed body lets the body go; the seat
        //   returns to the ghost where it stands, and the next press cycles heroes as usual.
        // tests: KitPossession.charmed_host_released_by_switch_character
        // tests: KitPossession.seat_redirect_follows_host_then_returns
        if (control->possess_link() != 0 && !control->hidden())
        {
            // rule: New Specials: Switch Character inside a possessed body lets the body go; the
            //   seat returns to the ghost where it stands, and the next press cycles heroes as
            //   usual.
            // tests: KitPossession.charmed_host_released_by_switch_character
            // tests: KitPossession.seat_redirect_follows_host_then_returns
            if (walker* rider = og::sim::release_possession(level, *control))
                control = rider;
            result.new_control = control;
            result.control_hp_changed = true;
            result.control_hp = control->stats()->hitpoints();
            return result;
        }

        // #346: cycle from, and fall back to, the hero this seat holds RIGHT
        // NOW. That is the entry control, or the hero the claim tail above
        // just seated when the seat entered with no control or a dead one.
        // The entry pointer (oldcontrol) is null or a corpse on that tick,
        // and anchoring the switch on it released the just-claimed hero,
        // cycled from nowhere and dereferenced null below. oldcontrol keeps
        // its one job: the control != oldcontrol clear-command check.
        walker* const anchor = control;
        control = sim_switch_control(level, *anchor, anchor, my_team,
                                     player_num, reverse);

        // rule: With no one else to switch to, the seat keeps its hero and is told 'NO ONE TO
        //   SWITCH TO' (#223).
        // tests: SimInputUnit.sim_input_switch_char_on_a_dead_entry_cycles_from_the_claimed_hero
        // tests: SimInputUnit.sim_input_switch_char_skips_dormant_and_dead_allies
        // tests: SimInputUnit.sim_input_switch_char_on_the_claim_tick_cycles_from_the_claimed_hero
        if (!control)
        {
            // #223: the key used to do nothing at all when the company is
            // down to one body. Say so instead of reading as a dead key.
            control = anchor;
            emit_throttled_cue(debounce, player_num, sim_events,
                               "NO ONE TO SWITCH TO");
        }

        result.control_hp_changed = true;
        result.control_hp = control->stats()->hitpoints();

        // Claim the selected hero in this same call, as the original game's
        // continuous_input() does right after input() (view.cpp:887-892): the
        // new hero acts under the seat's keys on the switch tick instead of
        // taking one more AI act. The fallback body, which sim_switch_control
        // released, is re-claimed here too.
        // rule: The hero switched to is taken under the seat's keys in the same tick (it does not
        //   take one more AI act).
        // tests: SimInputUnit.sim_input_switch_char_claims_the_new_hero_in_the_same_call
        if (control->user() == -1)
            sim_claim_control(level, *control, player_num);
    }

    // --- A hidden hero (New Specials: dug in, or riding a body) ---
    // It answers Special (a second press brings a dug-in hero up) and Switch
    // Character (above) and nothing else: no walking, firing, yelling or
    // special cycling. A hidden walker never acts, so its command queue
    // never drains; the cast does not wait for an empty queue here.
    // rule: New Specials: a hidden hero (dug in, or a ghost riding a body) answers only Special
    //   (press or hold; a second press brings a dug-in hero up) and Switch Character: no walking,
    //   firing, yelling or special cycling.
    // tests: KitSeams.hidden_control_reads_only_special_and_switch
    // tests: KitSkeleton.seated_hero_digs_in_where_it_stands
    // tests: SemSimInput.a_hidden_hero_holding_special_casts_on_the_repeat
    if (control->hidden())
    {
        // rule: New Specials: a hidden hero (dug in, or a ghost riding a body) answers only Special
        //   (press or hold.
        // tests: KitSeams.hidden_control_reads_only_special_and_switch
        // tests: KitSkeleton.seated_hero_digs_in_where_it_stands
        // tests: SemSimInput.a_hidden_hero_holding_special_casts_on_the_repeat
        if (control->user() == player_num)
        {
            // rule: New Specials: a hidden hero (dug in, or a ghost riding a body) answers only
            //   Special (press or hold.
            // tests: KitSeams.hidden_control_reads_only_special_and_switch
            // tests: KitSkeleton.seated_hero_digs_in_where_it_stands
            // tests: SemSimInput.a_hidden_hero_holding_special_casts_on_the_repeat
            if (pi.was_pressed(InputAction::Special))
                player_cast_special(control, debounce, player_num, sim_events,
                                    true);
            else if (pi.is_held(InputAction::Special))
                player_cast_special(control, debounce, player_num, sim_events,
                                    false);
        }
        result.new_control = control;
        return result;
    }

    // --- Switch special ---
    // rule: Switch Special advances once per press (debounced until released).
    // tests: SimInputHandler.sim_input_switch_special_wraps_and_debounces
    // tests: ViewInputPaths.view_input_special_switch_cycles_the_current_special
    if (!pi.was_pressed(InputAction::SwitchSpecial))
        debounce.changedspec = 0;

    // rule: Switch Special advances once per press (debounced until released).
    // tests: SimInputHandler.sim_input_switch_special_wraps_and_debounces
    // tests: ViewInputPaths.view_input_special_switch_cycles_the_current_special
    if (pi.was_pressed(InputAction::SwitchSpecial) && !debounce.changedspec)
    {
        debounce.changedspec = 1;
        sim_advance_current_special(level, *control);
    }

    // --- yo_delay tick ---
    // Make sure we haven't yelled recently (this is here because it is
    // guaranteed to run exactly once each frame)
    // rule: The Yell cooldown counts down once per input tick.
    // tests: SimInputHandler.sim_input_switch_char_no_candidate_keeps_old_control_and_ticks
    // tests: SimInputHandler.sim_input_yell_sets_follow_and_notification
    if (control->yo_delay() > 0)
        control->set_yo_delay(control->yo_delay() - 1);

    // --- Yell for help ---
    // rule: Yell (Yo!) recruits every unseated teammate of order Living that has no leader: it
    //   follows the hero (leader set, foe dropped, a forced 100-step follow); the filter has NO
    //   dead/dormant/hidden check, so a dead, dormant or hidden teammate is recruited too (classic
    //   quirk, old game view.cpp identical). The hero cannot yell again for 30 ticks.
    // tests: SimInputHandler.sim_input_yell_sets_follow_and_notification
    // tests: SimInputHandler.sim_input_yell_within_cooldown_emits_nothing
    // tests: SimInputHandler.sim_input_cheat_gates_and_command_queue_skip_movement_block
    // tests: SimInputHandler.sim_input_shift_yell_summon_and_release
    // tests: SemSimInput.yell_recruits_unled_teammates_and_classic_recruits_the_dead_dormant_and_hidden
    if (pi.was_pressed(InputAction::Yell) && !control->yo_delay()
        && !pi.is_held(InputAction::Shift)
        && !pi.is_held(InputAction::Cheat))
    {
        // plumbing: oblist scans of Yell / Summon Defense / Release Men; rules at
        //   above and below
        for (auto& uptr : level.oblist)
        {
            walker* w = uptr.get();
            // rule: Yell (Yo!) recruits every unseated teammate of order Living that has no leader.
            // tests: SimInputHandler.sim_input_yell_sets_follow_and_notification
            // tests: SimInputHandler.sim_input_yell_within_cooldown_emits_nothing
            // tests: SimInputHandler.sim_input_cheat_gates_and_command_queue_skip_movement_block
            // tests: SimInputHandler.sim_input_shift_yell_summon_and_release
            // tests: SemSimInput.yell_recruits_unled_teammates_and_classic_recruits_the_dead_dormant_and_hidden
            if (w && (w->query_order() == Order::Living) &&
                (w->act_type() != ACT_CONTROL) &&
                (w->team_num() == control->team_num()) &&
                (!w->leader()))
            {
                w->set_leader(control);
                // Remove any current foe ..
                w->set_foe(nullptr);
                w->stats()->force_command(COMMAND_FOLLOW, 100, 0, 0);
            }
        }
        control->set_yo_delay(30);
        result.play_sound = SOUND_YO;
        result.notify_text = "Yo!";
        result.notify_source = control;
        // The authoritative server owns input processing, so the cue has to
        // reach the mirrors as sim events. The render-layer consumer of the
        // result fields below is unreachable during gameplay (view.cpp early
        // return once the local transport shadow is live).
        og::sim::emit_sound(sim_events, SOUND_YO);
        og::sim::emit_notification(sim_events, "Yo!", 0,
                                   static_cast<std::int32_t>(player_num));
    }

    // --- Shift+Yell: summon/release ---
    // rule: Shift+Yell toggles Summon Defense: on, every same-team friendly object (no order
    //   filter, the hero itself included) follows the hero (leader set, foe dropped, ACTION_FOLLOW)
    //   and 'SUMMONING DEFENSE!'; pressed again, every unseated living teammate returns to normal
    //   and 'RELEASING MEN!'; any other action on the hero is just reset.
    // tests: SimInputHandler.sim_input_shift_yell_summon_and_release
    // tests: SimInputHandler.sim_input_yell_movement_and_idle_animate_branches
    // tests: ViewInputPaths.view_input_yell_and_shift_yell_team_actions
    // tests: SimInputHandler.shift_yell_still_summons_on_a_belt_tick
    // tests: SemSimInput.summon_defense_sets_the_leader_and_drops_the_foe_classic_including_the_hero
    if (pi.is_held(InputAction::Shift) && pi.was_pressed(InputAction::Yell)
        && !pi.is_held(InputAction::Cheat))
    {
        // rule: Shift+Yell toggles Summon Defense.
        // tests: SimInputHandler.sim_input_shift_yell_summon_and_release
        // tests: SimInputHandler.sim_input_yell_movement_and_idle_animate_branches
        // tests: ViewInputPaths.view_input_yell_and_shift_yell_team_actions
        // tests: SimInputHandler.shift_yell_still_summons_on_a_belt_tick
        // tests: SemSimInput.summon_defense_sets_the_leader_and_drops_the_foe_classic_including_the_hero
        switch (control->action())
        {
            case 0:
                // plumbing: oblist scans of Yell / Summon Defense / Release Men; rules at
                //   above
                for (auto& uptr : level.oblist)
                {
                    walker* w = uptr.get();
                    // rule: Shift+Yell toggles Summon Defense.
                    // tests: SimInputHandler.sim_input_shift_yell_summon_and_release
                    // tests: SimInputHandler.sim_input_yell_movement_and_idle_animate_branches
                    // tests: ViewInputPaths.view_input_yell_and_shift_yell_team_actions
                    // tests: SimInputHandler.shift_yell_still_summons_on_a_belt_tick
                    // tests: SemSimInput.summon_defense_sets_the_leader_and_drops_the_foe_classic_including_the_hero
                    if (w && (w->team_num() == control->team_num()) && w->is_friendly(control))
                    {
                        w->set_leader(control);
                        w->set_foe(nullptr);
                        w->set_action(ACTION_FOLLOW);
                    }
                }
                result.notify_text = "SUMMONING DEFENSE!";
                result.notify_source = control;
                og::sim::emit_notification(sim_events, "SUMMONING DEFENSE!", 0,
                                           static_cast<std::int32_t>(player_num));
                break;
            case ACTION_FOLLOW:
                // plumbing: oblist scans of Yell / Summon Defense / Release Men; rules at
                //   above
                for (auto& uptr : level.oblist)
                {
                    walker* w = uptr.get();
                    // rule: Shift+Yell toggles Summon Defense.
                    // tests: SimInputHandler.sim_input_shift_yell_summon_and_release
                    // tests: SimInputHandler.sim_input_yell_movement_and_idle_animate_branches
                    // tests: ViewInputPaths.view_input_yell_and_shift_yell_team_actions
                    // tests: SimInputHandler.shift_yell_still_summons_on_a_belt_tick
                    // tests: SemSimInput.summon_defense_sets_the_leader_and_drops_the_foe_classic_including_the_hero
                    if (w && (w->query_order() == Order::Living) &&
                        (w->act_type() != ACT_CONTROL) &&
                        (w->team_num() == control->team_num()))
                    {
                        // Set to normal operation
                        w->set_action(0);
                    }
                }
                control->set_action(0);
                result.notify_text = "RELEASING MEN!";
                result.notify_source = control;
                og::sim::emit_notification(sim_events, "RELEASING MEN!", 0,
                                           static_cast<std::int32_t>(player_num));
                break;
            default:
                control->set_action(0);
                break;
        }
    }

    // --- Ensure correct user assignment ---
    // rule: A seat never drives a hero another seat (or the AI) holds: input past this point is
    //   dropped.
    // tests: SimInputHandler.sim_input_frozen_and_user_mismatch_drop_movement_and_cue
    // tests: SimInputHandler.sim_input_yell_movement_and_idle_animate_branches
    if (control->user() != player_num)
    {
        result.new_control = control;
        return result;
    }

    // rule: A seated hero mid-animation advances its animation from the input tick as well as its
    //   act (the classic double-speed player animation).
    // tests: SemSimInput.a_seated_hero_mid_attack_animates_in_the_input_tick
    if (control->ani_type() != ANI_WALK)
        control->animate();

    // if we changed control characters
    // rule: A seat that changed body this tick clears the new body's commands, except a leading
    //   forced fright and charm.
    // tests: SimInputHandler.sim_input_assigns_unowned_control_and_clears_commands
    // tests: StatsFright.switch_and_claim_preserve_fright_and_hygiene
    // tests: SemSimInput.a_seat_that_keeps_its_body_keeps_its_queued_commands
    if (control != oldcontrol)
        control->stats()->clear_command_for_control_switch(); // forced fright + charm survive (runaway-specials §4)

    // rule: A frozen seated hero ignores every key; pressing Special or Fire says 'FROZEN!'; each
    //   input tick thaws it by one and the 1 -> 0 thaw writes the freeze-immunity window.
    // tests: SimInputUnit.sim_input_specials_disabled_and_frozen_cues
    // tests: SimInputHandler.sim_input_frozen_and_user_mismatch_drop_movement_and_cue
    // tests: StatsFright.sim_player_thaw_immunity_window
    // unreachable: a dead control never reaches here; the check above returns (respawn retains
    //   it) or replaces it, and nothing between (bonus-round walk, switch filter skips the dead,
    //   yell, the attack row's fire above) kills the seat's own walker
    if (control->dead() || control->stats()->frozen_delay())
    {
        // rule: A frozen seated hero ignores every key; pressing Special or Fire says 'FROZEN!';
        //   each input tick thaws it by one and the 1 -> 0 thaw writes the freeze-immunity window.
        // tests: SimInputUnit.sim_input_specials_disabled_and_frozen_cues
        // tests: SimInputHandler.sim_input_frozen_and_user_mismatch_drop_movement_and_cue
        // tests: StatsFright.sim_player_thaw_immunity_window
        if (control->stats()->frozen_delay())
        {
            // #222: every key from here down is dropped on the floor. Tell
            // the seat its hero is iced rather than letting the pad feel
            // broken.
            // rule: A frozen seated hero ignores every key; pressing Special or Fire says
            //   'FROZEN!'; each input tick thaws it by one and the 1 -> 0 thaw writes the
            //   freeze-immunity window.
            // tests: SimInputUnit.sim_input_specials_disabled_and_frozen_cues
            // tests: SimInputHandler.sim_input_frozen_and_user_mismatch_drop_movement_and_cue
            // tests: StatsFright.sim_player_thaw_immunity_window
            if (pi.was_pressed(InputAction::Special) ||
                pi.was_pressed(InputAction::Fire))
                emit_throttled_cue(debounce, player_num, sim_events, "FROZEN!");
            control->stats()->player_thaw_tick(); // 1 -> 0 writes -kFreezeThawImmunityTicks (runaway-specials §3.3)
        }
        result.new_control = control;
        return result;
    }

    // --- Movement and actions ---
    // Make sure we're not performing some queued action ..
    // rule: While a hero carries out a queued action (a shove, a fright, a special's command) the
    //   seat's movement, fire and special keys are ignored.
    // tests: SimInputHandler.sim_input_fire_press_attacks_and_a_queued_command_suppresses_it
    // tests: SimInputHandler.sim_input_cheat_gates_and_command_queue_skip_movement_block
    // tests: SemSimInput.a_hero_carrying_out_a_queued_action_does_not_cast
    if (control->stats()->commands.empty())
    {
        // New Ability Select: a seat with the belt on says which entry it means (PlayerInput::belt_cast).
        // 0 is a classic seat (or no belt key held): the Shift key drives shifter_down exactly as it always
        // has. A belt tick sets the slot and the shift from the ENTRY — the physical Shift key is the belt's
        // second row here and means nothing to the cast — and a refused entry voices why and casts nothing
        // (never the stale current_special). 0 is the only classic value: every other byte, well-formed
        // or not, goes through validate_cast (og::sim::belt).
        bool belt_refused = false;
        // rule: On a belt tick the slot and shift come from the belt entry and the physical Shift
        //   key means nothing; on a classic tick the Shift key sets the shift (alternate) for the
        //   cast.
        // tests: SimInputHandler.belt_cast_zero_runs_todays_shift_rule
        // tests: SimInputHandler.belt_hold_does_not_read_the_physical_shift
        // tests: SimInputHandler.sim_input_republishes_held_shift_as_shifter_down
        if (pi.belt_cast != 0 &&
            (pi.was_pressed(InputAction::Special) || pi.is_held(InputAction::Special)))
            belt_refused = !sim_arm_belt_entry(level, *control, pi.belt_cast,
                                               pi.was_pressed(InputAction::Special),
                                               debounce, player_num, sim_events);
        else
            control->set_shifter_down(pi.is_held(InputAction::Shift) ? 1 : 0);

        // rule: Pressing Special casts the current special once; a belt refusal casts nothing
        //   (never the stale slot).
        // tests: SimInputHandler.locked_slot_is_refused_with_a_cue_and_no_cast
        // tests: SimInputUnit.sim_input_one_press_is_one_cast_for_an_unlatched_special
        if (pi.was_pressed(InputAction::Special) && !belt_refused)
        {
            const bool cast = player_cast_special(control, debounce, player_num,
                                                  sim_events, true);
            // #222 alliance act-freeze: a hostile-to-my_team caster is
            // skipped by the act phase (game_world.cpp), so whatever the cast
            // left on the command queue never runs. Voice that only when
            // there IS queued work to strand — a special that finished inline
            // (a heal that already landed) is frozen out of nothing, and a
            // cast that failed has its own reason to give instead. Cast
            // exactly as before either way: the cue may not change a single MP.
            // rule: A seated caster on a side the running FREEZE TIME freezes is told 'SPECIALS
            //   FROZEN' when its cast left work queued that the act freeze will strand; an inline
            //   cast or a failed one is not.
            // tests: SimInputUnit.sim_input_enemy_freeze_cue_marks_a_stranded_command_queue
            // tests: SimInputUnit.sim_input_enemy_freeze_stays_quiet_for_an_inline_cast
            // tests: SemSimInput.a_friendly_caster_during_an_enemy_freeze_is_not_told_specials_frozen
            if (cast && !control->stats()->commands.empty() &&
                level.enemy_freeze > 0 &&
                !control->is_friendly_to_team(
                    static_cast<unsigned char>(level.my_team)))
            {
                emit_throttled_cue(debounce, player_num, sim_events,
                                   "SPECIALS FROZEN");
            }
            // A cast that hid its caster (a ghost entering a body) ends the
            // seat's input for the tick: a hidden hero neither fires nor
            // walks, and the next tick moves the seat into the body.
            // rule: New Specials: a cast that hides its caster (a ghost entering a body, a skeleton
            //   digging in) ends the seat's input for that tick: no step and no fire after it.
            // tests: KitSeams.a_cast_that_hides_its_caster_ends_the_tick
            if (control->hidden())
            {
                result.new_control = control;
                return result;
            }
        }

        // rule: Fire attacks on the press and keeps attacking while held.
        // tests: SimInputHandler.sim_input_fire_press_attacks_and_a_queued_command_suppresses_it
        // tests: SimInputUnit.sim_input_r11_animate_movement_and_bit_animate_paths
        // tests: SemSimInput.no_fire_key_means_no_attack
        if (pi.was_pressed(InputAction::Fire))
            control->init_fire();

        // Holding Special key for rapid use (MP cost naturally rate-limits)
        // #228: the held arm yields the tick the press arm ran on. pressed is
        // held && !was_held, so both were true on the key-down frame and a
        // special with no busy latch of its own (the thief's bomb, the elf's
        // volleys, starburst, scare) fired twice per tap. The 2002 game polled
        // the held key alone, once per cycle; one tap is one cast, and holding
        // fires once per tick from the next tick on.
        // rule: Holding Special recasts once per tick from the tick after the press; one tap is one
        //   cast (#228).
        // tests: SimInputUnit.sim_input_one_press_is_one_cast_for_an_unlatched_special
        if (pi.is_held(InputAction::Special) &&
            !pi.was_pressed(InputAction::Special) && !belt_refused)
            player_cast_special(control, debounce, player_num, sim_events,
                                false);
        // The held key can land a cast that hides its caster too (a held
        // POSSESS that reaches the foe a tick later): the same rule as the
        // press arm, no step and no fire after it.
        // rule: New Specials: a cast that hides its caster (a ghost entering a body, a skeleton
        //   digging in) ends the seat's input for that tick: no step and no fire after it.
        // tests: KitSeams.a_cast_that_hides_its_caster_ends_the_tick
        if (control->hidden())
        {
            result.new_control = control;
            return result;
        }

        int walkx = pi.move_x();
        int walky = pi.move_y();

        // rule: A direction key walks the hero one stepsize (walkstep); standing still, an
        //   always-animate hero still cycles its frames.
        // tests: SimInputHandler.sim_input_held_direction_walksteps_and_advances_one_stepsize
        // tests: SimInputHandler.sim_input_yell_movement_and_idle_animate_branches
        // tests: SemSimInput.an_idle_hero_that_does_not_always_animate_stands_still
        if (walkx != 0 || walky != 0)
        {
            control->walkstep(walkx, walky);
        }
        else if (control->stats()->query_bit_flags(BIT_ANIMATE))
        {
            control->set_cycle(control->cycle() + 1);
            control->set_frame_from_current_walk_animation();
        }

        // rule: Fire attacks on the press and keeps attacking while held.
        // tests: SimInputHandler.sim_input_fire_press_attacks_and_a_queued_command_suppresses_it
        // tests: SimInputUnit.sim_input_r11_animate_movement_and_bit_animate_paths
        // tests: SemSimInput.no_fire_key_means_no_attack
        if (pi.is_held(InputAction::Fire))
            control->init_fire();
    }

    result.new_control = control;
    return result;
}
