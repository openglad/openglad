/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// classpack.yaml reader + family string-id tests (headless).
//
// Covers: typed parsing of every field kind (ints, floats, bools, string
// lists, nullable strings), strictness (bad YAML / bad numbers / missing
// ids fail the pack), the canonical string-id round trip for all five
// registries, the exporter↔reader round trip over the committed
// packs/core/classpack.yaml, and — in the install section — that
// installing YAML data overwrites descriptor data fields while PRESERVING
// the C++ behavior callback pointers.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/generator_family_descriptor.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/resources/classpack_yaml.h>
#include <openglad/resources/packs.h>

#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using og::data::ClasspackData;
using og::data::parse_classpack_yaml;

namespace {

const char* kLivingSnippet = R"(pack: testpack
version: 3
title: "Test Pack"
authors: "Nobody"
families:
  living:
    - id: testpack:warlock
      wire_id: auto
      name: "WARLOCK"
      short_name: ~
      base_stats: [12, 6, 12, 8, 9, 1]
      hiring_cost: 250
      derived_bonuses: [120, 0, 20, 0, 0, 0, 4, 6]
      stat_costs: [6, 10, 6, 25, 50, 200]
      special_costs: [5000, 25, 100, 120, 150, 5000]
      weapon_cost: 2
      default_weapon: core:knife
      init_bit_flags: [FLYING, ETHEREAL]
      init_ani_type: 0
      init_max_magicpoints: 50.5
      special_names: ["NONE", "CHARGE", "NONE", "NONE", "NONE", "NONE"]
      alternate_names: ["NONE", "NONE", "NONE", "NONE", "NONE", "NONE"]
      leaves_bloodspot: true
      magic_damage_modifier: 0.5
      is_stationary: false
      has_returning_weapon: true
      is_undead: false
      promotes_to: core:archmage
      promotion_level_req: 6
      death_message: "WARLOCK UNDONE"
      sprite: "mage.png"
      animation: mage
      ai_line_of_sight: 7
      description: "Line one   \nline two"
      names: ["Foo", "Bar"]
      playable: true
      playable_order: 3
)";

} // namespace

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

TEST(ClasspackYaml, parse_full_living_entry)
{
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(kLivingSnippet, data, "snippet"));

    ASSERT_EQ(data.pack, "testpack");
    ASSERT_EQ(data.version, "3");
    ASSERT_EQ(data.title, "Test Pack");
    ASSERT_EQ(data.authors, "Nobody");
    ASSERT_EQ(data.living.size(), 1u);

    const auto& e = data.living[0];
    ASSERT_EQ(e.id, "testpack:warlock");
    ASSERT_EQ(e.wire_id, "auto");
    ASSERT_TRUE(e.name.has_value());
    ASSERT_EQ(*e.name, "WARLOCK");
    ASSERT_TRUE(e.short_name.present);
    ASSERT_TRUE(e.short_name.is_null);
    ASSERT_TRUE(e.base_stats.has_value());
    ASSERT_EQ(e.base_stats->size(), 6u);
    ASSERT_EQ((*e.base_stats)[0], 12);
    ASSERT_EQ((*e.base_stats)[5], 1);
    ASSERT_EQ(e.hiring_cost.value_or(-1), 250);
    ASSERT_TRUE(e.derived_bonuses.has_value());
    ASSERT_EQ(e.derived_bonuses->size(), 8u);
    ASSERT_EQ((*e.derived_bonuses)[0], 120.0f);
    ASSERT_EQ(e.stat_costs->at(1), 10);
    ASSERT_EQ(e.special_costs->at(0), 5000);
    ASSERT_EQ(e.weapon_cost.value_or(-1), 2);
    ASSERT_EQ(e.default_weapon.value_or(""), "core:knife");
    ASSERT_TRUE(e.init_bit_flags.has_value());
    ASSERT_EQ(e.init_bit_flags->size(), 2u);
    ASSERT_EQ((*e.init_bit_flags)[0], "FLYING");
    ASSERT_EQ((*e.init_bit_flags)[1], "ETHEREAL");
    ASSERT_EQ(e.init_ani_type.value_or(-1), 0);
    ASSERT_EQ(e.init_max_magicpoints.value_or(-1.0f), 50.5f);
    ASSERT_EQ(e.special_names->at(1), "CHARGE");
    ASSERT_EQ(e.leaves_bloodspot.value_or(false), true);
    ASSERT_EQ(e.magic_damage_modifier.value_or(-1.0f), 0.5f);
    ASSERT_EQ(e.is_stationary.value_or(true), false);
    ASSERT_EQ(e.has_returning_weapon.value_or(false), true);
    ASSERT_EQ(e.is_undead.value_or(true), false);
    ASSERT_TRUE(e.promotes_to.present);
    ASSERT_FALSE(e.promotes_to.is_null);
    ASSERT_EQ(e.promotes_to.value, "core:archmage");
    ASSERT_EQ(e.promotion_level_req.value_or(-1), 6);
    ASSERT_TRUE(e.death_message.present);
    ASSERT_EQ(e.death_message.value, "WARLOCK UNDONE");
    ASSERT_TRUE(e.sprite.present);
    ASSERT_EQ(e.sprite.value, "mage.png");
    ASSERT_EQ(e.animation.value_or(""), "mage");
    ASSERT_EQ(e.ai_line_of_sight.value_or(-1), 7);
    ASSERT_TRUE(e.description.present);
    ASSERT_EQ(e.description.value, "Line one   \nline two")
        << "double-quoted \\n and trailing spaces must survive";
    ASSERT_TRUE(e.names.has_value());
    ASSERT_EQ(e.names->size(), 2u);
    ASSERT_EQ((*e.names)[0], "Foo");
    ASSERT_EQ(e.playable.value_or(false), true);
    ASSERT_EQ(e.playable_order.value_or(-1), 3);
}

