/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Hostile-input tests for the class-pack loader (headless).
//
// packs/*/classpack.yaml is now the ONLY source of family descriptor data,
// and packs arrive over the network from another player's machine
// (src/gameplay/pack_transfer.cpp). So every one of these inputs is a
// thing a peer can hand this process:
//
//   * YAML that is structurally wrong in a way libyaml accepts locally but
//     the reader does not expect (a mapping where a scalar belongs, an
//     alias, a truncated stream inside a node being skipped),
//   * declared values that are the wrong TYPE for the field (a float list
//     item that is not a number, a null inside a string list),
//   * declarations that are well-formed but ask for more than the engine
//     has (a wire id past a registry's capacity, hundreds of auto ids).
//
// The contract under test is uniform and load-bearing: a bad pack is
// REJECTED, never obeyed and never fatal. parse_classpack_yaml returns
// false; install_classpack_data skips the entry and installs the rest;
// nothing reads out of bounds and nothing is left half-installed.
//
// tests/unit/test_classpack_yaml.cpp covers the happy paths and the
// round trip; this file is deliberately only the failure surface.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/generator_family_descriptor.h>
#include <openglad/gameplay/treasure_family_descriptor.h>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/resources/classpack_yaml.h>
#include <openglad/resources/packs.h>

#include <string>
#include <utility>
#include <vector>

using og::data::ClasspackData;
using og::data::parse_classpack_yaml;

namespace {

// Same guard the happy-path install tests use: start from a registry whose
// mod slots are freed, and free them again afterwards, so a rejected entry
// cannot leak a family into the next test.
class ModSlotGuard {
public:
    ModSlotGuard()
    {
        init_all_registries();
        reset_all_registry_mod_slots();
    }
    ~ModSlotGuard() { reset_all_registry_mod_slots(); }

    ModSlotGuard(const ModSlotGuard&) = delete;
    ModSlotGuard& operator=(const ModSlotGuard&) = delete;
};

// Parse must fail AND must not have thrown, crashed, or read past the end.
void expect_rejected(const char* yaml, const char* label)
{
    ClasspackData data;
    EXPECT_FALSE(parse_classpack_yaml(yaml, data, label))
        << "expected rejection: " << label;
}

}  // namespace

// ---------------------------------------------------------------------------
// Structural YAML the reader does not expect
// ---------------------------------------------------------------------------

TEST(ClasspackYamlErrors, root_shape_violations_reject_the_pack)
{
    // Root is a sequence, not a mapping.
    expect_rejected("- one\n- two\n", "root-sequence");
    // Root is a bare scalar.
    expect_rejected("just-a-string\n", "root-scalar");
    // A root KEY that is not a scalar (complex mapping key).
    expect_rejected("? [a, b]\n: value\n", "root-complex-key");
    // Well-formed YAML, but the document is empty: no stream content.
    expect_rejected("", "empty");
}

TEST(ClasspackYamlErrors, families_section_shape_violations_reject_the_pack)
{
    // A families KEY that is not a scalar order name.
    expect_rejected("families:\n  ? [living]\n  : []\n", "order-complex-key");
    // An order whose entries are scalars rather than mappings.
    expect_rejected("families:\n  living:\n    - just-a-string\n",
                    "entry-scalar");
    // An entry KEY that is not a scalar.
    expect_rejected("families:\n  living:\n    - ? [id]\n      : x\n",
                    "field-complex-key");
}

TEST(ClasspackYamlErrors, empty_and_skipped_sections_are_tolerated)
{
    // `weapon: ~` is an explicitly empty section, not an error; and an
    // order spelled as a mapping is skipped whole for forward
    // compatibility. Both must leave the sibling living entry installed.
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n"
        "  weapon: ~\n"
        "  effect:\n"
        "    later: {shape: mapping}\n"
        "  living:\n"
        "    - id: p:one\n"
        "      hiring_cost: 7\n",
        data, "tolerated"));
    EXPECT_TRUE(data.weapons.empty());
    EXPECT_TRUE(data.effects.empty());
    ASSERT_EQ(data.living.size(), 1u);
    EXPECT_EQ(data.living[0].hiring_cost.value_or(-1), 7);
}

TEST(ClasspackYamlErrors, aliases_and_nested_mappings_on_a_field_are_skipped)
{
    // An anchor/alias pair and an unknown nested mapping are both legal
    // YAML the reader has no field for. Neither may sink the pack, and
    // neither may swallow the entries that follow.
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "shared: &anchor 5\n"
        "families:\n"
        "  living:\n"
        "    - id: p:one\n"
        "      mystery: {a: 1, b: [2, 3]}\n"
        "      echo: *anchor\n"
        "      hiring_cost: 9\n"
        "    - id: p:two\n"
        "      hiring_cost: 11\n",
        data, "alias"));
    ASSERT_EQ(data.living.size(), 2u);
    EXPECT_EQ(data.living[0].hiring_cost.value_or(-1), 9);
    EXPECT_EQ(data.living[1].hiring_cost.value_or(-1), 11);
}

