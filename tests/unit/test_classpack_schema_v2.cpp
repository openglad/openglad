/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// classpack.yaml schema v2: the named stats/combat/costs/specials blocks
// that replaced a living entry's positional arrays.
//
// Two things are load-bearing here. v2_soldier_installs_the_shipped_bytes
// holds the numbers the array spelling installed, because v2 was a
// spelling change and nothing else. And retired_v1_keys_fail_by_name
// covers the trap the whole schema turns on: an unknown key is skipped in
// silence, so the eight keys v2 retired have to be refused by name or a v1
// file installs a family of defaults and says nothing. The rest pins the
// tiers — what is fatal, what warns, what stays silent.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/resources/classpack_yaml.h>
#include <openglad/resources/packs.h>

#include <cstdint>
#include <string>

using og::data::ClasspackData;
using og::data::parse_classpack_yaml;

namespace {

// The soldier's shipped numbers in the v2 spelling, on a free wire slot.
// Every value here is the one the positional arrays carried:
//   base_stats: [12, 6, 12, 8, 9, 1]      hiring_cost: 250
//   derived_bonuses: [120, 0, 20, 0, 0, 0, 4, 6]   weapon_cost: 2
//   stat_costs: [6, 10, 6, 25, 50, 200]
//   special_costs: [5000, 25, 100, 120, 150, 5000]
//   special_names: ["NONE", "CHARGE", "BOOMERANG", "WHIRLWIND", "DISARM",
//                   "NONE"]
//   alternate_names: ["NONE", "NONE", "NONE", "MYSTIC MACE", "NONE", "NONE"]
const char* kV2Soldier = R"(families:
  living:
    - id: v2:soldier
      wire_id: 61
      name: "SOLDIER"
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
        - id: boomerang
          name: BOOMERANG
          mp_cost: 100
        - id: whirlwind
          name: WHIRLWIND
          mp_cost: 120
          alternate: {name: MYSTIC MACE}
        - id: disarm
          name: DISARM
          mp_cost: 150
)";

// A v2 entry with one block filled in around the given body, for the
// per-block tier tests.
std::string v2_entry(const std::string& body)
{
    return "families:\n  living:\n    - id: p:one\n      wire_id: 62\n" +
           body;
}

bool parses(const std::string& text)
{
    ClasspackData data;
    return parse_classpack_yaml(text, data, "v2-test");
}

// Parses and answers everything the parser wrote to stderr, so the warn
// tier can be told apart from the silent one.
std::string parse_output(const std::string& text, ClasspackData& data,
                         bool& ok)
{
    testing::internal::CaptureStderr();
    ok = parse_classpack_yaml(text, data, "v2-test");
    return testing::internal::GetCapturedStderr();
}

std::string parse_output(const std::string& text, bool& ok)
{
    ClasspackData discarded;
    return parse_output(text, discarded, ok);
}

}  // namespace

// ---------------------------------------------------------------------------
// The shape
// ---------------------------------------------------------------------------

