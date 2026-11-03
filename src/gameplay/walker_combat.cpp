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

#include <cstdint>
#include <openglad/core/combat_math.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/core/test_trace.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/core/sound_ids.h>
#include <cassert>
#include <cstdlib>
#include <cmath>
#include <format>

namespace {
IRandom& combat_rng()
{
    // plumbing: combat RNG source: a test override when installed, else world.rng_; no world aborts
    if (IRandom* override_rng = gameplay_rng_override())
        return *override_rng;

    // plumbing: combat RNG source: a test override when installed, else world.rng_; no world aborts
    if (current_game != nullptr && current_game->world != nullptr)
        return current_game->world->rng_;

    assert(false && "combat math requires an injected RNG");
    std::abort();
}

bool positional_sound_visible(const walker* source, std::uint32_t sound_id)
{
    // plumbing: positional_sound_visible: context callback installed only by the parity runner
    //   (emulates the old game's on_screen() gate on the death cry); production leaves it null so
    //   this answers true.
    if (current_game && current_game->positional_sound_visible &&
        !current_game->positional_sound_visible(
            source, sound_id, current_game->positional_sound_visible_user))
        return false;
    return true;
}

} // namespace

// Thin adapter: delegates to combat_math pure functions
short exp_from_action(ExpAction action, walker* w, walker* target, short value)
{
    // rule: XP for an action scales with the target's level; an action with no target counts as
    //   against a level-0 target.
    // tests: WalkerCombat.attack_rewards_single_credit_weapon_hit
    // tests: WalkerMore.walker_friendliness_and_a_hostile_attack_lands_its_damage
    // tests: SemWalker3.experience_for_an_action_without_a_target_counts_a_level_zero_target
    const std::int32_t target_level =
        (target != nullptr && target->stats() != nullptr) ? target->stats()->level() : 0;

    return compute_xp_from_action(action, w->stats()->level(), target_level, value, combat_rng());
}

float get_base_damage(walker* w)
{
    return compute_base_damage(w->damage(), combat_rng());
}

float get_damage_reduction(walker* w, float damage, walker* target)
{
    (void)w; // kept for minimal churn at call sites
    return compute_damage_reduction(damage, target->stats()->armor());
}

static bool is_valid_score_team(unsigned char team_num)
{
    return team_num < SCORE_TEAM_COUNT;
}

static void award_score(unsigned char team_num, std::uint32_t points)
{
    // rule: Team score only accrues to a real score team; a hit by a walker whose team index is out
    //   of range scores nothing (and emits no ScoreChange).
    // tests: WalkerCombat.attack_rewards_single_credit_weapon_hit
    // tests: WalkerCombat.attack_ignores_out_of_range_team_score_index
    // tests: SemWalker3.a_blow_from_an_out_of_range_team_scores_nothing_and_emits_no_event
    if (!(current_game && current_game->world && is_valid_score_team(team_num)))
        return;

    current_game->world->m_score[team_num] += points;
    og::sim::emit_event(current_game->sim_events, og::sim::EventKind::ScoreChange,
                        static_cast<std::uint32_t>(team_num), points);
}

void walker::do_heal_effects(walker* healer, walker* target, short amount)
{
    // rule: A heal shows its amount (colour 56) to the healed walker, and also to the healer when
    //   there is one.
    // tests: ScriptBindingPropsTest.heal_clamped_adds_marks_then_clamps
    // tests: ScriptBindingPropsTest.heal_clamped_with_source_marks_both
    // tests: WalkerCombat.batch6_attack_friendly_team_death_messages_and_clamps
    if(healer)
        healer->damage_numbers.push_back(
            DamageNumber(target->xpos() + target->sizex()/2,
                         target->ypos(),
                         amount,
                         56,
                         current_game->world->tick_count_));
    target->damage_numbers.push_back(
        DamageNumber(target->xpos() + target->sizex()/2,
                     target->ypos(),
                     amount,
                     56,
                     current_game->world->tick_count_));
}

