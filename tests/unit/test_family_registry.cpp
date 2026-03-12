#include "unit.h"
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/legacy/base.h>
#include <openglad/gameplay/statistics.h>
#include <cstring>

TEST(FamilyRegistry, registry_returns_non_null_for_valid_ids)
{
    init_family_registry();
    for (int i = 0; i < NUM_FAMILIES; i++)
    {
        const FamilyDescriptor* d = get_family_descriptor(i);
        ASSERT_TRUE(d != nullptr);
        ASSERT_TRUE(d->family_id == i);
    }
}

TEST(FamilyRegistry, registry_returns_null_for_invalid_ids)
{
    init_family_registry();
    ASSERT_TRUE(get_family_descriptor(-1) == nullptr);
    ASSERT_TRUE(get_family_descriptor(NUM_FAMILIES) == nullptr);
    ASSERT_TRUE(get_family_descriptor(999) == nullptr);
}

TEST(FamilyRegistry, registry_family_names)
{
    init_family_registry();
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_SOLDIER)->name, "SOLDIER") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_ELF)->name, "ELF") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_ARCHER)->name, "ARCHER") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_MAGE)->name, "MAGE") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_SKELETON)->name, "SKELETON") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_CLERIC)->name, "CLERIC") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_FIREELEMENTAL)->name, "ELEMENTAL") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_FAERIE)->name, "FAERIE") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_SLIME)->name, "SLIME") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_SMALL_SLIME)->name, "SLIME") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_MEDIUM_SLIME)->name, "SLIME") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_THIEF)->name, "THIEF") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_GHOST)->name, "GHOST") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_DRUID)->name, "DRUID") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_ORC)->name, "ORC") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_BIG_ORC)->name, "ORC CAPTAIN") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_BARBARIAN)->name, "BARBARIAN") == 0);
    ASSERT_TRUE(std::strcmp(get_family_descriptor(FAMILY_ARCHMAGE)->name, "ARCHMAGE") == 0);
}

TEST(FamilyRegistry, registry_base_stats)
{
    init_family_registry();
    // Spot-check soldier stats
    auto* d = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_TRUE(d->base_stats[0] == 12); // STR
    ASSERT_TRUE(d->base_stats[1] == 6);  // DEX
    ASSERT_TRUE(d->base_stats[2] == 12); // CON
    ASSERT_TRUE(d->base_stats[3] == 8);  // INT
    ASSERT_TRUE(d->base_stats[4] == 9);  // ARMOR
    ASSERT_TRUE(d->base_stats[5] == 1);  // LVL

    // Spot-check mage stats
    d = get_family_descriptor(FAMILY_MAGE);
    ASSERT_TRUE(d->base_stats[0] == 4);  // STR
    ASSERT_TRUE(d->base_stats[3] == 16); // INT
}

TEST(FamilyRegistry, registry_hiring_costs)
{
    init_family_registry();
    ASSERT_TRUE(get_family_descriptor(FAMILY_SOLDIER)->hiring_cost == 250);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ELF)->hiring_cost == 150);
    ASSERT_TRUE(get_family_descriptor(FAMILY_MAGE)->hiring_cost == 450);
    ASSERT_TRUE(get_family_descriptor(FAMILY_CLERIC)->hiring_cost == 400);
    ASSERT_TRUE(get_family_descriptor(FAMILY_THIEF)->hiring_cost == 400);
}

TEST(FamilyRegistry, registry_special_costs)
{
    init_family_registry();
    auto* d = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_TRUE(d->special_cost[0] == 5000); // unused slot, high cost
    ASSERT_TRUE(d->special_cost[1] == 25);   // charge
    ASSERT_TRUE(d->special_cost[2] == 100);  // boomerang
    ASSERT_TRUE(d->special_cost[3] == 120);  // whirlwind
    ASSERT_TRUE(d->special_cost[4] == 150);  // disarm

    d = get_family_descriptor(FAMILY_MAGE);
    ASSERT_TRUE(d->special_cost[1] == 15);   // teleport
    ASSERT_TRUE(d->special_cost[5] == 100);  // heartburst
}

TEST(FamilyRegistry, registry_weapons)
{
    init_family_registry();
    ASSERT_TRUE(get_family_descriptor(FAMILY_SOLDIER)->default_weapon == FAMILY_KNIFE);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ELF)->default_weapon == FAMILY_ROCK);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ARCHER)->default_weapon == FAMILY_ARROW);
    ASSERT_TRUE(get_family_descriptor(FAMILY_MAGE)->default_weapon == FAMILY_FIREBALL);
    ASSERT_TRUE(get_family_descriptor(FAMILY_SKELETON)->default_weapon == FAMILY_BONE);
    ASSERT_TRUE(get_family_descriptor(FAMILY_CLERIC)->default_weapon == FAMILY_GLOW);
    ASSERT_TRUE(get_family_descriptor(FAMILY_DRUID)->default_weapon == FAMILY_LIGHTNING);
}

TEST(FamilyRegistry, registry_special_names)
{
    init_family_registry();
    auto* d = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_TRUE(std::strcmp(d->special_names[1], "CHARGE") == 0);
    ASSERT_TRUE(std::strcmp(d->special_names[2], "BOOMERANG") == 0);

    d = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(std::strcmp(d->special_names[1], "HEAL") == 0);
    ASSERT_TRUE(std::strcmp(d->alternate_names[1], "MYSTIC MACE") == 0);
}

