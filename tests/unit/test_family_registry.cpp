#include "unit.h"
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/legacy/base.h>
#include <openglad/gameplay/statistics.h>
#include <cstring>

OG_UNIT_TEST(test_registry_returns_non_null_for_valid_ids)
{
    init_family_registry();
    for (int i = 0; i < NUM_FAMILIES; i++)
    {
        const FamilyDescriptor* d = get_family_descriptor(i);
        OG_ASSERT(d != nullptr);
        OG_ASSERT(d->family_id == i);
    }
}

OG_UNIT_TEST(test_registry_returns_null_for_invalid_ids)
{
    init_family_registry();
    OG_ASSERT(get_family_descriptor(-1) == nullptr);
    OG_ASSERT(get_family_descriptor(NUM_FAMILIES) == nullptr);
    OG_ASSERT(get_family_descriptor(999) == nullptr);
}

OG_UNIT_TEST(test_registry_family_names)
{
    init_family_registry();
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_SOLDIER)->name, "SOLDIER") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_ELF)->name, "ELF") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_ARCHER)->name, "ARCHER") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_MAGE)->name, "MAGE") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_SKELETON)->name, "SKELETON") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_CLERIC)->name, "CLERIC") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_FIREELEMENTAL)->name, "ELEMENTAL") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_FAERIE)->name, "FAERIE") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_SLIME)->name, "SLIME") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_SMALL_SLIME)->name, "SLIME") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_MEDIUM_SLIME)->name, "SLIME") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_THIEF)->name, "THIEF") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_GHOST)->name, "GHOST") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_DRUID)->name, "DRUID") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_ORC)->name, "ORC") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_BIG_ORC)->name, "ORC CAPTAIN") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_BARBARIAN)->name, "BARBARIAN") == 0);
    OG_ASSERT(std::strcmp(get_family_descriptor(FAMILY_ARCHMAGE)->name, "ARCHMAGE") == 0);
}

OG_UNIT_TEST(test_registry_base_stats)
{
    init_family_registry();
    // Spot-check soldier stats
    auto* d = get_family_descriptor(FAMILY_SOLDIER);
    OG_ASSERT(d->base_stats[0] == 12); // STR
    OG_ASSERT(d->base_stats[1] == 6);  // DEX
    OG_ASSERT(d->base_stats[2] == 12); // CON
    OG_ASSERT(d->base_stats[3] == 8);  // INT
    OG_ASSERT(d->base_stats[4] == 9);  // ARMOR
    OG_ASSERT(d->base_stats[5] == 1);  // LVL

    // Spot-check mage stats
    d = get_family_descriptor(FAMILY_MAGE);
    OG_ASSERT(d->base_stats[0] == 4);  // STR
    OG_ASSERT(d->base_stats[3] == 16); // INT
}

OG_UNIT_TEST(test_registry_hiring_costs)
{
    init_family_registry();
    OG_ASSERT(get_family_descriptor(FAMILY_SOLDIER)->hiring_cost == 250);
    OG_ASSERT(get_family_descriptor(FAMILY_ELF)->hiring_cost == 150);
    OG_ASSERT(get_family_descriptor(FAMILY_MAGE)->hiring_cost == 450);
    OG_ASSERT(get_family_descriptor(FAMILY_CLERIC)->hiring_cost == 400);
    OG_ASSERT(get_family_descriptor(FAMILY_THIEF)->hiring_cost == 400);
}

OG_UNIT_TEST(test_registry_special_costs)
{
    init_family_registry();
    auto* d = get_family_descriptor(FAMILY_SOLDIER);
    OG_ASSERT(d->special_cost[0] == 5000); // unused slot, high cost
    OG_ASSERT(d->special_cost[1] == 25);   // charge
    OG_ASSERT(d->special_cost[2] == 100);  // boomerang
    OG_ASSERT(d->special_cost[3] == 120);  // whirlwind
    OG_ASSERT(d->special_cost[4] == 150);  // disarm

    d = get_family_descriptor(FAMILY_MAGE);
    OG_ASSERT(d->special_cost[1] == 15);   // teleport
    OG_ASSERT(d->special_cost[5] == 100);  // heartburst
}

OG_UNIT_TEST(test_registry_weapons)
{
    init_family_registry();
    OG_ASSERT(get_family_descriptor(FAMILY_SOLDIER)->default_weapon == FAMILY_KNIFE);
    OG_ASSERT(get_family_descriptor(FAMILY_ELF)->default_weapon == FAMILY_ROCK);
    OG_ASSERT(get_family_descriptor(FAMILY_ARCHER)->default_weapon == FAMILY_ARROW);
    OG_ASSERT(get_family_descriptor(FAMILY_MAGE)->default_weapon == FAMILY_FIREBALL);
    OG_ASSERT(get_family_descriptor(FAMILY_SKELETON)->default_weapon == FAMILY_BONE);
    OG_ASSERT(get_family_descriptor(FAMILY_CLERIC)->default_weapon == FAMILY_GLOW);
    OG_ASSERT(get_family_descriptor(FAMILY_DRUID)->default_weapon == FAMILY_LIGHTNING);
}