TEST(ClasspackYaml, absent_fields_stay_absent)
{
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n  living:\n    - id: x:sparse\n      hiring_cost: 9\n",
        data, "sparse"));
    ASSERT_EQ(data.living.size(), 1u);
    const auto& e = data.living[0];
    ASSERT_EQ(e.id, "x:sparse");
    ASSERT_TRUE(e.wire_id.empty());
    ASSERT_EQ(e.hiring_cost.value_or(-1), 9);
    ASSERT_FALSE(e.name.has_value());
    ASSERT_FALSE(e.short_name.present);
    ASSERT_FALSE(e.base_stats.has_value());
    ASSERT_FALSE(e.promotes_to.present);
    ASSERT_FALSE(e.description.present);
    ASSERT_FALSE(e.names.has_value());
    ASSERT_FALSE(e.playable.has_value());
}

TEST(ClasspackYaml, parse_weapon_effect_treasure_generator)
{
    const char* yaml =
        "families:\n"
        "  weapon:\n"
        "    - id: core:rock\n"
        "      wire_id: 1\n"
        "      name: \"ROCK\"\n"
        "      fire_sound: 10\n"
        "      skip_sit_notify: false\n"
        "      is_auto_attackable: true\n"
        "      init_bit_flags: [FORESTWALK]\n"
        "      init_lifetime: 350\n"
        "      init_ani_type: 5\n"
        "      vz: 0.7\n"
        "      gravity: 0.09\n"
        "      sizez: 12\n"
        "      can_drop_floors: true\n"
        "  effect:\n"
        "    - id: core:marker\n"
        "      wire_id: 8\n"
        "      loops_animation: true\n"
        "      creates_hit_effect: false\n"
        "      init_bit_flags: []\n"
        "  treasure:\n"
        "    - id: core:stain\n"
        "      wire_id: 0\n"
        "      init_ignore: true\n"
        "      init_frame: -1\n"
        "  generator:\n"
        "    - id: core:tent\n"
        "      wire_id: 0\n"
        "      default_weapon: core:skeleton\n"
        "      has_lifetime: true\n"
        "      spawn_ani_type: 3\n"
        "      clear_owner: false\n";
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(yaml, data, "orders"));

    ASSERT_EQ(data.weapons.size(), 1u);
    const auto& w = data.weapons[0];
    ASSERT_EQ(w.id, "core:rock");
    ASSERT_EQ(w.wire_id, "1");
    ASSERT_EQ(w.fire_sound.value_or(-1), 10);
    ASSERT_EQ(w.is_auto_attackable.value_or(false), true);
    ASSERT_EQ(w.init_bit_flags->at(0), "FORESTWALK");
    ASSERT_EQ(w.init_lifetime.value_or(-1), 350);
    ASSERT_EQ(w.init_ani_type.value_or(-1), 5);
    ASSERT_EQ(w.vz.value_or(-1.0f), 0.7f);
    ASSERT_EQ(w.gravity.value_or(-1.0f), 0.09f);
    ASSERT_EQ(w.sizez.value_or(-1), 12);
    ASSERT_EQ(w.can_drop_floors.value_or(false), true);

    ASSERT_EQ(data.effects.size(), 1u);
    ASSERT_EQ(data.effects[0].loops_animation.value_or(false), true);
    ASSERT_TRUE(data.effects[0].init_bit_flags.has_value());
    ASSERT_TRUE(data.effects[0].init_bit_flags->empty());

    ASSERT_EQ(data.treasures.size(), 1u);
    ASSERT_EQ(data.treasures[0].init_ignore.value_or(false), true);
    ASSERT_EQ(data.treasures[0].init_frame.value_or(0), -1);

    ASSERT_EQ(data.generators.size(), 1u);
    ASSERT_EQ(data.generators[0].default_weapon.value_or(""),
              "core:skeleton");
    ASSERT_EQ(data.generators[0].spawn_ani_type.value_or(-1), 3);
}

