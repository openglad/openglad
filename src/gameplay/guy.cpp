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
#include <array>
#include <cassert>
#include <cstdint>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <cstring>
#include <limits>

// Zardus: PORT, exception doesn't compile (dos thing?): int matherr(struct exception *);


const char* get_family_string(std::int32_t family);

namespace
{
struct UInt128
{
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
};

constexpr unsigned kStatCostFixedShift = 48;
constexpr std::uint64_t kStatCostFixedOne = 1ull << kStatCostFixedShift;
constexpr std::uint64_t kUInt16Mask = 0xFFFFu;

UInt128 make_u128(std::uint64_t value)
{
    return {0, value};
}

bool operator<=(const UInt128& lhs, const UInt128& rhs)
{
    return lhs.hi < rhs.hi || (lhs.hi == rhs.hi && lhs.lo <= rhs.lo);
}

std::array<std::uint16_t, 8> to_u16_limbs(const UInt128& value)
{
    return {
        static_cast<std::uint16_t>(value.lo & kUInt16Mask),
        static_cast<std::uint16_t>((value.lo >> 16) & kUInt16Mask),
        static_cast<std::uint16_t>((value.lo >> 32) & kUInt16Mask),
        static_cast<std::uint16_t>((value.lo >> 48) & kUInt16Mask),
        static_cast<std::uint16_t>(value.hi & kUInt16Mask),
        static_cast<std::uint16_t>((value.hi >> 16) & kUInt16Mask),
        static_cast<std::uint16_t>((value.hi >> 32) & kUInt16Mask),
        static_cast<std::uint16_t>((value.hi >> 48) & kUInt16Mask),
    };
}

std::array<std::uint16_t, 4> to_u16_limbs(std::uint64_t value)
{
    return {
        static_cast<std::uint16_t>(value & kUInt16Mask),
        static_cast<std::uint16_t>((value >> 16) & kUInt16Mask),
        static_cast<std::uint16_t>((value >> 32) & kUInt16Mask),
        static_cast<std::uint16_t>((value >> 48) & kUInt16Mask),
    };
}

UInt128 multiply_fixed(const UInt128& lhs, std::uint64_t rhs)
{
    const auto lhs_limbs = to_u16_limbs(lhs);
    const auto rhs_limbs = to_u16_limbs(rhs);

    std::array<std::uint64_t, 13> product{};
    for (std::size_t i = 0; i < lhs_limbs.size(); ++i)
    {
        for (std::size_t j = 0; j < rhs_limbs.size(); ++j)
            product[i + j] += static_cast<std::uint64_t>(lhs_limbs[i]) * rhs_limbs[j];
    }

    for (std::size_t i = 0; i + 1 < product.size(); ++i)
    {
        product[i + 1] += product[i] >> 16;
        product[i] &= kUInt16Mask;
    }

    assert(product[11] == 0);
    assert(product[12] == 0);

    return {
        (static_cast<std::uint64_t>(product[7]))
            | (static_cast<std::uint64_t>(product[8]) << 16)
            | (static_cast<std::uint64_t>(product[9]) << 32)
            | (static_cast<std::uint64_t>(product[10]) << 48),
        (static_cast<std::uint64_t>(product[3]))
            | (static_cast<std::uint64_t>(product[4]) << 16)
            | (static_cast<std::uint64_t>(product[5]) << 32)
            | (static_cast<std::uint64_t>(product[6]) << 48),
    };
}

UInt128 pow_fixed(std::uint64_t base, int exponent)
{
    UInt128 result = make_u128(kStatCostFixedOne);
    for (int i = 0; i < exponent; ++i)
        result = multiply_fixed(result, base);
    return result;
}

std::uint64_t twentieth_root_fixed(std::uint32_t value)
{
    if (value == 0)
        return 0;

    std::uint64_t low = 0;
    std::uint64_t high = 3ull * kStatCostFixedOne;
    const UInt128 target = make_u128(static_cast<std::uint64_t>(value) * kStatCostFixedOne);
    while (low + 1 < high)
    {
        const std::uint64_t mid = low + (high - low) / 2;
        if (pow_fixed(mid, 20) <= target)
            low = mid;
        else
            high = mid;
    }
    return low;
}

std::uint64_t multiply_u64_shift_48(std::uint64_t lhs, std::uint64_t rhs)
{
    const auto lhs_limbs = to_u16_limbs(lhs);
    const auto rhs_limbs = to_u16_limbs(rhs);

    std::array<std::uint64_t, 9> product{};
    for (std::size_t i = 0; i < lhs_limbs.size(); ++i)
    {
        for (std::size_t j = 0; j < rhs_limbs.size(); ++j)
            product[i + j] += static_cast<std::uint64_t>(lhs_limbs[i]) * rhs_limbs[j];
    }

    for (std::size_t i = 0; i + 1 < product.size(); ++i)
    {
        product[i + 1] += product[i] >> 16;
        product[i] &= kUInt16Mask;
    }

    return (static_cast<std::uint64_t>(product[3]))
        | (static_cast<std::uint64_t>(product[4]) << 16)
        | (static_cast<std::uint64_t>(product[5]) << 32)
        | (static_cast<std::uint64_t>(product[6]) << 48);
}

UInt128 raise_stat_cost_curve(std::int32_t value)
{
    if (value <= 0)
        return make_u128(0);

    // Legacy cost curve is value^1.85. Compute it with deterministic
    // fixed-point math so life-gem values no longer depend on libm exponentiation.
    const std::uint64_t root = twentieth_root_fixed(static_cast<std::uint32_t>(value));
    return pow_fixed(root, 37);
}

std::int32_t stat_cost_curve_contribution(std::int32_t value, std::int32_t stat_cost)
{
    if (value <= 0 || stat_cost <= 0)
        return 0;

    const UInt128 curve = raise_stat_cost_curve(value);
    const std::uint64_t cost = static_cast<std::uint64_t>(stat_cost);
    const std::uint64_t contribution =
        (curve.hi * cost << 16) + multiply_u64_shift_48(curve.lo, cost);
    return static_cast<std::int32_t>(std::min<std::uint64_t>(
        contribution,
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())));
}

