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
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>

// Forward declarations of family descriptor providers
const FamilyDescriptor& describe_family_golem();
const FamilyDescriptor& describe_family_giant_skeleton();
const FamilyDescriptor& describe_family_tower1();
const FamilyDescriptor& describe_family_big_orc();
const FamilyDescriptor& describe_family_ghost();
const FamilyDescriptor& describe_family_skeleton();
const FamilyDescriptor& describe_family_fire_elemental();
const FamilyDescriptor& describe_family_faerie();
const FamilyDescriptor& describe_family_soldier();
const FamilyDescriptor& describe_family_elf();
const FamilyDescriptor& describe_family_archer();
const FamilyDescriptor& describe_family_mage();
const FamilyDescriptor& describe_family_cleric();
const FamilyDescriptor& describe_family_slime();
const FamilyDescriptor& describe_family_small_slime();
const FamilyDescriptor& describe_family_medium_slime();
const FamilyDescriptor& describe_family_thief();
const FamilyDescriptor& describe_family_druid();
const FamilyDescriptor& describe_family_orc();
const FamilyDescriptor& describe_family_barbarian();
const FamilyDescriptor& describe_family_archmage();

static bool s_registry_initialized = false;
static FamilyDescriptor s_registry[NUM_FAMILIES];

void init_family_registry()
{
    if (s_registry_initialized)
        return;

    // Default all fields to safe values
    for (int i = 0; i < NUM_FAMILIES; i++)
    {
        auto& d = s_registry[i];
        d.family_id = i;
        d.name = "BEAST";
        for (int j = 0; j < 6; j++) d.base_stats[j] = 0;
        d.hiring_cost = 0;
        for (int j = 0; j < 8; j++) d.derived_bonuses[j] = 0;
        for (int j = 0; j < 6; j++) d.stat_costs[j] = 0;
        for (int j = 0; j < FD_NUM_SPECIALS; j++) d.special_cost[j] = 5000;
        d.weapon_cost = 1;  // default from gloader create_walker_owned
        d.default_weapon = FAMILY_KNIFE;
        d.init_bit_flags = 0;
        d.init_ani_type = 0;
        d.init_max_magicpoints = 0;
        for (int j = 0; j < FD_NUM_SPECIALS; j++) d.special_names[j] = "NONE";
        for (int j = 0; j < FD_NUM_SPECIALS; j++) d.alternate_names[j] = "NONE";
        d.leaves_bloodspot = true;
        d.magic_damage_modifier = 1.0f;
        d.is_stationary = false;
        d.has_returning_weapon = false;
        d.is_undead = false;
        d.promotes_to = -1;
        d.promotion_level_req = 0;
        d.promotion_new_level = nullptr;
        d.death_message = "SOMEONE DIED";
        d.do_special = nullptr;
        d.check_special_ai = nullptr;
        d.hit_response = nullptr;
        d.set_difficulty = nullptr;
        d.level_up = nullptr;
        d.on_death = nullptr;
        d.on_act_living = nullptr;
        d.on_shoved = nullptr;
        d.on_fire_weapon = nullptr;
        d.handle_teleport = nullptr;
        d.on_create = nullptr;
        d.customize_weapon = nullptr;
        d.on_ani_complete = nullptr;
        d.on_melee_hit = nullptr;
        d.pix_filename = nullptr;
        d.animation_type = FAMILY_ANIM_STANDARD;
        d.ai_line_of_sight = 7;
        d.description = nullptr;
        d.name_pool = nullptr;
        d.name_pool_size = 0;
        d.is_playable = false;
        d.playable_order = 999;
    }

    // === FAMILY_SOLDIER (0) — defined in families/family_soldier.cpp ===
    s_registry[FAMILY_SOLDIER] = describe_family_soldier();

    // === FAMILY_ELF (1) — defined in families/family_elf.cpp ===
    s_registry[FAMILY_ELF] = describe_family_elf();

    // === FAMILY_ARCHER (2) — defined in families/family_archer.cpp ===
    s_registry[FAMILY_ARCHER] = describe_family_archer();

    // === FAMILY_MAGE (3) — defined in families/family_mage.cpp ===
    s_registry[FAMILY_MAGE] = describe_family_mage();

    // === FAMILY_SKELETON (4) — defined in families/family_skeleton.cpp ===
    s_registry[FAMILY_SKELETON] = describe_family_skeleton();

    // === FAMILY_CLERIC (5) — defined in families/family_cleric.cpp ===
    s_registry[FAMILY_CLERIC] = describe_family_cleric();

    // === FAMILY_FIREELEMENTAL (6) — defined in families/family_fire_elemental.cpp ===
    s_registry[FAMILY_FIREELEMENTAL] = describe_family_fire_elemental();

    // === FAMILY_FAERIE (7) — defined in families/family_faerie.cpp ===
    s_registry[FAMILY_FAERIE] = describe_family_faerie();

    // === FAMILY_SLIME (8) — defined in families/family_slime.cpp ===
    s_registry[FAMILY_SLIME] = describe_family_slime();

    // === FAMILY_SMALL_SLIME (9) — defined in families/family_slime.cpp ===
    s_registry[FAMILY_SMALL_SLIME] = describe_family_small_slime();

    // === FAMILY_MEDIUM_SLIME (10) — defined in families/family_slime.cpp ===
    s_registry[FAMILY_MEDIUM_SLIME] = describe_family_medium_slime();

    // === FAMILY_THIEF (11) — defined in families/family_thief.cpp ===
    s_registry[FAMILY_THIEF] = describe_family_thief();

    // === FAMILY_GHOST (12) — defined in families/family_ghost.cpp ===
    s_registry[FAMILY_GHOST] = describe_family_ghost();

    // === FAMILY_DRUID (13) — defined in families/family_druid.cpp ===
    s_registry[FAMILY_DRUID] = describe_family_druid();

    // === FAMILY_ORC (14) — defined in families/family_orc.cpp ===
    s_registry[FAMILY_ORC] = describe_family_orc();

    // === FAMILY_BIG_ORC (15) — defined in families/family_big_orc.cpp ===
    s_registry[FAMILY_BIG_ORC] = describe_family_big_orc();

    // === FAMILY_BARBARIAN (16) — defined in families/family_barbarian.cpp ===
    s_registry[FAMILY_BARBARIAN] = describe_family_barbarian();

    // === FAMILY_ARCHMAGE (17) — defined in families/family_archmage.cpp ===
    s_registry[FAMILY_ARCHMAGE] = describe_family_archmage();

    // === FAMILY_GOLEM (18) — defined in families/family_golem.cpp ===
    s_registry[FAMILY_GOLEM] = describe_family_golem();

    // === FAMILY_GIANT_SKELETON (19) — defined in families/family_giant_skeleton.cpp ===
    s_registry[FAMILY_GIANT_SKELETON] = describe_family_giant_skeleton();

    // === FAMILY_TOWER1 (20) — defined in families/family_tower1.cpp ===
    s_registry[FAMILY_TOWER1] = describe_family_tower1();

    s_registry_initialized = true;
}

const FamilyDescriptor* get_family_descriptor(int family_id)
{
    if (family_id < 0 || family_id >= NUM_FAMILIES)
        return nullptr;

    if (!s_registry_initialized)
        init_family_registry();

    return &s_registry[family_id];
}
