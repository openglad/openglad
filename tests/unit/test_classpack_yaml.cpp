/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// classpack.yaml reader + family string-id tests (headless).
//
// Covers typed parsing of every field kind (ints, floats, bools, string
// lists, nullable strings), strictness (bad YAML / bad numbers / missing
// ids fail the pack), string-id resolution for all five registries, the
// committed split-layout core pack, and descriptor installation semantics.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/family_presentation.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/effect_family_descriptor.h>
#include <openglad/gameplay/generator_family_descriptor.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/treasure_family_descriptor.h>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/resources/classpack_yaml.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/pack_transfer_io.h>
#include <openglad/resources/packs.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>  // mkdtemp lives here on macOS (stdlib.h on glibc)
#endif

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
      stats:
        strength: 12
        dexterity: 6
        constitution: 12
        intelligence: 8
        armor: 9
        level: 1
      combat:
        hp: 120
        melee_damage: 20
        stepsize: 4
        fire_delay: 6
        fire_mp_cost: 2
      costs:
        hire: 250
        train:
          strength: 6
          dexterity: 10
          constitution: 6
          intelligence: 25
          armor: 50
          level: 200
      specials:
        - id: charge
          name: CHARGE
          mp_cost: 25
      default_weapon: core:knife
      init_bit_flags: [FLYING, ETHEREAL]
      init_ani_type: 0
      init_max_magicpoints: 50.5
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

// costs: with a hire price and no training table — the smallest declaration
// that gives an entry one value worth checking after an install.
og::data::ClasspackCostsBlock hire_only(std::int32_t gold)
{
    og::data::ClasspackCostsBlock costs;
    costs.hire = gold;
    return costs;
}

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
    ASSERT_TRUE(e.stats.has_value());
    ASSERT_EQ(e.stats->strength, 12);
    ASSERT_EQ(e.stats->level, 1);
    ASSERT_TRUE(e.combat.has_value());
    ASSERT_EQ(e.combat->hp, 120.0f);
    ASSERT_EQ(e.combat->fire_mp_cost, 2);
    ASSERT_TRUE(e.costs.has_value());
    ASSERT_EQ(e.costs->hire, 250);
    ASSERT_TRUE(e.costs->train.has_value());
    ASSERT_EQ(e.costs->train->dexterity, 10);
    ASSERT_EQ(e.default_weapon.value_or(""), "core:knife");
    ASSERT_TRUE(e.init_bit_flags.has_value());
    ASSERT_EQ(e.init_bit_flags->size(), 2u);
    ASSERT_EQ((*e.init_bit_flags)[0], "FLYING");
    ASSERT_EQ((*e.init_bit_flags)[1], "ETHEREAL");
    ASSERT_EQ(e.init_ani_type.value_or(-1), 0);
    ASSERT_EQ(e.init_max_magicpoints.value_or(-1.0f), 50.5f);
    ASSERT_TRUE(e.specials.has_value());
    ASSERT_EQ(e.specials->size(), 1u);
    ASSERT_EQ((*e.specials)[0].name, "CHARGE");
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
        "families:\n  living:\n    - id: x:sparse\n"
        "      costs:\n        hire: 9\n",
        data, "sparse"));
    ASSERT_EQ(data.living.size(), 1u);
    const auto& e = data.living[0];
    ASSERT_EQ(e.id, "x:sparse");
    ASSERT_TRUE(e.wire_id.empty());
    ASSERT_TRUE(e.costs.has_value());
    ASSERT_EQ(e.costs->hire, 9);
    ASSERT_FALSE(e.costs->train.has_value());
    ASSERT_FALSE(e.name.has_value());
    ASSERT_FALSE(e.short_name.present);
    ASSERT_FALSE(e.stats.has_value());
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
        "families:\n  living:\n    - id: x:y\n      ai_line_of_sight: soon\n",
        b, "bad-int"));

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
        "      costs:\n        hire: 5\n",
        data, "forward"));
    ASSERT_EQ(data.pack, "p");
    ASSERT_EQ(data.living.size(), 1u);
    ASSERT_EQ(data.living[0].id, "p:one");
    ASSERT_EQ(data.living[0].costs->hire, 5);
}

TEST(ClasspackYaml, unknown_list_fields_on_treasure_and_generator_skipped)
{
    // The sequence-shaped branch of the forward-compatibility rule above:
    // treasure and generator entries define no list-valued fields today, so
    // an unknown list on one is accepted and dropped — the entry's known
    // scalar fields still land — instead of failing the whole pack.
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n"
        "  treasure:\n"
        "    - id: p:chest\n"
        "      wire_id: 2\n"
        "      init_frame: 4\n"
        "      future_list: [gild, 7]\n"
        "  generator:\n"
        "    - id: p:spawner\n"
        "      wire_id: 3\n"
        "      spawn_ani_type: 1\n"
        "      future_spawns: [p:ghost, p:orc]\n",
        data, "forward-lists"));
    ASSERT_EQ(data.treasures.size(), 1u);
    ASSERT_EQ(data.treasures[0].id, "p:chest");
    ASSERT_EQ(data.treasures[0].init_frame.value_or(0), 4);
    ASSERT_EQ(data.generators.size(), 1u);
    ASSERT_EQ(data.generators[0].id, "p:spawner");
    ASSERT_EQ(data.generators[0].spawn_ani_type.value_or(-1), 1);
}

// ---------------------------------------------------------------------------
// Family string-id resolution + reader vocabulary
// ---------------------------------------------------------------------------

TEST(FamilyStringIds, resolution)
{
    init_all_registries();

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
}

TEST(FamilyStringIds, reader_vocabulary)
{
    const std::pair<const char*, FamilyAnimationType> animation_types[] = {
        {"standard", FamilyAnimationType::FAMILY_ANIM_STANDARD},
        {"mage", FamilyAnimationType::FAMILY_ANIM_MAGE},
        {"skeleton", FamilyAnimationType::FAMILY_ANIM_SKELETON},
        {"giant_skeleton", FamilyAnimationType::FAMILY_ANIM_GIANT_SKELETON},
        {"slime", FamilyAnimationType::FAMILY_ANIM_SLIME},
        {"small_slime", FamilyAnimationType::FAMILY_ANIM_SMALL_SLIME},
        {"static", FamilyAnimationType::FAMILY_ANIM_STATIC},
    };
    for (const auto& [name, expected] : animation_types) {
        FamilyAnimationType actual{};
        ASSERT_TRUE(og::families::animation_type_from_name(name, actual));
        ASSERT_EQ(actual, expected);
    }
    FamilyAnimationType unused{};
    ASSERT_FALSE(og::families::animation_type_from_name("moonwalk", unused));

    ASSERT_EQ(og::families::bit_flag_from_name("FLYING"), BIT_FLYING);
    ASSERT_EQ(og::families::bit_flag_from_name("ETHEREAL"), BIT_ETHEREAL);
    ASSERT_EQ(og::families::bit_flag_from_name("SPELUNKING"), 0);
}

// ---------------------------------------------------------------------------
// Reader coverage over the committed core pack
// ---------------------------------------------------------------------------

namespace {

// Parses the committed core pack in its shipped split layout: the
// header-only classpack.yaml first, then every families/*.yaml in sorted
// filename order, into ONE ClasspackData — the same concatenation contract
// install_classpacks() applies. Every committed-core test goes through
// this, so the tests read the pack the way the loader does.
void load_committed_core_pack(ClasspackData& data)
{
    const auto read_all = [](const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    };
    ASSERT_TRUE(std::filesystem::exists("packs/core/classpack.yaml"))
        << "packs/core/classpack.yaml missing (run from the repo root)";
    ASSERT_TRUE(parse_classpack_yaml(read_all("packs/core/classpack.yaml"),
                                     data, "packs/core"));
    ASSERT_TRUE(data.living.empty())
        << "core classpack.yaml is the header only; families live in "
           "families/*.yaml";
    std::vector<std::filesystem::path> family_files;
    for (const auto& entry :
         std::filesystem::directory_iterator("packs/core/families"))
    {
        if (entry.path().extension() == ".yaml")
            family_files.push_back(entry.path());
    }
    std::sort(family_files.begin(), family_files.end());
    ASSERT_EQ(family_files.size(), 73u);
    for (const std::filesystem::path& p : family_files)
        ASSERT_TRUE(parse_classpack_yaml(read_all(p), data,
                                         p.string().c_str()))
            << p;
}

}  // namespace