TEST(ClasspackYaml, positional_escape_id_is_plain_scalar)
{
    // "core:#19" must parse as one plain scalar: '#' only opens a YAML
    // comment after whitespace.
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n  living:\n    - id: core:#19\n      wire_id: 19\n",
        data, "escape"));
    ASSERT_EQ(data.living.size(), 1u);
    ASSERT_EQ(data.living[0].id, "core:#19");
}

TEST(ClasspackYaml, parse_errors_fail_pack)
{
    ClasspackData a;
    ASSERT_FALSE(parse_classpack_yaml("families: [unclosed", a, "bad-yaml"));

    ClasspackData b;
    ASSERT_FALSE(parse_classpack_yaml(
        "families:\n  living:\n    - id: x:y\n      hiring_cost: soon\n", b,
        "bad-int"));

    ClasspackData c;
    ASSERT_FALSE(parse_classpack_yaml(
        "families:\n  living:\n    - id: x:y\n      playable: yep\n", c,
        "bad-bool"));

    ClasspackData d;
    ASSERT_FALSE(parse_classpack_yaml(
        "families:\n  living:\n    - name: \"NO ID\"\n", d, "no-id"));

    ClasspackData e;
    ASSERT_FALSE(parse_classpack_yaml("", e, "empty"));
}

TEST(ClasspackYaml, unknown_keys_and_sections_skipped)
{
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "pack: p\n"
        "future_thing:\n"
        "  nested: {a: 1, b: [2, 3]}\n"
        "families:\n"
        "  holograms:\n"
        "    - id: p:ghost2\n"
        "      shimmer: 9\n"
        "  living:\n"
        "    - id: p:one\n"
        "      future_field: whatever\n"
        "      state_slots: 2\n"
        "      script: scripts/one.lua\n"
        "      hiring_cost: 5\n",
        data, "forward"));
    ASSERT_EQ(data.pack, "p");
    ASSERT_EQ(data.living.size(), 1u);
    ASSERT_EQ(data.living[0].id, "p:one");
    ASSERT_EQ(data.living[0].hiring_cost.value_or(-1), 5);
}

// ---------------------------------------------------------------------------
// Family string ids + vocabulary
// ---------------------------------------------------------------------------

