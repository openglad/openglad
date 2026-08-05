#include <gtest/gtest.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/generator_family_descriptor.h>
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
    // Family behavior lives in class-pack Lua. Every descriptor behavior
    // slot is nullptr, for core and mod families alike, so the engine has no
    // family-specific C++ fallback.
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

// --- registries hold only what a class pack installed ---------------------

TEST(FamilyRegistry, core_span_is_fully_declared_by_the_mounted_packs)
{
    og::test::mount_core_pack();
    // init_*_registry() installs nothing: every slot starts free and the
    // core pack fills the core span. A gap
    // here is the exact condition require_core_families_installed() refuses
    // to start on, so assert the same thing it does, per order.
    EXPECT_EQ(-1, first_unpopulated_core_family_slot()) << "living";
    EXPECT_EQ(-1, first_unpopulated_core_weapon_family_slot()) << "weapon";
    EXPECT_EQ(-1, first_unpopulated_core_effect_family_slot()) << "effect";
    EXPECT_EQ(-1, first_unpopulated_core_treasure_family_slot()) << "treasure";
    EXPECT_EQ(-1, first_unpopulated_core_generator_family_slot()) << "generator";
    EXPECT_NO_THROW(require_core_families_installed("unit test"));
}

TEST(FamilyRegistry, every_core_family_carries_its_pack_declared_id)
{
    og::test::mount_core_pack();
    // declared_id is stamped by the classpack installer and by nothing else,
    // so a "core:" id on every core slot is the direct proof that the data
    // came from packs/core/families/ rather than from compiled-in C++.
    auto declared = [](const char* id, const char* order, int slot) {
        ASSERT_NE(nullptr, id) << order << " slot " << slot
                               << " was not declared by any pack";
        EXPECT_EQ(0, std::strncmp(id, "core:", 5))
            << order << " slot " << slot << " declared as " << id;
    };
    for (int i = 0; i < NUM_FAMILIES; i++)
        declared(get_family_descriptor(i)->declared_id, "living", i);
    for (int i = 0; i < 20; i++)
        declared(get_weapon_family_descriptor(i)->declared_id, "weapon", i);
    for (int i = 0; i < 13; i++)
        declared(get_effect_family_descriptor(i)->declared_id, "effect", i);
    // Treasure core span ends at SPEED (12): wire bytes 13/14 are the
    // retired CTF slots, free for campaign packs (they answer nullptr with
    // no campaign mounted).
    for (int i = 0; i < 13; i++)
        declared(get_treasure_family_descriptor(i)->declared_id, "treasure", i);
    EXPECT_EQ(nullptr, get_treasure_family_descriptor(13));
    EXPECT_EQ(nullptr, get_treasure_family_descriptor(14));
    for (int i = 0; i < 4; i++)
        declared(get_generator_family_descriptor(i)->declared_id, "generator", i);
}

TEST(FamilyRegistry, cross_family_references_resolve_regardless_of_file_order)
{
    og::test::mount_core_pack();
    // A pack may name a family it declares LOWER DOWN the same file. The
    // core pack does exactly that: core:mage promotes_to core:archmage, and
    // core:tent's generator names core:skeleton. Nothing pre-populates those
    // slots any more, so the installer has to resolve same-pack references
    // from its own declarations (packs.cpp: PackWireIds) rather than from a
    // registry lookup that would depend on file order.
    EXPECT_EQ(FAMILY_ARCHMAGE, get_family_descriptor(FAMILY_MAGE)->promotes_to);
    EXPECT_EQ(FAMILY_BIG_ORC, get_family_descriptor(FAMILY_ORC)->promotes_to);
    EXPECT_EQ(-1, get_family_descriptor(FAMILY_SOLDIER)->promotes_to);
    // living -> weapon (declared in an order installed before livings) and
    // generator -> living (installed after them).
    EXPECT_EQ(FAMILY_KNIFE, get_family_descriptor(FAMILY_SOLDIER)->default_weapon);
    EXPECT_EQ(FAMILY_SKELETON,
              get_generator_family_descriptor(FAMILY_TENT)->default_weapon);
    EXPECT_EQ(FAMILY_MAGE,
              get_generator_family_descriptor(FAMILY_TOWER)->default_weapon);
}

TEST(FamilyRegistry, promotion_formula_survives_the_pack_install)
{
    og::test::mount_core_pack();
    // promotion_new_level is the one descriptor field a declaration cannot
    // spell (it is a formula, not a value), so family_registry.cpp seeds it
    // into the free slot and the installer — which copies the slot it
    // patches — carries it through. If that ever stops working, every mage
    // silently promotes to level 1.
    const FamilyDescriptor* mage = get_family_descriptor(FAMILY_MAGE);
    ASSERT_NE(nullptr, mage->promotion_new_level);
    EXPECT_EQ(3, static_cast<int>(mage->promotion_new_level(10)));
    EXPECT_EQ(1, static_cast<int>(mage->promotion_new_level(6)));
    const FamilyDescriptor* orc = get_family_descriptor(FAMILY_ORC);
    ASSERT_NE(nullptr, orc->promotion_new_level);
    EXPECT_EQ(1, static_cast<int>(orc->promotion_new_level(42)));
    EXPECT_EQ(nullptr, get_family_descriptor(FAMILY_SOLDIER)->promotion_new_level);
}