TEST(ClasspackYaml, committed_core_pack_matches_registries)
{
    ClasspackData data;
    load_committed_core_pack(data);
    if (::testing::Test::HasFatalFailure())
        return;
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
    ASSERT_TRUE(e.stats.has_value());
    ASSERT_EQ(e.stats->strength, fd->base_stats[StatAxis::Strength]);
    ASSERT_EQ(e.stats->dexterity, fd->base_stats[StatAxis::Dexterity]);
    ASSERT_EQ(e.stats->constitution, fd->base_stats[StatAxis::Constitution]);
    ASSERT_EQ(e.stats->intelligence, fd->base_stats[StatAxis::Intelligence]);
    ASSERT_EQ(e.stats->armor, fd->base_stats[StatAxis::Armor]);
    ASSERT_EQ(e.stats->level, fd->base_stats[StatAxis::Level]);
    ASSERT_TRUE(e.combat.has_value());
    ASSERT_EQ(e.combat->hp, fd->combat.hp);
    ASSERT_EQ(e.combat->melee_damage, fd->combat.melee_damage);
    ASSERT_EQ(e.combat->stepsize, fd->combat.stepsize);
    ASSERT_EQ(e.combat->fire_delay, fd->combat.fire_delay);
    ASSERT_EQ(e.combat->fire_mp_cost, fd->combat.fire_mp_cost);
    ASSERT_TRUE(e.costs.has_value());
    ASSERT_EQ(e.costs->hire, fd->hiring_cost);
    ASSERT_TRUE(e.costs->train.has_value());
    ASSERT_EQ(e.costs->train->strength, fd->stat_costs[StatAxis::Strength]);
    ASSERT_EQ(e.costs->train->armor, fd->stat_costs[StatAxis::Armor]);
    // The specials list is slot-ordered and its ids are what soldier.lua
    // keys its handlers by; the disabled slots it omits are the registry
    // defaults (index 0 is the engine artifact, index 5 is unused here).
    ASSERT_TRUE(e.specials.has_value());
    ASSERT_EQ(e.specials->size(), 4u);
    for (const og::data::ClasspackSpecialEntry& s : *e.specials) {
        ASSERT_STREQ(s.name.c_str(), fd->special_names[s.slot]);
        ASSERT_EQ(s.mp_cost, fd->special_cost[s.slot]);
        ASSERT_STREQ(s.id.c_str(), fd->special_ids[s.slot]);
    }
    ASSERT_EQ((*e.specials)[0].id, "charge");
    ASSERT_EQ((*e.specials)[0].slot, 1);
    ASSERT_EQ((*e.specials)[3].id, "disarm");
    ASSERT_EQ((*e.specials)[3].slot, 4);
    ASSERT_EQ(e.default_weapon.value_or(""), "core:knife");
    ASSERT_EQ(e.description.value, fd->description);
    ASSERT_EQ(e.names->size(),
              static_cast<std::size_t>(fd->name_pool_size));
    ASSERT_EQ(e.animation.value_or(""), "standard");

    // Every living entry's wire_id is the pinned legacy byte, and every one
    // of them declares all four blocks. A family that omitted one would
    // install whatever the registry slot happened to hold, which for a mod
    // slot is nothing at all.
    for (std::size_t i = 0; i < data.living.size(); i++) {
        ASSERT_EQ(data.living[i].wire_id, std::to_string(i));
        ASSERT_TRUE(data.living[i].stats.has_value()) << data.living[i].id;
        ASSERT_TRUE(data.living[i].combat.has_value()) << data.living[i].id;
        ASSERT_TRUE(data.living[i].costs.has_value()) << data.living[i].id;
        ASSERT_TRUE(data.living[i].specials.has_value()) << data.living[i].id;
    }
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
    e.stats = og::data::ClasspackStatsBlock{99, 6, 12, 8, 9, 1};
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
        "      costs: {hire: 7}\n",
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

// ---------------------------------------------------------------------------
// Mod families above the core pins
// ---------------------------------------------------------------------------

namespace {

// The five registries are process-global and every install test shares
// them. Frees the pack-installed slots on the way IN and OUT, so a
// shuffled run order can never leak a mod family into a test that counts
// core families (and so each test's `auto` ids start from the same place).
// Core pins are never touched by reset_all_registry_mod_slots().
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

// Which ids of one order currently answer a descriptor. Used to prove an
// install changes exactly one slot and leaves every never-populated id
// answering nullptr.
template <typename GetFn>
std::vector<bool> populated_map(GetFn get)
{
    std::vector<bool> out(static_cast<std::size_t>(NUM_FAMILY_SLOTS), false);
    for (int id = 0; id < NUM_FAMILY_SLOTS; id++)
        out[static_cast<std::size_t>(id)] = get(id) != nullptr;
    return out;
}

}  // namespace

// The headline third-party-class case: a pack entry with `wire_id: auto`
// must land in a real slot above the core pins, become visible through the
// ordinary getter, and resolve by string id — while every id the install
// did NOT claim keeps answering nullptr.
TEST(ClasspackInstall, auto_wire_id_lands_above_core_pins_and_resolves)
{
    ModSlotGuard guard;

    const std::vector<bool> living_before =
        populated_map(get_family_descriptor);
    const std::vector<bool> weapon_before =
        populated_map(get_weapon_family_descriptor);
    const std::vector<bool> effect_before =
        populated_map(get_effect_family_descriptor);
    ASSERT_EQ(get_family_descriptor(NUM_FAMILIES), nullptr)
        << "the first living mod slot starts free";

    og::data::ClasspackData data;
    data.pack = "mod";
    {
        og::data::ClasspackLivingEntry e;
        e.id = "mod:warlock";
        e.wire_id = "auto";
        e.name = "WARLOCK";
        e.costs = hire_only(250);
        e.default_weapon = "core:knife";
        e.death_message.present = true;
        e.death_message.value = "WARLOCK UNDONE";
        data.living.push_back(std::move(e));
    }
    {
        // The weapon core count is 20 and the auto counter starts at 21, so
        // this lands ABOVE a free slot 20 — a hole the id scans must walk
        // past rather than mistake for the end of the registry.
        og::data::ClasspackWeaponEntry w;
        w.id = "mod:hexbolt";
        w.wire_id = "auto";
        w.name = "HEXBOLT";
        w.fire_sound = 42;
        data.weapons.push_back(std::move(w));
    }
    {
        // Effects pin only 13 core ids, so this one sits above EIGHT
        // consecutive free slots (13..20).
        og::data::ClasspackEffectEntry fx;
        fx.id = "mod:hexburst";
        fx.wire_id = "auto";
        fx.name = "HEXBURST";
        fx.loops_animation = true;
        data.effects.push_back(std::move(fx));
    }
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 3);

    // (a) the auto ids land above the core pins.
    const int living_id =
        og::families::resolve_family_string_id(Order::Living, "mod:warlock");
    const int weapon_id =
        og::families::resolve_family_string_id(Order::Weapon, "mod:hexbolt");
    const int effect_id =
        og::families::resolve_family_string_id(Order::FX, "mod:hexburst");
    ASSERT_GE(living_id, NUM_FAMILIES) << "mod living family below the pins";
    ASSERT_GE(weapon_id, NUM_FAMILIES) << "mod weapon family below the pins";
    ASSERT_GE(effect_id, NUM_FAMILIES) << "mod effect family below the pins";
    // A fresh auto counter starts each order at the first id past the
    // living pins, so the assignment is exactly reproducible.
    EXPECT_EQ(living_id, NUM_FAMILIES);
    EXPECT_EQ(weapon_id, NUM_FAMILIES);
    EXPECT_EQ(effect_id, NUM_FAMILIES);

    // (b) the ordinary getters hand the mod descriptors back.
    const FamilyDescriptor* warlock = get_family_descriptor(living_id);
    ASSERT_NE(warlock, nullptr);
    EXPECT_EQ(warlock->family_id, living_id);
    EXPECT_STREQ(warlock->name, "WARLOCK");
    EXPECT_EQ(warlock->hiring_cost, 250);
    EXPECT_EQ(warlock->default_weapon, FAMILY_KNIFE)
        << "a mod family's references resolve against the core registries";
    EXPECT_STREQ(warlock->death_message, "WARLOCK UNDONE");

    const WeaponFamilyDescriptor* hexbolt =
        get_weapon_family_descriptor(weapon_id);
    ASSERT_NE(hexbolt, nullptr);
    EXPECT_STREQ(hexbolt->name, "HEXBOLT");
    EXPECT_EQ(hexbolt->fire_sound, 42);

    const EffectFamilyDescriptor* hexburst =
        get_effect_family_descriptor(effect_id);
    ASSERT_NE(hexburst, nullptr);
    EXPECT_STREQ(hexburst->name, "HEXBURST");
    EXPECT_TRUE(hexburst->loops_animation);

    // (c) exactly one slot per order changed; every id in between — and
    // every id above — still answers nullptr.
    const std::vector<bool> living_after =
        populated_map(get_family_descriptor);
    const std::vector<bool> weapon_after =
        populated_map(get_weapon_family_descriptor);
    const std::vector<bool> effect_after =
        populated_map(get_effect_family_descriptor);
    for (int id = 0; id < NUM_FAMILY_SLOTS; id++) {
        const auto i = static_cast<std::size_t>(id);
        EXPECT_EQ(living_after[i], living_before[i] || id == living_id)
            << "living slot " << id;
        EXPECT_EQ(weapon_after[i], weapon_before[i] || id == weapon_id)
            << "weapon slot " << id;
        EXPECT_EQ(effect_after[i], effect_before[i] || id == effect_id)
            << "effect slot " << id;
    }
    // Spelled out for the specific holes the scans have to survive.
    EXPECT_EQ(get_weapon_family_descriptor(weapon_id - 1), nullptr)
        << "weapon slot 20 is free and must stay invisible";
    for (int id = 13; id < effect_id; id++)
        EXPECT_EQ(get_effect_family_descriptor(id), nullptr)
            << "effect slot " << id << " was never populated";
}

// An explicitly pinned mod id far above the pins: the id scans must walk
// the whole byte range, not stop at the first free slot.
TEST(ClasspackInstall, pinned_mod_id_resolves_across_a_gap)
{
    ModSlotGuard guard;

    og::data::ClasspackData data;
    data.pack = "mod";
    og::data::ClasspackLivingEntry e;
    e.id = "mod:lich";
    e.wire_id = "40";  // deliberate gap: 21..39 stay free
    e.name = "LICH";
    e.costs = hire_only(900);
    data.living.push_back(std::move(e));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);

    const FamilyDescriptor* lich = get_family_descriptor(40);
    ASSERT_NE(lich, nullptr);
    EXPECT_STREQ(lich->name, "LICH");
    EXPECT_EQ(lich->family_id, 40);
    EXPECT_EQ(lich->hiring_cost, 900);

    for (int id = NUM_FAMILIES; id < 40; id++)
        EXPECT_EQ(get_family_descriptor(id), nullptr) << "gap slot " << id;

    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "mod:lich"),
              40)
        << "the name scan must walk past the 21..39 gap";
    // A gap slot has no positional escape.
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "core:#30"),
              -1)
        << "an unpopulated id must not become resolvable";
}

// The top of the byte range is a usable slot; anything outside it is not.
TEST(ClasspackInstall, wire_ids_outside_the_byte_range_are_skipped)
{
    ModSlotGuard guard;

    og::data::ClasspackData data;
    data.pack = "mod";
    for (const char* bad : {"256", "-1", "banana", "12x"}) {
        og::data::ClasspackLivingEntry e;
        e.id = std::string("mod:bad_") + bad;
        e.wire_id = bad;
        e.costs = hire_only(1);
        data.living.push_back(std::move(e));
    }
    {
        og::data::ClasspackLivingEntry e;
        e.id = "mod:edge";
        e.wire_id = "255";  // the last slot a family byte can name
        e.name = "EDGE";
        data.living.push_back(std::move(e));
    }
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1)
        << "only the in-range pin installs";

    const FamilyDescriptor* edge =
        get_family_descriptor(NUM_FAMILY_SLOTS - 1);
    ASSERT_NE(edge, nullptr);
    EXPECT_STREQ(edge->name, "EDGE");
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "mod:edge"),
              NUM_FAMILY_SLOTS - 1);
}

