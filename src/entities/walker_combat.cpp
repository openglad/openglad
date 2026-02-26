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
#include <openglad/core/stats.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/weapon_family_descriptor.h>
#include <openglad/entities/family_registries.h>
#include <openglad/entities/effect_family_descriptor.h>
#include <openglad/entities/guy.h>
#include <openglad/entities/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/data/gparser.h>
#include <openglad/sim/sim_emit.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/test_trace.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/legacy/soundob.h>
#include <cmath>
#include <format>

// namespace removed: rng/config wrappers replaced with SimEntity fields

namespace {
class WorldRandomAdapter final : public IRandom {
public:
    explicit WorldRandomAdapter(og::sim::SimRandom& sim_rng) : sim_rng_(sim_rng) {}
    std::uint32_t next(std::uint32_t max_exclusive) override { return sim_rng_.next(max_exclusive); }
private:
    og::sim::SimRandom& sim_rng_;
};
} // namespace

// Thin adapter: delegates to combat_math pure functions
short exp_from_action(ExpAction action, walker* w, walker* target, short value)
{
    if (!w)
        return 0;
    WorldRandomAdapter rng_adapter(og::gameplay::current_game->world->rng_);
    const std::uint32_t target_level = target ? target->stats()->level : w->stats()->level;
    return compute_xp_from_action(action, w->stats()->level, target_level,
                                  value, rng_adapter);
}

float get_base_damage(walker* w)
{
    WorldRandomAdapter rng_adapter(og::gameplay::current_game->world->rng_);
    return compute_base_damage(w->damage, rng_adapter);
}

float get_damage_reduction(walker* w, float damage, walker* target)
{
    (void)w; // kept for minimal churn at call sites
    return compute_damage_reduction(damage, target->stats()->armor);
}

static bool is_valid_score_team(unsigned char team_num)
{
    return team_num < SCORE_TEAM_COUNT;
}

void walker::do_heal_effects(walker* healer, walker* target, short amount)
{
    if(healer)
        healer->damage_numbers.push_back(DamageNumber(target->xpos + target->sizex/2, target->ypos, amount, 56));
    target->damage_numbers.push_back(DamageNumber(target->xpos + target->sizex/2, target->ypos, amount, 56));
}

void walker::do_hit_effects(walker* attacker, walker* target, short tempdamage)
{
    // Orange numbers for the attacker to see
    if(attacker)
        attacker->damage_numbers.push_back(DamageNumber(target->xpos + target->sizex/2, target->ypos, tempdamage, 235));
    // RED numbers for the target to see
    target->damage_numbers.push_back(DamageNumber(target->xpos + target->sizex/2, target->ypos, tempdamage, RED));
    if (target->stats()->hitpoints < 0)
        tempdamage = static_cast<short>(static_cast<float>(tempdamage) + target->stats()->hitpoints);

    if(og::gameplay::current_game->world->create_hit_effects)
    {
        // Create hit effect
        const auto* efd = (query_order() == Order::FX) ? get_effect_family_descriptor(family) : nullptr;
        if(query_order() != Order::FX || (efd && efd->creates_hit_effect))
        {
           walker* newob = og::gameplay::current_game->world->add_ob(Order::FX, FAMILY_HIT);
            if (newob)
            {
                newob->owner = target;
                newob->team_num = team_num;
                newob->stats()->level = 1;
                newob->damage = 0;
                newob->ani_type = static_cast<char>(1 + rand()%3);
                if(attacker == this)
                {
                    newob->center_on(target);
                }
                else
                {
                    // A projectile
                    newob->center_on(this);  // Make the hit effect start at the projectile position
                    // Then move it a little closer to its target (average)
                    newob->setworldxy((target->worldx_ + target->sizex/2 + newob->worldx_)/2, (target->worldy_ + target->sizey/2 + newob->worldy_)/2);
                }
            }
        }
    }

    if(tempdamage > 0)
    {
        target->hurt_flash = true;

        if(target->query_order() == Order::Living)
        {
            target->hit_recoil = 1.0f;
            const float dy = (static_cast<float>(target->ypos) + static_cast<float>(target->sizey) * 0.5f) -
                             (static_cast<float>(ypos) + static_cast<float>(sizey) * 0.5f);
            const float dx = (static_cast<float>(target->xpos) + static_cast<float>(target->sizex) * 0.5f) -
                             (static_cast<float>(xpos) + static_cast<float>(sizex) * 0.5f);
            target->hit_recoil_angle = atan2f(dy, dx);
        }
    }
}