TEST(ClasspackSchemaV2, parses_every_block)
{
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(kV2Soldier, data, "v2"));
    ASSERT_EQ(data.living.size(), 1u);
    const auto& e = data.living[0];

    ASSERT_TRUE(e.stats.has_value());
    ASSERT_EQ(e.stats->strength, 12);
    ASSERT_EQ(e.stats->dexterity, 6);
    ASSERT_EQ(e.stats->constitution, 12);
    ASSERT_EQ(e.stats->intelligence, 8);
    ASSERT_EQ(e.stats->armor, 9);
    ASSERT_EQ(e.stats->level, 1);

    ASSERT_TRUE(e.combat.has_value());
    ASSERT_EQ(e.combat->hp, 120.0f);
    ASSERT_EQ(e.combat->melee_damage, 20.0f);
    ASSERT_EQ(e.combat->stepsize, 4.0f);
    ASSERT_EQ(e.combat->fire_delay, 6.0f);
    ASSERT_EQ(e.combat->fire_mp_cost, 2);

    ASSERT_TRUE(e.costs.has_value());
    ASSERT_EQ(e.costs->hire, 250);
    ASSERT_TRUE(e.costs->train.has_value());
    ASSERT_EQ(e.costs->train->strength, 6);
    ASSERT_EQ(e.costs->train->intelligence, 25);
    ASSERT_EQ(e.costs->train->level, 200);

    ASSERT_TRUE(e.specials.has_value());
    ASSERT_EQ(e.specials->size(), 4u);
    ASSERT_EQ((*e.specials)[0].id, "charge");
    ASSERT_EQ((*e.specials)[0].name, "CHARGE");
    ASSERT_EQ((*e.specials)[0].mp_cost, 25);
    ASSERT_EQ((*e.specials)[0].slot, 1);
    ASSERT_FALSE((*e.specials)[0].alternate_name.has_value());
    ASSERT_EQ((*e.specials)[2].slot, 3);
    ASSERT_TRUE((*e.specials)[2].alternate_name.has_value());
    ASSERT_EQ(*(*e.specials)[2].alternate_name, "MYSTIC MACE");
    ASSERT_EQ((*e.specials)[3].slot, 4);
}

TEST(ClasspackSchemaV2, floats_survive_verbatim)
{
    // The cleric's fire_delay is 7.5: a v2 file has to carry a fractional
    // combat value as exactly the float the array column did.
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        v2_entry("      combat:\n"
                 "        hp: 100\n"
                 "        melee_damage: 10\n"
                 "        stepsize: 3\n"
                 "        fire_delay: 7.5\n"
                 "        fire_mp_cost: 0\n"),
        data, "v2-float"));
    ASSERT_EQ(data.living[0].combat->fire_delay, 7.5f);
}

// ---------------------------------------------------------------------------
// The point: the installed bytes did not move
// ---------------------------------------------------------------------------

TEST(ClasspackSchemaV2, v2_soldier_installs_the_shipped_bytes)
{
    init_all_registries();

    ClasspackData v2;
    ASSERT_TRUE(parse_classpack_yaml(kV2Soldier, v2, "v2"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(v2)), 1);

    const FamilyDescriptor* d = get_family_descriptor(61);
    ASSERT_NE(d, nullptr);

    // The six base_stats columns, in the order the array had them.
    const std::int32_t stats[StatAxis::Count] = {12, 6, 12, 8, 9, 1};
    const std::int32_t train[StatAxis::Count] = {6, 10, 6, 25, 50, 200};
    for (int i = 0; i < StatAxis::Count; i++) {
        ASSERT_EQ(d->base_stats[i], stats[i]) << "base_stats " << i;
        ASSERT_EQ(d->stat_costs[i], train[i]) << "stat_costs " << i;
    }
    ASSERT_EQ(d->hiring_cost, 250);
    // The four live derived_bonuses columns: 0, 2, 6, 7.
    ASSERT_EQ(d->combat.hp, 120.0f);
    ASSERT_EQ(d->combat.melee_damage, 20.0f);
    ASSERT_EQ(d->combat.stepsize, 4.0f);
    ASSERT_EQ(d->combat.fire_delay, 6.0f);
    ASSERT_EQ(d->combat.fire_mp_cost, 2);

    // The three parallel special arrays, folded out of one list. Slot 0 is
    // the engine artifact and slot 5 was never declared; both are the
    // disabled pair, which is what the v1 arrays spelled out by hand.
    const char* names[FD_NUM_SPECIALS] = {"NONE",      "CHARGE",   "BOOMERANG",
                                          "WHIRLWIND", "DISARM",   "NONE"};
    const unsigned short costs[FD_NUM_SPECIALS] = {5000, 25, 100, 120, 150,
                                                   5000};
    for (int i = 0; i < FD_NUM_SPECIALS; i++) {
        ASSERT_STREQ(d->special_names[i], names[i]) << "slot " << i;
        ASSERT_EQ(d->special_cost[i], costs[i]) << "slot " << i;
    }
    ASSERT_STREQ(d->alternate_names[3], "MYSTIC MACE");
    ASSERT_STREQ(d->alternate_names[1], "NONE");

    // ...and the ids the arrays could not spell, which is the one field v2
    // adds: a script keys its handler by these.
    ASSERT_STREQ(d->special_ids[1], "charge");
    ASSERT_STREQ(d->special_ids[4], "disarm");
    ASSERT_EQ(d->special_ids[0], nullptr) << "slot 0 is not a special";
    ASSERT_EQ(d->special_ids[5], nullptr) << "undeclared slot has no id";
}