// reset_all_registry_mod_slots() is what makes a re-install mirror exactly
// the packs mounted now: an unmounted pack must leave no family behind,
// and the core pins must survive untouched.
TEST(ClasspackInstall, reset_mod_slots_frees_pack_families_and_keeps_core)
{
    ModSlotGuard guard;

    og::data::ClasspackData data;
    data.pack = "mod";
    og::data::ClasspackLivingEntry e;
    e.id = "mod:transient";
    e.wire_id = "auto";
    e.name = "TRANSIENT";
    data.living.push_back(std::move(e));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);
    ASSERT_NE(get_family_descriptor(NUM_FAMILIES), nullptr);

    reset_all_registry_mod_slots();

    EXPECT_EQ(get_family_descriptor(NUM_FAMILIES), nullptr)
        << "the pack family is gone";
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "mod:transient"),
              -1)
        << "and is no longer resolvable by string id";
    for (int id = 0; id < NUM_FAMILIES; id++)
        EXPECT_NE(get_family_descriptor(id), nullptr)
            << "core pin " << id << " must survive the reset";
    EXPECT_STREQ(get_family_descriptor(FAMILY_SOLDIER)->name, "SOLDIER");
}

// ---------------------------------------------------------------------------
// Pack namespaces are a real scope
// ---------------------------------------------------------------------------

namespace {

// One living entry, spelled out because these tests care about exactly two
// fields: the declared id and the display name.
og::data::ClasspackData one_living(const char* pack, const char* id,
                                   const char* wire_id, const char* name,
                                   std::int32_t hiring_cost)
{
    og::data::ClasspackData data;
    data.pack = pack;
    og::data::ClasspackLivingEntry e;
    e.id = id;
    e.wire_id = wire_id;
    e.name = name;
    e.costs = hire_only(hiring_cost);
    data.living.push_back(std::move(e));
    return data;
}

// Restores every populated slot of all five registries verbatim. Same
// reason as CorePinGuard below, for a test that installs a whole pack over
// the pins.
class RegistrySnapshotGuard {
public:
    RegistrySnapshotGuard()
        : living_(grab<FamilyDescriptor>(get_family_descriptor)),
          weapons_(grab<WeaponFamilyDescriptor>(get_weapon_family_descriptor)),
          effects_(grab<EffectFamilyDescriptor>(get_effect_family_descriptor)),
          treasures_(
              grab<TreasureFamilyDescriptor>(get_treasure_family_descriptor)),
          generators_(grab<GeneratorFamilyDescriptor>(
              get_generator_family_descriptor))
    {
    }

    ~RegistrySnapshotGuard()
    {
        put(living_, set_family_descriptor);
        put(weapons_, set_weapon_family_descriptor);
        put(effects_, set_effect_family_descriptor);
        put(treasures_, set_treasure_family_descriptor);
        put(generators_, set_generator_family_descriptor);
    }

    RegistrySnapshotGuard(const RegistrySnapshotGuard&) = delete;
    RegistrySnapshotGuard& operator=(const RegistrySnapshotGuard&) = delete;

private:
    template <typename Desc, typename Get>
    static std::vector<std::pair<int, Desc>> grab(Get get)
    {
        std::vector<std::pair<int, Desc>> out;
        for (int id = 0; id < NUM_FAMILY_SLOTS; id++)
            if (const Desc* d = get(id))
                out.emplace_back(id, *d);
        return out;
    }

    template <typename Desc, typename Set>
    static void put(const std::vector<std::pair<int, Desc>>& saved, Set set)
    {
        for (const auto& entry : saved)
            (void)set(entry.first, entry.second);
    }

    std::vector<std::pair<int, FamilyDescriptor>> living_;
    std::vector<std::pair<int, WeaponFamilyDescriptor>> weapons_;
    std::vector<std::pair<int, EffectFamilyDescriptor>> effects_;
    std::vector<std::pair<int, TreasureFamilyDescriptor>> treasures_;
    std::vector<std::pair<int, GeneratorFamilyDescriptor>> generators_;
};

// Restores one core pin verbatim. The mod-slot reset deliberately leaves
// the pins alone, so a test that installs over one has to put it back or
// every later test (in any --gtest_shuffle order) inherits the edit.
class CorePinGuard {
public:
    explicit CorePinGuard(int family_id)
        : family_id_(family_id), saved_(*get_family_descriptor(family_id))
    {
    }
    ~CorePinGuard() { set_family_descriptor(family_id_, saved_); }

    CorePinGuard(const CorePinGuard&) = delete;
    CorePinGuard& operator=(const CorePinGuard&) = delete;

private:
    int family_id_;
    FamilyDescriptor saved_;
};

}  // namespace

// THE pluggability case: two independent packs that each ship a "WARLOCK".
// The fully-qualified declared id keeps each one separately addressable.
TEST(FamilyStringIds, two_packs_may_ship_the_same_family_name)
{
    ModSlotGuard guard;

    // Explicit wire ids: each install_classpack_data call starts its own
    // auto counter (a real install pass shares one across all packs), so
    // two separate calls would both claim the first auto slot.
    ASSERT_EQ(og::resources::install_classpack_data(
                  one_living("alpha", "alpha:warlock", "30", "WARLOCK", 111)),
              1);
    ASSERT_EQ(og::resources::install_classpack_data(
                  one_living("beta", "beta:warlock", "31", "WARLOCK", 222)),
              1);

    const int alpha =
        og::families::resolve_family_string_id(Order::Living, "alpha:warlock");
    const int beta =
        og::families::resolve_family_string_id(Order::Living, "beta:warlock");
    ASSERT_EQ(alpha, 30);
    ASSERT_EQ(beta, 31);
    ASSERT_NE(alpha, beta) << "the namespace is what tells them apart";

    // Each id reaches its OWN descriptor, not merely a valid one.
    ASSERT_NE(get_family_descriptor(alpha), nullptr);
    ASSERT_NE(get_family_descriptor(beta), nullptr);
    EXPECT_EQ(get_family_descriptor(alpha)->hiring_cost, 111);
    EXPECT_EQ(get_family_descriptor(beta)->hiring_cost, 222);
    EXPECT_STREQ(get_family_descriptor(alpha)->declared_id, "alpha:warlock");
    EXPECT_STREQ(get_family_descriptor(beta)->declared_id, "beta:warlock");

    // Case and spaces normalize on both sides, as for bare names.
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "Alpha:Warlock"),
              alpha);

    // The documented fallbacks: a bare name, or a namespace nobody
    // declared, still resolve — to the lowest matching byte.
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "warlock"),
              alpha)
        << "bare names keep working; first match wins";
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "gamma:warlock"),
              alpha)
        << "an undeclared namespace falls back to the bare-name match";
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "alpha:no_such"),
              -1)
        << "a namespace that exists does not invent families";
}

// With the real core pack installed, EVERY id it declares — all 73 across
// the five orders, positional escapes included — must resolve to the byte
// the pack pinned. This is what every `og.family_id(order, "core:...")` in
// packs/core/scripts/*.lua depends on.
TEST(FamilyStringIds, every_committed_core_pack_id_resolves_to_its_wire_id)
{
    ModSlotGuard mods;
    RegistrySnapshotGuard pins;

    ClasspackData data;
    load_committed_core_pack(data);
    if (::testing::Test::HasFatalFailure())
        return;

    // (order, declared id, pinned wire id) for every entry, kept before the
    // install moves the parsed data into the process-lifetime store.
    std::vector<std::tuple<Order, std::string, int>> expected;
    const auto collect = [&expected](Order order, const auto& entries) {
        for (const auto& e : entries)
            expected.emplace_back(order, e.id, std::stoi(e.wire_id));
    };
    collect(Order::Living, data.living);
    collect(Order::Weapon, data.weapons);
    collect(Order::FX, data.effects);
    collect(Order::Treasure, data.treasures);
    collect(Order::Generator, data.generators);
    ASSERT_EQ(expected.size(), 73u) << "the whole core pack";

    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 73);

    for (const auto& [order, id, wire] : expected) {
        EXPECT_EQ(og::families::resolve_family_string_id(order, id.c_str()),
                  wire)
            << id << " must resolve to its pinned byte";
    }
    EXPECT_STREQ(get_family_descriptor(FAMILY_SOLDIER)->declared_id,
                 "core:soldier");
    // The escapes the core pack ships for its own name collisions.
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "core:#19"),
              FAMILY_GIANT_SKELETON);
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "beast"),
              FAMILY_GOLEM)
        << "the shared display name still lands on the lowest byte";
}

// A mod family may reuse a CORE display name without shadowing the core
// family: "core:soldier" keeps its byte, the mod answers to its own id.
TEST(FamilyStringIds, a_mod_family_reusing_a_core_name_does_not_shadow_it)
{
    ModSlotGuard guard;

    ASSERT_EQ(og::resources::install_classpack_data(one_living(
                  "mod", "mod:soldier", "30", "SOLDIER", 777)),
              1);

    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "core:soldier"),
              FAMILY_SOLDIER);
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "mod:soldier"),
              30);
    EXPECT_EQ(get_family_descriptor(FAMILY_SOLDIER)->hiring_cost, 250)
        << "the core soldier is untouched";
    EXPECT_EQ(get_family_descriptor(30)->hiring_cost, 777);

    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "soldier"),
              FAMILY_SOLDIER)
        << "the bare name still means the core family (lowest byte)";
}

