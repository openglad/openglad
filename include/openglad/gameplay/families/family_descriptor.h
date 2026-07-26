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
#pragma once

#include <openglad/core/family_presentation.h>

#include <cstdint>

inline constexpr int FD_NUM_SPECIALS = 6;

enum class FamilyAnimationType : std::uint8_t {
    FAMILY_ANIM_STANDARD = 0,       // animan (most livings)
    FAMILY_ANIM_MAGE,               // animage (mage, archmage)
    FAMILY_ANIM_SKELETON,           // aniskel (skeleton with grow/shrink)
    FAMILY_ANIM_GIANT_SKELETON,     // anigs
    FAMILY_ANIM_SLIME,              // anislime
    FAMILY_ANIM_SMALL_SLIME,        // ani_small_slime
    FAMILY_ANIM_STATIC,             // anifood (tower)
};

class walker;
class living;
class statistics;
class guy;

// Per-level stat gains for level_up(). Each value is multiplied by level_diff.
struct LevelUpGains {
    std::int32_t str, dex, con, intel, armor;
};

// Default gains (no family-specific modifiers): base values 8,6,8,8,1.
inline constexpr LevelUpGains kDefaultLevelUpGains{8, 6, 8, 8, 1};

// Apply level-up stat gains to a guy.
void apply_level_up(guy* self, std::int32_t level_diff, const LevelUpGains& g);

// Per-family difficulty scaling coefficients for set_difficulty().
// HP/MP/armor scale with level^2; damage scales with level.
struct DifficultyScaling {
    float hp, mp, dmg, armor;
};

// Apply difficulty scaling to a living entity.
void apply_difficulty_scaling(living* self, std::uint32_t level, const DifficultyScaling& s);

// Common check_special_ai: find foe if needed, return true if within threshold distance.
bool check_special_ai_distance(living* self, std::uint32_t threshold);

struct FamilyDescriptor {
    int family_id;
    const char* name;                          // "SOLDIER", "ELF", etc.
    const char* short_name;                    // abbreviated picker label (nullptr = use name)

    // Base stats from guy.cpp statlist[]
    std::int32_t base_stats[6];                      // STR, DEX, CON, INT, ARMOR, LVL

    // Hiring cost from guy.cpp costlist[]
    std::int32_t hiring_cost;

    // Derived bonuses from guy.cpp derived_bonuses[]
    float derived_bonuses[8];                  // HP, MP, ATK, RATK, RNG, DEF, SPD, ATKSPD

    // Stat upgrade costs from guy.cpp statcosts[]
    std::int32_t stat_costs[6];                      // STR, DEX, CON, INT, ARMOR, LVL

    // Data from gloader.cpp set_walker()
    unsigned short special_cost[FD_NUM_SPECIALS]; // cost of each special (index 0 unused)
    short weapon_cost;                         // cost to fire weapon
    int default_weapon;                        // weapon family ID
    std::int32_t init_bit_flags;                     // BIT_ flags to set on creation
    char init_ani_type;                        // 0=default, ANI_SKEL_GROW for skeleton
    float init_max_magicpoints;                // override max MP (0 = use default)

    // Special ability display names from screen.cpp
    const char* special_names[FD_NUM_SPECIALS];
    const char* alternate_names[FD_NUM_SPECIALS];

    // Flags
    bool leaves_bloodspot;                     // false for ghost/skeleton/tower
    float magic_damage_modifier;               // multiplier for incoming magical damage (1.0 = normal)
    bool is_stationary;                        // true for TOWER1: can face but not move
    bool has_returning_weapon;                  // true for SOLDIER: knife returns on death
    bool is_undead;                            // true for SKELETON, GHOST: affected by turn_undead

    // Class promotion (picker)
    int promotes_to;                           // family to promote to (-1 = no promotion)
    int promotion_level_req;                   // minimum level required for promotion
    short (*promotion_new_level)(int old_level); // calculate new level after promotion (nullptr = level 1)

    const char* death_message;                 // "SOLDIER SLAIN", etc. (fallback: "SOMEONE DIED")

    // Behavioral callbacks (nullptr = use legacy switch / default behavior)
    bool (*do_special)(walker* self);
    bool (*check_special_ai)(living* self);
    void (*hit_response)(statistics* stats, walker* who);
    void (*set_difficulty)(living* self, std::uint32_t level);
    void (*level_up)(guy* self, std::int32_t level_diff);
    bool (*on_death)(walker* self);            // return true = handled

    // Step 3 callbacks: remaining per-family behavioral dispatch
    void (*on_act_living)(living* self);            // periodic effects during act()
    void (*on_shoved)(walker* self);                // called when shoved by an ally
    bool (*on_fire_weapon)(walker* self, walker* weapon); // before firing; false = block
    bool (*handle_teleport)(walker* self);          // teleport-out complete; true = handled
    void (*on_create)(walker* self);                // called when walker created from guy
    void (*customize_weapon)(walker* self, walker* weapon); // tweak weapon after creation
    bool (*on_ani_complete)(walker* self);    // animation end; true = handled
    void (*on_melee_hit)(walker* self, walker* target);    // called after successful melee attack

    // Graphics / loader data (replaces hardcoded gloader.cpp arrays)
    const char* pix_filename;          // "monk.png" or a pack-relative path
    FamilyAnimationType animation_type;  // built-in table (see anim_table)
    int ai_line_of_sight;              // AI's understanding of ranged attack range

    // Pack-shipped animation table. nullptr = use animation_type's built-in
    // gloader table; otherwise this is a `anim_row_count`-long array of row
    // pointers, each row a -1-terminated frame list, owned by the classpack
    // store for the life of the process. anim_row_count is authoritative for
    // walker::ani_count (which bounds the animate() index math against
    // snapshot-controlled ani_type/curdir), so it is NEVER 0 when anim_table
    // is non-null.
    const signed char* const* anim_table = nullptr;
    int anim_row_count = 0;

    // UI / picker data (replaces hardcoded switch statements)
    const char* description;           // multiline class description for team builder
    const char* const* name_pool;      // array of random names
    int name_pool_size;                // count of names in pool
    bool is_playable;                  // appears in hire menu
    int playable_order;                // sort order in hire menu (lower = earlier)

    // Presentation data (replaces the family switches in the UI layers).
    // The defaults are the legacy `default:` branches, so an undeclared mod
    // family renders exactly like an unknown family always did.
    og::FamilyGlyph glyph{U'?', '?', og::GlyphColor::Default, false, false};
    og::RadarBlip radar{};             // livings blip in their team colour

    // Fully-qualified id the declaring pack gave this family — the YAML
    // `id:` value, e.g. "core:soldier" or "mypack:warlock". This is the
    // name resolution matches FIRST, so two packs may ship families with
    // the same `name:` and stay distinguishable
    // (og::families::resolve_family_string_id).
    // nullptr = no pack declared this slot (a C++ core pin before the core
    // pack installs over it); resolution then falls back to `name`.
    // Borrowed, never owned: the installer points this at a string in the
    // process-lifetime ClasspackStore (src/resources/packs.cpp), exactly
    // like `name` and the other const char* fields.
    const char* declared_id = nullptr;
};