void walker::do_hit_effects(walker* attacker, walker* target, short tempdamage)
{
    // Orange numbers for the attacker to see
    // rule: Every hit shows its damage to the victim (red) and to the credited attacker (orange)
    //   when there is one.
    // tests: GameServerCoverage.step_lifts_every_owner_damage_numbers_and_drains_every_list
    // tests: SemWalker3.a_hit_with_no_attacker_shows_its_number_to_the_victim_only
    if(attacker)
        attacker->damage_numbers.push_back(
            DamageNumber(target->xpos() + target->sizex()/2,
                         target->ypos(),
                         tempdamage,
                         235,
                         current_game->world->tick_count_));
    // RED numbers for the target to see
    target->damage_numbers.push_back(
        DamageNumber(target->xpos() + target->sizex()/2,
                     target->ypos(),
                     tempdamage,
                     RED,
                     current_game->world->tick_count_));
    // rule: A hit that actually took hit points flashes the victim; a living victim also recoils
    //   away from the hitter (recoil 1.0 at the hitter->victim angle); a zero-damage hit neither
    //   flashes nor recoils.
    // tests: WalkerCombat.batch5_heal_and_hit_effect_variants
    // tests: WalkerCombat.effect_helpers_and_recoil_branches
    // tests: SemWalker3.a_damaging_hit_flashes_and_a_living_victim_recoils_away
    if (target->stats()->hitpoints() < 0)
        tempdamage = static_cast<short>(static_cast<float>(tempdamage) + target->stats()->hitpoints());

    // rule: Every hit spawns a hit flash (FAMILY_HIT fx, team of the hitter, on the victim's floor,
    //   random ani 1..3 from the cosmetic stream) except a hit dealt by an effect whose family says
    //   creates_hit_effect=false (boomerang, marker, chain, ghost scare); a melee hit's flash is
    //   centred on the victim, a projectile's halfway between the projectile and the victim's
    //   centre.
    // tests: SemWalker3.every_hit_spawns_a_hit_flash_except_from_a_quiet_effect
    if (current_game != nullptr && current_game->world != nullptr)
    {
        // Create hit effect
        // rule: Every hit spawns a hit flash (FAMILY_HIT fx, team of the hitter, on the victim's
        //   floor, random ani 1..3 from the cosmetic stream) except a hit dealt by an effect whose
        //   family says creates_hit_effect=false (boomerang, marker, chain, ghost scare).
        // tests: SemWalker3.every_hit_spawns_a_hit_flash_except_from_a_quiet_effect
        const auto* efd = (query_order() == Order::FX) ? get_effect_family_descriptor(family()) : nullptr;
        // rule: Every hit spawns a hit flash (FAMILY_HIT fx, team of the hitter, on the victim's
        //   floor, random ani 1..3 from the cosmetic stream) except a hit dealt by an effect whose
        //   family says creates_hit_effect=false (boomerang, marker, chain, ghost scare).
        // tests: SemWalker3.every_hit_spawns_a_hit_flash_except_from_a_quiet_effect
        if(query_order() != Order::FX || (efd && efd->creates_hit_effect))
        {
            walker* newob = current_game->world->add_ob(Order::FX, FAMILY_HIT);
            // rule: Every hit spawns a hit flash (FAMILY_HIT fx, team of the hitter, on the
            //   victim's floor, random ani 1..3 from the cosmetic stream) except a hit dealt by an
            //   effect whose family says creates_hit_effect=false (boomerang, marker, chain, ghost
            //   scare).
            // tests: SemWalker3.every_hit_spawns_a_hit_flash_except_from_a_quiet_effect
            if (newob)
            {
                newob->set_owner(target);
                newob->set_team_num(team_num());
                newob->stats()->set_level(1);
                newob->set_damage(0);
                newob->set_floor(target->floor());  // hit flash at the impact floor (A8)
                // Cosmetic FX animation type: master draws it from libc rand
                // (`1 + rand()%3`), NOT world.rng_. Production draws from
                // world.rng_ for snapshot/replay determinism; the parity
                // harness's libc-rand cosmetic override (when installed) matches
                // master's dual-RNG-stream behavior. combat_rng (damage/xp)
                // deliberately does NOT consult this override.
                // Non-null here: with a live world cosmetic_rng() answers
                // the override or &world->rng_ (gameplay_context.cpp).
                IRandom* ani_rng = cosmetic_rng();
                newob->set_ani_type(static_cast<char>(1 + ani_rng->next(3)));
                // rule: Every hit spawns a hit flash (FAMILY_HIT fx, team of the hitter, on the
                //   victim's floor, random ani 1..3 from the cosmetic stream) except a hit dealt by
                //   an effect whose family says creates_hit_effect=false (boomerang, marker, chain,
                //   ghost scare).
                // tests: SemWalker3.every_hit_spawns_a_hit_flash_except_from_a_quiet_effect
                if(attacker == this)
                {
                    newob->center_on(target);
                }
                else
                {
                    // A projectile
                    newob->center_on(this);  // Make the hit effect start at the projectile position
                    // Then move it a little closer to its target (average)
                    newob->setworldxy((target->worldx() + target->sizex()/2 + newob->worldx()) / 2.0f,
                                      (target->worldy() + target->sizey()/2 + newob->worldy()) / 2.0f);
                }
            }
        }
    }

    // rule: A hit that actually took hit points flashes the victim.
    // tests: WalkerCombat.batch5_heal_and_hit_effect_variants
    // tests: WalkerCombat.effect_helpers_and_recoil_branches
    // tests: SemWalker3.a_damaging_hit_flashes_and_a_living_victim_recoils_away
    if(tempdamage > 0)
    {
        target->set_hurt_flash(true);

        // rule: A hit that actually took hit points flashes the victim.
        // tests: WalkerCombat.batch5_heal_and_hit_effect_variants
        // tests: WalkerCombat.effect_helpers_and_recoil_branches
        // tests: SemWalker3.a_damaging_hit_flashes_and_a_living_victim_recoils_away
        if(target->query_order() == Order::Living)
        {
            target->set_hit_recoil(1.0f);
            const float dy = (static_cast<float>(target->ypos()) + static_cast<float>(target->sizey()) * 0.5f) -
                             (static_cast<float>(ypos()) + static_cast<float>(sizey()) * 0.5f);
            const float dx = (static_cast<float>(target->xpos()) + static_cast<float>(target->sizex()) * 0.5f) -
                             (static_cast<float>(xpos()) + static_cast<float>(sizex()) * 0.5f);
            // Audited for Phase 0: this only drives recoil presentation, and
            // the server snapshots the resulting angle to clients.
            target->set_hit_recoil_angle(atan2f(dy, dx));
        }
    }
}