// Two slots that declare the SAME id are ambiguous by name, but each remains
// addressable through the positional escape.
TEST(FamilyStringIds, duplicate_declared_ids_keep_positional_resolution)
{
    ModSlotGuard guard;

    og::data::ClasspackData data =
        one_living("dup", "dup:twin", "30", "FIRST TWIN", 1);
    {
        og::data::ClasspackLivingEntry e;
        e.id = "dup:twin";  // same declared id, different slot
        e.wire_id = "31";
        e.name = "SECOND TWIN";
        e.costs = hire_only(2);
        data.living.push_back(std::move(e));
    }
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 2);

    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "dup:#30"),
              30);
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "dup:#31"),
              31);
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "dup:twin"),
              30)
        << "the ambiguous id itself resolves to the lowest byte";
    // Their display names differ, so the bare-name path still separates them.
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "second_twin"),
              31);
}

// Overriding a stock family in place — the supported reskin. Pin the core
// family's wire_id, KEEP its id, and every reference to "core:soldier"
// (Lua hook registrations included) keeps landing on the same byte.
TEST(ClasspackInstall, a_pack_may_override_a_core_family_in_place)
{
    ModSlotGuard guard;
    CorePinGuard pin(FAMILY_SOLDIER);

    ASSERT_EQ(og::resources::install_classpack_data(one_living(
                  "mod", "core:soldier", "0", "SOLDIER", 1234)),
              1);

    const FamilyDescriptor* soldier = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(soldier, nullptr);
    EXPECT_EQ(soldier->hiring_cost, 1234) << "the pack's data won";
    EXPECT_STREQ(soldier->declared_id, "core:soldier");
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "core:soldier"),
              FAMILY_SOLDIER)
        << "the id an overriding pack keeps is the id that still resolves";
}

// ---------------------------------------------------------------------------
// Presentation fields (glyph + radar + editor label)
// ---------------------------------------------------------------------------

TEST(ClasspackYaml, parse_presentation_block)
{
    const char* yaml =
        "families:\n"
        "  living:\n"
        "    - id: mod:wisp\n"
        "      glyph: \"\xe2\x99\xa3\"\n"  // U+2663 CLUB, 3 UTF-8 bytes
        "      glyph_ascii: \"&\"\n"
        "      glyph_color: magenta\n"
        "      glyph_bold: true\n"
        "      glyph_transparent: false\n"
        "      radar_color: 88\n"
        "      radar_jitter: 5\n"
        "  treasure:\n"
        "    - id: mod:relic\n"
        "      radar_color: team\n"
        "      radar_jitter: 7\n"
        "    - id: mod:dust\n"
        "      radar_color: none\n"
        "  generator:\n"
        "    - id: mod:hut\n"
        "      editor_label: \"HUT\"\n"
        "      glyph_color: team\n";
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(yaml, data, "presentation"));

    const auto& p = data.living[0].presentation;
    ASSERT_EQ(p.glyph.value_or(""), "\xe2\x99\xa3");
    ASSERT_EQ(p.glyph_ascii.value_or(""), "&");
    ASSERT_EQ(p.glyph_color.value_or(""), "magenta");
    ASSERT_EQ(p.glyph_bold.value_or(false), true);
    ASSERT_EQ(p.glyph_transparent.value_or(true), false);
    ASSERT_EQ(p.radar_color.value_or(0), 88);
    ASSERT_EQ(p.radar_jitter.value_or(-1), 5);

    // The two radar sentinel spellings fold to their numeric values.
    ASSERT_EQ(data.treasures[0].presentation.radar_color.value_or(0),
              og::kRadarColorTeam);
    ASSERT_EQ(data.treasures[0].presentation.radar_jitter.value_or(-1), 7);
    ASSERT_EQ(data.treasures[1].presentation.radar_color.value_or(0),
              og::kRadarColorNone);

    ASSERT_EQ(data.generators[0].editor_label.value_or(""), "HUT");
    ASSERT_EQ(data.generators[0].presentation.glyph_color.value_or(""),
              "team");
}

// The committed core pack must carry an exact transcription of the UI
// switch tables, so the sweep that deletes them changes no pixel.
TEST(ClasspackYaml, committed_core_pack_carries_ui_presentation)
{
    ClasspackData data;
    load_committed_core_pack(data);
    if (::testing::Test::HasFatalFailure())
        return;

    // Every core family of every order declares the whole block: the sweep
    // agent may read the descriptor unconditionally.
    auto complete = [](const og::data::ClasspackPresentation& p) {
        return p.glyph && p.glyph_ascii && p.glyph_color && p.glyph_bold &&
               p.glyph_transparent && p.radar_color && p.radar_jitter;
    };
    for (const auto& e : data.living)
        EXPECT_TRUE(complete(e.presentation)) << e.id;
    for (const auto& e : data.weapons)
        EXPECT_TRUE(complete(e.presentation)) << e.id;
    for (const auto& e : data.effects)
        EXPECT_TRUE(complete(e.presentation)) << e.id;
    for (const auto& e : data.treasures)
        EXPECT_TRUE(complete(e.presentation)) << e.id;
    for (const auto& e : data.generators) {
        EXPECT_TRUE(complete(e.presentation)) << e.id;
        EXPECT_TRUE(e.editor_label.has_value()) << e.id;
    }

    // Spot pins against the legacy switch bodies.
    EXPECT_EQ(data.living[FAMILY_SOLDIER].presentation.glyph.value_or(""),
              "S");
    EXPECT_EQ(data.living[FAMILY_TOWER1].presentation.glyph.value_or(""), "Y");
    EXPECT_EQ(data.weapons[FAMILY_TREE].presentation.glyph.value_or(""),
              "\xe2\x99\xa3");
    EXPECT_EQ(data.weapons[FAMILY_TREE].presentation.glyph_ascii.value_or(""),
              "&");
    EXPECT_EQ(data.weapons[FAMILY_TREE].presentation.glyph_color.value_or(""),
              "green");
    EXPECT_EQ(data.treasures[FAMILY_STAIN]
                  .presentation.glyph_transparent.value_or(false),
              true);
    EXPECT_EQ(data.treasures[FAMILY_GOLD_BAR].presentation.radar_color.value_or(0),
              88);
    EXPECT_EQ(
        data.treasures[FAMILY_GOLD_BAR].presentation.radar_jitter.value_or(0),
        5);
    EXPECT_EQ(
        data.treasures[FAMILY_SILVER_BAR].presentation.radar_color.value_or(0),
        23);
    EXPECT_EQ(
        data.treasures[FAMILY_DRUMSTICK].presentation.radar_color.value_or(0),
        136);
    EXPECT_EQ(
        data.treasures[FAMILY_DRUMSTICK].presentation.radar_jitter.value_or(0),
        2);
    EXPECT_EQ(data.treasures[FAMILY_EXIT].presentation.radar_color.value_or(0),
              120);
    EXPECT_EQ(data.treasures[FAMILY_EXIT].presentation.radar_jitter.value_or(0),
              7);
    // SPEED_POTION is deliberately absent from the radar potion case list.
    EXPECT_EQ(
        data.treasures[FAMILY_SPEED_POTION].presentation.radar_color.value_or(0),
        og::kRadarColorNone);
    EXPECT_EQ(data.treasures[og::FAMILY_FLAG].presentation.radar_color.value_or(0),
              og::kRadarColorTeam);
    EXPECT_EQ(data.generators[FAMILY_TOWER].editor_label.value_or(""),
              "MAGE TOWER");
    EXPECT_EQ(data.generators[FAMILY_BONES].editor_label.value_or(""),
              "BONEPILE");
}

TEST(ClasspackInstall, presentation_lands_on_every_order)
{
    ModSlotGuard guard;

    og::data::ClasspackData data;
    data.pack = "mod";
    {
        og::data::ClasspackLivingEntry e;
        e.id = "mod:wisp";
        e.wire_id = "auto";
        e.presentation.glyph = "\xe2\x86\xaf";  // U+21AF
        e.presentation.glyph_ascii = "/";
        e.presentation.glyph_color = "team";
        e.presentation.glyph_bold = true;
        e.presentation.radar_color = og::kRadarColorTeam;
        e.presentation.radar_jitter = 7;
        data.living.push_back(std::move(e));
    }
    {
        og::data::ClasspackGeneratorEntry g;
        g.id = "mod:hut";
        g.wire_id = "auto";
        g.editor_label = "HUT";
        g.presentation.glyph = "H";
        g.presentation.glyph_transparent = true;
        data.generators.push_back(std::move(g));
    }
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 2);

    const FamilyDescriptor* living = get_family_descriptor(NUM_FAMILIES);
    ASSERT_NE(living, nullptr);
    EXPECT_EQ(living->glyph.codepoint, U'↯');
    EXPECT_EQ(living->glyph.ascii, '/');
    EXPECT_EQ(living->glyph.color, og::GlyphColor::Team);
    EXPECT_TRUE(living->glyph.bold);
    EXPECT_FALSE(living->glyph.transparent) << "undeclared keeps the default";
    EXPECT_EQ(living->radar.color, og::kRadarColorTeam);
    EXPECT_EQ(living->radar.jitter, 7);

    const GeneratorFamilyDescriptor* gen =
        get_generator_family_descriptor(4);  // 4 core generators, auto = 21
    (void)gen;
    const GeneratorFamilyDescriptor* installed =
        get_generator_family_descriptor(21);
    ASSERT_NE(installed, nullptr);
    EXPECT_STREQ(installed->editor_label, "HUT");
    EXPECT_EQ(installed->glyph.codepoint, U'H');
    EXPECT_TRUE(installed->glyph.transparent);
    EXPECT_EQ(installed->glyph.color, og::GlyphColor::Default)
        << "an undeclared colour keeps the generator default";
}

