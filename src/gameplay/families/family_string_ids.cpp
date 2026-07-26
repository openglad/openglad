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

// The fully-qualified id the declaring pack gave this family ("core:soldier",
// "mypack:warlock"), or nullptr when no pack has declared this slot — free
// slots included, since the getters hide those.
const char* descriptor_declared_id(Order order, int family_id)
{
    switch (order) {
        case Order::Living: {
            const FamilyDescriptor* d = get_family_descriptor(family_id);
            return d != nullptr ? d->declared_id : nullptr;
        }
        case Order::Weapon: {
            const WeaponFamilyDescriptor* d =
                get_weapon_family_descriptor(family_id);
            return d != nullptr ? d->declared_id : nullptr;
        }
        case Order::Treasure: {
            const TreasureFamilyDescriptor* d =
                get_treasure_family_descriptor(family_id);
            return d != nullptr ? d->declared_id : nullptr;
        }
        case Order::Generator: {
            const GeneratorFamilyDescriptor* d =
                get_generator_family_descriptor(family_id);
            return d != nullptr ? d->declared_id : nullptr;
        }
        case Order::FX: {
            const EffectFamilyDescriptor* d =
                get_effect_family_descriptor(family_id);
            return d != nullptr ? d->declared_id : nullptr;
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

// The id a slot answers to before ambiguity is considered: the pack-declared
// id verbatim when there is one, else the legacy "core:<name>" derived from
// the registry display name. Empty for a slot no family occupies.
std::string candidate_string_id(Order order, int family_id)
{
    const char* declared = descriptor_declared_id(order, family_id);
    if (declared != nullptr && declared[0] != '\0')
        return declared;
    const char* name = descriptor_name(order, family_id);
    if (name == nullptr)
        return {};
    return "core:" + normalize_family_name(name);
}

// "<namespace>:#<id>" — the escape that addresses a family by its exact byte
// when its candidate id is shared with another slot. The namespace is kept
// for readability only; resolution ignores it for the '#' form.
std::string positional_string_id(const std::string& candidate, int family_id)
{
    const std::size_t colon = candidate.find(':');
    const std::string ns =
        colon == std::string::npos ? std::string("core")
                                   : normalize_family_name(
                                         candidate.substr(0, colon).c_str());
    return ns + ":#" + std::to_string(family_id);
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
    const std::string mine = candidate_string_id(order, family_id);
    if (mine.empty())
        return {};
    const std::string key = normalize_family_name(mine.c_str());
    // Positional escape when the candidate id is ambiguous: two core
    // families sharing a display name (golem/giant_skeleton/tower1 all
    // answer to BEAST, the slime trio to SLIME), or two packs pinning the
    // same declared id into different slots. Every member of a collision
    // group escapes, so the exported ids stay unique and resolve back to
    // the exact family.
    for (int other = 0; other < NUM_FAMILY_SLOTS; other++) {
        if (other == family_id)
            continue;
        const std::string theirs = candidate_string_id(order, other);
        if (theirs.empty()) {
            // A free slot, not the end of the registry: class packs land
            // above the core pins, so the scan must cover the whole byte
            // range rather than stopping at the first gap.
            continue;
        }
        if (normalize_family_name(theirs.c_str()) == key)
            return positional_string_id(mine, family_id);
    }
    return mine;
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
    // Pass 1 — the namespace IS a scope: an exact match on the declared
    // pack id wins, so two packs may each ship a "WARLOCK" and stay
    // distinguishable by "alpha:warlock" / "beta:warlock".
    const std::string want_full = normalize_family_name(family_str);
    for (int id = 0; id < NUM_FAMILY_SLOTS; id++) {
        const char* declared = descriptor_declared_id(order, id);
        if (declared == nullptr || declared[0] == '\0') {
            // Free slot, or a family no pack declared (a C++ core pin
            // before the core pack installs over it) — pass 2 covers it.
            continue;
        }
        if (normalize_family_name(declared) == want_full)
            return id;
    }
    // Pass 2 — back-compat / convenience: match the local part against the
    // registry display name, ignoring the namespace. This is what makes a
    // bare "soldier" work, and what keeps ids resolving before any pack has
    // declared them. First (lowest) match wins, so a name shared by two
    // mounted families is only addressable through pass 1.
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