OG_UNIT_TEST(test_registry_special_names)
{
    init_family_registry();
    auto* d = get_family_descriptor(FAMILY_SOLDIER);
    OG_ASSERT(std::strcmp(d->special_names[1], "CHARGE") == 0);
    OG_ASSERT(std::strcmp(d->special_names[2], "BOOMERANG") == 0);

    d = get_family_descriptor(FAMILY_CLERIC);
    OG_ASSERT(std::strcmp(d->special_names[1], "HEAL") == 0);
    OG_ASSERT(std::strcmp(d->alternate_names[1], "MYSTIC MACE") == 0);
}

OG_UNIT_TEST(test_registry_bit_flags)
{
    init_family_registry();
    OG_ASSERT(get_family_descriptor(FAMILY_ELF)->init_bit_flags == BIT_FORESTWALK);
    OG_ASSERT(get_family_descriptor(FAMILY_FAERIE)->init_bit_flags == (BIT_ANIMATE | BIT_FLYING));
    OG_ASSERT((get_family_descriptor(FAMILY_GHOST)->init_bit_flags & BIT_ETHEREAL) != 0);
    OG_ASSERT((get_family_descriptor(FAMILY_GHOST)->init_bit_flags & BIT_FLYING) != 0);
    OG_ASSERT(get_family_descriptor(FAMILY_SOLDIER)->init_bit_flags == 0);
}

OG_UNIT_TEST(test_registry_bloodspot_flags)
{
    init_family_registry();
    OG_ASSERT(get_family_descriptor(FAMILY_SOLDIER)->leaves_bloodspot == true);
    OG_ASSERT(get_family_descriptor(FAMILY_GHOST)->leaves_bloodspot == false);
    OG_ASSERT(get_family_descriptor(FAMILY_SKELETON)->leaves_bloodspot == false);
    OG_ASSERT(get_family_descriptor(FAMILY_TOWER1)->leaves_bloodspot == false);
    OG_ASSERT(get_family_descriptor(FAMILY_GIANT_SKELETON)->leaves_bloodspot == false);
}

OG_UNIT_TEST(test_registry_callbacks_state)
{
    init_family_registry();
    // do_special: non-null for families with extracted specials
    OG_ASSERT(get_family_descriptor(FAMILY_SKELETON)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_GHOST)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_FIREELEMENTAL)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_SLIME)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_SMALL_SLIME)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_MEDIUM_SLIME)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_BARBARIAN)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_ARCHMAGE)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_CLERIC)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_DRUID)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_MAGE)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_THIEF)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_SOLDIER)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_ELF)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_ARCHER)->do_special != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_ORC)->do_special != nullptr);
    // Data-only families have nullptr
    OG_ASSERT(get_family_descriptor(FAMILY_GOLEM)->do_special == nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_GIANT_SKELETON)->do_special == nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_TOWER1)->do_special == nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_FAERIE)->do_special == nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_BIG_ORC)->do_special == nullptr);

    // check_special_ai: non-null for families with custom AI
    OG_ASSERT(get_family_descriptor(FAMILY_SOLDIER)->check_special_ai != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_ARCHER)->check_special_ai != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_FIREELEMENTAL)->check_special_ai != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_GHOST)->check_special_ai != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_ORC)->check_special_ai != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_THIEF)->check_special_ai != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_MAGE)->check_special_ai != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_SLIME)->check_special_ai != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_CLERIC)->check_special_ai != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_SKELETON)->check_special_ai != nullptr);
    // Default-AI families have nullptr
    OG_ASSERT(get_family_descriptor(FAMILY_DRUID)->check_special_ai == nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_BARBARIAN)->check_special_ai == nullptr);

    // hit_response: non-null for families with custom hit behavior
    OG_ASSERT(get_family_descriptor(FAMILY_MAGE)->hit_response != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_ARCHMAGE)->hit_response != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_ARCHER)->hit_response != nullptr);
    // Default families have nullptr
    OG_ASSERT(get_family_descriptor(FAMILY_SOLDIER)->hit_response == nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_CLERIC)->hit_response == nullptr);

    // on_death: non-null for families with custom death behavior
    OG_ASSERT(get_family_descriptor(FAMILY_FIREELEMENTAL)->on_death != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_SLIME)->on_death != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_MEDIUM_SLIME)->on_death != nullptr);
    // Default families have nullptr
    OG_ASSERT(get_family_descriptor(FAMILY_SOLDIER)->on_death == nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_GHOST)->on_death == nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_SMALL_SLIME)->on_death == nullptr);

    // set_difficulty: non-null for families with custom formulas
    OG_ASSERT(get_family_descriptor(FAMILY_SOLDIER)->set_difficulty != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_ARCHER)->set_difficulty != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_MAGE)->set_difficulty != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_CLERIC)->set_difficulty != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_DRUID)->set_difficulty != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_ORC)->set_difficulty != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_GOLEM)->set_difficulty != nullptr);
    // Default-formula families have nullptr
    OG_ASSERT(get_family_descriptor(FAMILY_ELF)->set_difficulty == nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_GHOST)->set_difficulty == nullptr);

    // level_up: non-null for families with custom modifiers
    OG_ASSERT(get_family_descriptor(FAMILY_ELF)->level_up != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_ARCHER)->level_up != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_MAGE)->level_up != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_SKELETON)->level_up != nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_ORC)->level_up != nullptr);
    // Default-modifier families have nullptr
    OG_ASSERT(get_family_descriptor(FAMILY_SOLDIER)->level_up == nullptr);
    OG_ASSERT(get_family_descriptor(FAMILY_CLERIC)->level_up == nullptr);
}
