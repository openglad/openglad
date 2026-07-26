#include <gtest/gtest.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/legacy/base.h>
#include <openglad/gameplay/statistics.h>
#include <cstring>

#include "test_family_hook_dispatch.h"

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

TEST(FamilyRegistry, registry_carries_no_cpp_behavior_callbacks)
{
    init_family_registry();
    // Stage A (design doc §9a): family BEHAVIOR is retired from C++ — it
    // lives only in the core class pack. Every descriptor behavior slot is
    // nullptr, for core families and mod families alike, so the engine has
    // nothing family-specific left to fall back to.
    for (int i = 0; i < NUM_FAMILIES; i++)
    {
        const FamilyDescriptor* d = get_family_descriptor(i);
        ASSERT_TRUE(d != nullptr);
        EXPECT_EQ(nullptr, d->do_special) << d->name;
        EXPECT_EQ(nullptr, d->check_special_ai) << d->name;
        EXPECT_EQ(nullptr, d->hit_response) << d->name;
        EXPECT_EQ(nullptr, d->set_difficulty) << d->name;
        EXPECT_EQ(nullptr, d->level_up) << d->name;
        EXPECT_EQ(nullptr, d->on_death) << d->name;
        EXPECT_EQ(nullptr, d->on_act_living) << d->name;
        EXPECT_EQ(nullptr, d->on_shoved) << d->name;
        EXPECT_EQ(nullptr, d->on_fire_weapon) << d->name;
        EXPECT_EQ(nullptr, d->handle_teleport) << d->name;
        EXPECT_EQ(nullptr, d->on_create) << d->name;
        EXPECT_EQ(nullptr, d->customize_weapon) << d->name;
        EXPECT_EQ(nullptr, d->on_ani_complete) << d->name;
        EXPECT_EQ(nullptr, d->on_melee_hit) << d->name;
    }
}

TEST(FamilyRegistry, registry_behavior_lives_in_pack_lua)
{
    using og::script::FamilyHook;
    init_family_registry();
    og::test::mount_core_pack();
    const og::script::WorldScripts& ws = og::script::active_world_scripts();

    auto has = [&ws](int family, FamilyHook hook) {
        return ws.has_hook(Order::Living, family, hook);
    };

    // do_special: registered for every family that has a special.
    for (int family : {FAMILY_SKELETON, FAMILY_GHOST, FAMILY_FIREELEMENTAL,
                       FAMILY_SLIME, FAMILY_SMALL_SLIME, FAMILY_MEDIUM_SLIME,
                       FAMILY_BARBARIAN, FAMILY_ARCHMAGE, FAMILY_CLERIC,
                       FAMILY_DRUID, FAMILY_MAGE, FAMILY_THIEF,
                       FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_ORC})
        EXPECT_TRUE(has(family, FamilyHook::DoSpecial))
            << get_family_descriptor(family)->name;
    // Data-only families register none.
    for (int family : {FAMILY_GOLEM, FAMILY_GIANT_SKELETON, FAMILY_TOWER1,
                       FAMILY_FAERIE, FAMILY_BIG_ORC})
        EXPECT_FALSE(has(family, FamilyHook::DoSpecial))
            << get_family_descriptor(family)->name;

    // check_special_ai: families with custom AI gating.
    for (int family : {FAMILY_SOLDIER, FAMILY_ARCHER, FAMILY_FIREELEMENTAL,
                       FAMILY_GHOST, FAMILY_ORC, FAMILY_THIEF, FAMILY_MAGE,
                       FAMILY_SLIME, FAMILY_CLERIC, FAMILY_SKELETON})
        EXPECT_TRUE(has(family, FamilyHook::CheckSpecialAi))
            << get_family_descriptor(family)->name;
    EXPECT_FALSE(has(FAMILY_DRUID, FamilyHook::CheckSpecialAi));
    EXPECT_FALSE(has(FAMILY_BARBARIAN, FamilyHook::CheckSpecialAi));

    // hit_response / on_death / set_difficulty / level_up.
    for (int family : {FAMILY_MAGE, FAMILY_ARCHMAGE, FAMILY_ARCHER})
        EXPECT_TRUE(has(family, FamilyHook::HitResponse))
            << get_family_descriptor(family)->name;
    EXPECT_FALSE(has(FAMILY_SOLDIER, FamilyHook::HitResponse));
    EXPECT_FALSE(has(FAMILY_CLERIC, FamilyHook::HitResponse));

    for (int family : {FAMILY_FIREELEMENTAL, FAMILY_SLIME, FAMILY_MEDIUM_SLIME})
        EXPECT_TRUE(has(family, FamilyHook::OnDeath))
            << get_family_descriptor(family)->name;
    EXPECT_FALSE(has(FAMILY_SOLDIER, FamilyHook::OnDeath));
    EXPECT_FALSE(has(FAMILY_GHOST, FamilyHook::OnDeath));
    EXPECT_FALSE(has(FAMILY_SMALL_SLIME, FamilyHook::OnDeath));

    for (int family : {FAMILY_SOLDIER, FAMILY_ARCHER, FAMILY_MAGE,
                       FAMILY_CLERIC, FAMILY_DRUID, FAMILY_ORC, FAMILY_GOLEM})
        EXPECT_TRUE(has(family, FamilyHook::SetDifficulty))
            << get_family_descriptor(family)->name;
    EXPECT_FALSE(has(FAMILY_ELF, FamilyHook::SetDifficulty));
    EXPECT_FALSE(has(FAMILY_GHOST, FamilyHook::SetDifficulty));

    for (int family : {FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                       FAMILY_SKELETON, FAMILY_ORC})
        EXPECT_TRUE(has(family, FamilyHook::LevelUp))
            << get_family_descriptor(family)->name;
    EXPECT_FALSE(has(FAMILY_SOLDIER, FamilyHook::LevelUp));
    EXPECT_FALSE(has(FAMILY_CLERIC, FamilyHook::LevelUp));
}