// A mod family that declares no presentation renders exactly like an
// unknown family always did: the order's legacy `default:` branch.
TEST(ClasspackInstall, undeclared_presentation_is_the_legacy_default)
{
    ModSlotGuard guard;

    og::data::ClasspackData data;
    data.pack = "mod";
    og::data::ClasspackLivingEntry e;
    e.id = "mod:blob";
    e.wire_id = "auto";
    data.living.push_back(std::move(e));
    og::data::ClasspackWeaponEntry w;
    w.id = "mod:shard";
    w.wire_id = "auto";
    data.weapons.push_back(std::move(w));
    og::data::ClasspackTreasureEntry t;
    t.id = "mod:coin";
    t.wire_id = "auto";
    data.treasures.push_back(std::move(t));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 3);

    EXPECT_EQ(get_family_descriptor(NUM_FAMILIES)->glyph.codepoint, U'?');
    EXPECT_EQ(get_weapon_family_descriptor(21)->glyph.codepoint, U'*');
    EXPECT_EQ(get_weapon_family_descriptor(21)->glyph.color,
              og::GlyphColor::White);
    EXPECT_EQ(get_treasure_family_descriptor(21)->glyph.codepoint, U'$');
    EXPECT_EQ(get_treasure_family_descriptor(21)->glyph.color,
              og::GlyphColor::Yellow);
    EXPECT_EQ(get_treasure_family_descriptor(21)->radar.color,
              og::kRadarColorNone);
}

// A malformed cosmetic value warns and keeps the current setting — it must
// never sink the pack (behaviour data is what a pack really ships).
TEST(ClasspackInstall, malformed_presentation_keeps_the_current_value)
{
    ModSlotGuard guard;

    og::data::ClasspackData data;
    data.pack = "mod";
    og::data::ClasspackLivingEntry e;
    e.id = "mod:garbled";
    e.wire_id = "auto";
    e.presentation.glyph = "ab";           // two characters
    e.presentation.glyph_ascii = "";       // zero bytes
    e.presentation.glyph_color = "puce";   // not in the vocabulary
    e.presentation.radar_jitter = -3;      // negative span
    data.living.push_back(std::move(e));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);

    const FamilyDescriptor* d = get_family_descriptor(NUM_FAMILIES);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->glyph.codepoint, U'?');
    EXPECT_EQ(d->glyph.ascii, '?');
    EXPECT_EQ(d->glyph.color, og::GlyphColor::Default);
    EXPECT_EQ(d->radar.jitter, 0);
}

// ---------------------------------------------------------------------------
// anims: pack-defined animation tables
// ---------------------------------------------------------------------------

TEST(ClasspackYaml, parse_anims_section)
{
    const char* yaml =
        "anims:\n"
        "  wisp_walk:\n"
        "    rows: 16\n"
        "    frames:\n"
        "      - [0, 1, 2, 3]\n"
        "      - [4, 5]\n"
        "      - ~\n"
        "  wisp_idle:\n"
        "    frames:\n"
        "      - [7]\n"
        "families:\n"
        "  living:\n"
        "    - id: mod:wisp\n"
        "      animation: wisp_walk\n";
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(yaml, data, "anims"));

    ASSERT_EQ(data.anims.size(), 2u);
    EXPECT_EQ(data.anims[0].name, "wisp_walk");
    EXPECT_EQ(data.anims[0].rows.value_or(-1), 16);
    ASSERT_EQ(data.anims[0].frames.size(), 3u);
    EXPECT_EQ(data.anims[0].frames[0].frames,
              (std::vector<std::int32_t>{0, 1, 2, 3}));
    EXPECT_FALSE(data.anims[0].frames[0].is_null);
    EXPECT_EQ(data.anims[0].frames[1].frames,
              (std::vector<std::int32_t>{4, 5}));
    EXPECT_TRUE(data.anims[0].frames[2].is_null);

    EXPECT_EQ(data.anims[1].name, "wisp_idle");
    EXPECT_FALSE(data.anims[1].rows.has_value());
    ASSERT_EQ(data.anims[1].frames.size(), 1u);

    ASSERT_EQ(data.living.size(), 1u);
    EXPECT_EQ(data.living[0].animation.value_or(""), "wisp_walk");
}

TEST(ClasspackYaml, malformed_anims_fail_the_pack)
{
    ClasspackData a;
    EXPECT_FALSE(parse_classpack_yaml("anims: [not, a, mapping]\n", a, "a"));

    ClasspackData b;
    EXPECT_FALSE(parse_classpack_yaml(
        "anims:\n  bad:\n    frames:\n      - [0, oops]\n", b, "b"));

    ClasspackData c;
    EXPECT_FALSE(parse_classpack_yaml(
        "anims:\n  bad:\n    frames:\n      - 7\n", c, "c"))
        << "a bare non-null scalar is not a frame row";

    // `anims: ~` is a legal empty section.
    ClasspackData d;
    EXPECT_TRUE(parse_classpack_yaml("anims: ~\npack: mod\n", d, "d"));
    EXPECT_TRUE(d.anims.empty());
}

// The headline Feature-2 case: a pack-defined set survives parse → install
// → descriptor, with the row pointers, the -1 sentinels and — critically —
// the explicit row count that bounds walker::ani_count.
TEST(ClasspackInstall, pack_animation_set_reaches_the_descriptor)
{
    ModSlotGuard guard;

    const char* yaml =
        "pack: mod\n"
        "anims:\n"
        "  wisp_walk:\n"
        "    rows: 16\n"
        "    frames:\n"
        "      - [0, 1, 2]\n"
        "      - ~\n"
        "families:\n"
        "  living:\n"
        "    - id: mod:wisp\n"
        "      wire_id: auto\n"
        "      animation: wisp_walk\n"
        "      sprite: packs/mod/sprites/wisp.png\n";
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(yaml, data, "packanim"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);

    const FamilyDescriptor* d = get_family_descriptor(NUM_FAMILIES);
    ASSERT_NE(d, nullptr);
    ASSERT_NE(d->anim_table, nullptr);
    EXPECT_EQ(d->anim_row_count, 16)
        << "the row count must be explicit: gloader's anim_table_count() "
           "registry lookup answers 0 for a pack table, and ani_count == 0 "
           "disables the animate() bounds checks";

    // Row 0 is the declared frame list plus the -1 end sentinel; row 1 is
    // the explicit null row; the remaining rows repeat cyclically.
    ASSERT_NE(d->anim_table[0], nullptr);
    EXPECT_EQ(d->anim_table[0][0], 0);
    EXPECT_EQ(d->anim_table[0][1], 1);
    EXPECT_EQ(d->anim_table[0][2], 2);
    EXPECT_EQ(d->anim_table[0][3], -1);
    EXPECT_EQ(d->anim_table[1], nullptr);
    EXPECT_EQ(d->anim_table[2], d->anim_table[0]);
    EXPECT_EQ(d->anim_table[15], d->anim_table[1]);

    // The pack-relative sprite path is passed through untouched:
    // read_pixie_file tries "pix/<name>" first and then the raw name.
    ASSERT_NE(d->pix_filename, nullptr);
    EXPECT_STREQ(d->pix_filename, "packs/mod/sprites/wisp.png");
}

// Built-in animation names stay reserved and keep working; naming one
// clears any pack table the slot was carrying.
TEST(ClasspackInstall, builtin_animation_names_still_win)
{
    ModSlotGuard guard;

    ClasspackData first;
    ASSERT_TRUE(parse_classpack_yaml(
        "anims:\n"
        "  custom:\n"
        "    frames:\n"
        "      - [0]\n"
        "families:\n"
        "  living:\n"
        "    - id: mod:shifter\n"
        "      wire_id: auto\n"
        "      animation: custom\n",
        first, "first"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(first)), 1);
    ASSERT_NE(get_family_descriptor(NUM_FAMILIES)->anim_table, nullptr);

    ClasspackData second;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n"
        "  living:\n"
        "    - id: mod:shifter\n"
        "      wire_id: 21\n"
        "      animation: skeleton\n",
        second, "second"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(second)), 1);

    const FamilyDescriptor* d = get_family_descriptor(NUM_FAMILIES);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->animation_type, FamilyAnimationType::FAMILY_ANIM_SKELETON);
    EXPECT_EQ(d->anim_table, nullptr)
        << "a built-in name returns the family to the gloader table";
    EXPECT_EQ(d->anim_row_count, 0);
}

// A set the installer rejects leaves the family's animation alone rather
// than planting a table with a bogus row count.
TEST(ClasspackInstall, rejected_animation_sets_leave_the_family_alone)
{
    ModSlotGuard guard;

    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "anims:\n"
        "  too_short:\n"
        "    rows: 1\n"
        "    frames:\n"
        "      - [0]\n"
        "      - [1]\n"        // rows < declared rows
        "  bad_frame:\n"
        "    frames:\n"
        "      - [999]\n"      // outside 0..127
        "  empty_row:\n"
        "    frames:\n"
        "      - []\n"
        "families:\n"
        "  living:\n"
        "    - id: mod:a\n"
        "      wire_id: 21\n"
        "      animation: too_short\n"
        "    - id: mod:b\n"
        "      wire_id: 22\n"
        "      animation: bad_frame\n"
        "    - id: mod:c\n"
        "      wire_id: 23\n"
        "      animation: empty_row\n"
        "    - id: mod:d\n"
        "      wire_id: 24\n"
        "      animation: nope\n",
        data, "rejects"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 4);

    for (int id = 21; id <= 24; id++) {
        const FamilyDescriptor* d = get_family_descriptor(id);
        ASSERT_NE(d, nullptr) << id;
        EXPECT_EQ(d->anim_table, nullptr) << id;
        EXPECT_EQ(d->anim_row_count, 0) << id;
    }
}

// The remaining set-level rejections: no frames at all, a row count past
// the cap, an over-long row, and a duplicate set name (first wins).
TEST(ClasspackInstall, animation_set_bounds_are_enforced)
{
    ModSlotGuard guard;

    std::string yaml =
        "anims:\n"
        "  no_frames:\n"
        "    rows: 4\n"
        "  too_many_rows:\n"
        "    rows: 257\n"
        "    frames:\n"
        "      - [0]\n"
        "  long_row:\n"
        "    frames:\n"
        "      - [";
    for (int i = 0; i < 256; i++)
        yaml += (i > 0 ? ", 1" : "1");
    yaml +=
        "]\n"
        "  twice:\n"
        "    frames:\n"
        "      - [1]\n"
        "  twice:\n"
        "    frames:\n"
        "      - [2]\n"
        "families:\n"
        "  living:\n"
        "    - id: mod:a\n"
        "      wire_id: 21\n"
        "      animation: no_frames\n"
        "    - id: mod:b\n"
        "      wire_id: 22\n"
        "      animation: too_many_rows\n"
        "    - id: mod:c\n"
        "      wire_id: 23\n"
        "      animation: long_row\n"
        "    - id: mod:d\n"
        "      wire_id: 24\n"
        "      animation: twice\n";
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(yaml, data, "bounds"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 4);

    for (int id = 21; id <= 23; id++) {
        const FamilyDescriptor* d = get_family_descriptor(id);
        ASSERT_NE(d, nullptr) << id;
        EXPECT_EQ(d->anim_table, nullptr) << id;
    }
    // The duplicate name keeps the FIRST definition.
    const FamilyDescriptor* dup = get_family_descriptor(24);
    ASSERT_NE(dup, nullptr);
    ASSERT_NE(dup->anim_table, nullptr);
    EXPECT_EQ(dup->anim_table[0][0], 1);
    EXPECT_EQ(dup->anim_row_count, 1);
}