TEST(FamilyRegistry, registry_bit_flags)
{
    init_family_registry();
    ASSERT_TRUE(get_family_descriptor(FAMILY_ELF)->init_bit_flags == BIT_FORESTWALK);
    ASSERT_TRUE(get_family_descriptor(FAMILY_FAERIE)->init_bit_flags == (BIT_ANIMATE | BIT_FLYING));
    ASSERT_TRUE((get_family_descriptor(FAMILY_GHOST)->init_bit_flags & BIT_ETHEREAL) != 0);
    ASSERT_TRUE((get_family_descriptor(FAMILY_GHOST)->init_bit_flags & BIT_FLYING) != 0);
    ASSERT_TRUE(get_family_descriptor(FAMILY_SOLDIER)->init_bit_flags == 0);
}

TEST(FamilyRegistry, registry_bloodspot_flags)
{
    init_family_registry();
    ASSERT_TRUE(get_family_descriptor(FAMILY_SOLDIER)->leaves_bloodspot == true);
    ASSERT_TRUE(get_family_descriptor(FAMILY_GHOST)->leaves_bloodspot == false);
    ASSERT_TRUE(get_family_descriptor(FAMILY_SKELETON)->leaves_bloodspot == false);
    ASSERT_TRUE(get_family_descriptor(FAMILY_TOWER1)->leaves_bloodspot == false);
    ASSERT_TRUE(get_family_descriptor(FAMILY_GIANT_SKELETON)->leaves_bloodspot == false);
}

TEST(FamilyRegistry, registry_callbacks_state)
{
    init_family_registry();
    // do_special: non-null for families with extracted specials
    ASSERT_TRUE(get_family_descriptor(FAMILY_SKELETON)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_GHOST)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_FIREELEMENTAL)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_SLIME)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_SMALL_SLIME)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_MEDIUM_SLIME)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_BARBARIAN)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ARCHMAGE)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_CLERIC)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_DRUID)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_MAGE)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_THIEF)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_SOLDIER)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ELF)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ARCHER)->do_special != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ORC)->do_special != nullptr);
    // Data-only families have nullptr
    ASSERT_TRUE(get_family_descriptor(FAMILY_GOLEM)->do_special == nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_GIANT_SKELETON)->do_special == nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_TOWER1)->do_special == nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_FAERIE)->do_special == nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_BIG_ORC)->do_special == nullptr);

    // check_special_ai: non-null for families with custom AI
    ASSERT_TRUE(get_family_descriptor(FAMILY_SOLDIER)->check_special_ai != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ARCHER)->check_special_ai != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_FIREELEMENTAL)->check_special_ai != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_GHOST)->check_special_ai != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ORC)->check_special_ai != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_THIEF)->check_special_ai != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_MAGE)->check_special_ai != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_SLIME)->check_special_ai != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_CLERIC)->check_special_ai != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_SKELETON)->check_special_ai != nullptr);
    // Default-AI families have nullptr
    ASSERT_TRUE(get_family_descriptor(FAMILY_DRUID)->check_special_ai == nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_BARBARIAN)->check_special_ai == nullptr);

    // hit_response: non-null for families with custom hit behavior
    ASSERT_TRUE(get_family_descriptor(FAMILY_MAGE)->hit_response != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ARCHMAGE)->hit_response != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ARCHER)->hit_response != nullptr);
    // Default families have nullptr
    ASSERT_TRUE(get_family_descriptor(FAMILY_SOLDIER)->hit_response == nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_CLERIC)->hit_response == nullptr);

    // on_death: non-null for families with custom death behavior
    ASSERT_TRUE(get_family_descriptor(FAMILY_FIREELEMENTAL)->on_death != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_SLIME)->on_death != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_MEDIUM_SLIME)->on_death != nullptr);
    // Default families have nullptr
    ASSERT_TRUE(get_family_descriptor(FAMILY_SOLDIER)->on_death == nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_GHOST)->on_death == nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_SMALL_SLIME)->on_death == nullptr);

    // set_difficulty: non-null for families with custom formulas
    ASSERT_TRUE(get_family_descriptor(FAMILY_SOLDIER)->set_difficulty != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ARCHER)->set_difficulty != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_MAGE)->set_difficulty != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_CLERIC)->set_difficulty != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_DRUID)->set_difficulty != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ORC)->set_difficulty != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_GOLEM)->set_difficulty != nullptr);
    // Default-formula families have nullptr
    ASSERT_TRUE(get_family_descriptor(FAMILY_ELF)->set_difficulty == nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_GHOST)->set_difficulty == nullptr);

    // level_up: non-null for families with custom modifiers
    ASSERT_TRUE(get_family_descriptor(FAMILY_ELF)->level_up != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ARCHER)->level_up != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_MAGE)->level_up != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_SKELETON)->level_up != nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_ORC)->level_up != nullptr);
    // Default-modifier families have nullptr
    ASSERT_TRUE(get_family_descriptor(FAMILY_SOLDIER)->level_up == nullptr);
    ASSERT_TRUE(get_family_descriptor(FAMILY_CLERIC)->level_up == nullptr);
}
