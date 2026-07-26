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
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>

#include "family_registry_base.h"

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

static FamilyRegistryBase<FamilyDescriptor, NUM_FAMILIES> s_registry;

static void apply_defaults(FamilyDescriptor& d)
{
    d.name = "BEAST";
    for (int j = 0; j < FD_NUM_SPECIALS; j++) d.special_cost[j] = 5000;
    d.weapon_cost = 1;
    for (int j = 0; j < FD_NUM_SPECIALS; j++) d.special_names[j] = "NONE";
    for (int j = 0; j < FD_NUM_SPECIALS; j++) d.alternate_names[j] = "NONE";
    d.leaves_bloodspot = true;
    d.magic_damage_modifier = 1.0f;
    d.promotes_to = -1;
    d.death_message = "SOMEONE DIED";
    d.ai_line_of_sight = 7;
}

static void populate(FamilyDescriptor* e)
{
    e[FAMILY_SOLDIER] = describe_family_soldier();
    e[FAMILY_ELF] = describe_family_elf();
    e[FAMILY_ARCHER] = describe_family_archer();
    e[FAMILY_MAGE] = describe_family_mage();
    e[FAMILY_SKELETON] = describe_family_skeleton();
    e[FAMILY_CLERIC] = describe_family_cleric();
    e[FAMILY_FIREELEMENTAL] = describe_family_fire_elemental();
    e[FAMILY_FAERIE] = describe_family_faerie();
    e[FAMILY_SLIME] = describe_family_slime();
    e[FAMILY_SMALL_SLIME] = describe_family_small_slime();
    e[FAMILY_MEDIUM_SLIME] = describe_family_medium_slime();
    e[FAMILY_THIEF] = describe_family_thief();
    e[FAMILY_GHOST] = describe_family_ghost();
    e[FAMILY_DRUID] = describe_family_druid();
    e[FAMILY_ORC] = describe_family_orc();
    e[FAMILY_BIG_ORC] = describe_family_big_orc();
    e[FAMILY_BARBARIAN] = describe_family_barbarian();
    e[FAMILY_ARCHMAGE] = describe_family_archmage();
    e[FAMILY_GOLEM] = describe_family_golem();
    e[FAMILY_GIANT_SKELETON] = describe_family_giant_skeleton();
    e[FAMILY_TOWER1] = describe_family_tower1();
}

void init_family_registry()
{
    s_registry.init(apply_defaults, populate);
}

const FamilyDescriptor* get_family_descriptor(int family_id)
{
    if (!s_registry.is_initialized())
        init_family_registry();
    return s_registry.get(family_id);
}

bool set_family_descriptor(int family_id, const FamilyDescriptor& d)
{
    if (!s_registry.is_initialized())
        init_family_registry();
    return s_registry.set(family_id, d);
}