// A non-living family naming a set that does not exist keeps its table.
TEST(ClasspackInstall, unknown_animation_name_on_a_weapon_keeps_the_table)
{
    ModSlotGuard guard;

    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n"
        "  weapon:\n"
        "    - id: mod:shard\n"
        "      wire_id: auto\n"
        "      animation: nope\n",
        data, "unknown"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);
    ASSERT_NE(get_weapon_family_descriptor(21), nullptr);
    EXPECT_EQ(get_weapon_family_descriptor(21)->anim_table, nullptr);
    EXPECT_EQ(get_weapon_family_descriptor(21)->anim_row_count, 0);
}

// Every non-living order can carry a pack table too, and the row count
// travels with it.
TEST(ClasspackInstall, pack_animation_sets_reach_the_other_orders)
{
    ModSlotGuard guard;

    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "anims:\n"
        "  spin:\n"
        "    rows: 24\n"
        "    frames:\n"
        "      - [3, 4]\n"
        "families:\n"
        "  weapon:\n"
        "    - id: mod:shard\n"
        "      wire_id: auto\n"
        "      animation: spin\n"
        "      sprite: packs/mod/sprites/shard.png\n"
        "  effect:\n"
        "    - id: mod:spark\n"
        "      wire_id: auto\n"
        "      animation: spin\n"
        "  treasure:\n"
        "    - id: mod:relic\n"
        "      wire_id: auto\n"
        "      animation: spin\n"
        "  generator:\n"
        "    - id: mod:hut\n"
        "      wire_id: auto\n"
        "      animation: spin\n",
        data, "orders"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 4);

    const WeaponFamilyDescriptor* w = get_weapon_family_descriptor(21);
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->anim_table, nullptr);
    EXPECT_EQ(w->anim_row_count, 24);
    EXPECT_EQ(w->anim_table[0][0], 3);
    EXPECT_EQ(w->anim_table[0][2], -1);
    EXPECT_STREQ(w->pix_filename, "packs/mod/sprites/shard.png");

    ASSERT_NE(get_effect_family_descriptor(21), nullptr);
    EXPECT_EQ(get_effect_family_descriptor(21)->anim_row_count, 24);
    ASSERT_NE(get_treasure_family_descriptor(21), nullptr);
    EXPECT_EQ(get_treasure_family_descriptor(21)->anim_row_count, 24);
    ASSERT_NE(get_generator_family_descriptor(21), nullptr);
    EXPECT_EQ(get_generator_family_descriptor(21)->anim_row_count, 24);
    // All four share the one materialized table.
    EXPECT_EQ(get_effect_family_descriptor(21)->anim_table, w->anim_table);
}

// Animation sets are pack-local: pack B cannot name pack A's set.
TEST(ClasspackInstall, animation_sets_do_not_leak_between_packs)
{
    ModSlotGuard guard;

    ClasspackData a;
    ASSERT_TRUE(parse_classpack_yaml(
        "anims:\n  shared:\n    frames:\n      - [0]\n", a, "a"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(a)), 0);

    ClasspackData b;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n"
        "  living:\n"
        "    - id: modb:thief\n"
        "      wire_id: auto\n"
        "      animation: shared\n",
        b, "b"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(b)), 1);

    const FamilyDescriptor* d = get_family_descriptor(NUM_FAMILIES);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->anim_table, nullptr);
    EXPECT_EQ(d->anim_row_count, 0);
}

// ---------------------------------------------------------------------------
// UTF-8 helpers behind the `glyph:` scalar
// ---------------------------------------------------------------------------

TEST(FamilyPresentation, glyph_utf8_round_trips)
{
    for (const char32_t cp : {U'S', U'$', U' ', U'é', U'♣', U'↯',
                              U'✺', U'◍', U'\U0001F600'}) {
        const std::string encoded = og::glyph_to_utf8(cp);
        ASSERT_FALSE(encoded.empty()) << static_cast<std::uint32_t>(cp);
        char32_t back = U'\0';
        ASSERT_TRUE(og::glyph_from_utf8(encoded, back))
            << static_cast<std::uint32_t>(cp);
        EXPECT_EQ(back, cp);
    }

    char32_t out = U'\0';
    EXPECT_FALSE(og::glyph_from_utf8("", out));
    EXPECT_FALSE(og::glyph_from_utf8("ab", out)) << "two characters";
    EXPECT_FALSE(og::glyph_from_utf8("\xC0\x80", out)) << "overlong NUL";
    EXPECT_FALSE(og::glyph_from_utf8("\xE2\x99", out)) << "truncated";
    EXPECT_FALSE(og::glyph_from_utf8("\x80", out)) << "lone continuation";
    EXPECT_FALSE(og::glyph_from_utf8("\xED\xA0\x80", out)) << "surrogate";
    EXPECT_TRUE(og::glyph_to_utf8(static_cast<char32_t>(0x110000u)).empty());

    // The colour vocabulary round-trips both ways.
    for (std::size_t i = 0; i < og::kGlyphColorNames.size(); i++) {
        og::GlyphColor c{};
        ASSERT_TRUE(og::glyph_color_from_name(og::kGlyphColorNames[i], c));
        EXPECT_EQ(static_cast<std::size_t>(c), i);
        EXPECT_STREQ(og::glyph_color_name(c), og::kGlyphColorNames[i]);
    }
    og::GlyphColor unknown{};
    EXPECT_FALSE(og::glyph_color_from_name("puce", unknown));
}

// ---------------------------------------------------------------------------
// tuning: maps (read by scripts via og.tuning)
// ---------------------------------------------------------------------------

namespace {

using og::data::ClasspackTuningValue;

// The tuning store is process-global (like the registries); every test
// that touches it restores emptiness on both sides of itself.
class TuningStoreGuard {
public:
    TuningStoreGuard() { og::script::clear_all_family_tuning(); }
    ~TuningStoreGuard() { og::script::clear_all_family_tuning(); }
};

}  // namespace

// Kind classification preserves the author's YAML spelling: plain integers
// stay integers (int64-wide), plain decimals/exponents become doubles,
// true/false become booleans, QUOTED text is a string even when it looks
// numeric, and an unparseable plain scalar falls back to a plain string.
// YAML mapping order is preserved.
TEST(ClasspackYaml, tuning_map_kinds_follow_the_yaml_spelling)
{
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n"
        "  living:\n"
        "    - id: p:orc\n"
        "      tuning:\n"
        "        yell_stun: 10\n"
        "        heal_scale: 2.5\n"
        "        eats_corpses: true\n"
        "        timid: false\n"
        "        label: \"5\"\n"
        "        big: 9223372036854775807\n"
        "        neg: -42\n"
        "        expo: 1e3\n"
        "        odd: 12abc\n",
        data, "tuning-kinds"));
    ASSERT_EQ(data.living.size(), 1u);
    const auto& t = data.living[0].tuning;
    ASSERT_EQ(t.size(), 9u);

    EXPECT_EQ(t[0].key, "yell_stun");
    ASSERT_EQ(t[0].value.kind, ClasspackTuningValue::Kind::Integer);
    EXPECT_EQ(t[0].value.integer, 10);

    EXPECT_EQ(t[1].key, "heal_scale");
    ASSERT_EQ(t[1].value.kind, ClasspackTuningValue::Kind::Number);
    EXPECT_EQ(t[1].value.number, 2.5);

    EXPECT_EQ(t[2].key, "eats_corpses");
    ASSERT_EQ(t[2].value.kind, ClasspackTuningValue::Kind::Boolean);
    EXPECT_TRUE(t[2].value.boolean);

    EXPECT_EQ(t[3].key, "timid");
    ASSERT_EQ(t[3].value.kind, ClasspackTuningValue::Kind::Boolean);
    EXPECT_FALSE(t[3].value.boolean);

    EXPECT_EQ(t[4].key, "label");
    ASSERT_EQ(t[4].value.kind, ClasspackTuningValue::Kind::String)
        << "a QUOTED \"5\" must stay a string";
    EXPECT_EQ(t[4].value.string, "5");

    ASSERT_EQ(t[5].value.kind, ClasspackTuningValue::Kind::Integer)
        << "tuning integers are int64-wide";
    EXPECT_EQ(t[5].value.integer, 9223372036854775807LL);

    ASSERT_EQ(t[6].value.kind, ClasspackTuningValue::Kind::Integer);
    EXPECT_EQ(t[6].value.integer, -42);

    ASSERT_EQ(t[7].value.kind, ClasspackTuningValue::Kind::Number)
        << "exponent spelling is a double";
    EXPECT_EQ(t[7].value.number, 1000.0);

    ASSERT_EQ(t[8].value.kind, ClasspackTuningValue::Kind::String)
        << "an unparseable plain scalar falls back to a plain string";
    EXPECT_EQ(t[8].value.string, "12abc");
}

