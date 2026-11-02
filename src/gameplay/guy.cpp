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
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/script/family_hooks.h>
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
    // plumbing: limb-multiply/carry loops of the fixed-point heart-value curve; see guy.cpp:181
    for (std::size_t i = 0; i < lhs_limbs.size(); ++i)
    {
        // plumbing: limb-multiply/carry loops of the fixed-point heart-value curve; see guy.cpp:181
        for (std::size_t j = 0; j < rhs_limbs.size(); ++j)
            product[i + j] += static_cast<std::uint64_t>(lhs_limbs[i]) * rhs_limbs[j];
    }

    // plumbing: limb-multiply/carry loops of the fixed-point heart-value curve; see guy.cpp:181
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
    // plumbing: repeated fixed-point multiply (pow); see guy.cpp:181
    for (int i = 0; i < exponent; ++i)
        result = multiply_fixed(result, base);
    return result;
}

std::uint64_t twentieth_root_fixed(std::uint32_t value)
{
    // plumbing: fixed-point 20th-root binary search (zero short-circuit, bisection arms) used by
    //   the deterministic value^1.85 curve; observable only through query_heart_value, see
    //   guy.cpp:181
    if (value == 0)
        return 0;

    std::uint64_t low = 0;
    std::uint64_t high = 3ull * kStatCostFixedOne;
    const UInt128 target = make_u128(static_cast<std::uint64_t>(value) * kStatCostFixedOne);
    // plumbing: fixed-point 20th-root binary search (zero short-circuit, bisection arms) used by
    //   the deterministic value^1.85 curve; observable only through query_heart_value, see
    //   guy.cpp:181
    while (low + 1 < high)
    {
        const std::uint64_t mid = low + (high - low) / 2;
        // plumbing: fixed-point 20th-root binary search (zero short-circuit, bisection arms) used
        //   by the deterministic value^1.85 curve; observable only through query_heart_value, see
        //   guy.cpp:181
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
    // plumbing: 64x64>>48 limb multiply loops; see guy.cpp:181
    for (std::size_t i = 0; i < lhs_limbs.size(); ++i)
    {
        // plumbing: 64x64>>48 limb multiply loops; see guy.cpp:181
        for (std::size_t j = 0; j < rhs_limbs.size(); ++j)
            product[i + j] += static_cast<std::uint64_t>(lhs_limbs[i]) * rhs_limbs[j];
    }

    // plumbing: 64x64>>48 limb multiply loops; see guy.cpp:181
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
    // rule: A trained stat adds to a hero's worth (life-gem/heart value and price) by (points above
    //   the family base)^1.85 x the family's per-stat cost, computed deterministically; a stat at
    //   or below base, or a family with a zero cost for that stat, adds nothing; the sum saturates
    //   at INT32_MAX.
    // tests: GuyCalcs.guy_query_heart_value_with_stats
    // tests: GuyCalcs.guy_query_heart_value_large_delta_matches_legacy_curve
    // tests: GuyExtended.guy_round10_query_heart_value_clamps_negative_stat_deltas_to_base_cost
    // tests: GuyExtended.guy_query_heart_value_all_families
    // tests: SemGuy.a_stat_trained_on_a_family_that_prices_it_at_zero_adds_nothing_to_its_worth
    if (value <= 0)
        return make_u128(0);

    // Legacy cost curve is value^1.85. Compute it with deterministic
    // fixed-point math so life-gem values no longer depend on libm exponentiation.
    const std::uint64_t root = twentieth_root_fixed(static_cast<std::uint32_t>(value));
    return pow_fixed(root, 37);
}

std::int32_t stat_cost_curve_contribution(std::int32_t value, std::int32_t stat_cost)
{
    // rule: Part of the rule above that begins "A trained stat adds to a hero's worth
    //   (life-gem/heart ..."
    // tests: GuyCalcs.guy_query_heart_value_with_stats
    // tests: GuyCalcs.guy_query_heart_value_large_delta_matches_legacy_curve
    // tests: GuyExtended.guy_round10_query_heart_value_clamps_negative_stat_deltas_to_base_cost
    // tests: GuyExtended.guy_query_heart_value_all_families
    // tests: SemGuy.a_stat_trained_on_a_family_that_prices_it_at_zero_adds_nothing_to_its_worth
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
    // plumbing: guy id allocation: the world's guy_id_counter when a world exists, else a static
    //   fallback counter (no world: editor/tests)
    if (current_game != nullptr && current_game->world != nullptr)
        return current_game->world->guy_id_counter++;

    static int fallback_id_counter = 0;
    return fallback_id_counter++;
}
} // namespace