TEST(ClasspackYamlErrors, a_truncated_stream_inside_a_skipped_node_rejects)
{
    // The unknown `mystery:` mapping is being skipped when the stream
    // ends. skip_node must notice rather than spin or walk off the end.
    expect_rejected("families:\n  living:\n    - id: p:one\n"
                    "      mystery: {a: 1, b: [2,",
                    "truncated-skip");
    // Same, one level up: an unknown ROOT section truncated mid-node.
    expect_rejected("future:\n  nested: {a: [1, 2", "truncated-root-skip");
}

// ---------------------------------------------------------------------------
// Wrong types in typed fields
// ---------------------------------------------------------------------------

TEST(ClasspackYamlErrors, wrong_typed_values_reject_the_pack)
{
    // Scalar field given a composite.
    expect_rejected("families:\n  living:\n    - id: p:one\n"
                    "      base_stats: [1, 2, x, 4, 5, 6]\n",
                    "bad-int-list");
    expect_rejected("families:\n  living:\n    - id: p:one\n"
                    "      base_stats: [1, 2, ~, 4, 5, 6]\n",
                    "null-in-int-list");
    expect_rejected("families:\n  living:\n    - id: p:one\n"
                    "      init_max_magicpoints: not-a-number\n",
                    "bad-float");
    expect_rejected("families:\n  living:\n    - id: p:one\n"
                    "      names: [Alpha, ~, Gamma]\n",
                    "null-in-string-list");
    // A list of composites where scalars belong.
    expect_rejected("families:\n  living:\n    - id: p:one\n"
                    "      names: [[nested]]\n",
                    "composite-in-list");
}

TEST(ClasspackYamlErrors, bad_float_lists_reject_the_pack)
{
    // derived_bonuses is the float list every living family carries.
    expect_rejected("families:\n  living:\n    - id: p:one\n"
                    "      derived_bonuses: [1.0, banana, 0, 0, 0, 0, 0, 0]\n",
                    "bad-float-list");
    expect_rejected("families:\n  living:\n    - id: p:one\n"
                    "      derived_bonuses: [1.0, ~, 0, 0, 0, 0, 0, 0]\n",
                    "null-in-float-list");
}

// ---------------------------------------------------------------------------
// anims: section
// ---------------------------------------------------------------------------

TEST(ClasspackYamlErrors, malformed_anims_shapes_reject_the_pack)
{
    expect_rejected("anims:\n  ? [set]\n  : {}\n", "anims-complex-key");
    expect_rejected("anims:\n  myset: [1, 2]\n", "anims-set-not-mapping");
    expect_rejected("anims:\n  myset:\n    rows: eight\n", "anims-bad-rows");
    expect_rejected("anims:\n  myset:\n    frames:\n      - notalist\n",
                    "anims-row-scalar");
    expect_rejected("anims:\n  myset:\n    ? [rows]\n    : 8\n",
                    "anims-field-complex-key");
}

TEST(ClasspackYamlErrors, unknown_anims_keys_are_skipped)
{
    // Unknown scalar, list and mapping keys inside a set must all be
    // skipped without disturbing the rows that follow.
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "anims:\n"
        "  myset:\n"
        "    future_scalar: 1\n"
        "    future_list: [1, 2]\n"
        "    future_map: {a: 1}\n"
        "    rows: 2\n"
        "    frames:\n"
        "      - [0, 1]\n"
        "      - ~\n",
        data, "anims-forward"));
    ASSERT_EQ(data.anims.size(), 1u);
    EXPECT_EQ(data.anims[0].rows.value_or(-1), 2);
    ASSERT_EQ(data.anims[0].frames.size(), 2u);
    EXPECT_FALSE(data.anims[0].frames[0].is_null);
    EXPECT_TRUE(data.anims[0].frames[1].is_null);
}

// ---------------------------------------------------------------------------
// Install: asking for more than the registries have
// ---------------------------------------------------------------------------