// Every order's entries may carry a tuning map — the shared entry walker
// owns the key, so one spelling works everywhere.
TEST(ClasspackYaml, tuning_parses_on_every_order)
{
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n"
        "  living:\n"
        "    - id: p:l\n"
        "      tuning: {a: 1}\n"
        "  weapon:\n"
        "    - id: p:w\n"
        "      tuning: {b: 2}\n"
        "  effect:\n"
        "    - id: p:e\n"
        "      tuning: {c: 3}\n"
        "  treasure:\n"
        "    - id: p:t\n"
        "      tuning: {d: 4}\n"
        "  generator:\n"
        "    - id: p:g\n"
        "      tuning: {e: 5}\n",
        data, "tuning-orders"));
    ASSERT_EQ(data.living.size(), 1u);
    ASSERT_EQ(data.living[0].tuning.size(), 1u);
    EXPECT_EQ(data.living[0].tuning[0].key, "a");
    ASSERT_EQ(data.weapons.size(), 1u);
    ASSERT_EQ(data.weapons[0].tuning.size(), 1u);
    EXPECT_EQ(data.weapons[0].tuning[0].value.integer, 2);
    ASSERT_EQ(data.effects.size(), 1u);
    ASSERT_EQ(data.effects[0].tuning.size(), 1u);
    ASSERT_EQ(data.treasures.size(), 1u);
    ASSERT_EQ(data.treasures[0].tuning.size(), 1u);
    ASSERT_EQ(data.generators.size(), 1u);
    ASSERT_EQ(data.generators[0].tuning.size(), 1u);
    EXPECT_EQ(data.generators[0].tuning[0].value.integer, 5);
}

// An entry with no tuning: parses to an empty map — absent means absent.
TEST(ClasspackYaml, absent_tuning_is_an_empty_map)
{
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n  living:\n    - id: p:plain\n", data, "no-tuning"));
    ASSERT_EQ(data.living.size(), 1u);
    EXPECT_TRUE(data.living[0].tuning.empty());
}

// Malformed tuning fails the pack, strict like every other field: null
// values, nested mappings, nested lists, non-scalar keys, empty keys.
TEST(ClasspackYaml, malformed_tuning_fails_the_pack)
{
    ClasspackData a;
    ASSERT_FALSE(parse_classpack_yaml(
        "families:\n  living:\n    - id: p:x\n      tuning:\n        k: ~\n",
        a, "tuning-null"))
        << "tuning keys carry values; null must fail";

    ClasspackData b;
    ASSERT_FALSE(parse_classpack_yaml(
        "families:\n  living:\n    - id: p:x\n"
        "      tuning:\n        k: {nested: 1}\n",
        b, "tuning-nested-map"));

    ClasspackData c;
    ASSERT_FALSE(parse_classpack_yaml(
        "families:\n  living:\n    - id: p:x\n"
        "      tuning:\n        k: [1, 2]\n",
        c, "tuning-nested-list"));

    ClasspackData d;
    ASSERT_FALSE(parse_classpack_yaml(
        "families:\n  living:\n    - id: p:x\n"
        "      tuning:\n        ? [1, 2]\n        : 3\n",
        d, "tuning-sequence-key"));

    ClasspackData e;
    ASSERT_FALSE(parse_classpack_yaml(
        "families:\n  living:\n    - id: p:x\n"
        "      tuning:\n        \"\": 3\n",
        e, "tuning-empty-key"));
}

// ---------------------------------------------------------------------------
// tuning install — YAML data lands in the gameplay-side store
// ---------------------------------------------------------------------------

// install_classpack_data carries each entry's tuning into
// og::script::set_family_tuning under the entry's (order, wire id), with
// every value kind converted; an entry that declares nothing installs
// nothing.
TEST(ClasspackInstall, tuning_reaches_the_gameplay_store)
{
    ModSlotGuard guard;
    TuningStoreGuard tuning_guard;

    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "pack: mod\n"
        "families:\n"
        "  living:\n"
        "    - id: mod:tuned\n"
        "      wire_id: auto\n"
        "      name: \"TUNED\"\n"
        "      tuning:\n"
        "        stun: 7\n"
        "        scale: 1.5\n"
        "        brave: true\n"
        "        tag: knife\n"
        "    - id: mod:plain\n"
        "      wire_id: auto\n"
        "      name: \"PLAIN\"\n"
        "  weapon:\n"
        "    - id: mod:zap\n"
        "      wire_id: auto\n"
        "      name: \"ZAP\"\n"
        "      tuning:\n"
        "        speed: 9\n",
        data, "tuning-install"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 3);

    const int tuned_id =
        og::families::resolve_family_string_id(Order::Living, "mod:tuned");
    const int plain_id =
        og::families::resolve_family_string_id(Order::Living, "mod:plain");
    const int zap_id =
        og::families::resolve_family_string_id(Order::Weapon, "mod:zap");
    ASSERT_GE(tuned_id, 0);
    ASSERT_GE(plain_id, 0);
    ASSERT_GE(zap_id, 0);

    const og::script::TuningMap* tuned =
        og::script::family_tuning(Order::Living, tuned_id);
    ASSERT_NE(tuned, nullptr);
    ASSERT_EQ(tuned->size(), 4u);
    EXPECT_EQ((*tuned)[0].key, "stun");
    ASSERT_EQ((*tuned)[0].value.kind,
              og::script::TuningValue::Kind::Integer);
    EXPECT_EQ((*tuned)[0].value.integer, 7);
    ASSERT_EQ((*tuned)[1].value.kind, og::script::TuningValue::Kind::Number);
    EXPECT_EQ((*tuned)[1].value.number, 1.5);
    ASSERT_EQ((*tuned)[2].value.kind,
              og::script::TuningValue::Kind::Boolean);
    EXPECT_TRUE((*tuned)[2].value.boolean);
    ASSERT_EQ((*tuned)[3].value.kind, og::script::TuningValue::Kind::String);
    EXPECT_EQ((*tuned)[3].value.string, "knife");

    EXPECT_EQ(og::script::family_tuning(Order::Living, plain_id), nullptr)
        << "an entry without tuning installs nothing";

    const og::script::TuningMap* zap =
        og::script::family_tuning(Order::Weapon, zap_id);
    ASSERT_NE(zap, nullptr);
    ASSERT_EQ(zap->size(), 1u);
    EXPECT_EQ((*zap)[0].key, "speed");
    EXPECT_EQ((*zap)[0].value.integer, 9);
}

// Tuning belongs to whoever occupies the slot NOW: a reinstall of the same
// wire id without tuning erases the previous occupant's map (the
// set_family_tuning empty-map contract the installer relies on between
// full clear_all passes).
TEST(ClasspackInstall, reinstalling_a_slot_without_tuning_clears_it)
{
    ModSlotGuard guard;
    TuningStoreGuard tuning_guard;

    ClasspackData first;
    ASSERT_TRUE(parse_classpack_yaml(
        "pack: mod\n"
        "families:\n"
        "  living:\n"
        "    - id: mod:v1\n"
        "      wire_id: 255\n"
        "      name: \"V1\"\n"
        "      tuning: {cap: 420}\n",
        first, "tuned-v1"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(first)), 1);
    ASSERT_NE(og::script::family_tuning(Order::Living, 255), nullptr);

    ClasspackData second;
    ASSERT_TRUE(parse_classpack_yaml(
        "pack: mod\n"
        "families:\n"
        "  living:\n"
        "    - id: mod:v2\n"
        "      wire_id: 255\n"
        "      name: \"V2\"\n",
        second, "tuned-v2"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(second)), 1);
    EXPECT_EQ(og::script::family_tuning(Order::Living, 255), nullptr)
        << "the replacing entry declared no tuning, so the slot is bare";
}

// ---------------------------------------------------------------------------
// lib/ end to end — the REAL enumeration path (packs.cpp), mount to unmount
// ---------------------------------------------------------------------------

namespace {

// The three Lua sources this fixture writes to disk. Their exact bytes are
// declared in scripts/coverage/runtime_only_lua.txt (the engine compiles
// them under packs/-prefixed chunk names, so an armed coverage run must
// recognize the digests) — CHANGING A BYTE HERE MEANS UPDATING THAT LEDGER.
constexpr const char* kLibAaa = "return { tag = 'aaa' }\n";
constexpr const char* kLibHelper = "return { bonus = 5 }\n";
constexpr const char* kLibProbeScript =
    "local aaa = og.use('aaa')\n"
    "local helper = og.use('helper')\n"
    "og.log('libpack', aaa.tag, helper.bonus)\n";

std::filesystem::path make_scratch_lib_pack()
{
    std::string templ =
        (std::filesystem::temp_directory_path() / "og_libpack_XXXXXX")
            .string();
    char* made = ::mkdtemp(templ.data());
    if (made == nullptr)
        return {};
    const std::filesystem::path root(made);
    std::error_code ec;
    std::filesystem::create_directories(root / "lib", ec);
    std::filesystem::create_directories(root / "scripts", ec);
    if (ec)
        return {};
    const auto put = [&](const std::filesystem::path& p,
                         const std::string& bytes) {
        std::ofstream out(p, std::ios::binary);
        out << bytes;
    };
    put(root / "lib" / "aaa.lua", kLibAaa);
    put(root / "lib" / "helper.lua", kLibHelper);
    put(root / "lib" / "notes.txt", "not lua; must be ignored\n");
    put(root / "lib" / "empty.lua", "");  // unreadable/empty: warn + skip
    put(root / "scripts" / "probe.lua", kLibProbeScript);
    put(root / "classpack.yaml",
        "pack: org.test.libpack\n"
        "families:\n"
        "  living:\n"
        "    - id: libpack:tuned\n"
        "      wire_id: auto\n"
        "      name: \"TUNED\"\n"
        "      tuning: {cap: 11}\n");
    return root;
}

}  // namespace