TEST(ClasspackSchemaV2, specials_list_rewrites_every_slot)
{
    init_all_registries();
    // First install fills four slots...
    ClasspackData full;
    ASSERT_TRUE(parse_classpack_yaml(kV2Soldier, full, "v2"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(full)), 1);
    ASSERT_STREQ(get_family_descriptor(61)->special_names[4], "DISARM");

    // ...a later pack restating the family with one special leaves the
    // others disabled rather than inheriting them.
    ClasspackData thin;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n  living:\n    - id: v2:soldier\n      wire_id: 61\n"
        "      specials:\n"
        "        - id: charge\n"
        "          name: CHARGE\n"
        "          mp_cost: 25\n",
        thin, "v2-thin"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(thin)), 1);

    const FamilyDescriptor* d = get_family_descriptor(61);
    ASSERT_STREQ(d->special_names[1], "CHARGE");
    ASSERT_STREQ(d->special_names[4], "NONE");
    ASSERT_EQ(d->special_cost[4], kSpecialCostDisabled);
    ASSERT_EQ(d->special_ids[4], nullptr);
    // Everything outside the list is untouched by a sparse entry.
    ASSERT_EQ(d->base_stats[StatAxis::Strength], 12);
}

TEST(ClasspackSchemaV2, absent_train_axes_install_zero)
{
    init_all_registries();
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        "families:\n  living:\n    - id: v2:sparse\n      wire_id: 63\n"
        "      costs:\n"
        "        hire: 40\n"
        "        train:\n"
        "          strength: 7\n",
        data, "v2-costs"));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);
    const FamilyDescriptor* d = get_family_descriptor(63);
    ASSERT_NE(d, nullptr);
    ASSERT_EQ(d->hiring_cost, 40);
    ASSERT_EQ(d->stat_costs[StatAxis::Strength], 7);
    ASSERT_EQ(d->stat_costs[StatAxis::Armor], 0)
        << "an unpriced axis is 0, exactly as v1 shipped it";
}

// ---------------------------------------------------------------------------
// R1: slots
// ---------------------------------------------------------------------------

TEST(ClasspackSchemaV2, slot_key_skips_holes)
{
    init_all_registries();
    ClasspackData data;
    ASSERT_TRUE(parse_classpack_yaml(
        v2_entry("      specials:\n"
                 "        - {id: a, name: A, mp_cost: 1}\n"
                 "        - {id: b, name: B, mp_cost: 2, slot: 4}\n"
                 "        - {id: c, name: C, mp_cost: 3}\n"),
        data, "v2-slots"));
    const auto& list = *data.living[0].specials;
    ASSERT_EQ(list[0].slot, 1);
    ASSERT_EQ(list[1].slot, 4);
    ASSERT_EQ(list[2].slot, 5) << "the list resumes after an explicit slot";

    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);
    const FamilyDescriptor* d = get_family_descriptor(62);
    ASSERT_NE(d, nullptr);
    ASSERT_STREQ(d->special_names[1], "A");
    ASSERT_STREQ(d->special_names[2], "NONE") << "the hole is a hole";
    ASSERT_STREQ(d->special_names[3], "NONE");
    ASSERT_STREQ(d->special_names[4], "B");
    ASSERT_STREQ(d->special_names[5], "C")
        << "slot 5 is real: a level-13 caster cycles into it";
    ASSERT_EQ(d->special_cost[5], 3);
    ASSERT_STREQ(d->special_ids[5], "c");
}