TEST(FamilyStringIds, canonical_ids_and_resolution)
{
    init_all_registries();

    ASSERT_EQ(og::families::family_string_id(Order::Living, FAMILY_SOLDIER),
              "core:soldier");
    ASSERT_EQ(og::families::family_string_id(Order::Living, FAMILY_BIG_ORC),
              "core:orc_captain")
        << "spaces in registry names map to underscores";
    // Collision groups take the positional escape — every member.
    ASSERT_EQ(og::families::family_string_id(Order::Living, FAMILY_SLIME),
              "core:#8");
    ASSERT_EQ(og::families::family_string_id(Order::Living,
                                             FAMILY_GIANT_SKELETON),
              "core:#19");
    ASSERT_EQ(og::families::family_string_id(Order::Weapon, FAMILY_KNIFE),
              "core:knife");
    ASSERT_EQ(og::families::family_string_id(Order::Living, 255), "");

    ASSERT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "core:soldier"),
              FAMILY_SOLDIER);
    ASSERT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "soldier"),
              FAMILY_SOLDIER)
        << "bare names resolve too";
    ASSERT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "core:#19"),
              FAMILY_GIANT_SKELETON);
    ASSERT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "core:no_such"),
              -1);

    // Round trip: every registered family in every order.
    const Order orders[] = {Order::Living, Order::Weapon, Order::Treasure,
                            Order::Generator, Order::FX};
    for (Order order : orders) {
        for (int id = 0; id < 256; id++) {
            const std::string sid =
                og::families::family_string_id(order, id);
            if (sid.empty())
                break; // dense registries: first miss ends the order
            ASSERT_EQ(og::families::resolve_family_string_id(order,
                                                             sid.c_str()),
                      id)
                << "round trip failed for order "
                << static_cast<int>(order) << " id " << id << " (" << sid
                << ")";
        }
    }
}

TEST(FamilyStringIds, vocabulary_round_trips)
{
    const FamilyAnimationType all_types[] = {
        FamilyAnimationType::FAMILY_ANIM_STANDARD,
        FamilyAnimationType::FAMILY_ANIM_MAGE,
        FamilyAnimationType::FAMILY_ANIM_SKELETON,
        FamilyAnimationType::FAMILY_ANIM_GIANT_SKELETON,
        FamilyAnimationType::FAMILY_ANIM_SLIME,
        FamilyAnimationType::FAMILY_ANIM_SMALL_SLIME,
        FamilyAnimationType::FAMILY_ANIM_STATIC,
    };
    for (FamilyAnimationType t : all_types) {
        FamilyAnimationType back{};
        ASSERT_TRUE(og::families::animation_type_from_name(
            og::families::animation_type_name(t), back));
        ASSERT_EQ(back, t);
    }
    FamilyAnimationType unused{};
    ASSERT_FALSE(og::families::animation_type_from_name("moonwalk", unused));

    ASSERT_EQ(og::families::bit_flag_from_name("FLYING"), BIT_FLYING);
    ASSERT_EQ(og::families::bit_flag_from_name("ETHEREAL"), BIT_ETHEREAL);
    ASSERT_EQ(og::families::bit_flag_from_name("SPELUNKING"), 0);

    std::int32_t unknown = -1;
    const auto names = og::families::bit_flag_names(
        BIT_FLYING | BIT_ANIMATE | BIT_ETHEREAL, &unknown);
    ASSERT_EQ(unknown, 0);
    ASSERT_EQ(names.size(), 3u);
    ASSERT_EQ(names[0], "FLYING");
    ASSERT_EQ(names[1], "ANIMATE");
    ASSERT_EQ(names[2], "ETHEREAL");

    (void)og::families::bit_flag_names(BIT_LAST, &unknown);
    ASSERT_EQ(unknown, BIT_LAST) << "unnamed bits must be reported";
}

// ---------------------------------------------------------------------------
// Exporter ↔ reader round trip over the committed core pack
// ---------------------------------------------------------------------------

TEST(ClasspackYaml, committed_core_pack_matches_registries)
{
    std::ifstream in("packs/core/classpack.yaml", std::ios::binary);
    ASSERT_TRUE(in.good())
        << "packs/core/classpack.yaml missing (run from the repo root)";
    std::stringstream buffer;
    buffer << in.rdbuf();

    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(buffer.str(), data, "packs/core"));
    ASSERT_EQ(data.pack, "core");
    ASSERT_EQ(data.living.size(), static_cast<std::size_t>(NUM_FAMILIES));
    ASSERT_EQ(data.weapons.size(), 20u);
    ASSERT_EQ(data.effects.size(), 13u);
    ASSERT_EQ(data.treasures.size(), 15u);
    ASSERT_EQ(data.generators.size(), 4u);

    init_all_registries();
    // Spot-check the soldier entry against the live registry: the
    // committed YAML must mirror the C++ descriptor data exactly.
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(fd, nullptr);
    const auto& e = data.living[0];
    ASSERT_EQ(e.id, "core:soldier");
    ASSERT_EQ(e.wire_id, "0");
    ASSERT_EQ(*e.name, fd->name);
    for (int i = 0; i < 6; i++)
        ASSERT_EQ((*e.base_stats)[static_cast<std::size_t>(i)],
                  fd->base_stats[i]);
    for (int i = 0; i < 8; i++)
        ASSERT_EQ((*e.derived_bonuses)[static_cast<std::size_t>(i)],
                  fd->derived_bonuses[i]);
    ASSERT_EQ(e.hiring_cost.value_or(-1), fd->hiring_cost);
    ASSERT_EQ(e.default_weapon.value_or(""), "core:knife");
    ASSERT_EQ(e.description.value, fd->description);
    ASSERT_EQ(e.names->size(),
              static_cast<std::size_t>(fd->name_pool_size));
    ASSERT_EQ(e.animation.value_or(""), "standard");

    // Every living entry's wire_id is the pinned legacy byte.
    for (std::size_t i = 0; i < data.living.size(); i++)
        ASSERT_EQ(data.living[i].wire_id, std::to_string(i));
}