void walker::do_combat_damage(walker* attacker, walker* target, short tempdamage)
{
    // Record damage done for records ..
    if (attacker && attacker->myguy && target->query_order() == Order::Living)  // hit a living
    {
        attacker->myguy->total_damage += tempdamage;
        attacker->myguy->scen_damage += tempdamage;
    }

    // Deal the damage
    target->last_hitpoints = target->stats()->hitpoints;
    target->stats()->hitpoints -= tempdamage;

    do_hit_effects(attacker, target, tempdamage);

    if (target->stats()->hitpoints < 0)
        tempdamage = static_cast<short>(static_cast<float>(tempdamage) + target->stats()->hitpoints);

    // Delay HP regeneration
    if(tempdamage > 0)
        target->regen_delay_ = 50;

    if(target->myguy != nullptr)
    {
        target->myguy->scen_damage_taken += tempdamage;
        if(target->myguy->scen_min_hp > target->stats()->hitpoints)
            target->myguy->scen_min_hp = target->stats()->hitpoints;
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
    char targetfamily= target->family;
    walker *attacker; // us or our owner ..
    static short tom = 0;

    if (myguy != nullptr || team_num == 0)
        getscore = 1;

    if (target && target->dead)
        return 0;

    //if ( (targetorder == Order::Living && is_friendly(target) ) ||
    if ( is_friendly(target) || (targetorder == Order::Treasure) )
        return 0;

    if (target->stats()->query_bit_flags(BIT_INVINCIBLE) ||
            target->invulnerable_left != 0 )
        return 0;

    if (order != Order::Living && owner)
        attacker = owner;
    else
        attacker = this;

    // who's the top on our chain (ie, weapon->summoned->mage)
    headguy = this;
    while (headguy->owner && (headguy->owner != headguy) )
        headguy = headguy->owner;

    if (headguy->myguy && headguy->user == 0 && order == Order::Weapon)
        tom++;

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

    const short tempdamage_i = static_cast<short>(tempdamage);
    do_combat_damage(attacker, target, tempdamage_i);
    TRACE("walker", "attack: %s deals %d damage", attacker->stats_->name.c_str(), tempdamage_i);

    // Base exp from this successful hit.
    short attack_exp = exp_from_action(ExpAction::Attack, this, target, tempdamage_i);

    // Set our target to fighting our owner
    //in the case of our weapon hit something
    if (order != Order::Living && owner)
    {
        owner->foe = target;
        target->stats()->hit_response(owner);
    }
    else  //melee combat, set target to hit_response to us
    {
        target->stats()->hit_response(this);
    }

    if (order == Order::Weapon)
    {
        stats_->hitpoints -= tempdamage_i;
        damage--;
        if (stats_->hitpoints <= 0)
        {
            if (!stats_->query_bit_flags(BIT_IMMORTAL))
                dead = 1;
            death();
        }
        //special effects
        const auto* wfd = get_weapon_family_descriptor(family);
        if (wfd && wfd->on_hit_target)
            wfd->on_hit_target(this, target, owner);

    }

    playerteam = 0;

    // Award base hit rewards once per successful enemy hit.
    if (targetorder == Order::Living && playerteam != target->team_num)
    {
        if (headguy->myguy)
            headguy->myguy->exp += attack_exp;
        if (getscore && is_valid_score_team(team_num))
        {
            og::gameplay::current_game->world->m_score[team_num] += static_cast<std::uint32_t>(tempdamage_i)
                + static_cast<std::uint32_t>(target->stats()->level);
        }
    }

    if (target->stats()->hitpoints <= 0)
    {
        if (targetorder == Order::Living)
        {
            if (playerteam > -1)
            {
                if (playerteam != target->team_num)
                {
                    if (headguy->myguy)  // headguy can == this
                    {
                        headguy->myguy->exp += exp_from_action(ExpAction::Kill, this, target, 0);
                        headguy->myguy->kills++;
                        headguy->myguy->scen_kills++;
                        headguy->myguy->level_kills += target->stats()->level;
                    }
                    //else if (myguy)
                    //{
                    //  myguy->exp += newexp + (8 * target->stats()->level);
                    //  myguy->kills++;
                    //  myguy->level_kills += target->stats()->level;
                    //}
                    if (getscore && is_valid_score_team(team_num))
                    {
                        og::gameplay::current_game->world->m_score[team_num] += static_cast<std::uint32_t>(tempdamage_i)
                            + static_cast<std::uint32_t>(10 * target->stats()->level);
                    }
                    // If named, alert us of the enemy's death
                    if (target->stats()->name.size() && !(target->lifetime)
                            && (!target->owner) ) // do we have an NPC name?
                    {
                        message = std::format("ENEMY DEATH: {} DIED!", target->stats()->name);
                        og::sim::emit_notification(og::gameplay::current_game->sim_events, message);
                    }
                    if(og::gameplay::current_game->world->remaining_foes(this) == 1)  // This is the last foe
                    {
                        message = "All foes defeated!";
                        og::sim::emit_notification(og::gameplay::current_game->sim_events, message);
                    }
                }
                else
                {
                    // Alert us of the death
                    if ( (target->owner || target->lifetime) // summoned?
                            && (target->stats()->name.size() ) ) // and have name
                        message = std::format("{} Dispelled!", target->stats()->name);
                    else if (target->stats()->name.size()) // do we have an NPC name?
                        message = std::format("{} DIED!", target->stats()->name);
                    else if (target->myguy && target->myguy->name.size() )
                        message = std::format("{} Died!", target->myguy->name);
                    else
                    {
                        const auto* fd = get_family_descriptor(target->family);
                        message = fd ? fd->death_message : "SOMEONE DIED";
                    }
                    og::sim::emit_notification(og::gameplay::current_game->sim_events, message);
                }
            }

            /* Blood splats at death */
            // Make temporary stain:
            blood = og::gameplay::current_game->world->add_ob(Order::Weapon, FAMILY_BLOOD);
            blood->team_num = target->team_num;
            blood->ani_type = ANI_GROW;
            blood->ignore = 1; // so that we can be walked over .. ?
            blood->setxy(target->xpos,target->ypos);
        }
        if (targetorder == Order::Living)
        {
            if (og::gameplay::current_game->world->rng_.next(2))
                og::sim::emit_sound(og::gameplay::current_game->sim_events, SOUND_DIE1);
            else
                og::sim::emit_sound(og::gameplay::current_game->sim_events, SOUND_DIE2);
        }

        target->dead = 1;
        target->death(); // any special effect upon death ..
    }
    collide_ob = nullptr;

    return 1;
}