TEST(ClasspackSchemaV2, five_specials_fit_and_a_sixth_does_not)
{
    ASSERT_TRUE(parses(
        v2_entry("      specials:\n"
                 "        - {id: a, name: A, mp_cost: 1}\n"
                 "        - {id: b, name: B, mp_cost: 1}\n"
                 "        - {id: c, name: C, mp_cost: 1}\n"
                 "        - {id: d, name: D, mp_cost: 1}\n"
                 "        - {id: e, name: E, mp_cost: 1}\n")))
        << "slot 5 is real: a level-13 mage casts HEARTBURST out of it";
    ASSERT_FALSE(parses(
        v2_entry("      specials:\n"
                 "        - {id: a, name: A, mp_cost: 1}\n"
                 "        - {id: b, name: B, mp_cost: 1}\n"
                 "        - {id: c, name: C, mp_cost: 1}\n"
                 "        - {id: d, name: D, mp_cost: 1}\n"
                 "        - {id: e, name: E, mp_cost: 1}\n"
                 "        - {id: f, name: F, mp_cost: 1}\n")));
}

TEST(ClasspackSchemaV2, duplicate_and_backwards_slots_fail)
{
    ASSERT_FALSE(parses(
        v2_entry("      specials:\n"
                 "        - {id: a, name: A, mp_cost: 1, slot: 2}\n"
                 "        - {id: b, name: B, mp_cost: 1, slot: 2}\n")))
        << "two entries cannot own one slot";
    ASSERT_FALSE(parses(
        v2_entry("      specials:\n"
                 "        - {id: a, name: A, mp_cost: 1, slot: 3}\n"
                 "        - {id: b, name: B, mp_cost: 1, slot: 2}\n")))
        << "the list is in slot order";
    ASSERT_FALSE(parses(
        v2_entry("      specials:\n"
                 "        - {id: a, name: A, mp_cost: 1, slot: 6}\n")))
        << "slot 6 does not exist";
    ASSERT_FALSE(parses(
        v2_entry("      specials:\n"
                 "        - {id: a, name: A, mp_cost: 1, slot: 0}\n")))
        << "slot 0 is an engine artifact, not a special";
}

// ---------------------------------------------------------------------------
// R2: per-special keys
// ---------------------------------------------------------------------------

TEST(ClasspackSchemaV2, a_special_needs_an_id_a_name_and_a_cost)
{
    ASSERT_FALSE(parses(v2_entry(
        "      specials:\n        - {name: A, mp_cost: 1}\n")));
    ASSERT_FALSE(parses(v2_entry(
        "      specials:\n        - {id: a, mp_cost: 1}\n")));
    ASSERT_FALSE(parses(v2_entry(
        "      specials:\n        - {id: a, name: A}\n")));
}

TEST(ClasspackSchemaV2, special_ids_are_bare_lua_keys_and_unique)
{
    ASSERT_FALSE(parses(v2_entry(
        "      specials:\n        - {id: \"Flare Burst\", name: A, "
        "mp_cost: 1}\n")))
        << "an id has to be spellable as a bare Lua table key";
    ASSERT_FALSE(parses(v2_entry(
        "      specials:\n"
        "        - {id: flare, name: A, mp_cost: 1}\n"
        "        - {id: flare, name: B, mp_cost: 2}\n")))
        << "one id, one special";
    ASSERT_FALSE(parses(v2_entry(
        "      specials:\n        - {id: default, name: A, mp_cost: 1}\n")))
        << "'default' is the specials table's catch-all key, so a special "
           "named that could never be handled by name";
}

TEST(ClasspackSchemaV2, an_alternate_needs_a_name)
{
    ASSERT_TRUE(parses(v2_entry(
        "      specials:\n"
        "        - {id: heal, name: HEAL, mp_cost: 2, "
        "alternate: {name: MYSTIC MACE}}\n")));
    ASSERT_FALSE(parses(v2_entry(
        "      specials:\n"
        "        - {id: heal, name: HEAL, mp_cost: 2, alternate: {}}\n")));
    ASSERT_FALSE(parses(v2_entry(
        "      specials:\n"
        "        - {id: heal, name: HEAL, mp_cost: 2, "
        "alternate: MYSTIC MACE}\n")))
        << "the bare-string shorthand gets the shape, not a shrug";
}