// ---------------------------------------------------------------------------
// Registry install: YAML data overwrites, callbacks preserved
// ---------------------------------------------------------------------------

TEST(ClasspackInstall, overrides_data_preserves_callbacks)
{
    init_all_registries();
    const FamilyDescriptor before = *get_family_descriptor(FAMILY_SOLDIER);

    og::data::ClasspackData data;
    data.pack = "test";
    og::data::ClasspackLivingEntry e;
    e.id = "test:soldier_override";
    e.wire_id = "0"; // pins the soldier slot
    e.base_stats = std::vector<std::int32_t>{99, 6, 12, 8, 9, 1};
    e.death_message.present = true;
    e.death_message.value = "SOLDIER TESTED";
    e.names = std::vector<std::string>{"Alpha", "Beta"};
    data.living.push_back(std::move(e));

    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);

    const FamilyDescriptor* after = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(after, nullptr);
    // Declared fields reflect the YAML override...
    ASSERT_EQ(after->base_stats[0], 99);
    ASSERT_STREQ(after->death_message, "SOLDIER TESTED");
    ASSERT_EQ(after->name_pool_size, 2);
    ASSERT_STREQ(after->name_pool[0], "Alpha");
    ASSERT_STREQ(after->name_pool[1], "Beta");
    // ...undeclared data fields keep their current values...
    ASSERT_EQ(after->base_stats[1], before.base_stats[1]);
    ASSERT_EQ(after->hiring_cost, before.hiring_cost);
    ASSERT_STREQ(after->name, "SOLDIER");
    ASSERT_EQ(after->description, before.description)
        << "absent description must keep the exact current pointer";
    ASSERT_EQ(after->default_weapon, before.default_weapon);
    // ...and EVERY behavior callback pointer is preserved unchanged.
    ASSERT_EQ(after->promotion_new_level, before.promotion_new_level);
    ASSERT_EQ(after->do_special, before.do_special);
    ASSERT_EQ(after->check_special_ai, before.check_special_ai);
    ASSERT_EQ(after->hit_response, before.hit_response);
    ASSERT_EQ(after->set_difficulty, before.set_difficulty);
    ASSERT_EQ(after->level_up, before.level_up);
    ASSERT_EQ(after->on_death, before.on_death);
    ASSERT_EQ(after->on_act_living, before.on_act_living);
    ASSERT_EQ(after->on_shoved, before.on_shoved);
    ASSERT_EQ(after->on_fire_weapon, before.on_fire_weapon);
    ASSERT_EQ(after->handle_teleport, before.handle_teleport);
    ASSERT_EQ(after->on_create, before.on_create);
    ASSERT_EQ(after->customize_weapon, before.customize_weapon);
    ASSERT_EQ(after->on_ani_complete, before.on_ani_complete);
    ASSERT_EQ(after->on_melee_hit, before.on_melee_hit);

    // Restore the pristine descriptor for the rest of the process.
    ASSERT_TRUE(set_family_descriptor(FAMILY_SOLDIER, before));
}