int next_guy_id()
{
    if (current_game != nullptr && current_game->world != nullptr)
        return current_game->world->guy_id_counter++;

    static int fallback_id_counter = 0;
    return fallback_id_counter++;
}
} // namespace

int MAX(int a,int b)
{
	if (a < b)
		return b;
	else
		return a;
}

guy::guy()
{
	name = "SOLDIER";
	family = FAMILY_SOLDIER;
	strength = 0;
	dexterity = 0;
	constitution = 0;
	intelligence = 0;
	level = 1;
	armor = 0;
	exp = 0;
	kills = 0;
	level_kills = 0;
	total_damage = total_hits = total_shots = 0;
	teamnum = 0;
	scen_damage = 0;
    scen_kills = 0;
    scen_damage_taken = 0;
    scen_min_hp = 5000000;
    scen_shots = 0;
    scen_hits = 0;
	
	id = next_guy_id();
}

// Set defaults for various types
guy::guy(int whatfamily)
{

	family = static_cast<char>(whatfamily);
	kills = 0;
	level_kills = 0;
	total_damage = total_hits = total_shots = 0;
	exp = 0;
	teamnum = 0;
	
	scen_damage = 0;
    scen_kills = 0;
    scen_damage_taken = 0;
    scen_min_hp = 5000000;
    scen_shots = 0;
    scen_hits = 0;
	
	// Set stats from family registry
	auto* fd = get_family_descriptor(whatfamily);
	if (fd)
	{
        strength = static_cast<short>(fd->base_stats[0]);
        dexterity = static_cast<short>(fd->base_stats[1]);
        constitution = static_cast<short>(fd->base_stats[2]);
        intelligence = static_cast<short>(fd->base_stats[3]);
        armor = static_cast<short>(fd->base_stats[4]);
        level = static_cast<short>(fd->base_stats[5]);
        name = fd->name;
	}
	else
    {
        strength = 12;
        dexterity = 6;
        constitution = 12;
        intelligence = 8;
        armor = 6;
        level = 1;
        name = "BEAST";
    }
	
	id = next_guy_id();
}


guy::guy(const guy& copy)
    : family(copy.family)
    , strength(copy.strength), dexterity(copy.dexterity), constitution(copy.constitution), intelligence(copy.intelligence)
    , armor(copy.armor)
    , exp(copy.exp), kills(copy.kills), level_kills(copy.level_kills)
    , total_damage(copy.total_damage), total_hits(copy.total_hits), total_shots(copy.total_shots)
    , teamnum(copy.teamnum)
    , scen_damage(copy.scen_damage)
    , scen_kills(copy.scen_kills)
    , scen_damage_taken(copy.scen_damage_taken)
    , scen_min_hp(copy.scen_min_hp)
    , scen_shots(copy.scen_shots)
    , scen_hits(copy.scen_hits)
    , id(copy.id)
    , level(copy.level)
    , owner_player_index(copy.owner_player_index)
    , owner_save_slot(copy.owner_save_slot)
{
    name = copy.name;
}

guy::~guy()
{
    
}