void walker::do_combat_damage(walker* attacker, walker* target, short tempdamage)
{
    // Every damage sink honors the ward. attack() gates earlier (its
    // early-out also skips retaliation and score), but a caller reaching
    // this sink directly — turn-undead's scripted arm — must not pierce
    // BIT_INVINCIBLE or a running invulnerability.
    // rule: No damage sink pierces a ward: an invincible or invulnerable target takes no damage
    //   from any path (attack, turn undead's scripted damage, a level on_damage gate that grants
    //   invulnerability mid-hit).
    // tests: ModeTick.damage_gate_granting_a_ward_mid_hit_lands_no_damage
    // tests: WalkerCombat.batch5_do_combat_damage_target_myguy_stats
    if (target->stats()->query_bit_flags(BIT_INVINCIBLE) ||
        target->invulnerable_left() != 0)
        return;

    // Record damage done for records ..
    // rule: A hero's record counts the damage he deals to living things only (total_damage,
    //   scen_damage); damage to generators, doors and objects is not recorded.
    // tests: WalkerCombat.batch5_do_combat_damage_target_myguy_stats
    // tests: WalkerMore.walker_friendliness_and_a_hostile_attack_lands_its_damage
    // tests: SemWalker3.a_heros_damage_record_counts_living_victims_only
    if (attacker && attacker->myguy && target->query_order() == Order::Living)  // hit a living
    {
        attacker->myguy->total_damage += tempdamage;
        attacker->myguy->scen_damage += tempdamage;
    }

    // Deal the damage
    target->set_last_hitpoints(target->stats()->hitpoints());
    target->stats()->set_hitpoints(target->stats()->hitpoints() - tempdamage);

    do_hit_effects(attacker, target, tempdamage);

    // rule: A hit that takes real hit points delays the victim's HP regeneration by 50 ticks;
    //   overkill below 0 hp does not count as damage taken; a 0-damage hit does not delay regen.
    // tests: WalkerCombat.batch5_do_combat_damage_target_myguy_stats
    // tests: WalkerMore.walker_friendliness_and_a_hostile_attack_lands_its_damage
    // tests: SemWalker3.a_zero_hit_leaves_regen_alone_and_overkill_counts_only_the_hp_lost
    if (target->stats()->hitpoints() < 0)
        tempdamage = static_cast<short>(static_cast<float>(tempdamage) + target->stats()->hitpoints());

    // Delay HP regeneration
    // rule: A hit that takes real hit points delays the victim's HP regeneration by 50 ticks;
    //   overkill below 0 hp does not count as damage taken; a 0-damage hit does not delay regen.
    // tests: WalkerCombat.batch5_do_combat_damage_target_myguy_stats
    // tests: WalkerMore.walker_friendliness_and_a_hostile_attack_lands_its_damage
    // tests: SemWalker3.a_zero_hit_leaves_regen_alone_and_overkill_counts_only_the_hp_lost
    if(tempdamage > 0)
        target->set_regen_delay(50);

    // rule: A hero's record tracks damage taken this scenario (overkill trimmed) and the lowest HP
    //   he reached.
    // tests: WalkerCombat.batch5_do_combat_damage_target_myguy_stats
    // tests: SemWalker3.a_hero_victims_record_keeps_its_lowest_hp
    if(target->myguy != nullptr)
    {
        target->myguy->scen_damage_taken += tempdamage;
        // rule: A hero's record tracks damage taken this scenario (overkill trimmed) and the lowest
        //   HP he reached.
        // tests: WalkerCombat.batch5_do_combat_damage_target_myguy_stats
        // tests: SemWalker3.a_hero_victims_record_keeps_its_lowest_hp
        if(target->myguy->scen_min_hp > target->stats()->hitpoints())
            target->myguy->scen_min_hp = target->stats()->hitpoints();
    }
}