TEST(ClasspackSchemaV2, malformed_values_fail_the_pack)
{
    ASSERT_FALSE(parses(v2_entry("      combat:\n"
                                 "        hp: banana\n"
                                 "        melee_damage: 20\n"
                                 "        stepsize: 4\n"
                                 "        fire_delay: 6\n"
                                 "        fire_mp_cost: 2\n")))
        << "a bad float is fatal inside a block, same as it was in a list";
    ASSERT_FALSE(parses(v2_entry("      costs:\n        hire: soon\n")));
    ASSERT_FALSE(parses(v2_entry("      specials: [1, 2]\n")))
        << "specials entries are mappings";
}

TEST(ClasspackSchemaV2, a_block_in_the_wrong_shape_fails)
{
    // The half-remembered v1 array. Skipping it as an unknown list would
    // install a family with no attributes at all and say nothing.
    ASSERT_FALSE(parses(v2_entry("      stats: [12, 6, 12, 8, 9, 1]\n")));
    ASSERT_FALSE(parses(v2_entry("      combat: 120\n")));
    ASSERT_FALSE(parses(v2_entry("      specials:\n        charge: 25\n")))
        << "specials is a list, not a map of name to cost";
    ASSERT_TRUE(parses(v2_entry("      stats: ~\n")))
        << "an explicit null is 'not declared', as it is everywhere else";
}

// ---------------------------------------------------------------------------
// R5: required members
// ---------------------------------------------------------------------------

TEST(ClasspackSchemaV2, stats_must_declare_every_axis)
{
    // Armor is the cautionary one: default it and the class ships naked.
    ASSERT_FALSE(parses(v2_entry("      stats:\n"
                                 "        strength: 12\n"
                                 "        dexterity: 6\n"
                                 "        constitution: 12\n"
                                 "        intelligence: 8\n"
                                 "        level: 1\n")));
    ASSERT_TRUE(parses(v2_entry("      stats:\n"
                                "        strength: 12\n"
                                "        dexterity: 6\n"
                                "        constitution: 12\n"
                                "        intelligence: 8\n"
                                "        armor: 9\n"
                                "        level: 1\n")));
}

TEST(ClasspackSchemaV2, combat_must_declare_every_number)
{
    ASSERT_FALSE(parses(v2_entry("      combat:\n"
                                 "        hp: 120\n"
                                 "        melee_damage: 20\n"
                                 "        stepsize: 4\n"
                                 "        fire_delay: 6\n")))
        << "fire_mp_cost is missing";
    ASSERT_FALSE(parses(v2_entry("      combat:\n"
                                 "        hp: ~\n"
                                 "        melee_damage: 20\n"
                                 "        stepsize: 4\n"
                                 "        fire_delay: 6\n"
                                 "        fire_mp_cost: 2\n")))
        << "an explicit null is not a declaration";
}

TEST(ClasspackSchemaV2, costs_must_declare_hire)
{
    ASSERT_FALSE(parses(v2_entry("      costs:\n"
                                 "        train:\n"
                                 "          strength: 6\n")));
    ASSERT_TRUE(parses(v2_entry("      costs:\n        hire: 250\n")))
        << "train: is optional";
}

// ---------------------------------------------------------------------------
// R7: the dead derived axes
// ---------------------------------------------------------------------------

TEST(ClasspackSchemaV2, dead_combat_axes_fail_with_a_pointer)
{
    bool ok = true;
    const std::string said = parse_output(
        v2_entry("      combat:\n"
                 "        hp: 120\n"
                 "        melee_damage: 20\n"
                 "        stepsize: 4\n"
                 "        fire_delay: 6\n"
                 "        fire_mp_cost: 2\n"
                 "        mp: 40\n"),
        ok);
    ASSERT_FALSE(ok);
    EXPECT_NE(said.find("combat.mp"), std::string::npos) << said;
    EXPECT_NE(said.find("init_max_magicpoints"), std::string::npos)
        << "the message has to say where the live knob is: " << said;

    for (const char* dead : {"ranged_damage", "range", "defense"}) {
        ASSERT_FALSE(parses(v2_entry(std::string("      combat:\n") +
                                     "        " + dead + ": 3\n")))
            << dead << " must not be silently skipped";
    }
}

