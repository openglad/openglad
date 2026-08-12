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
    if (IRandom* override_rng = gameplay_rng_override())
        return *override_rng;

    if (current_game != nullptr && current_game->world != nullptr)
        return current_game->world->rng_;

    assert(false && "combat math requires an injected RNG");
    std::abort();
}

bool positional_sound_visible(const walker* source, std::uint32_t sound_id)
{
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
    if (w == nullptr || w->stats() == nullptr)
        return 0;

    const std::int32_t target_level =
        (target != nullptr && target->stats() != nullptr) ? target->stats()->level() : 0;

    return compute_xp_from_action(action, w->stats()->level(), target_level, value, combat_rng());
}

float get_base_damage(walker* w)
{
    if (w == nullptr)
        return 0.0f;

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
    if (!(current_game && current_game->world && is_valid_score_team(team_num)))
        return;

    current_game->world->m_score[team_num] += points;
    og::sim::emit_event(current_game->sim_events, og::sim::EventKind::ScoreChange,
                        static_cast<std::uint32_t>(team_num), points);
}

void walker::do_heal_effects(walker* healer, walker* target, short amount)
{
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
    if (target->stats()->hitpoints() < 0)
        tempdamage = static_cast<short>(static_cast<float>(tempdamage) + target->stats()->hitpoints());

    if (current_game != nullptr && current_game->world != nullptr)
    {
        // Create hit effect
        const auto* efd = (query_order() == Order::FX) ? get_effect_family_descriptor(family()) : nullptr;
        if(query_order() != Order::FX || (efd && efd->creates_hit_effect))
        {
            walker* newob = current_game->world->add_ob(Order::FX, FAMILY_HIT);
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
                IRandom* ani_rng = cosmetic_rng();
                newob->set_ani_type(static_cast<char>(
                    1 + (ani_rng != nullptr ? ani_rng->next(3)
                                            : current_game->world->rng_.next(3))));
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

    if(tempdamage > 0)
    {
        target->set_hurt_flash(true);

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
    if (target->stats()->query_bit_flags(BIT_INVINCIBLE) ||
        target->invulnerable_left() != 0)
        return;

    // Record damage done for records ..
    if (attacker && attacker->myguy && target->query_order() == Order::Living)  // hit a living
    {
        attacker->myguy->total_damage += tempdamage;
        attacker->myguy->scen_damage += tempdamage;
    }

    // Deal the damage
    target->set_last_hitpoints(target->stats()->hitpoints());
    target->stats()->set_hitpoints(target->stats()->hitpoints() - tempdamage);

    do_hit_effects(attacker, target, tempdamage);

    if (target->stats()->hitpoints() < 0)
        tempdamage = static_cast<short>(static_cast<float>(tempdamage) + target->stats()->hitpoints());

    // Delay HP regeneration
    if(tempdamage > 0)
        target->set_regen_delay(50);

    if(target->myguy != nullptr)
    {
        target->myguy->scen_damage_taken += tempdamage;
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

    if (target && target->dead())
        return 0;

    //if ( (targetorder == Order::Living && is_friendly(target) ) ||
    if ( is_friendly(target) || (targetorder == Order::Treasure) )
        return 0;

    if (target->stats()->query_bit_flags(BIT_INVINCIBLE) ||
            target->invulnerable_left() != 0 )
        return 0;

    if (order() != Order::Living && owner())
        attacker = owner();
    else
        attacker = this;

    // who's the top on our chain (ie, weapon->summoned->mage)
    headguy = this;
    while (headguy->owner() && (headguy->owner() != headguy) )
        headguy = headguy->owner();

    // Score and kill semantics follow the actual attacker's team. The legacy
    // red-only checks made non-red company heroes deal damage without normal
    // projectile score, and treated their red victims as friendly deaths.
    playerteam = static_cast<short>(headguy->team_num());
    if (headguy->myguy != nullptr ||
        playerteam == current_game->world->my_team)
    {
        getscore = 1;
    }

    // Modify attack value based on things like magical attacks, etc.
    switch (targetorder) // generally going to be livings..
    {
        case Order::Living:
            // Hit a living target, so we get credit for a hit
            if (attacker->myguy)
            {
                attacker->myguy->total_hits++;
                attacker->myguy->scen_hits++;
            }

            if (stats_->query_bit_flags(BIT_MAGICAL))
            {
                const auto* fd = get_family_descriptor(targetfamily);
                if (fd)
                    tempdamage *= fd->magic_damage_modifier;
            }
            break; // end of living
        default:
            // We hit something, but it wasn't living, so don't count
            // as a shot, OR as a hit ..
            if (attacker->myguy)
            {
                attacker->myguy->total_shots--; // since we already counted it
                attacker->myguy->scen_shots--;
            }
            break;
    } // end of checking orders

    tempdamage -= get_damage_reduction(attacker, tempdamage, target);
    if (tempdamage < 0)
        tempdamage = 0;

    // Scripted-mode damage gate (level on_damage hook): nil keeps the
    // amount, a number replaces it (clamped >= 0), false cancels the hit
    // outright — no damage, no hit_response, no attribution stamp.
    // Early-outs on the level_hook_kinds bit, so classic paths are
    // byte-identical. `attacker` is the resolved local (owner for
    // projectiles), matching the score semantics above.
    const short gated_damage = og::script::hooks::level_damage_gate(
        target, attacker, static_cast<short>(tempdamage));
    if (gated_damage < 0)
        return 0;
    const short tempdamage_i = gated_damage;
    // Kill attribution (server-only transients): stamp the owner-chain ROOT
    // — weapon and summon kills credit the head, exactly like the score
    // path — AFTER the gate allowed a positive amount. A non-living root
    // (a scripted ball fx) never stamps, so its kills read as environment.
    if (tempdamage_i > 0 && headguy->query_order() == Order::Living)
        target->note_attacker(headguy, current_game->world->tick_count_);
    do_combat_damage(attacker, target, tempdamage_i);
    TRACE("walker", "attack: %s deals %d damage", attacker->stats_->name.c_str(), tempdamage_i);

    // Base exp from this successful hit.
    short attack_exp = exp_from_action(ExpAction::Attack, this, target, tempdamage_i);

    // Set our target to fighting our owner
    //in the case of our weapon hit something
    if (order() != Order::Living && owner())
    {
        owner()->set_foe(target);
        target->stats()->hit_response(owner());
        if (headguy->myguy)
            headguy->myguy->exp += static_cast<std::uint32_t>(attack_exp);
    }
    else  //melee combat, set target to hit_response to us
    {
        target->stats()->hit_response(this);
        if (myguy)
        {
            myguy->exp += static_cast<std::uint32_t>(attack_exp);
            if (getscore)
                award_score(team_num(), static_cast<std::uint32_t>(tempdamage_i)
                                      + static_cast<std::uint32_t>(target->stats()->level()));
        }
    }

    if (order() == Order::Weapon)
    {
        stats_->set_hitpoints( stats_->hitpoints() - tempdamage_i);
        set_damage(damage() - 1.0f);
        if (stats_->hitpoints() <= 0)
        {
            if (!stats_->query_bit_flags(BIT_IMMORTAL))
                set_dead(1);
            death();
        }
        //special effects
        const auto* wfd = get_weapon_family_descriptor(family());
        og::script::hooks::weapon_on_hit_target(wfd, this, target, owner());

    }

    // Positive score for hurting enemies, negative for us.
    if (owner() && targetorder != Order::Weapon && playerteam != target->team_num())
    {
        if (getscore)
            award_score(team_num(), static_cast<std::uint32_t>(tempdamage_i)
                                  + static_cast<std::uint32_t>(target->stats()->level()));
        if (order() != Order::Weapon && headguy->myguy)
            headguy->myguy->exp += static_cast<std::uint32_t>(attack_exp);
    }

    // Unconditional: how much damage the hit applied does not matter here.
    // A hit fully absorbed by damage reduction, or replaced with 0 by a level
    // on_damage hook, still reaches this check — which is why "return 0" from
    // that hook kills a target already sitting at 0 hp and only "return false"
    // (handled at the gate above) skips the hit entirely.
    if (target->stats()->hitpoints() <= 0)
    {
        if (targetorder == Order::Living)
        {
            if (playerteam > -1)
            {
                if (playerteam != target->team_num())
                {
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
                    if (target->stats()->name.size() && !(target->lifetime())
                            && (!target->owner()) ) // do we have an NPC name?
                    {
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
                    if(!mode_owned &&
                       current_game->world->remaining_foes(this) == 1)  // This is the last foe
                    {
                        message = "All foes defeated!";
                        og::sim::emit_notification(current_game->sim_events, message);
                    }
                }
                else
                {
                    // Alert us of the death
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
                        message = fd ? fd->death_message : "SOMEONE DIED";
                    }
                    og::sim::emit_notification(current_game->sim_events, message);
                }
            }

            /* Blood splats at death */
            // Make temporary stain:
            blood = current_game->world->add_ob(Order::Weapon, FAMILY_BLOOD);
            if (blood)
            {
                blood->set_team_num(target->team_num());
                blood->set_ani_type(ANI_GROW);
                blood->set_ignore(1); // so that we can be walked over .. ?
                blood->set_floor(target->floor());  // splat where the victim died (A8)
                blood->setxy(target->xpos(),target->ypos());
            }
        }
        if (targetorder == Order::Living && positional_sound_visible(this, SOUND_DIE1))
        {
            if (current_game->world->rng_.next(2))
                og::sim::emit_sound(current_game->sim_events, SOUND_DIE1);
            else
                og::sim::emit_sound(current_game->sim_events, SOUND_DIE2);
        }

        target->set_dead(1);
        target->death(); // any special effect upon death ..
    }
    set_collide_ob(nullptr);

    return 1;
}