std::int32_t guy::query_heart_value() // how much are we worth?
{
	guy normal(family); // for base comparisons
	std::int32_t cost=0, temp;

	auto* fd = get_family_descriptor(static_cast<int>(family));
	if (!fd)
		return 0;

	// Get strength cost ..
	temp = strength - normal.strength;
	temp = MAX(temp,0);
	cost += stat_cost_curve_contribution(temp, static_cast<std::int32_t>(fd->stat_costs[0]));

	// Get dexterity cost ..
	temp = dexterity - normal.dexterity;
	temp = MAX(temp,0);
	cost += stat_cost_curve_contribution(temp, static_cast<std::int32_t>(fd->stat_costs[1]));

	// Get constitution cost ..
	temp = constitution - normal.constitution;
	temp = MAX(temp,0);
	cost += stat_cost_curve_contribution(temp, static_cast<std::int32_t>(fd->stat_costs[2]));

	// Get intelligence cost ..
	temp = intelligence - normal.intelligence;
	temp = MAX(temp,0);
	cost += stat_cost_curve_contribution(temp, static_cast<std::int32_t>(fd->stat_costs[3]));

	// Get armor cost ..
	temp = armor - normal.armor;
	temp = MAX(temp,0);
	cost += stat_cost_curve_contribution(temp, static_cast<std::int32_t>(fd->stat_costs[4]));

	// Add in the base cost value for the guy ..
	cost += fd->hiring_cost;

	return cost;

}

std::uint32_t calculate_exp(std::int32_t level);





std::int32_t calculate_level(std::uint32_t experience)
{
	std::int32_t result=1;

	while (calculate_exp(result) <= experience)
		result++;
	return (result-1);
}

std::uint32_t calculate_exp(std::int32_t level)
{


	/*
	
	fn = ( (8000*(level+10)) / 10) + calculate_exp(level-1);
	excel: =( (8000*(F4+10)) / 10) + G3
    Level	XP
    1	0
    2	9600
    3	20000
    4	31200
    5	43200
    6	56000
    7	69600
    8	84000
    9	99200
    10	115200
    This is practically linear, so each level costs about 10000 more than the previous.

	*/
	if(level <= 1)
        return 0;

    // Iterative unrolling of the original recurrence to avoid stack
    // exhaustion when 'level' comes from untrusted save-file bytes (up to
    // SHRT_MAX). Each iteration reproduces the exact integer arithmetic and
    // accumulation order of the former recursive form.
    std::uint32_t result = 0;
    for(int k = 2; k <= level; ++k)
    {
        int level_1 = k - 1;
        int level_2 = k - 2; // k starts at 2, so level_2 is always >= 0
        result += static_cast<std::uint32_t>(8000 + 2000*level_1 + 4000*level_2);
    }
    return result;
}

void apply_level_up(guy* self, std::int32_t level_diff, const LevelUpGains& g)
{
    self->strength = static_cast<short>(static_cast<std::int32_t>(self->strength) + g.str * level_diff);
    self->dexterity = static_cast<short>(static_cast<std::int32_t>(self->dexterity) + g.dex * level_diff);
    self->constitution = static_cast<short>(static_cast<std::int32_t>(self->constitution) + g.con * level_diff);
    self->intelligence = static_cast<short>(static_cast<std::int32_t>(self->intelligence) + g.intel * level_diff);
    self->armor = static_cast<short>(static_cast<std::int32_t>(self->armor) + g.armor * level_diff);
}

void apply_difficulty_scaling(living* self, std::uint32_t level, const DifficultyScaling& s)
{
    const float levmult = static_cast<float>(level) * static_cast<float>(level);
    const float level_f = static_cast<float>(level);
    self->stats()->set_max_hitpoints(self->stats()->max_hitpoints() + s.hp * levmult);
    self->stats()->set_max_magicpoints(self->stats()->max_magicpoints() + s.mp * levmult);
    self->set_damage(self->damage() + s.dmg * level_f);
    self->stats()->set_armor(self->stats()->armor() + s.armor * levmult);
}

bool check_special_ai_distance(living* self, std::uint32_t threshold)
{
    if (self->foe())
    {
        std::uint32_t distance = static_cast<std::uint32_t>(self->distance_to_ob(self->foe()));
        return (distance < threshold);
    }
    self->set_foe(current_game->world->find_near_foe(self));
    if (!self->foe())
        return false;
    std::uint32_t distance = static_cast<std::uint32_t>(self->distance_to_ob(self->foe()));
    return (distance < threshold);
}

void guy::upgrade_to_level(short new_level, bool set_xp)
{
    std::int32_t level_diff = static_cast<std::int32_t>(new_level) - static_cast<std::int32_t>(this->level);

    auto* fd = get_family_descriptor(family);
    if (fd && fd->level_up)
    {
        fd->level_up(this, level_diff);
    }
    else
    {
        apply_level_up(this, level_diff, kDefaultLevelUpGains);
    }

    this->level = new_level;
    if(set_xp)
        exp = calculate_exp(new_level);
}