// ---------------------------------------------------------------------------
// R8: the three unknown-key tiers — silent at entry level, warn inside a
// block, warn inside a specials entry.
// ---------------------------------------------------------------------------

TEST(ClasspackSchemaV2, unknown_keys_inside_a_block_warn_and_keep)
{
    ClasspackData data;
    bool ok = false;
    const std::string said = parse_output(
        v2_entry("      combat:\n"
                 "        hp: 120\n"
                 "        melee_damage: 20\n"
                 "        step_size: 4\n"
                 "        stepsize: 4\n"
                 "        fire_delay: 6\n"
                 "        fire_mp_cost: 2\n"),
        data, ok);
    ASSERT_TRUE(ok) << "a typo does not sink the pack";
    EXPECT_NE(said.find("step_size"), std::string::npos)
        << "but it does not pass in silence either: " << said;
    ASSERT_TRUE(data.living[0].combat.has_value());
    EXPECT_EQ(data.living[0].combat->stepsize, 4.0f)
        << "and the keys around it still land";
}

TEST(ClasspackSchemaV2, unknown_keys_inside_a_specials_entry_warn_and_keep)
{
    // A specials entry is the third tier's own case: it is a block like
    // the others, one level deeper, and `cooldown:` is the key a modder
    // reaches for when they assume the engine has one.
    ClasspackData data;
    bool ok = false;
    const std::string said = parse_output(
        v2_entry("      specials:\n"
                 "        - id: charge\n"
                 "          name: CHARGE\n"
                 "          mp_cost: 25\n"
                 "          cooldown: 30\n"),
        data, ok);
    ASSERT_TRUE(ok);
    EXPECT_NE(said.find("cooldown"), std::string::npos) << said;
    ASSERT_TRUE(data.living[0].specials.has_value());
    ASSERT_EQ(data.living[0].specials->size(), 1u);
    EXPECT_EQ((*data.living[0].specials)[0].mp_cost, 25)
        << "the special is kept, minus the key that means nothing";
}

TEST(ClasspackSchemaV2, unknown_keys_warn_in_every_v2_block)
{
    struct Case {
        const char* body;
        const char* typo;
    };
    const Case cases[] = {
        {"      stats:\n"
         "        strength: 1\n        dexterity: 1\n"
         "        constitution: 1\n        intelligence: 1\n"
         "        armor: 1\n        level: 1\n        luck: 3\n",
         "luck"},
        {"      stats:\n"
         "        strength: 1\n        dexterity: 1\n"
         "        constitution: 1\n        intelligence: 1\n"
         "        armor: 1\n        level: 1\n"
         "        affinities: {fire: 2}\n",
         "affinities"},
        {"      costs:\n        hire: 1\n        upkeep: 2\n", "upkeep"},
        {"      costs:\n        hire: 1\n        train:\n          luck: 2\n",
         "luck"},
        {"      specials:\n        - {id: a, name: A, mp_cost: 1, "
         "cooldown: 3}\n",
         "cooldown"},
        {"      specials:\n        - {id: a, name: A, mp_cost: 1, "
         "alternate: {name: B, mp_cost: 1}}\n",
         "mp_cost"},
    };
    for (const Case& c : cases) {
        bool ok = false;
        const std::string said = parse_output(v2_entry(c.body), ok);
        EXPECT_TRUE(ok) << c.typo << ": " << said;
        EXPECT_NE(said.find(c.typo), std::string::npos)
            << "no warning for " << c.typo << ": " << said;
    }
}

