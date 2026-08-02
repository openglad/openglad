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
#include <openglad/gameplay/family_registries.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>

#include "family_registry_base.h"

#include <format>
#include <stdexcept>
#include <string>

static FamilyRegistryBase<FamilyDescriptor, NUM_FAMILIES> s_registry;

static void apply_defaults(FamilyDescriptor& d)
{
    d.name = "BEAST";
    for (int j = 0; j < FD_NUM_SPECIALS; j++) d.special_cost[j] = kSpecialCostDisabled;
    d.combat.fire_mp_cost = 1;
    for (int j = 0; j < FD_NUM_SPECIALS; j++) d.special_names[j] = kSpecialNameNone;
    for (int j = 0; j < FD_NUM_SPECIALS; j++) d.alternate_names[j] = kSpecialNameNone;
    d.leaves_bloodspot = true;
    d.magic_damage_modifier = 1.0f;
    d.promotes_to = -1;
    d.death_message = "SOMEONE DIED";
    d.ai_line_of_sight = 7;
}

// --- the one field classpack.yaml cannot declare yet ----------------------
//
// Everything else on a core family arrives from packs/core/classpack.yaml.
// `promotion_new_level` is the exception: it is a formula, and the pack
// format has no way to spell one. Until it grows a declarative equivalent
// (a `promotion_level_step` key, which needs the YAML reader and the
// installer to carry it), these two live on as seeds — written into the
// still-FREE slots before any pack installs, and preserved through the
// install because the installer copies the slot it is patching.
//
// This is the whole of the engine's remaining family knowledge. Deleting it
// without a replacement would silently promote every mage to level 1.
static short mage_promotion_level(int old_level)
{
    return static_cast<short>((old_level - 6) / 2 + 1);
}

static short orc_promotion_level([[maybe_unused]] int old_level)
{
    return 1;
}

static void seed_promotion_formulas(FamilyDescriptor* e)
{
    e[FAMILY_MAGE].promotion_new_level = mage_promotion_level;
    e[FAMILY_ORC].promotion_new_level = orc_promotion_level;
}

void init_family_registry()
{
    s_registry.init(apply_defaults, seed_promotion_formulas);
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

const FamilyDescriptor* get_family_descriptor_install_slot(int family_id)
{
    if (!s_registry.is_initialized())
        init_family_registry();
    return s_registry.install_slot(family_id);
}

void reset_family_registry_mod_slots()
{
    if (!s_registry.is_initialized())
        init_family_registry();
    s_registry.reset_mod_slots();
}

int first_unpopulated_core_family_slot()
{
    if (!s_registry.is_initialized())
        init_family_registry();
    return s_registry.first_unpopulated_core_slot();
}

void require_core_families_installed(const char* context)
{
    struct OrderCheck {
        const char* order;
        int (*first_gap)();
    };
    static const OrderCheck kChecks[] = {
        {"living", first_unpopulated_core_family_slot},
        {"weapon", first_unpopulated_core_weapon_family_slot},
        {"effect", first_unpopulated_core_effect_family_slot},
        {"treasure", first_unpopulated_core_treasure_family_slot},
        {"generator", first_unpopulated_core_generator_family_slot},
    };
    for (const OrderCheck& check : kChecks) {
        const int gap = check.first_gap();
        if (gap < 0)
            continue;
        const std::string msg = std::format(
            "Fatal: {}: no mounted class pack declares {} family {} — the "
            "core class pack (packs/core/classpack.yaml) is missing or "
            "malformed",
            context, check.order, gap);
        LogError("{}\n", msg);
        throw std::runtime_error(msg);
    }
}