bool walker::attack(walker  *target)
{
    walker  *blood; // temporary stain
    walker *headguy; // guy at top of chain..
    short playerteam = -1;
    std::string message;
    float tempdamage = get_base_damage(this);
    short getscore=0;
    Order targetorder = target->query_order();
    char targetfamily = target->family();
    walker *attacker; // us or our owner ..

    // rule: Nobody can strike a dead walker, a friend (same team at the head of both owner chains)
    //   or a treasure: such an attack does nothing and reports failure.
    // tests: WalkerCombat.round8_attack_early_return_guards
    // tests: WalkerCombat.walker_attack_friendly_fails
    // tests: WalkerCombat.batch6_attack_friendly_team_death_messages_and_clamps
    // tests: WalkerCombat.walker_attack_basic
    if (target && target->dead())
        return 0;

    //if ( (targetorder == Order::Living && is_friendly(target) ) ||
    // rule: Nobody can strike a dead walker, a friend (same team at the head of both owner chains)
    //   or a treasure: such an attack does nothing and reports failure.
    // tests: WalkerCombat.round8_attack_early_return_guards
    // tests: WalkerCombat.walker_attack_friendly_fails
    // tests: WalkerCombat.batch6_attack_friendly_team_death_messages_and_clamps
    // tests: WalkerCombat.walker_attack_basic
    if ( is_friendly(target) || (targetorder == Order::Treasure) )
        return 0;

    // rule: A target that is invincible, under an invulnerability potion, or hidden (a burrowed New
    //   Specials skeleton) cannot be attacked: no damage, no retaliation, no score.
    // tests: WalkerCombat.round8_attack_early_return_guards
    // tests: WalkerCombat.walker_attack_invulnerable
    // tests: KitSeamsSmall.hidden_walker_cannot_be_attacked
    if (target->stats()->query_bit_flags(BIT_INVINCIBLE) ||
            target->invulnerable_left() != 0 || target->hidden())
        return 0;

    // rule: A weapon or effect with an owner hits on its owner's behalf: hit/shot counters and the
    //   damage record go to the owner; a living (even a summon) is its own attacker.
    // tests: WalkerCombat.walker_attack_weapon_owner_chain_and_nonliving_target
    // tests: WalkerMore.walker_friendliness_and_a_hostile_attack_lands_its_damage
    if (order() != Order::Living && owner())
        attacker = owner();
    else
        attacker = this;

    // who's the top on our chain (ie, weapon->summoned->mage)
    headguy = this;
    // rule: Credit for a hit climbs the owner chain to its head (weapon -> summon -> caster); with
    //   Gameplay Fixes on a charmed walker heads its own chain, so its blows score for the colour
    //   it now wears; classic climbs through the charm to the original owner.
    // tests: GameplayFixesCharm.a_charmed_tent_troop_fights_for_its_charmer
    // tests: GameplayFixesCharm.a_charmed_summon_of_an_enemy_caster_fights_for_its_charmer
    // tests: SemWalker3.classic_credit_for_a_charmed_troops_blow_climbs_to_its_original_owner
    // tests: SemWalker3.classic_credit_for_a_blow_climbs_past_a_dead_thrower
    while (headguy->owner() && (headguy->owner() != headguy) && !headguy->charmed_head() )
        headguy = headguy->owner();

    // Score and kill semantics follow the actual attacker's team. The legacy
    // red-only checks made non-red company heroes deal damage without normal
    // projectile score, and treated their red victims as friendly deaths.
    playerteam = static_cast<short>(headguy->team_num());
    // rule: Team score is paid only for blows whose chain head is a saved hero or sits on the
    //   player's team; other NPC fights deal damage but score nothing.
    // tests: WalkerCombat.attack_rewards_single_credit_weapon_hit
    // tests: WalkerCombat.company_kill_credit_is_directional_for_every_team_pair
    // tests: SemWalker3.an_npc_fight_off_the_players_team_scores_nothing
    if (headguy->myguy != nullptr ||
        playerteam == current_game->world->my_team)
    {
        getscore = 1;
    }

    // Modify attack value based on things like magical attacks, etc.
    // rule: Hitting a living counts as a hit on the credited hero's record (total_hits, scen_hits).
    // tests: WalkerMore.walker_friendliness_and_a_hostile_attack_lands_its_damage
    // tests: SemWalker3.a_hit_by_a_walker_with_no_record_credits_no_hit
    switch (targetorder) // generally going to be livings..
    {
        case Order::Living:
            // Hit a living target, so we get credit for a hit
            // rule: Hitting a living counts as a hit on the credited hero's record (total_hits,
            //   scen_hits).
            // tests: WalkerMore.walker_friendliness_and_a_hostile_attack_lands_its_damage
            // tests: SemWalker3.a_hit_by_a_walker_with_no_record_credits_no_hit
            if (attacker->myguy)
            {
                attacker->myguy->total_hits++;
                attacker->myguy->scen_hits++;
            }

            // rule: Magical attacks are scaled by the victim family's magic_damage_modifier (slimes
            //   take double, barbarians half); non-magical attacks are unscaled.
            // tests: WalkerCombat.walker_attack_slime_magic_bonus
            // tests: WalkerCombat.walker_attack_barbarian_magic_resistance
            if (stats_->query_bit_flags(BIT_MAGICAL))
            {
                const auto* fd = get_family_descriptor(targetfamily);
                // rule: Magical attacks are scaled by the victim family's magic_damage_modifier
                //   (slimes take double, barbarians half); non-magical attacks are unscaled.
                // tests: WalkerCombat.walker_attack_slime_magic_bonus
                // tests: WalkerCombat.walker_attack_barbarian_magic_resistance
                if (fd)
                    tempdamage *= fd->magic_damage_modifier;
            }
            break; // end of living
        // rule: Hitting a non-living (generator, door, object) is not a shot on the hero's accuracy
        //   record: the shot counted at fire time is taken back (total_shots--, scen_shots--).
        // tests: WalkerCombat.walker_attack_weapon_owner_chain_and_nonliving_target
        // tests: SemWalker3.a_non_living_hit_by_a_walker_with_no_record_refunds_no_shot
        default:
            // We hit something, but it wasn't living, so don't count
            // as a shot, OR as a hit ..
            // rule: Hitting a non-living (generator, door, object) is not a shot on the hero's
            //   accuracy record: the shot counted at fire time is taken back (total_shots--,
            //   scen_shots--).
            // tests: WalkerCombat.walker_attack_weapon_owner_chain_and_nonliving_target
            // tests: SemWalker3.a_non_living_hit_by_a_walker_with_no_record_refunds_no_shot
            if (attacker->myguy)
            {
                attacker->myguy->total_shots--; // since we already counted it
                attacker->myguy->scen_shots--;
            }
            break;
    } // end of checking orders

    tempdamage -= get_damage_reduction(attacker, tempdamage, target);
    // rule: Armour can absorb a whole blow but never heals: damage after reduction is floored at 0.
    // tests: WalkerCombat.walker_attack_weapon_owner_chain_and_nonliving_target
    // unreachable: the tempdamage < 0 clamp is observably redundant; the only negative case (base
    //   damage < 0.25) is mapped to 0 by damage_to_hit_points before the gate, and tempdamage is
    //   not read again, so no test can tell the clamp from its absence
    if (tempdamage < 0)
        tempdamage = 0;

    // Scripted-mode damage gate (level on_damage hook): nil keeps the
    // amount, a number replaces it (clamped >= 0), false cancels the hit
    // outright — no damage, no hit_response, no attribution stamp.
    // Early-outs on the level_hook_kinds bit, so classic paths are
    // byte-identical. `attacker` is the resolved local (owner for
    // projectiles), matching the score semantics above.
    const short gated_damage = og::script::hooks::level_damage_gate(
        target, attacker, damage_to_hit_points(tempdamage));
    // rule: A scripted level's on_damage hook can cancel a hit outright (false): no damage, no
    //   retaliation, no attacker stamp, no kill check; nil keeps the amount, a number replaces it
    //   (>= 0).
    // tests: ModeTick.damage_gate_false_cancels_the_hit
    // tests: ModeTick.damage_gate_false_spares_a_zero_hp_target
    // tests: ModeTick.damage_gate_nil_keeps_authored_amount
    // tests: ModeTick.damage_gate_true_keeps_authored_amount
    // tests: ModeTick.damage_gate_non_number_return_keeps_amount
    // tests: ModeTick.damage_gate_number_replaces_amount
    // tests: ModeTick.damage_gate_negative_replacement_clamps_to_zero
    // tests: ModeTick.damage_gate_nan_replacement_is_treated_as_zero
    if (gated_damage < 0)
        return 0;
    const short tempdamage_i = gated_damage;
    // Kill attribution (server-only transients): stamp the topmost LIVING on
    // the owner chain (credit_head: a weapon's thrower, a summon's caster; a
    // tent's troop is its own credit) AFTER the gate allowed a positive
    // amount. No living on the chain (a scripted ball fx): no stamp.
    walker* const credit = credit_head();
    // rule: Only a blow that lands positive damage stamps the victim's last attacker (the topmost
    //   living on the hitter's chain, used for kill attribution in modes); a 0-damage blow leaves
    //   the previous stamp.
    // tests: ModeTick.damage_gate_negative_replacement_clamps_to_zero
    // tests: ModeTick.weapon_kill_attributes_to_owner_chain_root
    // tests: SemWalker3.a_zero_damage_blow_keeps_the_earlier_attacker_stamp
    if (tempdamage_i > 0) target->note_attacker(credit, current_game->world->tick_count_);
    do_combat_damage(attacker, target, tempdamage_i);
    TRACE("walker", "attack: %s deals %d damage", attacker->stats_->name.c_str(), tempdamage_i);

    // Base exp from this successful hit.
    short attack_exp = exp_from_action(ExpAction::Attack, this, target, tempdamage_i);

    // Set our target to fighting our owner
    //in the case of our weapon hit something
    // rule: The victim turns on whoever is behind the blow: a weapon's or effect's owner is made to
    //   target the victim and the victim retaliates against the owner (the chain head's hero gets
    //   the attack XP); in melee the victim retaliates against the striker, and a striking hero
    //   gets attack XP and, if the blow scores, team score += damage + victim level.
    // tests: StatsUnit.hit_response_guard_shields_non_living_targets
    // tests: WalkerCombat.attack_rewards_single_credit_weapon_hit
    // tests: WalkerCombat.attack_rewards_single_credit_melee_kill
    // tests: WalkerMore.walker_friendliness_and_a_hostile_attack_lands_its_damage
    // tests: SemWalker3.the_victim_turns_on_whoever_is_behind_the_blow
    if (order() != Order::Living && owner())
    {
        owner()->set_foe(target);
        target->stats()->hit_response(owner());
        // rule: The victim turns on whoever is behind the blow.
        // tests: StatsUnit.hit_response_guard_shields_non_living_targets
        // tests: WalkerCombat.attack_rewards_single_credit_weapon_hit
        // tests: WalkerCombat.attack_rewards_single_credit_melee_kill
        // tests: WalkerMore.walker_friendliness_and_a_hostile_attack_lands_its_damage
        // tests: SemWalker3.the_victim_turns_on_whoever_is_behind_the_blow
        if (headguy->myguy)
            headguy->myguy->exp += static_cast<std::uint32_t>(attack_exp);
    }
    else  //melee combat, set target to hit_response to us
    {
        target->stats()->hit_response(this);
        // rule: The victim turns on whoever is behind the blow.
        // tests: StatsUnit.hit_response_guard_shields_non_living_targets
        // tests: WalkerCombat.attack_rewards_single_credit_weapon_hit
        // tests: WalkerCombat.attack_rewards_single_credit_melee_kill
        // tests: WalkerMore.walker_friendliness_and_a_hostile_attack_lands_its_damage
        // tests: SemWalker3.the_victim_turns_on_whoever_is_behind_the_blow
        if (myguy)
        {
            myguy->exp += static_cast<std::uint32_t>(attack_exp);
            // rule: The victim turns on whoever is behind the blow.
            // tests: StatsUnit.hit_response_guard_shields_non_living_targets
            // tests: WalkerCombat.attack_rewards_single_credit_weapon_hit
            // tests: WalkerCombat.attack_rewards_single_credit_melee_kill
            // tests: WalkerMore.walker_friendliness_and_a_hostile_attack_lands_its_damage
            // tests: SemWalker3.the_victim_turns_on_whoever_is_behind_the_blow
            if (getscore)
                award_score(team_num(), static_cast<std::uint32_t>(tempdamage_i)
                                      + static_cast<std::uint32_t>(target->stats()->level()));
        }
    }

    // rule: A projectile is worn by what it hits: it loses hit points equal to the damage it dealt
    //   and 1 damage per hit; at <= 0 hp it dies (unless immortal, which still runs its death
    //   effect); then its family's on_hit_target hook runs (e.g. faerie sprinkle freezes).
    // tests: WalkerCombat.batch6_attack_branches_enemy_and_weapon_paths
    // tests: SemWalker3.a_projectile_is_worn_by_what_it_hits
    if (order() == Order::Weapon)
    {
        stats_->set_hitpoints( stats_->hitpoints() - tempdamage_i);
        set_damage(damage() - 1.0f);
        // rule: A projectile is worn by what it hits.
        // tests: WalkerCombat.batch6_attack_branches_enemy_and_weapon_paths
        // tests: SemWalker3.a_projectile_is_worn_by_what_it_hits
        if (stats_->hitpoints() <= 0)
        {
            // rule: A projectile is worn by what it hits.
            // tests: WalkerCombat.batch6_attack_branches_enemy_and_weapon_paths
            // tests: SemWalker3.a_projectile_is_worn_by_what_it_hits
            if (!stats_->query_bit_flags(BIT_IMMORTAL))
                set_dead(1);
            death();
        }
        //special effects
        const auto* wfd = get_weapon_family_descriptor(family());
        og::script::hooks::weapon_on_hit_target(wfd, this, target, owner());

    }

    // Positive score for hurting enemies, negative for us.
    // rule: An owned hitter (a projectile or a summoned living) striking any non-weapon of another
    //   team than its chain head scores for the hitter's team (damage + victim level, generators
    //   included); a summoned living's blow also gives its chain head's hero the attack XP (a
    //   projectile's XP was paid in the owner arm).
    // tests: WalkerCombat.attack_rewards_single_credit_weapon_hit
    // tests: SemWalker3.a_summons_blow_pays_its_heros_experience_and_a_weapon_victim_scores_nothing
    if (owner() && targetorder != Order::Weapon && playerteam != target->team_num())
    {
        // rule: An owned hitter (a projectile or a summoned living) striking any non-weapon of
        //   another team than its chain head scores for the hitter's team (damage + victim level,
        //   generators included).
        // tests: WalkerCombat.attack_rewards_single_credit_weapon_hit
        // tests: SemWalker3.a_summons_blow_pays_its_heros_experience_and_a_weapon_victim_scores_nothing
        if (getscore)
            award_score(team_num(), static_cast<std::uint32_t>(tempdamage_i)
                                  + static_cast<std::uint32_t>(target->stats()->level()));
        // rule: An owned hitter (a projectile or a summoned living) striking any non-weapon of
        //   another team than its chain head scores for the hitter's team (damage + victim level,
        //   generators included).
        // tests: WalkerCombat.attack_rewards_single_credit_weapon_hit
        // tests: SemWalker3.a_summons_blow_pays_its_heros_experience_and_a_weapon_victim_scores_nothing
        if (order() != Order::Weapon && headguy->myguy)
            headguy->myguy->exp += static_cast<std::uint32_t>(attack_exp);
    }

    // Unconditional: how much damage the hit applied does not matter here.
    // A hit fully absorbed by damage reduction, or replaced with 0 by a level
    // on_damage hook, still reaches this check — which is why "return 0" from
    // that hook kills a target already sitting at 0 hp and only "return false"
    // (handled at the gate above) skips the hit entirely.
    // rule: Any landed blow (even 0 damage) on a target at or below 0 hp kills it; only a cancelled
    //   blow (on_damage false) spares it.
    // tests: ModeTick.damage_gate_zero_still_kills_a_zero_hp_target
    // tests: WalkerCombat.attack_rewards_single_credit_melee_kill
    // tests: ModeTick.damage_gate_huge_replacement_clamps_to_short_max
    // tests: ModeTick.damage_gate_nan_replacement_is_treated_as_zero
    if (target->stats()->hitpoints() <= 0)
    {
        // New Specials: a REASSEMBLE ward stands the walker back up inside
        // death(). That blow did not kill, so it is treated like a hit that
        // does not: no death message, no "All foes defeated!", no kill on
        // the killer's record, no blood and no death cry. A ward cannot take
        // only when this walker's death() already ran once, and then death()
        // did nothing here: the walker is put back as it was and the kill
        // below runs exactly as for any other walker.
        // rule: A REASSEMBLE-warded walker struck down stands back up (at a quarter hp) instead of
        //   dying: no death message, no 'All foes defeated!', no kill credit, no blood, no cry, no
        //   on_kill; a ward that cannot take (its death already ran once) leaves it dead and the
        //   kill counts in full.
        // tests: KitSkeleton.warded_death_tells_nobody_and_counts_no_kill
        // tests: KitSkeleton.warded_victim_is_not_a_kill
        // tests: KitSkeleton.warded_skeleton_stands_back_up_at_a_quarter
        if (targetorder == Order::Living &&
            (target->kit_state() & KIT_WARD) != 0)
        {
            target->set_dead(1);
            target->death();
            // rule: A REASSEMBLE-warded walker struck down stands back up (at a quarter hp) instead
            //   of dying.
            // tests: KitSkeleton.warded_death_tells_nobody_and_counts_no_kill
            // tests: KitSkeleton.warded_victim_is_not_a_kill
            // tests: KitSkeleton.warded_skeleton_stands_back_up_at_a_quarter
            if (!target->dead())
            {
                set_collide_ob(nullptr);
                return 1;
            }
            target->set_dead(0);
        }
        // rule: Only a living's death is announced, credited and splattered; a destroyed generator,
        //   door or object just dies (death() runs) with no message, kill, blood or cry.
        // tests: WalkerCombat.walker_attack_basic
        // tests: KitSkeleton.warded_death_tells_nobody_and_counts_no_kill
        // tests: SemWalker3.a_destroyed_generator_dies_without_credit_blood_or_cry
        if (targetorder == Order::Living)
        {
            // rule: Death announcements and kill credit apply whenever the killer has a team
            //   (always).
            // tests: WalkerCombat.attack_rewards_single_credit_melee_kill
            // tests: WalkerCombat.named_ally_death_is_not_announced_as_enemy_death
            if (playerteam > -1)
            {
                // rule: Killing a living of another team than the killer's chain head credits the
                //   head's hero with kill XP (kill XP only; the hit's attack XP was paid once
                //   already), kills+1, scen_kills+1, level_kills += victim level; a scoring kill
                //   adds damage + 10 x victim level to the killer's team score.
                // tests: WalkerCombat.attack_rewards_single_credit_melee_kill
                // tests: WalkerCombat.company_kill_credit_is_directional_for_every_team_pair
                // tests: SemWalker3.an_npc_kill_off_the_players_team_credits_no_score
                // deferred: #440 divergence #440: a hero's killing blow pays attack xp once
                //   (current) vs twice (old game); the NPC-head arm is asserted, the hero xp arm is
                //   not pinned
                if (playerteam != target->team_num())
                {
                    // rule: Killing a living of another team than the killer's chain head credits
                    //   the head's hero with kill XP (kill XP only.
                    // tests: WalkerCombat.attack_rewards_single_credit_melee_kill
                    // tests: WalkerCombat.company_kill_credit_is_directional_for_every_team_pair
                    // tests: SemWalker3.an_npc_kill_off_the_players_team_credits_no_score
                    if (headguy->myguy)  // headguy can == this
                    {
                        headguy->myguy->exp += static_cast<std::uint32_t>(exp_from_action(ExpAction::Kill, this, target, 0));
                        headguy->myguy->kills++;
                        headguy->myguy->scen_kills++;
                        headguy->myguy->level_kills += target->stats()->level();
                    }
                    //else if (myguy)
                    //{
                    //  myguy->exp += newexp + (8 * target->stats()->level());
                    //  myguy->kills++;
                    //  myguy->level_kills += target->stats()->level();
                    //}
                    // rule: Killing a living of another team than the killer's chain head credits
                    //   the head's hero with kill XP (kill XP only.
                    // tests: WalkerCombat.attack_rewards_single_credit_melee_kill
                    // tests: WalkerCombat.company_kill_credit_is_directional_for_every_team_pair
                    // tests: SemWalker3.an_npc_kill_off_the_players_team_credits_no_score
                    if (getscore)
                        award_score(team_num(), static_cast<std::uint32_t>(tempdamage_i)
                                              + static_cast<std::uint32_t>(10 * target->stats()->level()));
                    // If named, alert us of the death. This arm is selected by
                    // the KILLER's team (`playerteam`, set from headguy at the
                    // top), so an enemy killing one of the player's OWN named
                    // NPCs lands here too -- and used to be announced as an
                    // "ENEMY DEATH". Classic hardcoded `playerteam = 0`, i.e.
                    // it compared the victim against the PLAYER's team, and
                    // reported a player-team victim with the same-team arm's
                    // plain "<name> DIED!" text. Pick the wording off the same
                    // comparison classic made; a victim on neither team is
                    // still an enemy, so that text is unchanged.
                    // rule: The death of a named, unsummoned NPC killed by another team is
                    //   announced: 'ENEMY DEATH: NAME DIED!' when the victim is not on the player's
                    //   team, plain 'NAME DIED!' when it is (#189); unnamed or summoned victims get
                    //   no toast in this arm.
                    // tests: WalkerCombat.named_ally_death_is_not_announced_as_enemy_death
                    // tests: WalkerCombat.batch6_attack_friendly_team_death_messages_and_clamps
                    if (target->stats()->name.size() && !(target->lifetime())
                            && (!target->owner()) ) // do we have an NPC name?
                    {
                        // rule: The death of a named, unsummoned NPC killed by another team is
                        //   announced.
                        // tests: WalkerCombat.named_ally_death_is_not_announced_as_enemy_death
                        // tests: WalkerCombat.batch6_attack_friendly_team_death_messages_and_clamps
                        message = (current_game->world->my_team != target->team_num())
                            ? std::format("ENEMY DEATH: {} DIED!", target->stats()->name)
                            : std::format("{} DIED!", target->stats()->name);
                        og::sim::emit_notification(current_game->sim_events, message);
                    }
                    // Scripted-mode (TYPE_SCRIPTED) matches own their win
                    // channel and continue past momentary wipes, so the
                    // classic toast stays on classic-completion worlds only
                    // (same predicate as the tick fork; a demoted scripted
                    // map falls back to classic rules AND this toast).
                    const bool mode_owned =
                        (current_game->world->type & GameWorld::TYPE_SCRIPTED) != 0 &&
                        !(current_game->world->mode.init_attempted &&
                          !current_game->world->mode.active);
                    // rule: On a classic-completion level, killing the last foe announces 'All foes
                    //   defeated!'; a scripted game mode that owns its win channel (TYPE_SCRIPTED,
                    //   mode active) never shows it; a scripted map whose mode failed to start
                    //   falls back to the toast.
                    // tests: ModesOnslaught.momentary_wipe_emits_no_classic_foes_toast
                    // tests: ModesOnslaught.classic_worlds_keep_the_foes_toast
                    // tests: ModesOnslaught.demoted_scripted_worlds_keep_the_foes_toast
                    // tests: KitSkeleton.warded_death_tells_nobody_and_counts_no_kill
                    // tests: SemWalker4.all_foes_defeated_is_announced_only_on_the_last_foe
                    if(!mode_owned &&
                       current_game->world->remaining_foes(this) == 1)  // This is the last foe
                    {
                        message = "All foes defeated!";
                        og::sim::emit_notification(current_game->sim_events, message);
                    }
                }
                else  // rule: A living killed by its own team's chain is announced without credit:
                    //   'NAME Dispelled!' for a named summon or timed walker, 'NAME DIED!' for a
                    //   named NPC, 'NAME Died!' for a hero, else the family's death message (e.g.
                    //   'SOLDIER SLAIN'), else 'SOMEONE DIED'.
                    // tests: SameTeamDeath.a_same_team_death_is_announced_without_credit_classic
                {
                    // Alert us of the death
                    // rule: A living killed by its own team's chain is announced without credit.
                    // tests: SameTeamDeath.a_same_team_death_is_announced_without_credit_classic
                    if ( (target->owner() || target->lifetime()) // summoned?
                            && (target->stats()->name.size() ) ) // and have name
                        message = std::format("{} Dispelled!", target->stats()->name);
                    else if (target->stats()->name.size()) // do we have an NPC name?
                        message = std::format("{} DIED!", target->stats()->name);
                    else if (target->myguy && target->myguy->name.size() )
                        message = std::format("{} Died!", target->myguy->name);
                    else
                    {
                        const auto* fd = get_family_descriptor(target->family());
                        // rule: A living killed by its own team's chain is announced without
                        //   credit.
                        // tests: SameTeamDeath.a_same_team_death_is_announced_without_credit_classic
                        message = fd ? fd->death_message : "SOMEONE DIED";
                    }
                    og::sim::emit_notification(current_game->sim_events, message);
                }
            }

            /* Blood splats at death */
            // Make temporary stain:
            blood = current_game->world->add_ob(Order::Weapon, FAMILY_BLOOD);
            // rule: A living's death leaves a blood stain (FAMILY_BLOOD weapon) on its spot and
            //   floor, in the victim's team, walk-over (ignore), growing.
            // tests: WalkerCombat.walker_attack_basic
            // tests: SemWalker4.a_living_death_leaves_a_blood_stain_unless_none_can_be_made
            if (blood)
            {
                blood->set_team_num(target->team_num());
                blood->set_ani_type(ANI_GROW);
                blood->set_ignore(1); // so that we can be walked over .. ?
                blood->set_floor(target->floor());  // splat where the victim died (A8)
                blood->setxy(target->xpos(),target->ypos());
            }
        }
        // rule: A living's death plays one of two death cries chosen by a world-RNG coin (DIE1 on
        //   1, DIE2 on 0); in the parity harness the cry (and its RNG draw) happens only when the
        //   old game's on-screen test says it is heard; in production it always draws.
        // tests: SemWalker4.a_death_cries_die1_or_die2_by_a_coin_unless_unheard
        if (targetorder == Order::Living && positional_sound_visible(this, SOUND_DIE1))
        {
            // rule: A living's death plays one of two death cries chosen by a world-RNG coin (DIE1
            //   on 1, DIE2 on 0).
            // tests: SemWalker4.a_death_cries_die1_or_die2_by_a_coin_unless_unheard
            if (current_game->world->rng_.next(2))
                og::sim::emit_sound(current_game->sim_events, SOUND_DIE1);
            else
                og::sim::emit_sound(current_game->sim_events, SOUND_DIE2);
        }

        target->set_dead(1);
        target->death(); // any special effect upon death ..
        // New Specials: the killer's family hears of the kill only now, when
        // death() has left the corpse and the life gem behind. A walker that
        // got back up inside death() (a warded skeleton) was not killed.
        // rule: The killer's family hears of a kill (on_kill hook, e.g. skeleton LEGION raising the
        //   dead) only after death() left the corpse, only if the victim stayed dead, and only if a
        //   living heads the hitter's chain.
        // tests: KitSeamsSmall.on_kill_fires_for_melee_projectile_and_explosion_kills_of_a_declaring_family
        // tests: KitSkeleton.warded_victim_is_not_a_kill
        if (targetorder == Order::Living && target->dead() &&
            credit != nullptr)
            og::script::hooks::on_kill(
                get_family_descriptor(static_cast<int>(
                    static_cast<unsigned char>(credit->family()))),
                credit, target);
    }
    set_collide_ob(nullptr);

    return 1;
}