// One mount drives the whole class-pack resources contract: lib/*.lua (and
// only *.lua with content) registers under deterministic packs/<id>/lib/
// chunk names, the pack's script og.use-binds the exports when the shared
// VM rebuilds, classpack.yaml tuning reaches the gameplay store, the MP
// transfer manifest carries the lib files (they are pack CONTENT — the
// same walk that ships scripts ships lib), and unmount+refresh removes all
// of it.
TEST(ClasspackLibE2e, mount_registers_loads_transfers_and_unmounts)
{
    ModSlotGuard guard;
    TuningStoreGuard tuning_guard;

    const std::filesystem::path root = make_scratch_lib_pack();
    ASSERT_FALSE(root.empty()) << "scratch pack creation failed";
    ASSERT_TRUE(og::resources::mount(root.string().c_str(),
                                     "packs/org.test.libpack", 1));
    og::resources::refresh_pack_scripts();

    // Registration: exactly the two real modules, filename-lexicographic,
    // with the virtual-tree chunk names the loader will report.
    std::vector<const og::script::PackLibModule*> mine;
    for (const og::script::PackLibModule& m : og::script::pack_lib_modules())
    {
        if (m.pack_id == "org.test.libpack")
            mine.push_back(&m);
    }
    ASSERT_EQ(mine.size(), 2u)
        << "notes.txt and empty.lua must not have registered";
    EXPECT_EQ(mine[0]->name, "aaa");
    EXPECT_EQ(mine[0]->chunk_name, "packs/org.test.libpack/lib/aaa.lua");
    EXPECT_EQ(mine[1]->name, "helper");
    EXPECT_EQ(mine[1]->source, kLibHelper);

    // Load: the rebuilt shared VM ran the modules and the pack script.
    const std::vector<std::string>& log =
        og::script::active_world_scripts().host().log();
    bool saw_probe = false;
    for (const std::string& line : log)
        saw_probe = saw_probe || line == "libpack\taaa\t5";
    EXPECT_TRUE(saw_probe)
        << "probe.lua must have read both lib exports through og.use";

    // Tuning: the classpack.yaml `tuning:` map reached the gameplay store
    // through the same install pass.
    const int tuned_id = og::families::resolve_family_string_id(
        Order::Living, "libpack:tuned");
    ASSERT_GE(tuned_id, 0);
    const og::script::TuningMap* tuned =
        og::script::family_tuning(Order::Living, tuned_id);
    ASSERT_NE(tuned, nullptr);
    ASSERT_EQ(tuned->size(), 1u);
    EXPECT_EQ((*tuned)[0].key, "cap");
    EXPECT_EQ((*tuned)[0].value.integer, 11);

    // Transfer: the host-side manifest walk ships lib/ (and classpack.yaml)
    // as ordinary pack content. Tuning rides INSIDE classpack.yaml — it is
    // load-time pack data, never wire state.
    bool found_pack = false;
    for (const og::sim::HostedPack& hp :
         og::resources::build_transferable_packs())
    {
        if (hp.manifest.pack_id != "org.test.libpack")
            continue;
        found_pack = true;
        std::vector<std::string> paths;
        for (const auto& f : hp.manifest.files)
            paths.push_back(f.path);
        EXPECT_NE(paths.end(),
                  std::find(paths.begin(), paths.end(), "lib/aaa.lua"));
        EXPECT_NE(paths.end(),
                  std::find(paths.begin(), paths.end(), "lib/helper.lua"));
        EXPECT_NE(paths.end(),
                  std::find(paths.begin(), paths.end(), "scripts/probe.lua"));
        EXPECT_NE(paths.end(),
                  std::find(paths.begin(), paths.end(), "classpack.yaml"));
    }
    EXPECT_TRUE(found_pack)
        << "the mounted scratch pack must be transferable";

    // Unmount: modules, scripts and tuning all follow the mount out.
    ASSERT_TRUE(og::resources::unmount(root.string().c_str()));
    og::resources::refresh_pack_scripts();
    for (const og::script::PackLibModule& m : og::script::pack_lib_modules())
        EXPECT_NE(m.pack_id, "org.test.libpack");
    EXPECT_EQ(og::script::family_tuning(Order::Living, tuned_id), nullptr)
        << "an unmounted pack must leave no tuning behind";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// ---------------------------------------------------------------------------
// Split layout: packs/<id>/families/*.yaml
// ---------------------------------------------------------------------------

namespace {

// A scratch pack in the SPLIT layout: header-only classpack.yaml (plus one
// header-declared family when `header_family` is set), two families/ files
// in sorted order, a non-YAML note and a subdirectory that must both be
// ignored. bb.yaml re-declares aa.yaml's slot with one field, proving the
// documented precedence: later files overwrite exactly the fields they
// declare.
std::filesystem::path make_scratch_split_pack(bool header_family,
                                              bool broken_family_file)
{
    std::string templ =
        (std::filesystem::temp_directory_path() / "og_split_XXXXXX")
            .string();
    char* made = ::mkdtemp(templ.data());
    if (made == nullptr)
        return {};
    const std::filesystem::path root(made);
    std::error_code ec;
    std::filesystem::create_directories(root / "families" / "ignored.yaml",
                                        ec);
    if (ec)
        return {};
    const auto put = [&](const std::filesystem::path& p,
                         const std::string& bytes) {
        std::ofstream out(p, std::ios::binary);
        out << bytes;
    };
    if (header_family)
        put(root / "classpack.yaml",
            "pack: org.test.splitpack\n"
            "families:\n"
            "  living:\n"
            "    - id: splitpack:from_header\n"
            "      wire_id: auto\n"
            "      name: \"FROM HEADER\"\n");
    put(root / "families" / "aa.yaml",
        "families:\n"
        "  living:\n"
        "    - id: splitpack:alpha\n"
        "      wire_id: 30\n"
        "      name: \"ALPHA\"\n"
        "      costs: {hire: 111}\n"
        "      tuning: {split_key: 7}\n");
    put(root / "families" / "bb.yaml",
        broken_family_file
            ? "families: [unclosed\n"
            : "families:\n"
              "  living:\n"
              "    - id: splitpack:alpha\n"
              "      wire_id: 30\n"
              "      costs: {hire: 222}\n"
              "      tuning: {split_key: 9}\n");
    put(root / "families" / "notes.txt", "not yaml; must be ignored\n");
    return root;
}

class SplitPackMount {
public:
    explicit SplitPackMount(const std::filesystem::path& root) : root_(root)
    {
        mounted_ = og::resources::mount(root.string().c_str(),
                                        "packs/org.test.splitpack", 1);
        if (mounted_)
            og::resources::refresh_pack_scripts();
    }
    ~SplitPackMount()
    {
        if (mounted_) {
            (void)og::resources::unmount(root_.string().c_str());
            og::resources::refresh_pack_scripts();
        }
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    [[nodiscard]] bool ok() const { return mounted_; }

private:
    std::filesystem::path root_;
    bool mounted_ = false;
};

}  // namespace

// Both layouts in one pack: the classpack.yaml header entry AND the
// families/ entries install; the two families/ files parse in sorted
// filename order into one pack, so bb.yaml's sparse re-declaration of
// aa.yaml's WIRE slot overwrites exactly the data field it declares
// (costs.hire) while undeclared fields (name) keep aa.yaml's values —
// and the tuning map follows the install-always-replaces rule (bb.yaml's
// map wins whole). notes.txt and the directory named *.yaml are skipped.
TEST(ClasspackSplitLayout, families_dir_installs_like_a_monolith)
{
    ModSlotGuard guard;
    TuningStoreGuard tuning_guard;
    const std::filesystem::path root = make_scratch_split_pack(
        /*header_family=*/true, /*broken_family_file=*/false);
    ASSERT_FALSE(root.empty());
    SplitPackMount mount(root);
    ASSERT_TRUE(mount.ok());

    const int header_id = og::families::resolve_family_string_id(
        Order::Living, "splitpack:from_header");
    ASSERT_GE(header_id, 0) << "classpack.yaml families must still install";
    const int alpha_id = og::families::resolve_family_string_id(
        Order::Living, "splitpack:alpha");
    ASSERT_GE(alpha_id, 0) << "families/*.yaml entries must install";

    EXPECT_EQ(alpha_id, 30) << "wire_id pins the slot across both files";
    const FamilyDescriptor* alpha = get_family_descriptor(alpha_id);
    ASSERT_NE(alpha, nullptr);
    EXPECT_STREQ(alpha->name, "ALPHA") << "aa.yaml's undeclared-in-bb "
                                          "fields must survive";
    EXPECT_EQ(alpha->hiring_cost, 222)
        << "bb.yaml loads after aa.yaml (sorted) and overwrites the one "
           "field it declares";

    const og::script::TuningMap* tuned =
        og::script::family_tuning(Order::Living, alpha_id);
    ASSERT_NE(tuned, nullptr) << "tuning from a families/ file must reach "
                                 "the gameplay store";
    ASSERT_EQ(tuned->size(), 1u);
    EXPECT_EQ((*tuned)[0].key, "split_key");
    EXPECT_EQ((*tuned)[0].value.integer, 9)
        << "an installed entry always installs its tuning map, so the "
           "later file's map replaces the earlier one whole";
}

// families/ alone is a complete pack: no classpack.yaml at all.
TEST(ClasspackSplitLayout, families_only_pack_installs)
{
    ModSlotGuard guard;
    TuningStoreGuard tuning_guard;
    const std::filesystem::path root = make_scratch_split_pack(
        /*header_family=*/false, /*broken_family_file=*/false);
    ASSERT_FALSE(root.empty());
    SplitPackMount mount(root);
    ASSERT_TRUE(mount.ok());

    EXPECT_GE(og::families::resolve_family_string_id(Order::Living,
                                                     "splitpack:alpha"),
              0);
    EXPECT_LT(og::families::resolve_family_string_id(
                  Order::Living, "splitpack:from_header"),
              0);
}

// One unusable families/ file rejects the WHOLE pack — including entries a
// perfectly good classpack.yaml in the same pack declared — matching the
// all-or-nothing contract a broken classpack.yaml always had.
TEST(ClasspackSplitLayout, bad_family_file_rejects_the_whole_pack)
{
    ModSlotGuard guard;
    TuningStoreGuard tuning_guard;
    const std::filesystem::path root = make_scratch_split_pack(
        /*header_family=*/true, /*broken_family_file=*/true);
    ASSERT_FALSE(root.empty());
    SplitPackMount mount(root);
    ASSERT_TRUE(mount.ok());

    EXPECT_LT(og::families::resolve_family_string_id(Order::Living,
                                                     "splitpack:alpha"),
              0);
    EXPECT_LT(og::families::resolve_family_string_id(
                  Order::Living, "splitpack:from_header"),
              0);
    EXPECT_EQ(og::script::family_tuning(Order::Living, 21), nullptr)
        << "no tuning may leak from a rejected pack";
}