TEST(ClasspackSchemaV2, unknown_keys_at_entry_level_stay_silent)
{
    // The forward-compatibility contract is unchanged where it was argued
    // for: a future engine adding an entry-level key must not make this
    // engine noisy about a file it can still read.
    bool ok = false;
    const std::string said = parse_output(
        v2_entry("      future_field: whatever\n"
                 "      future_block:\n        nested: 1\n"),
        ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(said.find("future_field"), std::string::npos) << said;
    EXPECT_EQ(said.find("future_block"), std::string::npos) << said;
}

// ---------------------------------------------------------------------------
// R6: the eight retired keys
// ---------------------------------------------------------------------------

TEST(ClasspackSchemaV2, retired_v1_keys_fail_by_name)
{
    // One case per key v2 retired. Each must fail the pack, and the
    // message must name where the value went — a rejection that only says
    // "unknown key" leaves the author with nothing to do about it.
    struct Case {
        const char* body;      // the key as a v1 file spells it
        const char* moved_to;  // the substring that has to be in the error
    };
    const Case cases[] = {
        {"      base_stats: [12, 6, 12, 8, 9, 1]\n", "stats: {strength"},
        {"      hiring_cost: 250\n", "costs.hire"},
        {"      derived_bonuses: [120, 0, 20, 0, 0, 0, 4, 6]\n",
         "combat: {hp"},
        {"      stat_costs: [6, 10, 6, 25, 50, 200]\n", "costs.train"},
        {"      weapon_cost: 2\n", "combat.fire_mp_cost"},
        {"      special_costs: [5000, 25, 100, 120, 150, 5000]\n",
         "mp_cost of each specials: entry"},
        {"      special_names: [\"NONE\", \"CHARGE\"]\n",
         "name of each specials: entry"},
        {"      alternate_names: [\"NONE\", \"MYSTIC MACE\"]\n",
         "alternate: {name"},
    };
    for (const Case& c : cases) {
        bool ok = true;
        const std::string said = parse_output(v2_entry(c.body), ok);
        EXPECT_FALSE(ok) << "a v1 key must sink the pack: " << c.body;
        EXPECT_NE(said.find(c.moved_to), std::string::npos)
            << "the error must say where the value went: " << said;
        EXPECT_NE(said.find("scripts/migrate_classpack_v2.py"),
                  std::string::npos)
            << "and how to do the rewrite: " << said;
    }
}

TEST(ClasspackSchemaV2, a_retired_key_fails_in_any_shape)
{
    // The refusal is by NAME, so it cannot be dodged by writing the key as
    // something other than the list it used to be — a scalar or a mapping
    // would otherwise take the silent forward-compatibility path.
    ASSERT_FALSE(parses(v2_entry("      base_stats: 12\n")));
    ASSERT_FALSE(parses(v2_entry("      base_stats: {strength: 12}\n")));
    ASSERT_FALSE(parses(v2_entry("      weapon_cost: [2]\n")));
    ASSERT_FALSE(parses(v2_entry("      special_names: ~\n")))
        << "even nulled out: the author still believes the key exists";
}

TEST(ClasspackSchemaV2, a_whole_v1_family_is_rejected_not_defaulted)
{
    // The failure this is all here to prevent: a v1 file read as if its
    // keys were unknown installs a family with no attributes, no
    // hitpoints and no specials, and nothing anywhere says why.
    init_all_registries();
    ClasspackData data;
    ASSERT_FALSE(parse_classpack_yaml(R"(families:
  living:
    - id: v1:soldier
      wire_id: 64
      name: "SOLDIER"
      base_stats: [12, 6, 12, 8, 9, 1]
      hiring_cost: 250
      derived_bonuses: [120, 0, 20, 0, 0, 0, 4, 6]
      stat_costs: [6, 10, 6, 25, 50, 200]
      special_costs: [5000, 25, 100, 120, 150, 5000]
      weapon_cost: 2
      special_names: ["NONE", "CHARGE", "BOOMERANG", "WHIRLWIND", "DISARM", "NONE"]
      alternate_names: ["NONE", "NONE", "NONE", "MYSTIC MACE", "NONE", "NONE"]
)",
                                     data, "v1"));
    ASSERT_EQ(get_family_descriptor(64), nullptr)
        << "a rejected pack installs nothing at all";
}