// Wire ids are one byte, but the auto counter is not: it runs 21, 22, ...
// per order and simply walks off the end of the 256-slot registry. The
// install-slot lookup is the only thing standing between a pack that
// declares more families than the engine can hold and an out-of-bounds
// registry write, so exercise it on EVERY order — each has its own
// installer with its own copy of the guard.
TEST(ClasspackInstallErrors, an_oversized_pack_stops_at_every_registry_end)
{
    ModSlotGuard guard;

    constexpr int kFirstModId = 21;
    constexpr int kFits = NUM_FAMILY_SLOTS - kFirstModId;
    constexpr int kOverflow = 8;

    ClasspackData data;
    data.pack = "mod";
    for (int i = 0; i < kFits + kOverflow; i++) {
        const std::string suffix = "_" + std::to_string(i);
        {
            og::data::ClasspackLivingEntry e;
            e.id = "mod:bulk_living" + suffix;
            e.hiring_cost = 1;
            data.living.push_back(std::move(e));
        }
        {
            og::data::ClasspackWeaponEntry e;
            e.id = "mod:bulk_weapon" + suffix;
            e.init_lifetime = 1;
            data.weapons.push_back(std::move(e));
        }
        {
            og::data::ClasspackEffectEntry e;
            e.id = "mod:bulk_effect" + suffix;
            e.loops_animation = true;
            data.effects.push_back(std::move(e));
        }
        {
            og::data::ClasspackTreasureEntry e;
            e.id = "mod:bulk_treasure" + suffix;
            e.init_frame = 1;
            data.treasures.push_back(std::move(e));
        }
        {
            og::data::ClasspackGeneratorEntry e;
            e.id = "mod:bulk_generator" + suffix;
            e.has_lifetime = true;
            data.generators.push_back(std::move(e));
        }
    }

    EXPECT_EQ(og::resources::install_classpack_data(std::move(data)),
              5 * kFits)
        << "each order installs exactly its free slots and rejects the rest";

    // Per order: the last id that fits is populated, the first past the end
    // is not, and nothing wrapped around onto a core pin.
    const std::pair<Order, const char*> orders[] = {
        {Order::Living, "mod:bulk_living_"},
        {Order::Weapon, "mod:bulk_weapon_"},
        {Order::FX, "mod:bulk_effect_"},
        {Order::Treasure, "mod:bulk_treasure_"},
        {Order::Generator, "mod:bulk_generator_"},
    };
    for (const auto& [order, prefix] : orders) {
        const std::string last = prefix + std::to_string(kFits - 1);
        const std::string past = prefix + std::to_string(kFits);
        EXPECT_EQ(og::families::resolve_family_string_id(order, last.c_str()),
                  NUM_FAMILY_SLOTS - 1)
            << last;
        EXPECT_EQ(og::families::resolve_family_string_id(order, past.c_str()),
                  -1)
            << past;
    }

    const FamilyDescriptor* soldier = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(soldier, nullptr);
    EXPECT_STREQ(soldier->name, "SOLDIER") << "core pin untouched";
}

// init_bit_flags is a list of NAMES. One unknown name must keep the whole
// mask the descriptor already had rather than folding a partial mask — a
// partial fold would silently drop, say, BIT_IMMORTAL off a family and turn
// an immortal boss mortal. The rest of the entry still installs: an
// unrecognised flag is a forward-compatibility miss, not a corrupt pack.
TEST(ClasspackInstallErrors, an_unknown_bit_flag_name_keeps_the_whole_mask)
{
    ModSlotGuard guard;

    // Pin onto a core family so there is a non-zero mask to preserve.
    const FamilyDescriptor* ghost_before =
        get_family_descriptor(FAMILY_GHOST);
    ASSERT_NE(ghost_before, nullptr);
    const std::int32_t mask_before = ghost_before->init_bit_flags;
    ASSERT_NE(mask_before, 0) << "the ghost is the family with flags to lose";

    ClasspackData data;
    data.pack = "mod";
    og::data::ClasspackLivingEntry e;
    e.id = "mod:bad_flags";
    e.wire_id = std::to_string(FAMILY_GHOST);
    e.hiring_cost = 77;
    // FLYING is real and would fold if the loop folded partially.
    e.init_bit_flags =
        std::vector<std::string>{"FLYING", "NOT_A_REAL_FLAG"};
    data.living.push_back(std::move(e));

    EXPECT_EQ(og::resources::install_classpack_data(std::move(data)), 1)
        << "an unknown flag name does not sink the entry";

    const FamilyDescriptor* after = get_family_descriptor(FAMILY_GHOST);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->hiring_cost, 77) << "the good fields still applied";
    EXPECT_EQ(after->init_bit_flags, mask_before)
        << "an unknown flag name must keep the mask whole, not fold a prefix";
}

// An entry with no `id:` at all still resolves a slot, and the slot keeps
// whatever declared id it already had. Nothing may write a null name.
TEST(ClasspackInstallErrors, an_entry_without_a_declared_id_keeps_the_slot_id)
{
    ModSlotGuard guard;

    const FamilyDescriptor* before = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(before, nullptr);
    const char* declared_before = before->declared_id;

    ClasspackData data;
    data.pack = "mod";
    og::data::ClasspackLivingEntry e;
    e.id.clear();      // no id declared
    e.wire_id = "0";   // but pinned onto the soldier slot
    e.hiring_cost = 4242;
    data.living.push_back(std::move(e));

    EXPECT_EQ(og::resources::install_classpack_data(std::move(data)), 1);

    const FamilyDescriptor* after = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->hiring_cost, 4242) << "declared field applied";
    EXPECT_EQ(after->declared_id, declared_before)
        << "an id-less entry must not clear the slot's string id";
}