int MAX(int a,int b)
{
	// rule: A stat trained BELOW the family base is not a discount: each stat's delta is floored at
	//   0 before pricing (same rule as guy.cpp:181).
	// tests: GuyExtended.guy_batch5_max_helper_and_more_unknown_family_paths
	// tests: GuyExtended.guy_round10_query_heart_value_clamps_negative_stat_deltas_to_base_cost
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
	// rule: A new recruit starts with its family's base stat block, level and name; an unregistered
	//   family id gets the fallback BEAST block 12/6/12/8 armor 6, level 1.
	// tests: GuyCalcs.guy_family_constructor_applies_each_familys_base_stat_block
	// tests: GuyExtended.guy_unknown_family_fallback_and_zero_heart_value
	// tests: GuyExtended.guy_batch5_max_helper_and_more_unknown_family_paths
	if (fd)
	{
        strength = static_cast<short>(fd->base_stats[StatAxis::Strength]);
        dexterity = static_cast<short>(fd->base_stats[StatAxis::Dexterity]);
        constitution = static_cast<short>(fd->base_stats[StatAxis::Constitution]);
        intelligence = static_cast<short>(fd->base_stats[StatAxis::Intelligence]);
        armor = static_cast<short>(fd->base_stats[StatAxis::Armor]);
        // should always be 1...
        level = static_cast<short>(fd->base_stats[StatAxis::Level]);
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


guy::guy(const guy& copy) = default;

guy::~guy()
{
    
}

std::int32_t guy::query_heart_value() // how much are we worth?
{
	guy normal(family); // for base comparisons
	std::int32_t cost=0, temp;

	auto* fd = get_family_descriptor(static_cast<int>(family));
	// rule: A guy of an unregistered family is worth nothing (heart value 0).
	// tests: GuyExtended.guy_unknown_family_fallback_and_zero_heart_value
	// tests: GuyExtended.guy_batch5_max_helper_and_more_unknown_family_paths
	// tests: GuyExtended.guy_query_heart_value_all_families
	// tests: GuyCalcs.guy_query_heart_value_with_stats
	if (!fd)
		return 0;

	// Get strength cost ..
	temp = strength - normal.strength;
	temp = MAX(temp,0);
	cost += stat_cost_curve_contribution(temp, static_cast<std::int32_t>(fd->stat_costs[StatAxis::Strength]));

	// Get dexterity cost ..
	temp = dexterity - normal.dexterity;
	temp = MAX(temp,0);
	cost += stat_cost_curve_contribution(temp, static_cast<std::int32_t>(fd->stat_costs[StatAxis::Dexterity]));

	// Get constitution cost ..
	temp = constitution - normal.constitution;
	temp = MAX(temp,0);
	cost += stat_cost_curve_contribution(temp, static_cast<std::int32_t>(fd->stat_costs[StatAxis::Constitution]));

	// Get intelligence cost ..
	temp = intelligence - normal.intelligence;
	temp = MAX(temp,0);
	cost += stat_cost_curve_contribution(temp, static_cast<std::int32_t>(fd->stat_costs[StatAxis::Intelligence]));

	// Get armor cost ..
	temp = armor - normal.armor;
	temp = MAX(temp,0);
	cost += stat_cost_curve_contribution(temp, static_cast<std::int32_t>(fd->stat_costs[StatAxis::Armor]));

	// Add in the base cost value for the guy ..
	cost += fd->hiring_cost;

	return cost;

}

std::uint32_t calculate_exp(std::int32_t level);





std::int32_t calculate_level(std::uint32_t experience)
{
	std::int32_t result=1;

	// rule: A hero's level is the highest level whose XP threshold he has reached (thresholds
	//   inclusive: exactly 10000 XP is level 2).
	// tests: GuyCalcs.calculate_level_exact_boundary, GuyCalcs.calculate_level_just_below
	// tests: GuyCalcs.calculate_level_roundtrip_extended
	while (calculate_exp(result) <= experience)
		result++;
	return (result-1);
}

std::uint32_t calculate_exp(std::int32_t level)
{


	/*
	
	Stale-comment note (2026): the formula and table below are Dearborn's
	2013-09-12 work (bc69afd5, written in picker.cpp), kept verbatim as
	heritage. They were superseded two weeks later by his own 58035061
	(2013-09-26, "Made level ups take more XP"), which moved this comment
	here unchanged next to a new recurrence:
	    fn(L) = 8000 + 2000*(L-1) + 4000*(L-2) + fn(L-1)
	That later recurrence is what the loop below unrolls; the numbers in the
	old table have never described the shipped code. The live ladder follows
	the table.
	
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

	The live ladder, as produced by the loop below:
    Level	XP
    1	0
    2	10000
    3	26000
    4	48000
    5	76000
    6	110000
    7	150000
    8	196000
    9	248000
    10	306000
    Each level step costs 6000 more than the one before it --
    step(L) = calculate_exp(L) - calculate_exp(L-1) = 6000*L - 2000 --
    so the curve is quadratic, not "about 10000 per level".
    Pinned by GuyCalcs.calculate_exp_ladder_matches_the_documented_table.

	*/
	// rule: The XP ladder: level 1 (and anything below) needs 0 XP; each further level costs
	//   6000*L-2000 more (10000, 26000, 48000, ... 306000 at L10).
	// tests: GuyCalcs.calculate_exp_ladder_matches_the_documented_table
	// tests: GuyCalcs.calculate_exp_level_0
	if(level <= 1)
        return 0;

    // Iterative unrolling of the original recurrence to avoid stack
    // exhaustion when 'level' comes from untrusted save-file bytes (up to
    // SHRT_MAX). Each iteration reproduces the exact integer arithmetic and
    // accumulation order of the former recursive form.
    std::uint32_t result = 0;
    // rule: Part of the rule above that begins "The XP ladder: level 1 (and anything below) needs
    //   ..."
    // tests: GuyCalcs.calculate_exp_ladder_matches_the_documented_table
    // tests: GuyCalcs.calculate_exp_level_0
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
    // rule: A bot's 'foe within N' special gate measures to the foe it already has; only a walker
    //   with no foe acquires the nearest foe (and keeps it as its foe); with no foe to find the
    //   gate is closed; the distance test is strict (< N).
    // tests: SemGuy.classic_foe_within_gate_measures_only_the_held_foe_and_acquires_only_when_foeless
    if (self->foe())
    {
        std::uint32_t distance = static_cast<std::uint32_t>(self->distance_to_ob(self->foe()));
        return (distance < threshold);
    }
    self->set_foe(current_game->world->find_near_foe(self));
    // rule: Part of the rule above that begins "A bot's 'foe within N' special gate measures to
    //   ..."
    // tests: SemGuy.classic_foe_within_gate_measures_only_the_held_foe_and_acquires_only_when_foeless
    if (!self->foe())
        return false;
    std::uint32_t distance = static_cast<std::uint32_t>(self->distance_to_ob(self->foe()));
    return (distance < threshold);
}

void guy::upgrade_to_level(short new_level, bool set_xp)
{
    std::int32_t level_diff = static_cast<std::int32_t>(new_level) - static_cast<std::int32_t>(this->level);

    auto* fd = get_family_descriptor(family);
    // rule: Levelling up applies the family's own Lua level_up stat gains when the class pack
    //   declares one; otherwise the default gain table (kDefaultLevelUpGains) times the levels
    //   gained.
    // tests: GuyExtended.guy_upgrade_applies_each_familys_own_gain_table
    // tests: GuyExtended.guy_upgrade_cleric_takes_the_default_level_up_gains
    if (og::script::hooks::level_up(fd, this, level_diff))
    {
        // Scripted or family-specific stat gains ran.
    }
    else
    {
        apply_level_up(this, level_diff, kDefaultLevelUpGains);
    }

    this->level = new_level;
    // rule: Promoting a hero with set_xp also sets his XP to exactly the new level's threshold;
    //   without it XP is untouched.
    // tests: GuyCalcs.guy_upgrade_to_level_sets_xp, GuyCalcs.guy_upgrade_to_level_no_xp
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
    // plumbing: no world: skip the family loader row (derived stats then start from zero); every
    //   real path has a world
    if (current_game && current_game->world)
        current_game->world->set_entity_derived_stats(w, Order::Living, temp_guy->family);
    
    
    // Set hitpoints based on stats:
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
    // rule: Dexterity speeds a hero up (stepsize = family base + dex/54) but never past 12 px per
    //   step; normal_stepsize follows the clamped value.
    // tests: GuyExtended.guy_update_derived_stats_clamps_speed_and_regen_delays
    // tests: SemGuy.a_heros_step_below_the_cap_is_its_family_step_plus_dex_over_54
    if (w->stepsize() > 12)
        w->set_stepsize(12);
    w->set_normal_stepsize(w->stepsize());

    //fire_frequency makes us fire faster, min is 1
    w->set_fire_frequency(w->fire_frequency() - temp_guy->get_fire_frequency_bonus());
    // rule: Dexterity shortens a hero's fire delay (base - dex/47) but never below 1 tick.
    // tests: GuyExtended.guy_update_derived_stats_clamps_speed_and_regen_delays
    // tests: SemGuy.a_heros_fire_delay_above_the_floor_is_its_family_delay_minus_dex_over_47
    if (w->fire_frequency() < 1)
        w->set_fire_frequency(1);

    // Per-family walker creation hooks (e.g. soldier weapons_left)
    {
        const auto* fd = get_family_descriptor(w->family());
        og::script::hooks::on_create(fd, w);
    }

    // Set the heal delay ..
    w->stats()->set_max_heal_delay(REGEN);
    {
        float heal_delay = static_cast<float>(temp_guy->constitution) + static_cast<float>(temp_guy->strength) / 6.0f + 20.0f + 1000.0f;
        w->stats()->set_current_heal_delay(static_cast<std::int32_t>(heal_delay)); // for purposes of calculation only
    }

    // rule: HP regeneration: budget = con + str/6 + 1020; each whole REGEN(4000) is one hit point
    //   per regen round, the remainder sets the delay between fractional ticks (REGEN/(rem+1)),
    //   never below 2; a new hero starts with an empty heal counter.
    // tests: GuyExtended.guy_update_derived_stats_soldier
    // tests: GuyExtended.guy_update_derived_stats_clamps_speed_and_regen_delays
    // tests: SemGuy.a_heros_heal_delay_keeps_regen_for_a_remainder_of_one_and_never_drops_below_two
    while (w->stats()->current_heal_delay() > REGEN)
    {
        w->stats()->set_current_heal_delay(w->stats()->current_heal_delay() - REGEN);
        w->stats()->set_heal_per_round(w->stats()->heal_per_round() + 1);
    } // this takes care of the integer part, now calculate the fraction

    // rule: Part of the rule above that begins "HP regeneration: budget = con + str/6 + 1020; ..."
    // tests: GuyExtended.guy_update_derived_stats_soldier
    // tests: GuyExtended.guy_update_derived_stats_clamps_speed_and_regen_delays
    // tests: SemGuy.a_heros_heal_delay_keeps_regen_for_a_remainder_of_one_and_never_drops_below_two
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
    // rule: Part of the rule above that begins "HP regeneration: budget = con + str/6 + 1020; ..."
    // tests: GuyExtended.guy_update_derived_stats_soldier
    // tests: GuyExtended.guy_update_derived_stats_clamps_speed_and_regen_delays
    // tests: SemGuy.a_heros_heal_delay_keeps_regen_for_a_remainder_of_one_and_never_drops_below_two
    if (w->stats()->max_heal_delay() < 2)
        w->stats()->set_max_heal_delay(2);

    // Set the magic delay ..
    w->stats()->set_max_magic_delay(REGEN);
    w->stats()->set_current_magic_delay(temp_guy->intelligence * 45 + temp_guy->dexterity * 15 + 200);

    // rule: Magic regeneration: budget = int*45 + dex*15 + 200, split like HP regen (same rule as
    //   guy.cpp:541): whole REGENs are MP per round, the remainder sets the delay, floor 2.
    // tests: GuyExtended.guy_update_derived_stats_soldier
    // tests: GuyExtended.guy_update_derived_stats_clamps_speed_and_regen_delays
    while (w->stats()->current_magic_delay() > REGEN)
    {
        w->stats()->set_current_magic_delay(w->stats()->current_magic_delay() - REGEN);
        w->stats()->set_magic_per_round(w->stats()->magic_per_round() + 1);
    } // this takes care of the integer part, now calculate the fraction

    // rule: Part of the rule above that begins "Magic regeneration: budget = int*45 + dex*15 + 200,
    //   ..."
    // tests: GuyExtended.guy_update_derived_stats_soldier
    // tests: GuyExtended.guy_update_derived_stats_clamps_speed_and_regen_delays
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
    // rule: Part of the rule above that begins "Magic regeneration: budget = int*45 + dex*15 + 200,
    //   ..."
    // tests: GuyExtended.guy_update_derived_stats_soldier
    // tests: GuyExtended.guy_update_derived_stats_clamps_speed_and_regen_delays
    if (w->stats()->max_magic_delay() < 2)
        w->stats()->set_max_magic_delay(2);
}

// guy_create_walker_owned and guy_create_and_add_walker are free functions
// in src/runtime/guy_create.cpp (declared in runtime/guy_create.h) because
// they depend on screen* (runtime layer).