TEST(ClasspackInstall, wire_id_pins_and_references_resolve)
{
    init_all_registries();
    const FamilyDescriptor before_mage = *get_family_descriptor(FAMILY_MAGE);
    const GeneratorFamilyDescriptor before_tent =
        *get_generator_family_descriptor(FAMILY_TENT);

    og::data::ClasspackData data;
    {
        og::data::ClasspackLivingEntry e;
        e.id = "test:mage_override";
        e.wire_id = "3"; // FAMILY_MAGE
        e.default_weapon = "core:rock";
        e.promotes_to.present = true; // explicit ~ → no promotion
        e.promotes_to.is_null = true;
        data.living.push_back(std::move(e));
    }
    {
        og::data::ClasspackGeneratorEntry g;
        g.id = "test:tent_override";
        g.wire_id = "0"; // FAMILY_TENT
        g.default_weapon = "core:ghost"; // living family reference
        data.generators.push_back(std::move(g));
    }
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 2);

    const FamilyDescriptor* mage = get_family_descriptor(FAMILY_MAGE);
    ASSERT_EQ(mage->default_weapon, FAMILY_ROCK)
        << "default_weapon resolves through the weapon registry";
    ASSERT_EQ(mage->promotes_to, -1) << "explicit ~ clears the promotion";
    ASSERT_EQ(mage->do_special, before_mage.do_special);

    const GeneratorFamilyDescriptor* tent =
        get_generator_family_descriptor(FAMILY_TENT);
    ASSERT_EQ(tent->default_weapon, FAMILY_GHOST)
        << "generator default_weapon resolves through the living registry";

    ASSERT_TRUE(set_family_descriptor(FAMILY_MAGE, before_mage));
    ASSERT_TRUE(set_generator_family_descriptor(FAMILY_TENT, before_tent));
}

TEST(ClasspackInstall, parsed_yaml_installs_weapon_and_skips_bad_refs)
{
    init_all_registries();
    const WeaponFamilyDescriptor before_rock =
        *get_weapon_family_descriptor(FAMILY_ROCK);
    const FamilyDescriptor before_elf = *get_family_descriptor(FAMILY_ELF);

    og::data::ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n"
        "  weapon:\n"
        "    - id: test:rock\n"
        "      wire_id: 1\n"
        "      fire_sound: 42\n"
        "      init_bit_flags: [MAGICAL, FIRE]\n"
        "  living:\n"
        "    - id: test:elf\n"
        "      wire_id: 1\n"
        "      default_weapon: test:no_such_weapon\n"
        "      animation: moonwalk\n"
        "      hiring_cost: 7\n",
        data, "install-yaml"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 2);

    const WeaponFamilyDescriptor* rock =
        get_weapon_family_descriptor(FAMILY_ROCK);
    ASSERT_EQ(rock->fire_sound, 42);
    ASSERT_EQ(rock->init_bit_flags, BIT_MAGICAL | BIT_FIRE);
    ASSERT_EQ(rock->on_death, before_rock.on_death);
    ASSERT_EQ(rock->on_animate, before_rock.on_animate);
    ASSERT_EQ(rock->on_hit_target, before_rock.on_hit_target);

    const FamilyDescriptor* elf = get_family_descriptor(FAMILY_ELF);
    ASSERT_EQ(elf->hiring_cost, 7) << "good fields apply";
    ASSERT_EQ(elf->default_weapon, before_elf.default_weapon)
        << "unresolved reference keeps the current value";
    ASSERT_EQ(elf->animation_type, before_elf.animation_type)
        << "unknown animation name keeps the current value";

    ASSERT_TRUE(set_weapon_family_descriptor(FAMILY_ROCK, before_rock));
    ASSERT_TRUE(set_family_descriptor(FAMILY_ELF, before_elf));
}

TEST(ClasspackInstall, auto_wire_id_beyond_capacity_is_skipped)
{
    init_all_registries();
    og::data::ClasspackData data;
    {
        og::data::ClasspackLivingEntry e;
        e.id = "mod:brand_new";
        e.wire_id = "auto"; // first free id >= 21; living capacity is 21
        e.hiring_cost = 1;
        data.living.push_back(std::move(e));
    }
    {
        og::data::ClasspackLivingEntry e;
        e.id = "mod:bad_wire";
        e.wire_id = "banana";
        data.living.push_back(std::move(e));
    }
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 0)
        << "no capacity above the core pins yet — skipped with a warning";
    ASSERT_EQ(get_family_descriptor(NUM_FAMILIES), nullptr);
}