// Derived stat calculations
float guy::get_hp_bonus() const
{
    return 10.0f + static_cast<float>(constitution) * 3.0f;
}

float guy::get_mp_bonus() const
{
    return 10.0f + static_cast<float>(intelligence) * 3.0f;
}

float guy::get_damage_bonus() const
{
    return strength/4.0f;
}

float guy::get_armor_bonus() const
{
    return armor;
}

float guy::get_speed_bonus() const
{
    return dexterity/54.0f;
}

float guy::get_fire_frequency_bonus() const
{
    return dexterity/47.0f;
}




void guy::update_derived_stats(walker* w)
{
    guy* temp_guy = w->myguy;
    if (current_game && current_game->world)
        current_game->world->set_entity_derived_stats(w, Order::Living, temp_guy->family);
    
    
    w->stats()->set_max_hitpoints(w->stats()->max_hitpoints() + temp_guy->get_hp_bonus());
    w->stats()->set_hitpoints(w->stats()->max_hitpoints());
    
    // No class base value for MP...
    w->stats()->set_max_magicpoints(temp_guy->get_mp_bonus());
    w->stats()->set_magicpoints(w->stats()->max_magicpoints());

    w->set_damage(w->damage() + temp_guy->get_damage_bonus());

    // No class base value for armor...
    w->stats()->set_armor(temp_guy->get_armor_bonus());

    //stepsize makes us run faster, max for a non-weapon is 12
    w->set_stepsize(w->stepsize() + temp_guy->get_speed_bonus());
    if (w->stepsize() > 12)
        w->set_stepsize(12);
    w->set_normal_stepsize(w->stepsize());

    //fire_frequency makes us fire faster, min is 1
    w->set_fire_frequency(w->fire_frequency() - temp_guy->get_fire_frequency_bonus());
    if (w->fire_frequency() < 1)
        w->set_fire_frequency(1);

    // Per-family walker creation hooks (e.g. soldier weapons_left)
    {
        const auto* fd = get_family_descriptor(w->family());
        if (fd && fd->on_create)
            fd->on_create(w);
    }

    // Set the heal delay ..
    w->stats()->set_max_heal_delay(REGEN);
    {
        float heal_delay = static_cast<float>(temp_guy->constitution) + static_cast<float>(temp_guy->strength) / 6.0f + 20.0f + 1000.0f;
        w->stats()->set_current_heal_delay(static_cast<std::int32_t>(heal_delay)); // for purposes of calculation only
    }

    while (w->stats()->current_heal_delay() > REGEN)
    {
        w->stats()->set_current_heal_delay(w->stats()->current_heal_delay() - REGEN);
        w->stats()->set_heal_per_round(w->stats()->heal_per_round() + 1);
    } // this takes care of the integer part, now calculate the fraction

    if (w->stats()->current_heal_delay() > 1)
    {
        w->stats()->set_max_heal_delay(
            w->stats()->max_heal_delay() /
            static_cast<std::int32_t>(w->stats()->current_heal_delay() + 1));
    }
    w->stats()->set_current_heal_delay(0); //start off without healing

    //make sure we have at least a 2 wait, otherwise we should have
    //calculated our heal_per_round as one higher, and the math must
    //have been screwed up some how
    if (w->stats()->max_heal_delay() < 2)
        w->stats()->set_max_heal_delay(2);

    // Set the magic delay ..
    w->stats()->set_max_magic_delay(REGEN);
    w->stats()->set_current_magic_delay(temp_guy->intelligence * 45 + temp_guy->dexterity * 15 + 200);

    while (w->stats()->current_magic_delay() > REGEN)
    {
        w->stats()->set_current_magic_delay(w->stats()->current_magic_delay() - REGEN);
        w->stats()->set_magic_per_round(w->stats()->magic_per_round() + 1);
    } // this takes care of the integer part, now calculate the fraction

    if (w->stats()->current_magic_delay() > 1)
    {
        w->stats()->set_max_magic_delay(
            w->stats()->max_magic_delay() /
            static_cast<std::int32_t>(w->stats()->current_magic_delay() + 1));
    }
    w->stats()->set_current_magic_delay(0); //start off without magic regen

    //make sure we have at least a 2 wait, otherwise we should have
    //calculated our magic_per_round as one higher, and the math must
    //have been screwed up some how
    if (w->stats()->max_magic_delay() < 2)
        w->stats()->set_max_magic_delay(2);
}

// guy_create_walker_owned and guy_create_and_add_walker are free functions
// in src/runtime/guy_create.cpp (declared in runtime/guy_create.h) because
// they depend on screen* (runtime layer).
