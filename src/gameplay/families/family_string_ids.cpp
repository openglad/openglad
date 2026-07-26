/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/families/family_string_ids.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/gameplay/effect_family_descriptor.h>
#include <openglad/gameplay/treasure_family_descriptor.h>
#include <openglad/gameplay/generator_family_descriptor.h>
#include <openglad/gameplay/statistics.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace og::families {

namespace {

const char* descriptor_name(Order order, int family_id)
{
    switch (order) {
        case Order::Living: {
            const FamilyDescriptor* d = get_family_descriptor(family_id);
            return d != nullptr ? d->name : nullptr;
        }
        case Order::Weapon: {
            const WeaponFamilyDescriptor* d =
                get_weapon_family_descriptor(family_id);
            return d != nullptr ? d->name : nullptr;
        }
        case Order::Treasure: {
            const TreasureFamilyDescriptor* d =
                get_treasure_family_descriptor(family_id);
            return d != nullptr ? d->name : nullptr;
        }
        case Order::Generator: {
            const GeneratorFamilyDescriptor* d =
                get_generator_family_descriptor(family_id);
            return d != nullptr ? d->name : nullptr;
        }
        case Order::FX: {
            const EffectFamilyDescriptor* d =
                get_effect_family_descriptor(family_id);
            return d != nullptr ? d->name : nullptr;
        }
        default:
            return nullptr;
    }
}

std::string normalize_family_name(const char* raw)
{
    std::string out;
    for (const char* p = raw; *p != '\0'; p++) {
        const unsigned char c = static_cast<unsigned char>(*p);
        out.push_back(c == ' ' ? '_'
                               : static_cast<char>(std::tolower(c)));
    }
    return out;
}

struct BitFlagName {
    const char* name;
    std::int32_t value;
};

// classpack.yaml init_bit_flags vocabulary (statistics.h BIT_* constants).
constexpr BitFlagName kBitFlagNames[] = {
    {"FLYING", BIT_FLYING},
    {"SWIMMING", BIT_SWIMMING},
    {"ANIMATE", BIT_ANIMATE},
    {"INVINCIBLE", BIT_INVINCIBLE},
    {"NO_RANGED", BIT_NO_RANGED},
    {"IMMORTAL", BIT_IMMORTAL},
    {"NO_COLLIDE", BIT_NO_COLLIDE},
    {"PHANTOM", BIT_PHANTOM},
    {"NAMED", BIT_NAMED},
    {"FORESTWALK", BIT_FORESTWALK},
    {"MAGICAL", BIT_MAGICAL},
    {"FIRE", BIT_FIRE},
    {"ETHEREAL", BIT_ETHEREAL},
};

struct AnimationName {
    const char* name;
    FamilyAnimationType type;
};

constexpr AnimationName kAnimationNames[] = {
    {"standard", FamilyAnimationType::FAMILY_ANIM_STANDARD},
    {"mage", FamilyAnimationType::FAMILY_ANIM_MAGE},
    {"skeleton", FamilyAnimationType::FAMILY_ANIM_SKELETON},
    {"giant_skeleton", FamilyAnimationType::FAMILY_ANIM_GIANT_SKELETON},
    {"slime", FamilyAnimationType::FAMILY_ANIM_SLIME},
    {"small_slime", FamilyAnimationType::FAMILY_ANIM_SMALL_SLIME},
    {"static", FamilyAnimationType::FAMILY_ANIM_STATIC},
};

}  // namespace

std::string family_string_id(Order order, int family_id)
{
    const char* name = descriptor_name(order, family_id);
    if (name == nullptr)
        return {};
    const std::string normalized = normalize_family_name(name);
    // Positional escape when the registry display name is ambiguous
    // (golem/giant_skeleton/tower1 all answer to BEAST, the slime trio to
    // SLIME): every member of a collision group uses "core:#<id>" so the
    // exported ids stay unique and resolve back to the exact family.
    for (int other = 0; other < NUM_FAMILY_SLOTS; other++) {
        const char* other_name = descriptor_name(order, other);
        if (other_name == nullptr) {
            // A free slot, not the end of the registry: class packs land
            // above the core pins, so the scan must cover the whole byte
            // range rather than stopping at the first gap.
            continue;
        }
        if (other != family_id &&
            normalize_family_name(other_name) == normalized)
            return "core:#" + std::to_string(family_id);
    }
    return "core:" + normalized;
}

int resolve_family_string_id(Order order, const char* family_str)
{
    const char* colon = std::strchr(family_str, ':');
    const char* name_part = colon != nullptr ? colon + 1 : family_str;
    if (name_part[0] == '#') {
        char* end = nullptr;
        const long id = std::strtol(name_part + 1, &end, 10);
        if (end != name_part + 1 && *end == '\0' && id >= 0 &&
            id < NUM_FAMILY_SLOTS &&
            descriptor_name(order, static_cast<int>(id)) != nullptr)
            return static_cast<int>(id);
        return -1;
    }
    const std::string want = normalize_family_name(name_part);
    for (int id = 0; id < NUM_FAMILY_SLOTS; id++) {
        const char* name = descriptor_name(order, id);
        if (name == nullptr) {
            // Free slot — keep scanning; pack families sit above the pins.
            continue;
        }
        if (normalize_family_name(name) == want)
            return id;
    }
    return -1;
}

const char* animation_type_name(FamilyAnimationType type)
{
    for (const auto& entry : kAnimationNames) {
        if (entry.type == type)
            return entry.name;
    }
    return "standard";
}

bool animation_type_from_name(std::string_view name, FamilyAnimationType& out)
{
    for (const auto& entry : kAnimationNames) {
        if (name == entry.name) {
            out = entry.type;
            return true;
        }
    }
    return false;
}

std::int32_t bit_flag_from_name(std::string_view name)
{
    for (const auto& entry : kBitFlagNames) {
        if (name == entry.name)
            return entry.value;
    }
    return 0;
}

std::vector<std::string> bit_flag_names(std::int32_t flags,
                                        std::int32_t* unknown_bits)
{
    std::vector<std::string> out;
    std::int32_t remaining = flags;
    for (const auto& entry : kBitFlagNames) {
        if ((remaining & entry.value) != 0) {
            out.emplace_back(entry.name);
            remaining &= ~entry.value;
        }
    }
    if (unknown_bits != nullptr)
        *unknown_bits = remaining;
    return out;
}

}  // namespace og::families
